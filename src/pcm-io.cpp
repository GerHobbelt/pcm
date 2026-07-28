// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Intel Corporation

// written by Alexander Antonov

#include "cpucounters.h"
#include "pcm-io-metrics.h"
#include "event-resolver.h"
#include "utils.h"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#define PCM_DELAY_DEFAULT 1.0

namespace pcm {

class MetricsDisplay {
public:
    void init(const MetricsConfig* config, uint32 numSockets);
    void display(std::ostream& os, bool csv, bool useLayout) const;
    void printHeader(std::ostream& os, bool csv) const;

    // Point to the platform's counter values (no copy, valid for lifetime of platform)
    void setCounterValues(const std::vector<std::unordered_map<std::string, double>>* values);

private:
    const MetricsConfig* m_config = nullptr;
    uint32 m_numSockets = 0;
    const std::vector<std::unordered_map<std::string, double>>* m_counterValues = nullptr;

    void displayLayoutMode(std::ostream& os) const;
    void displayMultiRowSection(std::ostream& os, const LayoutSection& section) const;
    void displayFlatMode(std::ostream& os) const;
    void displayCsv(std::ostream& os) const;
    bool hasLayoutSections() const;
    std::unordered_map<std::string, double> getSystemCounterValues() const;
    static std::string formatValue(double value);
    static std::string formatCsvValue(double value);
    static std::string metricDisplayName(const IOMetric& metric);
};

class MetricsDrivenPlatform {
public:
    bool init(PCM* pcm, const std::string& metricsPath, const std::string& eventPrefix);
    void collect(int delayMs);
    const MetricsConfig& getConfig() const { return m_config; }
    MetricsDisplay& getDisplay() { return m_display; }
    int getNumGroups() const { return m_numGroups; }
    static std::string cpuModelToDir(int cpuModel);

private:
    PCM* m_pcm = nullptr;
    uint32 m_numSockets = 0;
    int m_numGroups = 1;
    MetricsConfig m_config;
    MetricsDisplay m_display;
    PerfmonEventResolver m_resolver;
    std::vector<PCM::RawPMUConfigs> m_pmuConfigGroups;

    struct EventLocation {
        size_t groupIndex;
        std::string pmuName;
        size_t counterIndex;
    };
    std::unordered_map<std::string, EventLocation> m_eventLocations;

    // PMU counter reading dispatch
    using CounterGetter = std::function<uint64(uint32 unit, uint32 counter,
        const ServerUncoreCounterState& before, const ServerUncoreCounterState& after)>;
    using UnitCountGetter = std::function<size_t(uint32 socket)>;
    struct PMUCounterDesc {
        CounterGetter getter;
        UnitCountGetter getNumUnits;
    };
    std::unordered_map<std::string, PMUCounterDesc> m_pmuCounterDescs;
    void initPMUCounterDescs();

    std::vector<std::vector<ServerUncoreCounterState>> m_groupBeforeStates; // [group][socket]
    std::vector<std::vector<ServerUncoreCounterState>> m_groupAfterStates;  // [group][socket]
    std::vector<std::unordered_map<std::string, double>> m_counterValues;

    void readCounterValues();
};

} // namespace pcm

using namespace std;
using namespace pcm;

// --- MetricsDrivenPlatform implementation ---

std::string MetricsDrivenPlatform::cpuModelToDir(int cpuModel)
{
    switch (cpuModel)
    {
        case (int)PCM::ICX:
            return "icelake-sp";
        default:
            return "";
    }
}

// Counter-constraint-aware event grouping
//
// Each hardware PMU has N physical counters (e.g., CHA has 4: slots 0-3).
// Each perfmon event has a "Counter" field listing which slots it can use
// (e.g., "0,1" means only slots 0 or 1). The position in the programmable
// vector maps 1:1 to the hardware counter slot:
//
//   programmable[0] -> HW counter 0
//   programmable[1] -> HW counter 1
//   ...
//
// When more events need the same slot than one group can hold, we create
// multiple measurement groups that are time-multiplexed during collect().
//
// Example: 5 CHA events with these constraints:
//
//   Event A: Counter "0,1"       Event D: Counter "0,1,2,3"
//   Event B: Counter "0,1"       Event E: Counter "2,3"
//   Event C: Counter "0"
//
//   Group 0                      Group 1
//   slot 0: C  (only fits 0)    slot 0: A  (spillover)
//   slot 1: B  (fits 0,1)
//   slot 2: D  (fits 0-3)
//   slot 3: E  (fits 2,3)
//
//   collect() programs Group 0, sleeps, reads counters,
//   then programs Group 1, sleeps, reads counters.
//
// Events without a Counter field are rejected (local events in metrics.json
// must explicitly declare it). Fixed-counter events go to the fixed vector.
// Register events (mmio, pcicfg, pmt, tpmi) are rejected — not yet supported.
//
bool MetricsDrivenPlatform::init(PCM* pcm, const std::string& metricsPath, const std::string& eventPrefix)
{
    m_pcm = pcm;

    if (!m_config.load(metricsPath))
    {
        cerr << "ERROR: Failed to load metrics from " << metricsPath << "\n";
        return false;
    }

    if (!m_resolver.init(pcm->getCPUFamilyModelString(), eventPrefix))
    {
        cerr << "ERROR: Failed to initialize perfmon event resolver\n";
        cerr << "  CPU: " << pcm->getCPUFamilyModelString() << "\n";
        cerr << "  Event prefix: " << eventPrefix << "\n";
        return false;
    }

    // Register local events from metrics.json (takes priority over perfmon)
    m_resolver.addLocalEvents(m_config.getLocalEvents());

    // Resolve all events referenced in metric formulas, batching into groups
    // respecting per-event hardware counter constraints from the Counter field.
    CounterConstraintGrouper grouper;
    PCM::RawEventConfig placeholder{{0, 0, 0, 0, 0, 0}, ""};
    auto eventNames = m_config.extractEventNames();
    for (const auto& eventName : eventNames)
    {
        if (m_eventLocations.count(eventName))
            continue;

        std::string pmuName;
        PCM::RawEventConfig config;
        if (!m_resolver.resolveEvent(eventName, pmuName, config))
        {
            cerr << "WARNING: Could not resolve event: " << eventName << "\n";
            continue;
        }

        if (isRegisterEvent(pmuName))
        {
            cerr << "ERROR: Register-based events (mmio/pcicfg/pmt/tpmi) are not yet supported in pcm-io. "
                 << "Event: " << eventName << " (PMU: " << pmuName << ")\n";
            return false;
        }

        std::string counterStr = m_resolver.getField(eventName, "Counter");
        if (counterStr.empty())
        {
            cerr << "ERROR: Event \"" << eventName << "\" has no Counter field. "
                 << "Add \"Counter\": \"0,1,2,3\" (or appropriate value) to the event definition in metrics.json\n";
            return false;
        }

        if (CounterConstraintGrouper::isFixedCounter(counterStr))
        {
            if (m_pmuConfigGroups.empty())
                m_pmuConfigGroups.emplace_back();
            m_pmuConfigGroups[0][pmuName].fixed.push_back(config);
            m_eventLocations[eventName] = {0, pmuName, 0};
            continue;
        }

        std::set<int> allowedCounters;
        if (!CounterConstraintGrouper::parseCounterField(counterStr, allowedCounters))
        {
            cerr << "ERROR: Could not parse Counter field \"" << counterStr
                 << "\" for event " << eventName << "\n";
            return false;
        }

        auto placement = grouper.placeEvent(pmuName, allowedCounters);

        while (m_pmuConfigGroups.size() <= placement.groupIndex)
            m_pmuConfigGroups.emplace_back();

        auto& prog = m_pmuConfigGroups[placement.groupIndex][pmuName].programmable;
        if (prog.size() <= placement.counterIndex)
            prog.resize(placement.counterIndex + 1, placeholder);
        prog[placement.counterIndex] = config;
        m_eventLocations[eventName] = {placement.groupIndex, pmuName, placement.counterIndex};
    }

    if (m_eventLocations.empty())
    {
        cerr << "ERROR: No events could be resolved\n";
        return false;
    }

    // Allocate counter state vectors (programming happens in collect())
    m_numSockets = pcm->getNumSockets();
    m_counterValues.resize(m_numSockets);

    m_numGroups = static_cast<int>(m_pmuConfigGroups.size());
    m_groupBeforeStates.resize(m_numGroups);
    m_groupAfterStates.resize(m_numGroups);
    for (int g = 0; g < m_numGroups; ++g)
    {
        m_groupBeforeStates[g].resize(m_numSockets);
        m_groupAfterStates[g].resize(m_numSockets);
    }

    if (m_numGroups > 1)
        cerr << "INFO: Events split into " << m_numGroups << " measurement groups\n";

    m_display.init(&m_config, m_numSockets);
    initPMUCounterDescs();

    return true;
}

void MetricsDrivenPlatform::collect(int delayMs)
{
    for (size_t g = 0; g < m_pmuConfigGroups.size(); ++g)
    {
        PCM::ErrorCode status = m_pcm->program(m_pmuConfigGroups[g], true);
        if (status != PCM::Success)
        {
            m_pcm->checkError(status);
            return;
        }

        m_pcm->globalFreezeUncoreCounters();
        for (uint32 s = 0; s < m_numSockets; ++s)
            m_groupBeforeStates[g][s] = m_pcm->getServerUncoreCounterState(s);
        m_pcm->globalUnfreezeUncoreCounters();

        MySleepMs(delayMs);

        m_pcm->globalFreezeUncoreCounters();
        for (uint32 s = 0; s < m_numSockets; ++s)
            m_groupAfterStates[g][s] = m_pcm->getServerUncoreCounterState(s);
        m_pcm->globalUnfreezeUncoreCounters();
    }

    readCounterValues();
    m_display.setCounterValues(&m_counterValues);
}

void MetricsDrivenPlatform::initPMUCounterDescs()
{
    // Helper for discovery-based PMUs (CBO, MDF, PCU, UBOX, and future DMR types)
    auto discoveryDesc = [this](int pmuId) -> PMUCounterDesc {
        return {
            [pmuId](uint32 u, uint32 c, const ServerUncoreCounterState& b, const ServerUncoreCounterState& a) {
                return getUncoreCounter(pmuId, u, c, b, a);
            },
            [this, pmuId](uint32 s) { return m_pcm->getMaxNumOfUncorePMUs(pmuId, s); }
        };
    };

    m_pmuCounterDescs["cbo"] = discoveryDesc(PCM::CBO_PMU_ID);
    m_pmuCounterDescs["cha"] = discoveryDesc(PCM::CBO_PMU_ID);
    m_pmuCounterDescs["pcu"] = discoveryDesc(PCM::PCU_PMU_ID);
    m_pmuCounterDescs["ubox"] = discoveryDesc(PCM::UBOX_PMU_ID);
    m_pmuCounterDescs["mdf"] = discoveryDesc(PCM::MDF_PMU_ID);

    // IIO / IRP — indexed by stack
    m_pmuCounterDescs["iio"] = {
        [](uint32 u, uint32 c, const ServerUncoreCounterState& b, const ServerUncoreCounterState& a) { return getIIOCounter(u, c, b, a); },
        [this](uint32) { return static_cast<size_t>(m_pcm->getMaxNumOfIIOStacks()); }
    };
    m_pmuCounterDescs["irp"] = {
        [](uint32 u, uint32 c, const ServerUncoreCounterState& b, const ServerUncoreCounterState& a) { return getIRPCounter(u, c, b, a); },
        [this](uint32) { return static_cast<size_t>(m_pcm->getMaxNumOfIIOStacks()); }
    };

    // Memory controller
    m_pmuCounterDescs["imc"] = {
        [](uint32 u, uint32 c, const ServerUncoreCounterState& b, const ServerUncoreCounterState& a) { return getMCCounter(u, c, b, a); },
        [this](uint32) { return static_cast<size_t>(m_pcm->getMCChannelsPerSocket()); }
    };
    m_pmuCounterDescs["m2m"] = {
        [](uint32 u, uint32 c, const ServerUncoreCounterState& b, const ServerUncoreCounterState& a) { return getM2MCounter(u, c, b, a); },
        [this](uint32) { return static_cast<size_t>(m_pcm->getMCPerSocket()); }
    };

    // UPI / M3UPI interconnect
    PMUCounterDesc upiDesc = {
        [](uint32 u, uint32 c, const ServerUncoreCounterState& b, const ServerUncoreCounterState& a) { return getXPICounter(u, c, b, a); },
        [this](uint32) { return static_cast<size_t>(m_pcm->getQPILinksPerSocket()); }
    };
    m_pmuCounterDescs["xpi"] = upiDesc;
    m_pmuCounterDescs["upi"] = upiDesc;
    m_pmuCounterDescs["qpi"] = upiDesc;

    m_pmuCounterDescs["m3upi"] = {
        [](uint32 u, uint32 c, const ServerUncoreCounterState& b, const ServerUncoreCounterState& a) { return getM3UPICounter(u, c, b, a); },
        [this](uint32) { return static_cast<size_t>(m_pcm->getQPILinksPerSocket()); }
    };
}

void MetricsDrivenPlatform::readCounterValues()
{
    for (uint32 s = 0; s < m_numSockets; ++s)
    {
        m_counterValues[s].clear();
        for (const auto& [eventName, loc] : m_eventLocations)
        {
            auto it = m_pmuCounterDescs.find(loc.pmuName);
            if (it == m_pmuCounterDescs.end()) continue;

            const auto& desc = it->second;
            size_t numUnits = desc.getNumUnits(s);
            double sum = 0.0;
            for (size_t u = 0; u < numUnits; ++u)
            {
                sum += static_cast<double>(desc.getter((uint32)u, (uint32)loc.counterIndex,
                                           m_groupBeforeStates[loc.groupIndex][s],
                                           m_groupAfterStates[loc.groupIndex][s]));
            }
            // Rescale by group count to approximate a full-interval count: each group
            // only observed delay/numGroups of wall time. Assumes the event's rate is
            // steady across the interval.
            m_counterValues[s][eventName] = sum * m_numGroups;
        }
    }
}

// --- MetricsDisplay implementation ---

void MetricsDisplay::init(const MetricsConfig* config, uint32 numSockets)
{
    m_config = config;
    m_numSockets = numSockets;
}

void MetricsDisplay::setCounterValues(const std::vector<std::unordered_map<std::string, double>>* values)
{
    m_counterValues = values;
}

std::unordered_map<std::string, double> MetricsDisplay::getSystemCounterValues() const
{
    std::unordered_map<std::string, double> systemValues;
    for (const auto& socketValues : *m_counterValues)
    {
        for (const auto& [name, val] : socketValues)
            systemValues[name] += val;
    }
    return systemValues;
}

std::string MetricsDisplay::formatValue(double value)
{
    if (value >= 0.0 && value <= static_cast<double>(UINT64_MAX))
        return unit_format(static_cast<uint64>(value));

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

// Raw CSV value: integer when representable as uint64, otherwise fixed-point.
// Guards against UB from casting negative or out-of-range doubles to uint64.
std::string MetricsDisplay::formatCsvValue(double value)
{
    if (value >= 0.0 && value <= static_cast<double>(UINT64_MAX))
        return std::to_string(static_cast<uint64>(value));

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << value;
    return oss.str();
}

std::string MetricsDisplay::metricDisplayName(const IOMetric& metric)
{
    return metric.short_name.empty() ? metric.name : metric.short_name;
}

void MetricsDisplay::printHeader(std::ostream& os, bool csv) const
{
    if (!csv) return;

    const auto& metrics = m_config->getMetrics();
    const auto allowed = m_config->getLayoutMetricNames();
    os << "Skt";
    for (const auto& metric : metrics)
    {
        if (metric.aggregation == "system") continue;
        if (!allowed.count(metric.name)) continue;
        os << "," << metricDisplayName(metric);
    }
    for (const auto& metric : metrics)
    {
        if (metric.aggregation != "system") continue;
        if (!allowed.count(metric.name)) continue;
        os << "," << metricDisplayName(metric);
    }
    os << "\n";
}

bool MetricsDisplay::hasLayoutSections() const
{
    const auto& layout = m_config->getLayout();
    if (layout.size() > 1) return true;
    for (const auto& s : layout)
        if (s.isMultiRow() || !s.title.empty()) return true;
    return false;
}

void MetricsDisplay::display(std::ostream& os, bool csv, bool useLayout) const
{
    if (csv)
        displayCsv(os);
    else if (useLayout && hasLayoutSections())
        displayLayoutMode(os);
    else
        displayFlatMode(os);
}

void MetricsDisplay::displayCsv(std::ostream& os) const
{
    FormulaEvaluator evaluator;
    const auto& metrics = m_config->getMetrics();
    const auto allowed = m_config->getLayoutMetricNames();

    size_t numSocketMetrics = 0;
    size_t numSystemMetrics = 0;
    for (const auto& m : metrics)
    {
        if (!allowed.count(m.name)) continue;
        if (m.aggregation == "system") ++numSystemMetrics;
        else ++numSocketMetrics;
    }

    if (numSocketMetrics > 0)
    {
        for (uint32 s = 0; s < m_numSockets; ++s)
        {
            os << s;
            for (const auto& m : metrics)
            {
                if (m.aggregation == "system") continue;
                if (!allowed.count(m.name)) continue;
                double val = evaluator.evaluate(m.formula, (*m_counterValues)[s]);
                os << "," << formatCsvValue(val);
            }
            for (size_t i = 0; i < numSystemMetrics; ++i)
                os << ",";
            os << "\n";
        }
    }

    if (numSystemMetrics == 0) return;

    auto systemValues = getSystemCounterValues();
    os << "*";
    for (size_t i = 0; i < numSocketMetrics; ++i)
        os << ",";
    for (const auto& m : metrics)
    {
        if (m.aggregation != "system") continue;
        if (!allowed.count(m.name)) continue;
        double val = evaluator.evaluate(m.formula, systemValues);
        os << "," << formatCsvValue(val);
    }
    os << "\n";
}

void MetricsDisplay::displayLayoutMode(std::ostream& os) const
{
    for (const auto& section : m_config->getLayout())
    {
        if (section.isMultiRow())
        {
            displayMultiRowSection(os, section);
            continue;
        }

        // Flat section rendering
        FormulaEvaluator evaluator;
        const auto& metrics = m_config->getMetrics();
        std::vector<std::string> headers;
        std::vector<size_t> metricIdxs;
        std::vector<size_t> sysMetricIdxs;
        bool hasSocketMetrics = false;
        bool hasSystemMetrics = false;

        for (const auto& metricName : section.metrics)
        {
            for (size_t i = 0; i < metrics.size(); ++i)
            {
                if (metrics[i].name == metricName)
                {
                    if (metrics[i].aggregation == "system")
                    {
                        sysMetricIdxs.push_back(i);
                        hasSystemMetrics = true;
                    }
                    else
                    {
                        headers.push_back(metricDisplayName(metrics[i]));
                        metricIdxs.push_back(i);
                        hasSocketMetrics = true;
                    }
                    break;
                }
            }
        }

        if (headers.empty() && sysMetricIdxs.empty()) continue;

        if (!hasSocketMetrics && hasSystemMetrics)
        {
            auto systemValues = getSystemCounterValues();
            std::vector<std::pair<std::string,std::string>> sysSection;
            for (size_t idx : sysMetricIdxs)
            {
                std::string name = metricDisplayName(metrics[idx]);
                double val = evaluator.evaluate(metrics[idx].formula, systemValues);
                sysSection.emplace_back(name, formatValue(val));
            }
            TableRenderer::renderStandaloneSystemSection(os, section.title, sysSection);
            os << "\n";
            continue;
        }

        std::vector<std::string> fullHeaders;
        if (hasSocketMetrics)
            fullHeaders.push_back("Skt");
        fullHeaders.insert(fullHeaders.end(), headers.begin(), headers.end());

        TableRenderer table;
        table.setHeaders(fullHeaders);

        if (!section.title.empty())
            table.addSectionHeader(section.title);

        if (hasSocketMetrics)
        {
            for (uint32 s = 0; s < m_numSockets; ++s)
            {
                std::vector<std::string> row;
                row.push_back(std::to_string(s));
                for (size_t idx : metricIdxs)
                {
                    if (metrics[idx].aggregation == "system") continue;
                    double val = evaluator.evaluate(metrics[idx].formula, (*m_counterValues)[s]);
                    row.push_back(formatValue(val));
                }
                table.addRow(row);
            }
        }

        if (hasSystemMetrics)
        {
            auto systemValues = getSystemCounterValues();
            std::vector<std::pair<std::string,std::string>> sysSection;
            for (size_t idx : sysMetricIdxs)
            {
                std::string name = metricDisplayName(metrics[idx]);
                double val = evaluator.evaluate(metrics[idx].formula, systemValues);
                sysSection.emplace_back(name, formatValue(val));
            }
            table.addSystemSection("System Wide", sysSection);
        }

        table.render(os);
        os << "\n";
    }
}

void MetricsDisplay::displayMultiRowSection(std::ostream& os, const LayoutSection& section) const
{
    FormulaEvaluator evaluator;
    const auto& metrics = m_config->getMetrics();
    const size_t numRows = section.rowLabels.size();

    // Headers: "Skt" | "" (row-label col) | col-group-1 | col-group-2 | ...
    std::vector<std::string> headers = {"Skt", ""};
    // metricMatrix[colIdx][rowIdx] = index into metrics[], or -1 if absent
    std::vector<std::vector<int>> metricMatrix;
    for (const auto& [colHeader, colMetricNames] : section.columns)
    {
        headers.push_back(colHeader);
        std::vector<int> col(numRows, -1);
        for (size_t r = 0; r < colMetricNames.size() && r < numRows; ++r)
        {
            for (size_t mi = 0; mi < metrics.size(); ++mi)
            {
                if (metrics[mi].name == colMetricNames[r])
                {
                    col[r] = static_cast<int>(mi);
                    break;
                }
            }
        }
        metricMatrix.push_back(std::move(col));
    }

    TableRenderer table;
    table.setHeaders(headers);
    if (!section.title.empty())
        table.addSectionHeader(section.title);

    for (uint32 s = 0; s < m_numSockets; ++s)
    {
        for (size_t r = 0; r < numRows; ++r)
        {
            std::vector<std::string> row;
            row.push_back(r == 0 ? std::to_string(s) : "");  // socket number only in first sub-row
            row.push_back(section.rowLabels[r]);
            for (const auto& col : metricMatrix)
            {
                int mi = col[r];
                if (mi < 0)
                    row.push_back("");
                else
                {
                    double val = evaluator.evaluate(metrics[mi].formula, (*m_counterValues)[s]);
                    row.push_back(formatValue(val));
                }
            }
            table.addRow(row);
        }
    }

    if (!section.systemWideMetrics.empty())
    {
        auto systemValues = getSystemCounterValues();
        std::vector<std::pair<std::string,std::string>> sysSection;
        for (const auto& sysName : section.systemWideMetrics)
        {
            for (const auto& m : metrics)
            {
                if (m.name == sysName)
                {
                    double val = evaluator.evaluate(m.formula, systemValues);
                    sysSection.emplace_back(metricDisplayName(m), formatValue(val));
                    break;
                }
            }
        }
        if (!sysSection.empty())
            table.addSystemSection("System Wide", sysSection);
    }

    table.render(os);
    os << "\n";
}

void MetricsDisplay::displayFlatMode(std::ostream& os) const
{
    FormulaEvaluator evaluator;
    const auto& metrics = m_config->getMetrics();

    std::vector<size_t> socketMetricIndices;
    std::vector<size_t> systemMetricIndices;
    for (size_t i = 0; i < metrics.size(); ++i)
    {
        if (metrics[i].aggregation == "system")
            systemMetricIndices.push_back(i);
        else
            socketMetricIndices.push_back(i);
    }

    const bool hasSocketMetrics = !socketMetricIndices.empty();
    const bool hasSystemMetrics = !systemMetricIndices.empty();

    if (!hasSocketMetrics && hasSystemMetrics)
    {
        auto systemValues = getSystemCounterValues();
        std::vector<std::pair<std::string,std::string>> sysSection;
        for (size_t idx : systemMetricIndices)
        {
            std::string name = metricDisplayName(metrics[idx]);
            double val = evaluator.evaluate(metrics[idx].formula, systemValues);
            sysSection.emplace_back(name, formatValue(val));
        }
        TableRenderer::renderStandaloneSystemSection(os, "", sysSection);
        return;
    }

    std::vector<std::string> fullHeaders;
    if (hasSocketMetrics)
    {
        fullHeaders.push_back("Skt");
        for (size_t idx : socketMetricIndices)
            fullHeaders.push_back(metricDisplayName(metrics[idx]));
    }

    TableRenderer table;
    table.setHeaders(fullHeaders);

    if (hasSocketMetrics)
    {
        for (uint32 s = 0; s < m_numSockets; ++s)
        {
            std::vector<std::string> row;
            row.push_back(std::to_string(s));
            for (size_t idx : socketMetricIndices)
            {
                double val = evaluator.evaluate(metrics[idx].formula, (*m_counterValues)[s]);
                row.push_back(formatValue(val));
            }
            table.addRow(row);
        }
    }

    if (hasSystemMetrics)
    {
        auto systemValues = getSystemCounterValues();
        std::vector<std::pair<std::string,std::string>> sysSection;
        for (size_t idx : systemMetricIndices)
        {
            std::string name = metricDisplayName(metrics[idx]);
            double val = evaluator.evaluate(metrics[idx].formula, systemValues);
            sysSection.emplace_back(name, formatValue(val));
        }
        table.addSystemSection("System Wide", sysSection);
    }

    table.render(os);
}

// --- CLI ---

static void print_usage(const string& progname)
{
    cout << "\n Usage: \n " << progname
         << " --help | [delay] [options] [-- external_program [external_program_options]]\n";
    cout << "   <delay>                           => time interval to sample performance counters (seconds).\n";
    cout << "                                        If not specified, or 0, with external program given\n";
    cout << "                                        will read counters only after external program finishes\n";
    cout << " Supported <options> are: \n";
    cout << "  -h    | --help  | /h               => print this help and exit\n";
    cout << "  -silent                            => silence information output and print only measurements\n";
    cout << "  --version                          => print application version\n";
    cout << "  -csv[=file.csv] | /csv[=file.csv]  => output compact CSV format to screen or\n"
         << "                                        to a file, in case filename is provided\n";
    cout << "  -i[=number] | /i[=number]          => allow to determine number of iterations\n";
    cout << "  -ep <path> | /ep <path>            => event file prefix (perfmon directory path)\n";
    cout << "  --metrics <path>                   => custom metrics.json file path\n";
    cout << "  --no-layout                        => flat output without section grouping\n";
    cout << "  --validate                         => validate events against perfmon and exit\n";
    cout << "  --show-format                      => print metrics.json authoring guide and exit\n";
    cout << "\n";
    cout << " Examples:\n";
    cout << "  " << progname << " 1                  => print counters every second\n";
    cout << "  " << progname << " 0.5 -csv=test.log  => save counter values to test.log in CSV format\n";
    cout << "  " << progname << " --validate          => check which metrics are available on this CPU\n";
    cout << "\n";
}

static void print_metrics_format()
{
    cout << "\n metrics.json authoring guide\n";
    cout << " ============================\n\n";
    cout << " A metrics.json file has three top-level sections:\n";
    cout << "   events  (optional) - local event definitions\n";
    cout << "   metrics (required) - metric names and formulas\n";
    cout << "   layout  (optional) - how to group metrics for display\n\n";

    cout << " [events] - optional array of local event definitions\n";
    cout << "   Local events override same-name perfmon events. Fields:\n";
    cout << "     EventName   (required)  => lookup key referenced from metric formulas\n";
    cout << "     Unit        (uncore)    => CHA, iMC, M2M, UPI LL, IIO, IRP, PCU, UBOX, M3UPI etc\n";
    cout << "                                (omitted => event is treated as core)\n";
    cout << "     EventCode   (required)  => hex, e.g. \"0x35\"\n";
    cout << "     UMask       (required)  => hex, e.g. \"0x01\"\n";
    cout << "     UMaskExt    (optional)  => Extension UMask\n";
    cout << "     Counter     (required)  => counter slot(s), e.g. \"0,1,2,3\" or\n";
    cout << "                                \"Fixed counter 0\"\n";
    cout << "     MSRIndex    (optional)  => offcore events only\n";
    cout << "     MSRValue    (optional)  => offcore events only\n\n";

    cout << " [metrics] - required array of metric definitions\n";
    cout << "     name        (required)  => unique metric identifier\n";
    cout << "     formula     (required)  => arithmetic over event names\n";
    cout << "                                operators: + - * / and parentheses;\n";
    cout << "                                operands: event names and numeric literals\n";
    cout << "     short_name  (optional)  => compact column header\n";
    cout << "     aggregation (optional)  => \"socket\" (default) | \"system\" | \"stack\"\n";
    cout << "     description (optional)  => shown in available-metrics listing\n";
    cout << "   Event names shared across metrics are deduplicated - two metrics\n";
    cout << "   referencing the same events cost only the unique event count.\n\n";

    cout << " [layout] - optional; if omitted, all metrics render in a single flat table\n";
    cout << "   sections  (required when layout present) - array of section objects:\n";
    cout << "     title               (optional)  => section heading\n";
    cout << "   Flat section:\n";
    cout << "     metrics             (array)     => metric names in display order\n";
    cout << "   Multi-row section:\n";
    cout << "     rows                (array)     => row labels (e.g. [\"Total\",\"Miss\",\"Hit\"])\n";
    cout << "     columns             (object)    => column header => [metricName per row]\n";
    cout << "     system-wide-metrics (optional)  => metrics rendered as system rows\n\n";

    cout << " See src/metrics/icelake-sp/io.json for a complete example.\n\n";
}

static std::string findMetricsPath(const std::string& programPath, const std::string& platformDir)
{
    if (platformDir.empty()) return "";

    const std::string relPath = "metrics/" + platformDir + "/io.json";

    // 1. Next to the binary (post-build copy)
    size_t lastSlash = programPath.find_last_of("/\\");
    std::string binDir = (lastSlash != std::string::npos) ? programPath.substr(0, lastSlash) : ".";
    std::string candidate = binDir + "/" + relPath;
    if (std::ifstream(candidate).good()) return candidate;

    // 2. Install path
    candidate = getInstallPathPrefix() + relPath;
    if (std::ifstream(candidate).good()) return candidate;

    return "";
}

static void print_available_metrics(const string& metricsPath, const string& platformDir)
{
    if (metricsPath.empty()) return;

    MetricsConfig config;
    if (!config.load(metricsPath)) return;

    cout << " Available metrics for " << platformDir << ":\n\n";

    const auto& layout = config.getLayout();
    const auto& metrics = config.getMetrics();

    auto printMetricByName = [&](const std::string& name, const std::string& indent) {
        for (const auto& metric : metrics)
            if (metric.name == name)
            {
                cout << indent << metric.name << "  =  " << metric.formula << "\n";
                if (!metric.description.empty())
                    cout << std::string(indent.size(), ' ') << "    " << metric.description << "\n";
                break;
            }
    };

    bool hasLayout = layout.size() > 1 || (!layout.empty() && layout[0].isMultiRow());
    if (hasLayout)
    {
        for (const auto& section : layout)
        {
            if (!section.title.empty())
                cout << "  [" << section.title << "]\n";

            if (section.isMultiRow())
            {
                for (const auto& [colHeader, colMetricNames] : section.columns)
                {
                    cout << "  " << colHeader << ":\n";
                    for (const auto& mName : colMetricNames)
                        printMetricByName(mName, "    ");
                }
                for (const auto& mName : section.systemWideMetrics)
                    printMetricByName(mName, "    [sys] ");
            }
            else
            {
                for (const auto& metricName : section.metrics)
                    printMetricByName(metricName, "    ");
            }
            cout << "\n";
        }
    }
    else
    {
        for (const auto& metric : metrics)
        {
            cout << "    " << metric.name << "  =  " << metric.formula << "\n";
            if (!metric.description.empty())
                cout << "        " << metric.description << "\n";
        }
        cout << "\n";
    }
}

static double resolveDelay(double delay, bool csv, bool hasExternalCmd, PCM* m)
{
    m->setBlocked(hasExternalCmd && delay <= 0.0);

    if (csv)
    {
        if (delay <= 0.0) delay = PCM_DELAY_DEFAULT;
    }
    else
    {
        if (((delay < 1.0) && (delay > 0.0)) || (delay <= 0.0))
        {
            cerr << "For non-CSV mode delay < 1.0s does not make a lot of practical sense. "
                    "Default delay 1s is used. Consider CSV mode for lower delay values\n";
            delay = PCM_DELAY_DEFAULT;
        }
    }

    cerr << "Update every " << delay << " seconds\n";

    return delay;
}

static bool printValidation(std::ostream& os, const std::string& metricsPath,
                            const std::string& cpuFamilyModel, const std::string& perfmonPath)
{
    MetricsConfig config;
    if (!config.load(metricsPath))
    {
        cerr << "ERROR: Failed to load metrics from " << metricsPath << "\n";
        return false;
    }
    PerfmonEventResolver resolver;
    if (!resolver.init(cpuFamilyModel, perfmonPath))
    {
        cerr << "ERROR: Failed to initialize event resolver\n";
        return false;
    }
    resolver.addLocalEvents(config.getLocalEvents());
    return config.printValidatedMetrics(os, [&resolver](const std::string& event) { return resolver.isEvent(event); });
}

PCM_MAIN_NOTHROW;

int mainThrows(int argc, char* argv[])
{
    if (print_version(argc, argv))
        exit(EXIT_SUCCESS);

    null_stream nullStream2;
#ifdef PCM_FORCE_SILENT
    null_stream nullStream1;
    cout.rdbuf(&nullStream1);
    cerr.rdbuf(&nullStream2);
#else
    check_and_set_silent(argc, argv, nullStream2);
#endif

    set_signal_handlers();

    cerr << "\n";
    cerr << " Intel(r) Performance Counter Monitor: Metrics-Driven I/O Bandwidth Monitoring Utility\n";
    cerr << " This utility measures I/O bandwidth using JSON-defined metrics and perfmon events\n";
    cerr << "\n";

    double delay = -1.0;
    bool csv = false;
    bool useLayout = true;
    bool validateOnly = false;
    bool showHelp = false;
    std::string perfmonPath;
    std::string metricsPath;
    char* sysCmd = nullptr;
    char** sysArgv = nullptr;
    MainLoop mainLoop;

    string program = string(argv[0]);

    PCM* m = PCM::getInstance();

    if (argc > 1) do
    {
        argv++;
        argc--;
        string arg_value;

        if (check_argument_equals(*argv, {"--help", "-h", "/h"}))
        {
            showHelp = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"-silent", "/silent"}))
        {
            continue;
        }
        else if (check_argument_equals(*argv, {"-csv", "/csv"}))
        {
            csv = true;
        }
        else if (extract_argument_value(*argv, {"-csv", "/csv"}, arg_value))
        {
            csv = true;
            if (!arg_value.empty())
                m->setOutput(arg_value);
            continue;
        }
        else if (mainLoop.parseArg(*argv))
        {
            continue;
        }
        else if (check_argument_equals(*argv, {"--no-layout"}))
        {
            useLayout = false;
            continue;
        }
        else if (check_argument_equals(*argv, {"--validate"}))
        {
            validateOnly = true;
            continue;
        }
        else if (check_argument_equals(*argv, {"--show-format"}))
        {
            print_metrics_format();
            exit(EXIT_SUCCESS);
        }
        else if (check_argument_equals(*argv, {"-ep", "/ep"}))
        {
            argv++;
            argc--;
            if (argc <= 0)
            {
                cerr << "ERROR: no parameter provided for option -ep\n";
                exit(EXIT_FAILURE);
            }
            perfmonPath = *argv;
            continue;
        }
        else if (check_argument_equals(*argv, {"--metrics"}))
        {
            argv++;
            argc--;
            if (argc <= 0)
            {
                cerr << "ERROR: no parameter provided for option --metrics\n";
                exit(EXIT_FAILURE);
            }
            metricsPath = *argv;
            continue;
        }
        else if (check_argument_equals(*argv, {"--"}))
        {
            argv++;
            sysCmd = *argv;
            sysArgv = argv;
            break;
        }
        else
        {
            delay = parse_delay(*argv, program, (print_usage_func)print_usage);
            continue;
        }
    } while (argc > 1);

    // Auto-detect platform
    std::string platformDir = MetricsDrivenPlatform::cpuModelToDir(m->getCPUFamilyModel());
    if (platformDir.empty() && !showHelp)
    {
        print_cpu_details();
        cerr << "ERROR: No metrics definition available for this CPU model.\n";
        cerr << "Use --metrics <path> to specify a custom metrics.json file.\n";
        exit(EXIT_FAILURE);
    }

    if (perfmonPath.empty()) perfmonPath = PerfmonEventResolver::findPerfmonPath(program);

    if (perfmonPath.empty() && !showHelp)
    {
        cerr << "ERROR: Could not find perfmon directory (mapfile.csv not found).\n";
        cerr << "Use -ep <path> to specify the perfmon directory location.\n";
        exit(EXIT_FAILURE);
    }

    if (!perfmonPath.empty() && !std::ifstream(perfmonPath + "/mapfile.csv").good())
    {
        cerr << "WARNING: mapfile.csv not found in " << perfmonPath << "\n";
    }

    if (metricsPath.empty()) metricsPath = findMetricsPath(program, platformDir);

    if (metricsPath.empty() && !showHelp)
    {
        cerr << "ERROR: Could not find metrics.json for platform " << platformDir << "\n";
        cerr << "Use --metrics <path> to specify the file location.\n";
        exit(EXIT_FAILURE);
    }

    cout << "Metrics file: " << metricsPath << "\n";
    cout << "Perfmon event path: " << perfmonPath << "\n";

    if (showHelp)
    {
        print_usage(program);
        print_available_metrics(metricsPath, platformDir);
        exit(EXIT_SUCCESS);
    }

    std::string cpuFamilyModel = m->getCPUFamilyModelString();

    if (validateOnly)
    {
        cerr << "\nMetrics validation for " << platformDir << ":\n\n";
        exit(printValidation(cout, metricsPath, cpuFamilyModel, perfmonPath) ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    // Initialize platform
    MetricsDrivenPlatform platform;
    if (!platform.init(m, metricsPath, perfmonPath))
    {
        cerr << "ERROR: Platform initialization failed\n\nMetrics validation:\n\n";
        printValidation(cerr, metricsPath, cpuFamilyModel, perfmonPath);
        exit(EXIT_FAILURE);
    }

    // Delay handling
    delay = resolveDelay(delay, csv, sysCmd != nullptr, m);

    const auto& config = platform.getConfig();
    cerr << "Monitoring " << config.getMetrics().size() << " metrics, " << config.extractEventNames().size() << " events\n\n";

    int delayMs = static_cast<int>(delay * 1000) / platform.getNumGroups();

    if (sysCmd) MySystem(sysCmd, sysArgv);

    auto& display = platform.getDisplay();
    bool firstIteration = true;
    mainLoop([&]()
    {
        if (!csv) cout << flush;

        platform.collect(delayMs);

        if (firstIteration || csv)
        {
            display.printHeader(cout, csv);
            firstIteration = false;
        }

        display.display(cout, csv, useLayout);

        if (m->isBlocked()) return false;

        return true;
    });

    exit(EXIT_SUCCESS);
}
