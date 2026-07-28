// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Intel Corporation

#include "pcm-io-metrics.h"

#include <iostream>
#include <algorithm>

namespace pcm {

namespace {

struct FormulaParser {
    const std::string& input;
    const std::unordered_map<std::string, double>& vars;
    size_t pos = 0;

    void skipWS()
    {
        while (pos < input.size() && std::isspace(static_cast<unsigned char>(input[pos])))
            ++pos;
    }

    double expression()
    {
        double left = term();
        skipWS();
        while (pos < input.size() && (input[pos] == '+' || input[pos] == '-'))
        {
            char op = input[pos++];
            double right = term();
            left = (op == '+') ? left + right : left - right;
            skipWS();
        }
        return left;
    }

    double term()
    {
        double left = factor();
        skipWS();
        while (pos < input.size() && (input[pos] == '*' || input[pos] == '/'))
        {
            char op = input[pos++];
            double right = factor();
            if (op == '*')
                left *= right;
            else
                left = (right != 0.0) ? left / right : 0.0;
            skipWS();
        }
        return left;
    }

    double factor()
    {
        skipWS();
        if (pos < input.size() && input[pos] == '(')
        {
            ++pos;
            double val = expression();
            skipWS();
            if (pos < input.size() && input[pos] == ')')
                ++pos;
            return val;
        }
        if (pos < input.size() && (std::isdigit(static_cast<unsigned char>(input[pos])) || input[pos] == '.'))
        {
            size_t start = pos;
            while (pos < input.size() && (std::isdigit(static_cast<unsigned char>(input[pos])) || input[pos] == '.'))
                ++pos;
            return std::stod(input.substr(start, pos - start));
        }
        if (pos < input.size() && (std::isalpha(static_cast<unsigned char>(input[pos])) || input[pos] == '_'))
        {
            size_t start = pos;
            while (pos < input.size() &&
                   (std::isalnum(static_cast<unsigned char>(input[pos])) || input[pos] == '_' || input[pos] == '.'))
                ++pos;
            std::string name = input.substr(start, pos - start);
            auto it = vars.find(name);
            return (it != vars.end()) ? it->second : 0.0;
        }
        return 0.0;
    }
};

struct BoxChars {
    const char* horizontal;
    const char* vertical;
    const char* top_left;
    const char* top_right;
    const char* bottom_left;
    const char* bottom_right;
    const char* tee_down;
    const char* tee_up;
    const char* tee_right;
    const char* tee_left;
    const char* cross;
};

#ifdef _MSC_VER
static const BoxChars BOX {
    "\xC4", "\xB3", "\xDA", "\xBF", "\xC0", "\xD9",
    "\xC2", "\xC1", "\xC3", "\xB4", "\xC5"
};
#else
static const BoxChars BOX {
    u8"\u2500", u8"\u2502", u8"\u250C", u8"\u2510",
    u8"\u2514", u8"\u2518", u8"\u252C", u8"\u2534",
    u8"\u251C", u8"\u2524", u8"\u253C"
};
#endif

void renderCenteredText(std::ostream& os, const std::string& text, size_t width)
{
    size_t pad = (width > text.size()) ? width - text.size() : 0;
    size_t left = pad / 2;
    for (size_t j = 0; j < left; ++j) os << " ";
    os << text;
    for (size_t j = left; j < pad; ++j) os << " ";
}

void renderLine(std::ostream& os, const char* left, const char* mid,
                const char* right, const std::vector<size_t>& colWidths)
{
    os << left;
    for (size_t i = 0; i < colWidths.size(); ++i)
    {
        for (size_t j = 0; j < colWidths[i]; ++j)
            os << BOX.horizontal;
        if (i + 1 < colWidths.size())
            os << mid;
    }
    os << right << "\n";
}

} // anonymous namespace

double FormulaEvaluator::evaluate(const std::string& formula,
                                   const std::unordered_map<std::string, double>& variables) const
{
    FormulaParser parser{formula, variables};
    return parser.expression();
}

std::set<std::string> FormulaEvaluator::extractVariables(const std::string& formula) const
{
    std::set<std::string> vars;
    size_t i = 0;
    while (i < formula.size())
    {
        if (std::isalpha(static_cast<unsigned char>(formula[i])) || formula[i] == '_')
        {
            size_t start = i;
            while (i < formula.size() &&
                   (std::isalnum(static_cast<unsigned char>(formula[i])) || formula[i] == '_' || formula[i] == '.'))
                ++i;
            vars.insert(formula.substr(start, i - start));
        }
        else
        {
            ++i;
        }
    }
    return vars;
}

// --- TableRenderer ---

void TableRenderer::setHeaders(const std::vector<std::string>& headers)
{
    m_headers = headers;
}

void TableRenderer::addRow(const std::vector<std::string>& values)
{
    m_rows.push_back({false, false, "", values, {}});
}

void TableRenderer::addSectionHeader(const std::string& title)
{
    m_rows.push_back({true, false, title, {}, {}});
}

void TableRenderer::addSystemSection(const std::string& title,
                                     const std::vector<std::pair<std::string,std::string>>& pairs)
{
    m_rows.push_back({false, true, title, {}, pairs});
}

std::vector<size_t> TableRenderer::calculateColumnWidths() const
{
    const size_t padding = 2;
    std::vector<size_t> widths(m_headers.size(), 0);
    for (size_t i = 0; i < m_headers.size(); ++i)
        widths[i] = m_headers[i].size();
    for (const auto& row : m_rows)
    {
        if (row.isSectionHeader) continue;
        for (size_t i = 0; i < row.values.size() && i < widths.size(); ++i)
            widths[i] = (std::max)(widths[i], row.values[i].size());
    }
    for (auto& w : widths)
        w += padding;
    return widths;
}

size_t TableRenderer::calculateTableWidth(const std::vector<size_t>& colWidths) const
{
    // total = sum of column widths + (numCols + 1) border chars
    // but border chars are multi-byte on Linux; we track display columns here
    size_t width = colWidths.size() + 1;  // border characters
    for (auto w : colWidths)
        width += w;
    return width;
}

void TableRenderer::render(std::ostream& os) const
{
    if (m_headers.empty()) return;

    auto colWidths = calculateColumnWidths();
    size_t tableWidth = calculateTableWidth(colWidths);
    // innerWidth = tableWidth minus the 2 outer border chars (in display columns)
    size_t innerWidth = tableWidth - 2;

    // If first row is a section header, render it above the column headers
    bool titleAtTop = !m_rows.empty() && m_rows[0].isSectionHeader;
    size_t firstDataRow = titleAtTop ? 1 : 0;

    if (titleAtTop)
    {
        // Full-width top border (no column dividers)
        os << BOX.top_left;
        for (size_t j = 0; j < innerWidth; ++j)
            os << BOX.horizontal;
        os << BOX.top_right << "\n";

        // Section title row (pad clamped to 0 if title exceeds inner width,
        // so the closing border still renders instead of underflowing).
        os << BOX.vertical << " " << m_rows[0].sectionTitle;
        const size_t titleSize = m_rows[0].sectionTitle.size();
        size_t pad = (innerWidth > titleSize + 1) ? innerWidth - 1 - titleSize : 0;
        for (size_t j = 0; j < pad; ++j)
            os << " ";
        os << BOX.vertical << "\n";

        // Columned separator before column headers
        renderLine(os, BOX.tee_right, BOX.tee_down, BOX.tee_left, colWidths);
    }
    else
    {
        // Top border with column dividers
        renderLine(os, BOX.top_left, BOX.tee_down, BOX.top_right, colWidths);
    }

    // Header row (left-aligned)
    os << BOX.vertical;
    for (size_t i = 0; i < m_headers.size(); ++i)
    {
        os << " " << m_headers[i];
        size_t pad = colWidths[i] - 1 - m_headers[i].size();
        for (size_t j = 0; j < pad; ++j)
            os << " ";
        os << BOX.vertical;
    }
    os << "\n";

    if (m_rows.size() <= firstDataRow)
    {
        // No data rows: close immediately
        renderLine(os, BOX.bottom_left, BOX.tee_up, BOX.bottom_right, colWidths);
        return;
    }

    bool needDataSeparator = true;
    bool lastRowWasSystem = false;
    std::vector<size_t> lastWideColWidths;
    for (size_t ri = firstDataRow; ri < m_rows.size(); ++ri)
    {
        const auto& row = m_rows[ri];
        if (row.isSectionHeader)
        {
            // Full-width separator before section title
            os << BOX.tee_right;
            for (size_t j = 0; j < innerWidth; ++j)
                os << BOX.horizontal;
            os << BOX.tee_left << "\n";

            // Section title row (left-aligned, spans full width)
            os << BOX.vertical << " " << row.sectionTitle;
            const size_t titleSize = row.sectionTitle.size();
            size_t pad = (innerWidth > titleSize + 1) ? innerWidth - 1 - titleSize : 0;
            for (size_t j = 0; j < pad; ++j)
                os << " ";
            os << BOX.vertical << "\n";

            // Columned separator after section title
            renderLine(os, BOX.tee_right, BOX.tee_down, BOX.tee_left, colWidths);
            needDataSeparator = false;
            lastRowWasSystem = false;
        }
        else if (row.isSystemSection)
        {
            needDataSeparator = false;
            size_t numColumns = row.systemPairs.size();
            if (numColumns == 0) continue;

            // Transition separator: closes socket columns with tee_up (┴)
            renderLine(os, BOX.tee_right, BOX.tee_up, BOX.tee_left, colWidths);

            // Title row (left-aligned)
            os << BOX.vertical << " " << row.sectionTitle;
            const size_t titleSize = row.sectionTitle.size();
            size_t titlePad = (innerWidth > titleSize + 1) ? innerWidth - 1 - titleSize : 0;
            for (size_t j = 0; j < titlePad; ++j) os << " ";
            os << BOX.vertical << "\n";

            // Compute equal column widths across all system metrics.
            // Guard against underflow when the parent table is too narrow to fit
            // all system columns: fall back to 1-char-per-column so the table stays
            // visible (borders may misalign with the socket section, but no loop).
            const size_t separators = numColumns - 1;
            size_t innerSpace = (innerWidth > separators) ? innerWidth - separators : numColumns;
            std::vector<size_t> wideColWidths(numColumns, innerSpace / numColumns);
            for (size_t r = 0; r < innerSpace % numColumns; ++r) wideColWidths[r]++;

            // Opening multi-column separator (┬)
            renderLine(os, BOX.tee_right, BOX.tee_down, BOX.tee_left, wideColWidths);

            // Name row (centered)
            os << BOX.vertical;
            for (size_t i = 0; i < numColumns; ++i)
            {
                renderCenteredText(os, row.systemPairs[i].first, wideColWidths[i]);
                os << BOX.vertical;
            }
            os << "\n";

            // Inner separator (┼)
            renderLine(os, BOX.tee_right, BOX.cross, BOX.tee_left, wideColWidths);

            // Value row (centered)
            os << BOX.vertical;
            for (size_t i = 0; i < numColumns; ++i)
            {
                renderCenteredText(os, row.systemPairs[i].second, wideColWidths[i]);
                os << BOX.vertical;
            }
            os << "\n";

            lastWideColWidths = wideColWidths;
            lastRowWasSystem = true;
        }
        else
        {
            if (needDataSeparator)
            {
                renderLine(os, BOX.tee_right, BOX.cross, BOX.tee_left, colWidths);
                needDataSeparator = false;
            }
            // Data row (right-aligned)
            os << BOX.vertical;
            for (size_t i = 0; i < m_headers.size(); ++i)
            {
                const std::string& val = (i < row.values.size()) ? row.values[i] : "";
                size_t pad = colWidths[i] - 1 - val.size();
                for (size_t j = 0; j < pad; ++j)
                    os << " ";
                os << val << " " << BOX.vertical;
            }
            os << "\n";
            lastRowWasSystem = false;
        }
    }

    // Bottom border
    if (lastRowWasSystem)
        renderLine(os, BOX.bottom_left, BOX.tee_up, BOX.bottom_right, lastWideColWidths);
    else
        renderLine(os, BOX.bottom_left, BOX.tee_up, BOX.bottom_right, colWidths);
}

std::string TableRenderer::renderToString() const
{
    std::ostringstream oss;
    render(oss);
    return oss.str();
}

void TableRenderer::renderStandaloneSystemSection(
    std::ostream& os,
    const std::string& title,
    const std::vector<std::pair<std::string,std::string>>& pairs)
{
    const size_t numCols = pairs.size();
    if (numCols == 0) return;

    const size_t padding = 2;
    std::vector<size_t> colWidths(numCols, 0);
    for (size_t i = 0; i < numCols; ++i)
        colWidths[i] = (std::max)(pairs[i].first.size(), pairs[i].second.size()) + padding;

    size_t innerWidth = numCols - 1;  // column separators
    for (auto w : colWidths) innerWidth += w;

    if (!title.empty())
    {
        // Full-width top border
        os << BOX.top_left;
        for (size_t j = 0; j < innerWidth; ++j) os << BOX.horizontal;
        os << BOX.top_right << "\n";

        // Title row (left-aligned, clamp pad to 0 on overflow)
        os << BOX.vertical << " " << title;
        size_t pad = (innerWidth > title.size() + 1) ? innerWidth - 1 - title.size() : 0;
        for (size_t j = 0; j < pad; ++j) os << " ";
        os << BOX.vertical << "\n";

        // Separator: ├───┬───┤
        renderLine(os, BOX.tee_right, BOX.tee_down, BOX.tee_left, colWidths);
    }
    else
    {
        // Top border with column dividers: ┌───┬───┐
        renderLine(os, BOX.top_left, BOX.tee_down, BOX.top_right, colWidths);
    }

    // Name row (centered)
    os << BOX.vertical;
    for (size_t i = 0; i < numCols; ++i)
    {
        renderCenteredText(os, pairs[i].first, colWidths[i]);
        os << BOX.vertical;
    }
    os << "\n";

    // Inner separator: ├───┼───┤
    renderLine(os, BOX.tee_right, BOX.cross, BOX.tee_left, colWidths);

    // Value row (centered)
    os << BOX.vertical;
    for (size_t i = 0; i < numCols; ++i)
    {
        renderCenteredText(os, pairs[i].second, colWidths[i]);
        os << BOX.vertical;
    }
    os << "\n";

    // Bottom border: └───┴───┘
    renderLine(os, BOX.bottom_left, BOX.tee_up, BOX.bottom_right, colWidths);
}

// --- MetricsConfig ---

#ifdef PCM_SIMDJSON_AVAILABLE

bool MetricsConfig::load(const std::string& path)
{
    try
    {
        m_jsonParser = std::make_shared<simdjson::dom::parser>();
        simdjson::dom::element doc = m_jsonParser->load(path);
        return parseMetrics(doc);
    }
    catch (std::exception& e)
    {
        std::cerr << "Error loading metrics from " << path << ": " << e.what() << "\n";
        return false;
    }
}

bool MetricsConfig::loadFromString(const std::string& jsonStr)
{
    try
    {
        m_jsonParser = std::make_shared<simdjson::dom::parser>();
        simdjson::dom::element doc = m_jsonParser->parse(jsonStr);
        return parseMetrics(doc);
    }
    catch (std::exception& e)
    {
        std::cerr << "Error parsing metrics JSON: " << e.what() << "\n";
        return false;
    }
}

bool MetricsConfig::parseMetrics(simdjson::dom::element doc)
{
    // Parse optional "events" array (local event definitions)
    m_localEvents.clear();
    auto eventsArr = doc["events"];
    if (!eventsArr.error())
    {
        for (simdjson::dom::object eventObj : eventsArr)
        {
            std::string eventName;
            LocalEvent fields;
            for (const auto& kv : eventObj)
            {
                std::string key{kv.key.begin(), kv.key.end()};
                std::string_view val;
                std::string valStr;
                if (!kv.value.get(val)) valStr = std::string(val);

                if (key == "EventName")
                    eventName = valStr;
                else
                    fields[key] = valStr;
            }
            if (!eventName.empty())
            {
                m_localEvents.emplace_back(eventName, std::move(fields));
            }
        }
    }

    auto metricsArr = doc["metrics"];
    if (metricsArr.error())
    {
        std::cerr << "ERROR: No \"metrics\" array in metrics JSON\n";
        return false;
    }

    m_metrics.clear();
    for (simdjson::dom::object metricObj : metricsArr)
    {
        IOMetric m;
        m.name = std::string{metricObj["name"].get_c_str()};
        m.formula = std::string{metricObj["formula"].get_c_str()};

        auto shortName = metricObj["short_name"];
        if (!shortName.error())
            m.short_name = std::string{shortName.get_c_str()};

        auto agg = metricObj["aggregation"];
        if (!agg.error())
            m.aggregation = std::string{agg.get_c_str()};
        else
            m.aggregation = "socket";

        auto desc = metricObj["description"];
        if (!desc.error())
            m.description = std::string{desc.get_c_str()};

        m_metrics.push_back(std::move(m));
    }
    parseLayout(doc);
    return !m_metrics.empty();
}

void MetricsConfig::parseLayout(simdjson::dom::element doc)
{
    m_layout.clear();

    auto layoutObj = doc["layout"];
    if (layoutObj.error())
    {
        generateFlatLayout();
        return;
    }

    auto sectionsArr = layoutObj["sections"];
    if (sectionsArr.error())
    {
        generateFlatLayout();
        return;
    }

    try
    {
        for (simdjson::dom::object sectionObj : sectionsArr)
        {
            LayoutSection section;
            auto title = sectionObj["title"];
            if (!title.error())
            {
                section.title = std::string{title.get_c_str()};
            }

            auto rowsEl = sectionObj["rows"];
            if (!rowsEl.error())
            {
                // Multi-row section: parse rowLabels, columns, system-wide-metrics
                for (auto rowLabel : rowsEl.get_array())
                    section.rowLabels.emplace_back(rowLabel.get_c_str());

                auto columnsEl = sectionObj["columns"];
                if (!columnsEl.error())
                {
                    simdjson::dom::object colsObj;
                    if (!columnsEl.get(colsObj))
                    {
                        for (const auto& kv : colsObj)
                        {
                            std::string colHeader{kv.key.begin(), kv.key.end()};
                            std::vector<std::string> colMetrics;
                            for (auto metricName : kv.value.get_array())
                                colMetrics.emplace_back(metricName.get_c_str());
                            section.columns.emplace_back(std::move(colHeader), std::move(colMetrics));
                        }
                    }
                }

                auto sysEl = sectionObj["system-wide-metrics"];
                if (!sysEl.error())
                {
                    for (auto m : sysEl.get_array())
                        section.systemWideMetrics.emplace_back(m.get_c_str());
                }
            }
            else
            {
                // Flat section: parse metrics list
                auto metricsArr = sectionObj["metrics"];
                if (!metricsArr.error())
                {
                    for (auto metricName : metricsArr.get_array())
                        section.metrics.emplace_back(metricName.get_c_str());
                }
            }
            m_layout.emplace_back(std::move(section));
        }
    }
    catch (const std::exception& e)
    {
        std::cerr << "WARNING: Malformed layout section in metrics JSON: " << e.what() << "\n";
        m_layout.clear();
    }

    if (m_layout.empty()) generateFlatLayout();
}

#else // !PCM_SIMDJSON_AVAILABLE

bool MetricsConfig::load(const std::string&) { return false; }
bool MetricsConfig::loadFromString(const std::string&) { return false; }

#endif // PCM_SIMDJSON_AVAILABLE

void MetricsConfig::generateFlatLayout()
{
    m_layout.clear();
    LayoutSection section;
    for (const auto& metric : m_metrics)
    {
        section.metrics.emplace_back(metric.name);
    }
    m_layout.emplace_back(std::move(section));
}

std::set<std::string> MetricsConfig::extractEventNames() const
{
    std::set<std::string> allEvents;
    for (const auto& metric : m_metrics)
    {
        auto vars = m_evaluator.extractVariables(metric.formula);
        allEvents.insert(vars.begin(), vars.end());
    }
    return allEvents;
}

std::set<std::string> MetricsConfig::getLayoutMetricNames() const
{
    std::set<std::string> names;
    for (const auto& section : m_layout)
    {
        for (const auto& n : section.metrics) names.insert(n);
        for (const auto& col : section.columns)
            for (const auto& n : col.second) names.insert(n);
        for (const auto& n : section.systemWideMetrics) names.insert(n);
    }
    return names;
}

bool ValidationResult::allValid() const
{
    for (const auto& m : metrics)
    {
        if (!m.valid) return false;
    }
    return true;
}

ValidationResult MetricsConfig::validateEvents(const EventValidator& validator) const
{
    ValidationResult result;
    for (const auto& metric : m_metrics)
    {
        MetricValidation mv;
        mv.metricName = metric.name;
        auto vars = m_evaluator.extractVariables(metric.formula);
        for (const auto& var : vars)
        {
            if (!validator(var))
            {
                mv.missingEvents.insert(var);
            }
        }
        mv.valid = mv.missingEvents.empty();
        result.metrics.emplace_back(std::move(mv));
    }
    return result;
}

bool MetricsConfig::printValidatedMetrics(std::ostream& os, const EventValidator& validator) const
{
    auto result = validateEvents(validator);
    size_t validCount = 0;
    for (const auto& mv : result.metrics)
    {
        if (mv.valid)
        {
            os << "  [OK]       " << mv.metricName << "\n";
            ++validCount;
        }
        else
        {
            os << "  [INVALID]  " << mv.metricName << "  (missing:";
            for (const auto& ev : mv.missingEvents)
            {
                os << " " << ev;
            }
            os << ")\n";
        }
    }
    os << "\n" << validCount << " of " << result.metrics.size() << " metrics valid\n";
    return result.allValid();
}

// --- CounterConstraintGrouper ---

bool CounterConstraintGrouper::parseCounterField(const std::string& counterStr, std::set<int>& allowed)
{
    allowed.clear();
    if (counterStr.empty())
        return false;

    if (isFixedCounter(counterStr))
        return true;

    std::stringstream ss(counterStr);
    for (int i = 0; ss >> i;)
    {
        allowed.insert(i);
        if (ss.peek() == ',')
            ss.ignore();
    }
    return !allowed.empty();
}

bool CounterConstraintGrouper::isFixedCounter(const std::string& counterStr)
{
    return counterStr.find("Fixed") != std::string::npos
        || counterStr.find("FIXED") != std::string::npos;
}

CounterConstraintGrouper::EventPlacement CounterConstraintGrouper::placeEvent(
    const std::string& pmuName, const std::set<int>& allowedCounters)
{
    for (size_t g = 0; g < m_slotMap.size(); ++g)
    {
        auto& occupied = m_slotMap[g][pmuName];
        for (int c : allowedCounters)
        {
            if (occupied.find(c) == occupied.end())
            {
                occupied.insert(c);
                return {g, static_cast<size_t>(c)};
            }
        }
    }

    size_t newG = m_slotMap.size();
    m_slotMap.emplace_back();
    int c = *allowedCounters.begin();
    m_slotMap[newG][pmuName].insert(c);
    return {newG, static_cast<size_t>(c)};
}

} // namespace pcm
