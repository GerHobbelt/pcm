// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2025, Intel Corporation
#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <map>
#include "cpucounters.h"

#ifdef PCM_SIMDJSON_AVAILABLE
#include "simdjson.h"
#endif

namespace pcm {

// Field name → value map for a locally defined event
using LocalEvent = std::unordered_map<std::string, std::string>;

class PerfmonEventResolver {
public:
    // Search for the perfmon directory containing mapfile.csv.
    // Checks: next to programPath binary, then install prefix.
    // Returns empty string if not found.
    static std::string findPerfmonPath(const std::string& programPath);

    // Initialize from explicit CPU identification.
    // eventFilePrefix must point to a directory containing mapfile.csv and PMURegisterDeclarations/.
    bool init(const std::string& cpuFamilyModel, const std::string& eventFilePrefix);

    // Register local events (from metrics.json "events" array).
    // Local events take priority over perfmon database events.
    void addLocalEvents(const std::vector<std::pair<std::string, LocalEvent>>& events);

    // Query interface (for event validation)
    bool isEvent(const std::string& eventName) const;
    bool isField(const std::string& eventName, const std::string& fieldName) const;
    std::string getField(const std::string& eventName, const std::string& fieldName) const;

    // Map PMU unit name to PMURegisterDeclarations key (e.g. "cbo" -> "cha")
    std::string mapPMUName(const std::string& unit) const;

    // Resolve event name to PMU name + raw config (for PMU programming)
    bool resolveEvent(const std::string& eventName, std::string& pmuName, PCM::RawEventConfig& config) const;

    // Enumeration interface (for listing events)
    std::vector<std::string> getEventNames() const;
    std::vector<std::pair<std::string, std::string>> getEventFields(const std::string& eventName) const;

    // Access PMU register declarations (for advanced event programming in pcm-raw)
#ifdef PCM_SIMDJSON_AVAILABLE
    const simdjson::dom::element* getPMUDeclarations() const { return m_pmuDeclarations.get(); }
#endif
    const std::string& getPMUDeclarationsPath() const { return m_pmuDeclPath; }

    bool isInitialized() const { return m_initialized; }
private:

#ifdef PCM_SIMDJSON_AVAILABLE
    bool loadPerfmonEvents(const std::string& cpuFamilyModel, const std::string& prefix);
    bool loadPMUDeclarations(const std::string& cpuFamilyModel, int stepping, const std::string& prefix);
    bool parseTSV(const std::string& path);

    std::unordered_map<std::string, simdjson::dom::object> m_eventMapJSON;
    std::vector<std::unordered_map<std::string, std::vector<std::string>>> m_eventMapsTSV;
    std::shared_ptr<simdjson::dom::element> m_pmuDeclarations;
    std::vector<std::shared_ptr<simdjson::dom::parser>> m_jsonParsers;
#endif

    bool m_initialized = false;
    std::string m_pmuDeclPath;
    static const std::map<std::string, std::string> s_pmuNameMap;
    std::unordered_map<std::string, LocalEvent> m_localEvents;
};

} // namespace pcm
