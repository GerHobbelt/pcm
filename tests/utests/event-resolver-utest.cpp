// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Intel Corporation

#include "event-resolver.h"
#include "utils.h"
#include <gtest/gtest.h>

using namespace pcm;

// Test fixture: initializes resolver with real ICX perfmon data
class EventResolverTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ASSERT_TRUE(resolver.init("GenuineIntel-6-6A-0", "perfmon"));
    }
    PerfmonEventResolver resolver;
};

TEST_F(EventResolverTest, InitWithRealPerfmon)
{
    EXPECT_TRUE(resolver.isInitialized());
}

TEST(EventResolverInitTest, InitFailsWithBadPath)
{
    PerfmonEventResolver resolver;
    EXPECT_FALSE(resolver.init("GenuineIntel-6-6A-0", "nonexistent/path"));
    EXPECT_FALSE(resolver.isInitialized());
}

TEST(EventResolverInitTest, InitFailsWithBadCPU)
{
    PerfmonEventResolver resolver;
    EXPECT_FALSE(resolver.init("GenuineIntel-99-FF-0", "perfmon"));
}

TEST_F(EventResolverTest, IsEventFindsUncoreEvent)
{
    // UNC_CHA_DIR_UPDATE.HA is the first event in icelakex_uncore.json
    EXPECT_TRUE(resolver.isEvent("UNC_CHA_DIR_UPDATE.HA"));
}

TEST_F(EventResolverTest, IsEventFindsCoreEvent)
{
    // INST_RETIRED.ANY should be in icelakex_core.json
    EXPECT_TRUE(resolver.isEvent("INST_RETIRED.ANY"));
}

TEST_F(EventResolverTest, UnknownEventReturnsFalse)
{
    EXPECT_FALSE(resolver.isEvent("NONEXISTENT_EVENT.FOO"));
}

TEST_F(EventResolverTest, IsFieldAndGetField)
{
    const std::string event = "UNC_CHA_DIR_UPDATE.HA";

    EXPECT_TRUE(resolver.isField(event, "Unit"));
    EXPECT_TRUE(resolver.isField(event, "EventCode"));
    EXPECT_TRUE(resolver.isField(event, "UMask"));
    EXPECT_TRUE(resolver.isField(event, "EventName"));

    EXPECT_EQ(resolver.getField(event, "Unit"), "CHA");
    EXPECT_EQ(resolver.getField(event, "EventCode"), "0x54");
    EXPECT_EQ(resolver.getField(event, "UMask"), "0x01");
}

TEST_F(EventResolverTest, GetFieldMissing)
{
    const std::string event = "UNC_CHA_DIR_UPDATE.HA";

    EXPECT_FALSE(resolver.isField(event, "NonExistentField"));
    EXPECT_EQ(resolver.getField(event, "NonExistentField"), "");
}

TEST_F(EventResolverTest, GetFieldForUnknownEvent)
{
    EXPECT_FALSE(resolver.isField("NONEXISTENT.EVENT", "Unit"));
    EXPECT_EQ(resolver.getField("NONEXISTENT.EVENT", "Unit"), "");
}

TEST_F(EventResolverTest, ResolveEventBitPacking)
{
    // UNC_CHA_DIR_UPDATE.HA: Unit=CHA, EventCode=0x54, UMask=0x01
    // PMURegisterDeclarations for CHA programmable:
    //   EventCode: Config[0], Position 0, Width 8
    //   UMask:     Config[0], Position 8, Width 8
    std::string pmuName;
    PCM::RawEventConfig config;

    ASSERT_TRUE(resolver.resolveEvent("UNC_CHA_DIR_UPDATE.HA", pmuName, config));
    EXPECT_EQ(pmuName, "cha");

    // Check EventCode in bits 0-7 of config[0]
    uint64 eventCode = config.first[0] & 0xFF;
    EXPECT_EQ(eventCode, 0x54u);

    // Check UMask in bits 8-15 of config[0]
    uint64 umask = (config.first[0] >> 8) & 0xFF;
    EXPECT_EQ(umask, 0x01u);

    // Event name stored in second element
    EXPECT_EQ(config.second, "UNC_CHA_DIR_UPDATE.HA");
}

TEST_F(EventResolverTest, ResolveSecondEvent)
{
    // UNC_CHA_DIR_UPDATE.TOR: Unit=CHA, EventCode=0x54, UMask=0x02
    std::string pmuName;
    PCM::RawEventConfig config;

    ASSERT_TRUE(resolver.resolveEvent("UNC_CHA_DIR_UPDATE.TOR", pmuName, config));
    EXPECT_EQ(pmuName, "cha");

    uint64 eventCode = config.first[0] & 0xFF;
    EXPECT_EQ(eventCode, 0x54u);

    uint64 umask = (config.first[0] >> 8) & 0xFF;
    EXPECT_EQ(umask, 0x02u);
}

TEST_F(EventResolverTest, ResolveEventUnknownReturnsFalse)
{
    std::string pmuName;
    PCM::RawEventConfig config;
    EXPECT_FALSE(resolver.resolveEvent("NONEXISTENT.EVENT", pmuName, config));
}

TEST_F(EventResolverTest, ResolveUninitializedReturnsFalse)
{
    PerfmonEventResolver uninitResolver;
    std::string pmuName;
    PCM::RawEventConfig config;
    EXPECT_FALSE(uninitResolver.resolveEvent("UNC_CHA_DIR_UPDATE.HA", pmuName, config));
}

// Verify all ICX TMA events exist with correct EventCode, UMask, and Unit fields
TEST_F(EventResolverTest, AllICXTmaEventsFieldValues)
{
    struct TmaEvent { std::string name, eventCode, umask, unit; };
    // 216 events from perfmon/ICX/metrics/icelakex_metrics.json (base names, modifiers stripped)
    // 4 PERF_METRICS.* events excluded — they are fixed-counter metrics, not in perfmon DB
    const std::vector<TmaEvent> tmaEvents = {
        {"ARITH.DIVIDER_ACTIVE", "0x14", "0x09", ""},
        {"ARITH.FP_DIVIDER_ACTIVE", "0x14", "0x01", ""},
        {"ASSISTS.ANY", "0xc1", "0x07", ""},
        {"ASSISTS.FP", "0xc1", "0x02", ""},
        {"BACLEARS.ANY", "0xe6", "0x01", ""},
        {"BR_INST_RETIRED.ALL_BRANCHES", "0xc4", "0x00", ""},
        {"BR_INST_RETIRED.COND_NTAKEN", "0xc4", "0x10", ""},
        {"BR_INST_RETIRED.COND_TAKEN", "0xc4", "0x01", ""},
        {"BR_INST_RETIRED.FAR_BRANCH", "0xc4", "0x40", ""},
        {"BR_INST_RETIRED.NEAR_CALL", "0xc4", "0x02", ""},
        {"BR_INST_RETIRED.NEAR_RETURN", "0xc4", "0x08", ""},
        {"BR_INST_RETIRED.NEAR_TAKEN", "0xc4", "0x20", ""},
        {"BR_MISP_RETIRED.ALL_BRANCHES", "0xc5", "0x00", ""},
        {"BR_MISP_RETIRED.COND_NTAKEN", "0xc5", "0x10", ""},
        {"BR_MISP_RETIRED.COND_TAKEN", "0xc5", "0x01", ""},
        {"BR_MISP_RETIRED.INDIRECT", "0xc5", "0x80", ""},
        {"BR_MISP_RETIRED.RET", "0xc5", "0x08", ""},
        {"CORE_POWER.LVL0_TURBO_LICENSE", "0x28", "0x07", ""},
        {"CORE_POWER.LVL1_TURBO_LICENSE", "0x28", "0x18", ""},
        {"CORE_POWER.LVL2_TURBO_LICENSE", "0x28", "0x20", ""},
        {"CPU_CLK_UNHALTED.DISTRIBUTED", "0xec", "0x02", ""},
        {"CPU_CLK_UNHALTED.ONE_THREAD_ACTIVE", "0x3C", "0x02", ""},
        {"CPU_CLK_UNHALTED.REF_DISTRIBUTED", "0x3c", "0x08", ""},
        {"CPU_CLK_UNHALTED.REF_TSC", "0x00", "0x03", ""},
        {"CPU_CLK_UNHALTED.THREAD", "0x00", "0x02", ""},
        {"CPU_CLK_UNHALTED.THREAD_P", "0x3C", "0x00", ""},
        {"CYCLE_ACTIVITY.CYCLES_L1D_MISS", "0xA3", "0x08", ""},
        {"CYCLE_ACTIVITY.CYCLES_MEM_ANY", "0xA3", "0x10", ""},
        {"CYCLE_ACTIVITY.STALLS_L1D_MISS", "0xA3", "0x0C", ""},
        {"CYCLE_ACTIVITY.STALLS_L2_MISS", "0xa3", "0x05", ""},
        {"CYCLE_ACTIVITY.STALLS_L3_MISS", "0xa3", "0x06", ""},
        {"CYCLE_ACTIVITY.STALLS_MEM_ANY", "0xa3", "0x14", ""},
        {"CYCLE_ACTIVITY.STALLS_TOTAL", "0xa3", "0x04", ""},
        {"DECODE.LCP", "0x87", "0x01", ""},
        {"DSB2MITE_SWITCHES.PENALTY_CYCLES", "0xab", "0x02", ""},
        {"DTLB_LOAD_MISSES.STLB_HIT", "0x08", "0x20", ""},
        {"DTLB_LOAD_MISSES.WALK_ACTIVE", "0x08", "0x10", ""},
        {"DTLB_LOAD_MISSES.WALK_COMPLETED", "0x08", "0x0e", ""},
        {"DTLB_LOAD_MISSES.WALK_COMPLETED_1G", "0x08", "0x08", ""},
        {"DTLB_LOAD_MISSES.WALK_COMPLETED_2M_4M", "0x08", "0x04", ""},
        {"DTLB_LOAD_MISSES.WALK_COMPLETED_4K", "0x08", "0x02", ""},
        {"DTLB_LOAD_MISSES.WALK_PENDING", "0x08", "0x10", ""},
        {"DTLB_STORE_MISSES.STLB_HIT", "0x49", "0x20", ""},
        {"DTLB_STORE_MISSES.WALK_ACTIVE", "0x49", "0x10", ""},
        {"DTLB_STORE_MISSES.WALK_COMPLETED", "0x49", "0x0e", ""},
        {"DTLB_STORE_MISSES.WALK_COMPLETED_1G", "0x49", "0x08", ""},
        {"DTLB_STORE_MISSES.WALK_COMPLETED_2M_4M", "0x49", "0x04", ""},
        {"DTLB_STORE_MISSES.WALK_COMPLETED_4K", "0x49", "0x02", ""},
        {"DTLB_STORE_MISSES.WALK_PENDING", "0x49", "0x10", ""},
        {"EXE_ACTIVITY.1_PORTS_UTIL", "0xa6", "0x02", ""},
        {"EXE_ACTIVITY.2_PORTS_UTIL", "0xa6", "0x04", ""},
        {"EXE_ACTIVITY.3_PORTS_UTIL", "0xa6", "0x08", ""},
        {"EXE_ACTIVITY.BOUND_ON_STORES", "0xA6", "0x40", ""},
        {"FP_ARITH_INST_RETIRED.128B_PACKED_DOUBLE", "0xc7", "0x04", ""},
        {"FP_ARITH_INST_RETIRED.128B_PACKED_SINGLE", "0xc7", "0x08", ""},
        {"FP_ARITH_INST_RETIRED.256B_PACKED_DOUBLE", "0xc7", "0x10", ""},
        {"FP_ARITH_INST_RETIRED.256B_PACKED_SINGLE", "0xc7", "0x20", ""},
        {"FP_ARITH_INST_RETIRED.4_FLOPS", "0xc7", "0x18", ""},
        {"FP_ARITH_INST_RETIRED.512B_PACKED_DOUBLE", "0xc7", "0x40", ""},
        {"FP_ARITH_INST_RETIRED.512B_PACKED_SINGLE", "0xc7", "0x80", ""},
        {"FP_ARITH_INST_RETIRED.8_FLOPS", "0xc7", "0x60", ""},
        {"FP_ARITH_INST_RETIRED.SCALAR", "0xc7", "0x03", ""},
        {"FP_ARITH_INST_RETIRED.SCALAR_DOUBLE", "0xc7", "0x01", ""},
        {"FP_ARITH_INST_RETIRED.SCALAR_SINGLE", "0xc7", "0x02", ""},
        {"FP_ARITH_INST_RETIRED.VECTOR", "0xc7", "0xfc", ""},
        {"FRONTEND_RETIRED.ANY_DSB_MISS", "0xc6", "0x01", ""},
        {"FRONTEND_RETIRED.L2_MISS", "0xc6", "0x01", ""},
        {"ICACHE_16B.IFDATA_STALL", "0x80", "0x04", ""},
        {"ICACHE_DATA.STALLS", "0x80", "0x04", ""},
        {"ICACHE_TAG.STALLS", "0x83", "0x04", ""},
        {"IDQ.DSB_CYCLES_ANY", "0x79", "0x08", ""},
        {"IDQ.DSB_CYCLES_OK", "0x79", "0x08", ""},
        {"IDQ.DSB_UOPS", "0x79", "0x08", ""},
        {"IDQ.MITE_CYCLES_ANY", "0x79", "0x04", ""},
        {"IDQ.MITE_CYCLES_OK", "0x79", "0x04", ""},
        {"IDQ.MITE_UOPS", "0x79", "0x04", ""},
        {"IDQ.MS_SWITCHES", "0x79", "0x30", ""},
        {"IDQ.MS_UOPS", "0x79", "0x30", ""},
        {"IDQ_UOPS_NOT_DELIVERED.CYCLES_0_UOPS_DELIV.CORE", "0x9c", "0x01", ""},
        {"INST_DECODED.DECODERS", "0x55", "0x01", ""},
        {"INST_RETIRED.ANY", "0x00", "0x01", ""},
        {"INST_RETIRED.ANY_P", "0xc0", "0x00", ""},
        {"INST_RETIRED.NOP", "0xc0", "0x02", ""},
        {"INT_MISC.CLEARS_COUNT", "0x0D", "0x01", ""},
        {"INT_MISC.CLEAR_RESTEER_CYCLES", "0x0d", "0x80", ""},
        {"INT_MISC.UOP_DROPPING", "0x0d", "0x10", ""},
        {"ITLB_MISSES.WALK_ACTIVE", "0x85", "0x10", ""},
        {"ITLB_MISSES.WALK_COMPLETED", "0x85", "0x0e", ""},
        {"ITLB_MISSES.WALK_COMPLETED_2M_4M", "0x85", "0x04", ""},
        {"ITLB_MISSES.WALK_COMPLETED_4K", "0x85", "0x02", ""},
        {"ITLB_MISSES.WALK_PENDING", "0x85", "0x10", ""},
        {"L1D.REPLACEMENT", "0x51", "0x01", ""},
        {"L1D_PEND_MISS.FB_FULL", "0x48", "0x02", ""},
        {"L1D_PEND_MISS.FB_FULL_PERIODS", "0x48", "0x02", ""},
        {"L1D_PEND_MISS.L2_STALL", "0x48", "0x04", ""},
        {"L1D_PEND_MISS.PENDING", "0x48", "0x01", ""},
        {"L1D_PEND_MISS.PENDING_CYCLES", "0x48", "0x01", ""},
        {"L2_LINES_IN.ALL", "0xF1", "0x1F", ""},
        {"L2_LINES_OUT.NON_SILENT", "0xF2", "0x02", ""},
        {"L2_LINES_OUT.SILENT", "0xF2", "0x01", ""},
        {"L2_LINES_OUT.USELESS_HWPF", "0xf2", "0x04", ""},
        {"L2_RQSTS.ALL_CODE_RD", "0x24", "0xE4", ""},
        {"L2_RQSTS.ALL_DEMAND_DATA_RD", "0x24", "0xE1", ""},
        {"L2_RQSTS.ALL_DEMAND_MISS", "0x24", "0x27", ""},
        {"L2_RQSTS.ALL_RFO", "0x24", "0xE2", ""},
        {"L2_RQSTS.CODE_RD_MISS", "0x24", "0x24", ""},
        {"L2_RQSTS.DEMAND_DATA_RD_HIT", "0x24", "0xc1", ""},
        {"L2_RQSTS.DEMAND_DATA_RD_MISS", "0x24", "0x21", ""},
        {"L2_RQSTS.RFO_HIT", "0x24", "0xc2", ""},
        {"L2_RQSTS.RFO_MISS", "0x24", "0x22", ""},
        {"L2_RQSTS.SWPF_MISS", "0x24", "0x28", ""},
        {"LD_BLOCKS.NO_SR", "0x03", "0x08", ""},
        {"LD_BLOCKS.STORE_FORWARD", "0x03", "0x02", ""},
        {"LD_BLOCKS_PARTIAL.ADDRESS_ALIAS", "0x07", "0x01", ""},
        {"LONGEST_LAT_CACHE.MISS", "0x2e", "0x41", ""},
        {"LSD.UOPS", "0xa8", "0x01", ""},
        {"MACHINE_CLEARS.COUNT", "0xc3", "0x01", ""},
        {"MACHINE_CLEARS.MEMORY_ORDERING", "0xc3", "0x02", ""},
        {"MEM_INST_RETIRED.ALL_LOADS", "0xd0", "0x81", ""},
        {"MEM_INST_RETIRED.ALL_STORES", "0xd0", "0x82", ""},
        {"MEM_INST_RETIRED.ANY", "0xd0", "0x83", ""},
        {"MEM_INST_RETIRED.LOCK_LOADS", "0xd0", "0x21", ""},
        {"MEM_INST_RETIRED.SPLIT_STORES", "0xd0", "0x42", ""},
        {"MEM_LOAD_L3_HIT_RETIRED.XSNP_HIT", "0xd2", "0x02", ""},
        {"MEM_LOAD_L3_HIT_RETIRED.XSNP_HITM", "0xd2", "0x04", ""},
        {"MEM_LOAD_L3_HIT_RETIRED.XSNP_MISS", "0xd2", "0x01", ""},
        {"MEM_LOAD_L3_MISS_RETIRED.LOCAL_DRAM", "0xd3", "0x01", ""},
        {"MEM_LOAD_L3_MISS_RETIRED.REMOTE_DRAM", "0xd3", "0x02", ""},
        {"MEM_LOAD_L3_MISS_RETIRED.REMOTE_FWD", "0xd3", "0x08", ""},
        {"MEM_LOAD_L3_MISS_RETIRED.REMOTE_HITM", "0xd3", "0x04", ""},
        {"MEM_LOAD_MISC_RETIRED.UC", "0xd4", "0x04", ""},
        {"MEM_LOAD_RETIRED.FB_HIT", "0xd1", "0x40", ""},
        {"MEM_LOAD_RETIRED.L1_HIT", "0xd1", "0x01", ""},
        {"MEM_LOAD_RETIRED.L1_MISS", "0xd1", "0x08", ""},
        {"MEM_LOAD_RETIRED.L2_HIT", "0xd1", "0x02", ""},
        {"MEM_LOAD_RETIRED.L2_MISS", "0xd1", "0x10", ""},
        {"MEM_LOAD_RETIRED.L3_HIT", "0xd1", "0x04", ""},
        {"MEM_LOAD_RETIRED.L3_MISS", "0xd1", "0x20", ""},
        {"MISC_RETIRED.PAUSE_INST", "0xcc", "0x40", ""},
        {"OCR.DEMAND_DATA_RD.L3_HIT.SNOOP_HITM", "0xB7, 0xBB", "0x01", ""},
        {"OCR.DEMAND_DATA_RD.L3_HIT.SNOOP_HIT_WITH_FWD", "0xB7, 0xBB", "0x01", ""},
        {"OCR.DEMAND_RFO.L3_HIT.SNOOP_HITM", "0xB7, 0xBB", "0x01", ""},
        {"OCR.DEMAND_RFO.L3_MISS", "0xB7, 0xBB", "0x01", ""},
        {"OCR.STREAMING_WR.ANY_RESPONSE", "0xB7, 0xBB", "0x01", ""},
        {"OFFCORE_REQUESTS.ALL_DATA_RD", "0xB0", "0x08", ""},
        {"OFFCORE_REQUESTS.ALL_REQUESTS", "0xB0", "0x80", ""},
        {"OFFCORE_REQUESTS.DEMAND_DATA_RD", "0xb0", "0x01", ""},
        {"OFFCORE_REQUESTS_OUTSTANDING.ALL_DATA_RD", "0x60", "0x08", ""},
        {"OFFCORE_REQUESTS_OUTSTANDING.CYCLES_WITH_DATA_RD", "0x60", "0x08", ""},
        {"OFFCORE_REQUESTS_OUTSTANDING.CYCLES_WITH_DEMAND_CODE_RD", "0x60", "0x02", ""},
        {"OFFCORE_REQUESTS_OUTSTANDING.CYCLES_WITH_DEMAND_RFO", "0x60", "0x04", ""},
        {"OFFCORE_REQUESTS_OUTSTANDING.DEMAND_DATA_RD", "0x60", "0x01", ""},
        {"RESOURCE_STALLS.SCOREBOARD", "0xa2", "0x02", ""},
        {"RS_EVENTS.EMPTY_CYCLES", "0x5e", "0x01", ""},
        {"SQ_MISC.BUS_LOCK", "0xF4", "0x10", ""},
        {"SW_PREFETCH_ACCESS.ANY", "0x32", "0x0F", ""},
        {"TOPDOWN.SLOTS", "0x00", "0x04", ""},
        {"UNC_CHA_DIR_UPDATE.HA", "0x54", "0x01", "CHA"},
        {"UNC_CHA_DIR_UPDATE.TOR", "0x54", "0x02", "CHA"},
        {"UNC_CHA_REQUESTS.READS_LOCAL", "0x50", "0x01", "CHA"},
        {"UNC_CHA_REQUESTS.READS_REMOTE", "0x50", "0x02", "CHA"},
        {"UNC_CHA_REQUESTS.WRITES_LOCAL", "0x50", "0x04", "CHA"},
        {"UNC_CHA_REQUESTS.WRITES_REMOTE", "0x50", "0x08", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_CRD", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_CRD_PREF", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD_DDR", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD_LOCAL", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD_PMM", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD_PREF", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD_PREF_LOCAL", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD_PREF_REMOTE", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_DRD_REMOTE", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IA_MISS_LLCPREFDATA", "0x35", "0x01", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_HIT_ITOM", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_HIT_ITOMCACHENEAR", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_HIT_PCIRDCUR", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_ITOM", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR_LOCAL", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR_REMOTE", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_ITOM_LOCAL", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_ITOM_REMOTE", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_MISS_ITOM", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_MISS_ITOMCACHENEAR", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_MISS_PCIRDCUR", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_MISS_RFO", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_PCIRDCUR", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_PCIRDCUR_LOCAL", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_PCIRDCUR_REMOTE", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_INSERTS.IO_RFO", "0x35", "0x04", "CHA"},
        {"UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD", "0x36", "0x01", "CHA"},
        {"UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD_DDR", "0x36", "0x01", "CHA"},
        {"UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD_LOCAL", "0x36", "0x01", "CHA"},
        {"UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD_PMM", "0x36", "0x01", "CHA"},
        {"UNC_CHA_TOR_OCCUPANCY.IA_MISS_DRD_REMOTE", "0x36", "0x01", "CHA"},
        {"UNC_M2M_DIRECTORY_UPDATE.ANY", "0x2e", "0x01", "M2M"},
        {"UNC_M_CAS_COUNT.RD", "0x04", "0x0f", "iMC"},
        {"UNC_M_CAS_COUNT.WR", "0x04", "0x30", "iMC"},
        {"UNC_UPI_RxL_FLITS.ALL_DATA", "0x03", "0x0F", "UPI LL"},
        {"UNC_UPI_TxL_FLITS.ALL_DATA", "0x02", "0x0F", "UPI LL"},
        {"UOPS_DECODED.DEC0", "0x56", "0x01", ""},
        {"UOPS_DISPATCHED.PORT_0", "0xa1", "0x01", ""},
        {"UOPS_DISPATCHED.PORT_1", "0xa1", "0x02", ""},
        {"UOPS_DISPATCHED.PORT_2_3", "0xa1", "0x04", ""},
        {"UOPS_DISPATCHED.PORT_4_9", "0xa1", "0x10", ""},
        {"UOPS_DISPATCHED.PORT_5", "0xa1", "0x20", ""},
        {"UOPS_DISPATCHED.PORT_6", "0xa1", "0x40", ""},
        {"UOPS_DISPATCHED.PORT_7_8", "0xa1", "0x80", ""},
        {"UOPS_EXECUTED.CORE_CYCLES_GE_1", "0xB1", "0x02", ""},
        {"UOPS_EXECUTED.CYCLES_GE_3", "0xb1", "0x01", ""},
        {"UOPS_EXECUTED.THREAD", "0xb1", "0x01", ""},
        {"UOPS_EXECUTED.X87", "0xB1", "0x10", ""},
        {"UOPS_ISSUED.ANY", "0x0e", "0x01", ""},
        {"UOPS_ISSUED.VECTOR_WIDTH_MISMATCH", "0x0e", "0x02", ""},
        {"UOPS_RETIRED.SLOTS", "0xc2", "0x02", ""},
    };

    ASSERT_EQ(tmaEvents.size(), 216u);

    for (const auto& evt : tmaEvents)
    {
        ASSERT_TRUE(resolver.isEvent(evt.name)) << "Event not found: " << evt.name;
        EXPECT_EQ(resolver.getField(evt.name, "EventCode"), evt.eventCode)
            << "EventCode mismatch for " << evt.name;
        EXPECT_EQ(resolver.getField(evt.name, "UMask"), evt.umask)
            << "UMask mismatch for " << evt.name;
        if (!evt.unit.empty())
        {
            EXPECT_EQ(resolver.getField(evt.name, "Unit"), evt.unit)
                << "Unit mismatch for " << evt.name;
        }
    }
}

// --- Local Events Tests ---

TEST_F(EventResolverTest, AddLocalEventsNewEvent)
{
    // Register a custom event not in perfmon
    std::vector<std::pair<std::string, pcm::LocalEvent>> localEvents = {
        {"MY_CUSTOM_EVENT.SUB", {{"Unit", "CHA"}, {"EventCode", "0x99"}, {"UMask", "0x42"}}}
    };
    resolver.addLocalEvents(localEvents);

    EXPECT_TRUE(resolver.isEvent("MY_CUSTOM_EVENT.SUB"));
    EXPECT_TRUE(resolver.isField("MY_CUSTOM_EVENT.SUB", "Unit"));
    EXPECT_TRUE(resolver.isField("MY_CUSTOM_EVENT.SUB", "EventCode"));
    EXPECT_TRUE(resolver.isField("MY_CUSTOM_EVENT.SUB", "UMask"));
    EXPECT_FALSE(resolver.isField("MY_CUSTOM_EVENT.SUB", "NonExistent"));

    EXPECT_EQ(resolver.getField("MY_CUSTOM_EVENT.SUB", "Unit"), "CHA");
    EXPECT_EQ(resolver.getField("MY_CUSTOM_EVENT.SUB", "EventCode"), "0x99");
    EXPECT_EQ(resolver.getField("MY_CUSTOM_EVENT.SUB", "UMask"), "0x42");
    EXPECT_EQ(resolver.getField("MY_CUSTOM_EVENT.SUB", "NonExistent"), "");
}

TEST_F(EventResolverTest, AddLocalEventsOverridesPerfmon)
{
    // UNC_CHA_DIR_UPDATE.HA exists in perfmon with Unit=CHA, EventCode=0x54, UMask=0x01
    ASSERT_TRUE(resolver.isEvent("UNC_CHA_DIR_UPDATE.HA"));
    EXPECT_EQ(resolver.getField("UNC_CHA_DIR_UPDATE.HA", "Unit"), "CHA");

    // Override with local definition
    std::vector<std::pair<std::string, pcm::LocalEvent>> localEvents = {
        {"UNC_CHA_DIR_UPDATE.HA", {{"Unit", "CUSTOM_UNIT"}, {"EventCode", "0xFF"}, {"UMask", "0xAA"}}}
    };
    resolver.addLocalEvents(localEvents);

    // Local fields should win
    EXPECT_TRUE(resolver.isEvent("UNC_CHA_DIR_UPDATE.HA"));
    EXPECT_EQ(resolver.getField("UNC_CHA_DIR_UPDATE.HA", "Unit"), "CUSTOM_UNIT");
    EXPECT_EQ(resolver.getField("UNC_CHA_DIR_UPDATE.HA", "EventCode"), "0xFF");
    EXPECT_EQ(resolver.getField("UNC_CHA_DIR_UPDATE.HA", "UMask"), "0xAA");
}

TEST_F(EventResolverTest, AddLocalEventResolves)
{
    // Register a CHA event with known EventCode, UMask, and UMaskExt
    // (UMaskExt is required by ICX CHA PMURegisterDeclarations with no DefaultValue)
    std::vector<std::pair<std::string, pcm::LocalEvent>> localEvents = {
        {"MY_LOCAL_CHA_EVENT.TEST", {{"Unit", "CHA"}, {"EventCode", "0x35"}, {"UMask", "0x04"}, {"UMaskExt", "0x00"}}}
    };
    resolver.addLocalEvents(localEvents);

    std::string pmuName;
    PCM::RawEventConfig config;
    ASSERT_TRUE(resolver.resolveEvent("MY_LOCAL_CHA_EVENT.TEST", pmuName, config));
    EXPECT_EQ(pmuName, "cha");

    // EventCode in bits 0-7
    uint64 eventCode = config.first[0] & 0xFF;
    EXPECT_EQ(eventCode, 0x35u);

    // UMask in bits 8-15
    uint64 umask = (config.first[0] >> 8) & 0xFF;
    EXPECT_EQ(umask, 0x04u);
}
