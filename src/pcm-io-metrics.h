// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Intel Corporation
#pragma once

#include <string>
#include <vector>
#include <set>
#include <map>
#include <functional>
#include <unordered_map>
#include <sstream>
#include <ostream>

#include "event-resolver.h"

#ifdef PCM_SIMDJSON_AVAILABLE
#include <memory>
#include "simdjson.h"
#endif

namespace pcm {

struct IOMetric {
    std::string name;
    std::string formula;
    std::string short_name;
    std::string aggregation;  // "socket", "system", "stack"
    std::string description;
};

struct LayoutSection {
    std::string title;
    std::vector<std::string> metrics;  // references IOMetric::name (flat scheme)

    // Multi-row scheme — all empty => flat section
    std::vector<std::string> rowLabels;  // e.g. {"Total", "Miss", "Hit"}
    // Ordered pairs: (column-group-header, [metric-name-per-row]).
    // std::vector preserves JSON insertion order (simdjson dom::object is ordered).
    std::vector<std::pair<std::string, std::vector<std::string>>> columns;
    std::vector<std::string> systemWideMetrics;  // metric names for the system section

    bool isMultiRow() const { return !rowLabels.empty(); }
};

using EventValidator = std::function<bool(const std::string&)>;

struct MetricValidation {
    std::string metricName;
    bool valid;
    std::set<std::string> missingEvents;
};

struct ValidationResult {
    std::vector<MetricValidation> metrics;
    bool allValid() const;
};

class FormulaEvaluator {
public:
    double evaluate(const std::string& formula, const std::unordered_map<std::string, double>& variables) const;
    std::set<std::string> extractVariables(const std::string& formula) const;
};

class TableRenderer {
public:
    void setHeaders(const std::vector<std::string>& headers);
    void addRow(const std::vector<std::string>& values);
    void addSectionHeader(const std::string& title);
    void addSystemSection(const std::string& title,
                          const std::vector<std::pair<std::string,std::string>>& pairs);
    void render(std::ostream& os) const;
    std::string renderToString() const;

    // Renders a standalone titled box with a two-row centered table
    // (metric names row + values row). Used for sections that contain
    // only system-aggregated metrics, which have no per-socket rows.
    static void renderStandaloneSystemSection(
        std::ostream& os,
        const std::string& title,
        const std::vector<std::pair<std::string,std::string>>& pairs);

private:
    struct Row {
        bool isSectionHeader = false;
        bool isSystemSection = false;
        std::string sectionTitle;
        std::vector<std::string> values;
        std::vector<std::pair<std::string,std::string>> systemPairs;
    };
    std::vector<std::string> m_headers;
    std::vector<Row> m_rows;

    std::vector<size_t> calculateColumnWidths() const;
    size_t calculateTableWidth(const std::vector<size_t>& colWidths) const;
};

class CounterConstraintGrouper {
public:
    struct EventPlacement {
        size_t groupIndex;
        size_t counterIndex;
    };

    static bool parseCounterField(const std::string& counterStr, std::set<int>& allowed);
    static bool isFixedCounter(const std::string& counterStr);

    EventPlacement placeEvent(const std::string& pmuName,
                              const std::set<int>& allowedCounters);

private:
    std::vector<std::map<std::string, std::set<int>>> m_slotMap;
};

class MetricsConfig {
public:
    bool load(const std::string& path);
    bool loadFromString(const std::string& jsonStr);

    const std::vector<IOMetric>& getMetrics() const { return m_metrics; }
    const std::vector<LayoutSection>& getLayout() const { return m_layout; }
    const std::vector<std::pair<std::string, LocalEvent>>& getLocalEvents() const { return m_localEvents; }
    std::set<std::string> extractEventNames() const;
    // Returns the set of metric names referenced by any layout section.
    // Walks flat `metrics`, multi-row `columns[*].second`, and `systemWideMetrics`.
    // When `layout` is absent in JSON, generateFlatLayout() populates m_layout
    // with every metric name, so this returns all metrics in that case.
    std::set<std::string> getLayoutMetricNames() const;
    ValidationResult validateEvents(const EventValidator& validator) const;
    bool printValidatedMetrics(std::ostream& os, const EventValidator& validator) const;

private:
    std::vector<IOMetric> m_metrics;
    std::vector<LayoutSection> m_layout;
    std::vector<std::pair<std::string, LocalEvent>> m_localEvents;
    FormulaEvaluator m_evaluator;

    void generateFlatLayout();

#ifdef PCM_SIMDJSON_AVAILABLE
    bool parseMetrics(simdjson::dom::element doc);
    void parseLayout(simdjson::dom::element doc);
    std::shared_ptr<simdjson::dom::parser> m_jsonParser;
#endif
};

} // namespace pcm
