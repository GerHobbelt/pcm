// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2026, Intel Corporation

#include "pcm-io-metrics.h"
#include <gtest/gtest.h>
#include <fstream>
#include <cstdio>

using namespace pcm;

// --- CounterConstraintGrouper tests ---

TEST(ParseCounterFieldTest, AllFourCounters)
{
    std::set<int> allowed;
    EXPECT_TRUE(CounterConstraintGrouper::parseCounterField("0,1,2,3", allowed));
    EXPECT_EQ(allowed, (std::set<int>{0, 1, 2, 3}));
}

TEST(ParseCounterFieldTest, RestrictedCounters01)
{
    std::set<int> allowed;
    EXPECT_TRUE(CounterConstraintGrouper::parseCounterField("0,1", allowed));
    EXPECT_EQ(allowed, (std::set<int>{0, 1}));
}

TEST(ParseCounterFieldTest, RestrictedCounters23)
{
    std::set<int> allowed;
    EXPECT_TRUE(CounterConstraintGrouper::parseCounterField("2,3", allowed));
    EXPECT_EQ(allowed, (std::set<int>{2, 3}));
}

TEST(ParseCounterFieldTest, SingleCounter)
{
    std::set<int> allowed;
    EXPECT_TRUE(CounterConstraintGrouper::parseCounterField("0", allowed));
    EXPECT_EQ(allowed, (std::set<int>{0}));
}

TEST(ParseCounterFieldTest, FixedUpperCase)
{
    std::set<int> allowed;
    EXPECT_TRUE(CounterConstraintGrouper::parseCounterField("FIXED", allowed));
    EXPECT_TRUE(allowed.empty());
}

TEST(ParseCounterFieldTest, FixedCounterN)
{
    std::set<int> allowed;
    EXPECT_TRUE(CounterConstraintGrouper::parseCounterField("Fixed counter 0", allowed));
    EXPECT_TRUE(allowed.empty());
}

TEST(ParseCounterFieldTest, EmptyStringReturnsFalse)
{
    std::set<int> allowed;
    EXPECT_FALSE(CounterConstraintGrouper::parseCounterField("", allowed));
}

TEST(ParseCounterFieldTest, EightCounters)
{
    std::set<int> allowed;
    EXPECT_TRUE(CounterConstraintGrouper::parseCounterField("0,1,2,3,4,5,6,7", allowed));
    EXPECT_EQ(allowed, (std::set<int>{0, 1, 2, 3, 4, 5, 6, 7}));
}

TEST(IsFixedCounterTest, FixedUpperCase)
{
    EXPECT_TRUE(CounterConstraintGrouper::isFixedCounter("FIXED"));
}

TEST(IsFixedCounterTest, FixedCounterN)
{
    EXPECT_TRUE(CounterConstraintGrouper::isFixedCounter("Fixed counter 0"));
}

TEST(IsFixedCounterTest, ProgrammableCounters)
{
    EXPECT_FALSE(CounterConstraintGrouper::isFixedCounter("0,1,2,3"));
}

TEST(IsFixedCounterTest, EmptyString)
{
    EXPECT_FALSE(CounterConstraintGrouper::isFixedCounter(""));
}

TEST(EventGroupingTest, BasicSequentialPlacement)
{
    CounterConstraintGrouper grouper;
    std::set<int> all4{0, 1, 2, 3};
    auto p0 = grouper.placeEvent("cha", all4);
    auto p1 = grouper.placeEvent("cha", all4);
    auto p2 = grouper.placeEvent("cha", all4);
    auto p3 = grouper.placeEvent("cha", all4);

    EXPECT_EQ(p0.groupIndex, 0u);
    EXPECT_EQ(p1.groupIndex, 0u);
    EXPECT_EQ(p2.groupIndex, 0u);
    EXPECT_EQ(p3.groupIndex, 0u);

    std::set<size_t> counters{p0.counterIndex, p1.counterIndex, p2.counterIndex, p3.counterIndex};
    EXPECT_EQ(counters, (std::set<size_t>{0, 1, 2, 3}));
}

TEST(EventGroupingTest, OverflowToSecondGroup)
{
    CounterConstraintGrouper grouper;
    std::set<int> all4{0, 1, 2, 3};
    for (int i = 0; i < 4; ++i)
        grouper.placeEvent("cha", all4);

    auto p4 = grouper.placeEvent("cha", all4);
    EXPECT_EQ(p4.groupIndex, 1u);
}

TEST(EventGroupingTest, CounterZeroOnlyConflict)
{
    CounterConstraintGrouper grouper;
    std::set<int> zero{0};
    auto p0 = grouper.placeEvent("cha", zero);
    auto p1 = grouper.placeEvent("cha", zero);

    EXPECT_EQ(p0.groupIndex, 0u);
    EXPECT_EQ(p0.counterIndex, 0u);
    EXPECT_EQ(p1.groupIndex, 1u);
    EXPECT_EQ(p1.counterIndex, 0u);
}

TEST(EventGroupingTest, MixedConstraintsFitOneGroup)
{
    CounterConstraintGrouper grouper;
    std::set<int> c01{0, 1};
    std::set<int> c23{2, 3};
    std::set<int> all4{0, 1, 2, 3};

    auto pA = grouper.placeEvent("cha", c01);
    auto pB = grouper.placeEvent("cha", c23);
    auto pC = grouper.placeEvent("cha", all4);

    EXPECT_EQ(pA.groupIndex, 0u);
    EXPECT_EQ(pB.groupIndex, 0u);
    EXPECT_EQ(pC.groupIndex, 0u);

    EXPECT_TRUE(c01.count(pA.counterIndex));
    EXPECT_TRUE(c23.count(pB.counterIndex));
    EXPECT_NE(pA.counterIndex, pC.counterIndex);
    EXPECT_NE(pB.counterIndex, pC.counterIndex);
}

TEST(EventGroupingTest, IIOPatternFitsOneGroup)
{
    CounterConstraintGrouper grouper;
    std::set<int> c01{0, 1};
    std::set<int> c23{2, 3};

    auto p0 = grouper.placeEvent("iio", c01);
    auto p1 = grouper.placeEvent("iio", c01);
    auto p2 = grouper.placeEvent("iio", c23);
    auto p3 = grouper.placeEvent("iio", c23);

    EXPECT_EQ(p0.groupIndex, 0u);
    EXPECT_EQ(p1.groupIndex, 0u);
    EXPECT_EQ(p2.groupIndex, 0u);
    EXPECT_EQ(p3.groupIndex, 0u);
}

TEST(EventGroupingTest, IIOPatternOverflow)
{
    CounterConstraintGrouper grouper;
    std::set<int> c01{0, 1};
    std::set<int> c23{2, 3};

    grouper.placeEvent("iio", c01);
    grouper.placeEvent("iio", c01);
    auto overflow = grouper.placeEvent("iio", c01);
    grouper.placeEvent("iio", c23);
    grouper.placeEvent("iio", c23);

    EXPECT_EQ(overflow.groupIndex, 1u);
    EXPECT_TRUE(c01.count(overflow.counterIndex));
}

TEST(EventGroupingTest, MultiplePMUsIndependent)
{
    CounterConstraintGrouper grouper;
    std::set<int> all4{0, 1, 2, 3};

    auto pCha = grouper.placeEvent("cha", all4);
    auto pIio = grouper.placeEvent("iio", all4);

    EXPECT_EQ(pCha.groupIndex, 0u);
    EXPECT_EQ(pIio.groupIndex, 0u);
    EXPECT_EQ(pCha.counterIndex, 0u);
    EXPECT_EQ(pIio.counterIndex, 0u);
}

class FormulaEvaluatorTest : public ::testing::Test {
protected:
    FormulaEvaluator eval;
    std::unordered_map<std::string, double> noVars;
};

TEST_F(FormulaEvaluatorTest, Constants)
{
    EXPECT_DOUBLE_EQ(eval.evaluate("42", noVars), 42.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("3.14", noVars), 3.14);
    EXPECT_DOUBLE_EQ(eval.evaluate("0", noVars), 0.0);
}

TEST_F(FormulaEvaluatorTest, Arithmetic)
{
    EXPECT_DOUBLE_EQ(eval.evaluate("2 + 3 * 4", noVars), 14.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("(2 + 3) * 4", noVars), 20.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("10 - 3 - 2", noVars), 5.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("20 / 4", noVars), 5.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("2 + 3", noVars), 5.0);
    EXPECT_DOUBLE_EQ(eval.evaluate("6 * 7", noVars), 42.0);
}

TEST_F(FormulaEvaluatorTest, WithVariables)
{
    std::unordered_map<std::string, double> vars = {
        {"UNC_CHA_TOR_INSERTS.IO_PCIRDCUR", 100.0},
        {"UNC_CHA_TOR_INSERTS.IO_ITOM", 50.0}
    };
    EXPECT_DOUBLE_EQ(eval.evaluate("UNC_CHA_TOR_INSERTS.IO_PCIRDCUR * 64", vars), 6400.0);
    EXPECT_DOUBLE_EQ(
        eval.evaluate("(UNC_CHA_TOR_INSERTS.IO_PCIRDCUR + UNC_CHA_TOR_INSERTS.IO_ITOM) * 64", vars),
        9600.0);
}

TEST_F(FormulaEvaluatorTest, NestedParens)
{
    std::unordered_map<std::string, double> vars = {
        {"a", 2.0}, {"b", 3.0}, {"c", 7.0}, {"d", 1.0}
    };
    // ((2+3) * (7-1)) / 2 = (5*6)/2 = 15
    EXPECT_DOUBLE_EQ(eval.evaluate("((a + b) * (c - d)) / 2", vars), 15.0);
}

TEST_F(FormulaEvaluatorTest, DivisionByZero)
{
    EXPECT_DOUBLE_EQ(eval.evaluate("10 / 0", noVars), 0.0);
    std::unordered_map<std::string, double> vars = {{"x", 0.0}};
    EXPECT_DOUBLE_EQ(eval.evaluate("42 / x", vars), 0.0);
}

TEST_F(FormulaEvaluatorTest, UnknownVariableIsZero)
{
    EXPECT_DOUBLE_EQ(eval.evaluate("UNKNOWN_EVENT * 64", noVars), 0.0);
}

TEST_F(FormulaEvaluatorTest, ExtractVariables)
{
    auto vars = eval.extractVariables("UNC_CHA_TOR_INSERTS.IO_PCIRDCUR * 64");
    EXPECT_EQ(vars.size(), 1u);
    EXPECT_EQ(vars.count("UNC_CHA_TOR_INSERTS.IO_PCIRDCUR"), 1u);

    auto vars2 = eval.extractVariables("(UNC_CHA_TOR_INSERTS.IO_ITOM + UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR) * 64");
    EXPECT_EQ(vars2.size(), 2u);
    EXPECT_EQ(vars2.count("UNC_CHA_TOR_INSERTS.IO_ITOM"), 1u);
    EXPECT_EQ(vars2.count("UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR"), 1u);
}

TEST_F(FormulaEvaluatorTest, ExtractVariablesNoDuplicates)
{
    auto vars = eval.extractVariables("x + x * x");
    EXPECT_EQ(vars.size(), 1u);
    EXPECT_EQ(vars.count("x"), 1u);
}

TEST_F(FormulaEvaluatorTest, ExtractVariablesNoConstants)
{
    auto vars = eval.extractVariables("42 + 3.14");
    EXPECT_TRUE(vars.empty());
}

static const char* kTestMetricsJSON = R"json({
    "metrics": [
        {
            "name": "PCIe Rd (B)",
            "formula": "UNC_CHA_TOR_INSERTS.IO_PCIRDCUR * 64",
            "short_name": "PCIeRd",
            "aggregation": "socket"
        },
        {
            "name": "PCIe Wr (B)",
            "formula": "(UNC_CHA_TOR_INSERTS.IO_ITOM + UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR) * 64",
            "short_name": "PCIeWr",
            "aggregation": "socket"
        },
        {
            "name": "Total BW (B)",
            "formula": "(UNC_CHA_TOR_INSERTS.IO_PCIRDCUR + UNC_CHA_TOR_INSERTS.IO_ITOM + UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR) * 64",
            "aggregation": "system"
        }
    ]
})json";

TEST(MetricsConfigTest, LoadFromString)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));
    EXPECT_EQ(config.getMetrics().size(), 3u);
}

TEST(MetricsConfigTest, MetricFields)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    const auto& metrics = config.getMetrics();
    EXPECT_EQ(metrics[0].name, "PCIe Rd (B)");
    EXPECT_EQ(metrics[0].formula, "UNC_CHA_TOR_INSERTS.IO_PCIRDCUR * 64");
    EXPECT_EQ(metrics[0].short_name, "PCIeRd");
    EXPECT_EQ(metrics[0].aggregation, "socket");

    EXPECT_EQ(metrics[1].name, "PCIe Wr (B)");
    EXPECT_EQ(metrics[1].short_name, "PCIeWr");

    EXPECT_EQ(metrics[2].name, "Total BW (B)");
    EXPECT_EQ(metrics[2].short_name, "");
    EXPECT_EQ(metrics[2].aggregation, "system");
}

TEST(MetricsConfigTest, DefaultAggregation)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(R"({
        "metrics": [
            { "name": "Foo", "formula": "x * 2" }
        ]
    })"));
    EXPECT_EQ(config.getMetrics()[0].aggregation, "socket");
}

TEST(MetricsConfigTest, MetricDescriptionWhenPresent)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(R"json({
        "metrics": [
            {
                "name": "Foo",
                "formula": "x * 2",
                "description": "Total foo bytes, computed as x times two."
            }
        ]
    })json"));
    EXPECT_EQ(config.getMetrics()[0].description,
              "Total foo bytes, computed as x times two.");
}

TEST(MetricsConfigTest, MetricDescriptionDefaultsEmpty)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));
    for (const auto& metric : config.getMetrics())
        EXPECT_TRUE(metric.description.empty());
}

TEST(MetricsConfigTest, ExtractEventNames)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    auto events = config.extractEventNames();
    EXPECT_EQ(events.size(), 3u);
    EXPECT_EQ(events.count("UNC_CHA_TOR_INSERTS.IO_PCIRDCUR"), 1u);
    EXPECT_EQ(events.count("UNC_CHA_TOR_INSERTS.IO_ITOM"), 1u);
    EXPECT_EQ(events.count("UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR"), 1u);
}

TEST(MetricsConfigTest, LoadFromStringInvalidJSON)
{
    MetricsConfig config;
    EXPECT_FALSE(config.loadFromString("not valid json"));
}

TEST(MetricsConfigTest, LoadFromStringMissingMetrics)
{
    MetricsConfig config;
    EXPECT_FALSE(config.loadFromString(R"({"other": "data"})"));
}

TEST(MetricsConfigTest, LoadFromStringEmptyMetrics)
{
    MetricsConfig config;
    EXPECT_FALSE(config.loadFromString(R"({"metrics": []})"));
}

TEST(MetricsConfigTest, LoadFromFile)
{
    const std::string path = "test_metrics_tmp.json";
    {
        std::ofstream out(path);
        out << kTestMetricsJSON;
    }
    MetricsConfig config;
    ASSERT_TRUE(config.load(path));
    EXPECT_EQ(config.getMetrics().size(), 3u);
    (void)std::remove(path.c_str());
}

TEST(MetricsConfigTest, LoadFromFileBadPath)
{
    MetricsConfig config;
    EXPECT_FALSE(config.load("nonexistent/metrics.json"));
}

// --- TableRenderer Tests ---

// Box-drawing character helpers for building expected strings
#ifdef _MSC_VER
#define B_H  "\xC4"
#define B_V  "\xB3"
#define B_TL "\xDA"
#define B_TR "\xBF"
#define B_BL "\xC0"
#define B_BR "\xD9"
#define B_TD "\xC2"
#define B_TU "\xC1"
#define B_ML "\xC3"
#define B_MR "\xB4"
#define B_X  "\xC5"
#else
#define B_H  u8"\u2500"
#define B_V  u8"\u2502"
#define B_TL u8"\u250C"
#define B_TR u8"\u2510"
#define B_BL u8"\u2514"
#define B_BR u8"\u2518"
#define B_TD u8"\u252C"
#define B_TU u8"\u2534"
#define B_ML u8"\u251C"
#define B_MR u8"\u2524"
#define B_X  u8"\u253C"
#endif

static std::string hline(int n)
{
    std::string s;
    for (int i = 0; i < n; ++i) s += B_H;
    return s;
}

class TableRendererTest : public ::testing::Test {
protected:
    TableRenderer renderer;
};

TEST_F(TableRendererTest, RenderSingleColumn)
{
    renderer.setHeaders({"Value"});
    renderer.addRow({"42"});
    std::string result = renderer.renderToString();

    // Column width = max(5, 2) + 2 = 7
    std::string expected =
        std::string(B_TL) + hline(7) + B_TR + "\n" +
        B_V + " Value " + B_V + "\n" +
        B_ML + hline(7) + B_MR + "\n" +
        B_V + "    42 " + B_V + "\n" +
        B_BL + hline(7) + B_BR + "\n";

    EXPECT_EQ(result, expected);
}

TEST_F(TableRendererTest, RenderMultipleColumns)
{
    renderer.setHeaders({"PCIeRd", "PCIeWr", "Total"});
    renderer.addRow({"1234", "5678", "6912"});
    renderer.addRow({"100", "200", "300"});
    std::string result = renderer.renderToString();

    // Col widths: max(6,4)+2=8, max(6,4)+2=8, max(5,4)+2=7
    std::string expected =
        std::string(B_TL) + hline(8) + B_TD + hline(8) + B_TD + hline(7) + B_TR + "\n" +
        B_V + " PCIeRd " + B_V + " PCIeWr " + B_V + " Total " + B_V + "\n" +
        B_ML + hline(8) + B_X + hline(8) + B_X + hline(7) + B_MR + "\n" +
        B_V + "   1234 " + B_V + "   5678 " + B_V + "  6912 " + B_V + "\n" +
        B_V + "    100 " + B_V + "    200 " + B_V + "   300 " + B_V + "\n" +
        B_BL + hline(8) + B_TU + hline(8) + B_TU + hline(7) + B_BR + "\n";

    EXPECT_EQ(result, expected);
}

TEST_F(TableRendererTest, RenderWithSectionHeader)
{
    renderer.setHeaders({"Read", "Write"});
    renderer.addSectionHeader("PCIe BW");
    renderer.addRow({"100", "200"});
    std::string result = renderer.renderToString();

    // Col widths: max(4,3)+2=6, max(5,3)+2=7
    // Table width = 6+7+3 borders = 16 display cols, inner = 14
    // Section header is first row → title-at-top: full-width top border, then title, then columned separator
    std::string expected =
        std::string(B_TL) + hline(14) + B_TR + "\n" +
        B_V + " PCIe BW      " + B_V + "\n" +
        B_ML + hline(6) + B_TD + hline(7) + B_MR + "\n" +
        B_V + " Read " + B_V + " Write " + B_V + "\n" +
        B_ML + hline(6) + B_X + hline(7) + B_MR + "\n" +
        B_V + "  100 " + B_V + "   200 " + B_V + "\n" +
        B_BL + hline(6) + B_TU + hline(7) + B_BR + "\n";

    EXPECT_EQ(result, expected);
}

TEST_F(TableRendererTest, RenderEmptyTable)
{
    renderer.setHeaders({"A", "B"});
    std::string result = renderer.renderToString();

    // Col widths: 1+2=3, 1+2=3
    std::string expected =
        std::string(B_TL) + hline(3) + B_TD + hline(3) + B_TR + "\n" +
        B_V + " A " + B_V + " B " + B_V + "\n" +
        B_BL + hline(3) + B_TU + hline(3) + B_BR + "\n";

    EXPECT_EQ(result, expected);
}

TEST_F(TableRendererTest, RenderWithSystemSection)
{
    renderer.setHeaders({"Skt", "RdCur"});
    renderer.addSectionHeader("PCIe Data");
    renderer.addRow({"0", "100"});
    renderer.addSystemSection("System Wide",
        {{"TotRd", "6400"}, {"TotWr", "3200"}});
    std::string result = renderer.renderToString();

    EXPECT_NE(result.find("System Wide"), std::string::npos);
    EXPECT_NE(result.find("TotRd"), std::string::npos);
    EXPECT_NE(result.find("TotWr"), std::string::npos);
    EXPECT_NE(result.find("6400"), std::string::npos);
    EXPECT_NE(result.find("3200"), std::string::npos);
    EXPECT_GT(result.size(), 0u);
}

TEST(TableRendererStandaloneTest, RenderStandaloneSystemSectionWithTitle)
{
    std::ostringstream oss;
    TableRenderer::renderStandaloneSystemSection(oss, "System Only",
        {{"Total Read (B)", "0"}, {"Total Write (B)", "51"}});

    // colW: max(14,1)+2=16, max(15,2)+2=17; innerWidth = 16+17+1 = 34
    // Title " System Only" -> pad = 34 - 1 - 11 = 22
    // Name centering: "Total Read (B)" in 16 => 1 left, 1 right
    //                 "Total Write (B)" in 17 => 1 left, 1 right
    // Value centering: "0" in 16 => 7 left, 8 right
    //                  "51" in 17 => 7 left, 8 right
    std::string expected =
        std::string(B_TL) + hline(34) + B_TR + "\n" +
        B_V + " System Only" + std::string(22, ' ') + B_V + "\n" +
        B_ML + hline(16) + B_TD + hline(17) + B_MR + "\n" +
        B_V + " Total Read (B) " + B_V + " Total Write (B) " + B_V + "\n" +
        B_ML + hline(16) + B_X + hline(17) + B_MR + "\n" +
        B_V + "       0        " + B_V + "       51        " + B_V + "\n" +
        B_BL + hline(16) + B_TU + hline(17) + B_BR + "\n";

    EXPECT_EQ(oss.str(), expected);
}

TEST(TableRendererStandaloneTest, RenderStandaloneSystemSectionNoTitle)
{
    std::ostringstream oss;
    TableRenderer::renderStandaloneSystemSection(oss, "",
        {{"Total Read (B)", "0"}, {"Total Write (B)", "51"}});

    // Same column widths as with-title case; top border has column divider.
    std::string expected =
        std::string(B_TL) + hline(16) + B_TD + hline(17) + B_TR + "\n" +
        B_V + " Total Read (B) " + B_V + " Total Write (B) " + B_V + "\n" +
        B_ML + hline(16) + B_X + hline(17) + B_MR + "\n" +
        B_V + "       0        " + B_V + "       51        " + B_V + "\n" +
        B_BL + hline(16) + B_TU + hline(17) + B_BR + "\n";

    EXPECT_EQ(oss.str(), expected);
}

TEST(TableRendererStandaloneTest, RenderStandaloneSystemSectionEmpty)
{
    std::ostringstream oss;
    TableRenderer::renderStandaloneSystemSection(oss, "Empty", {});
    EXPECT_EQ(oss.str(), "");
}

// --- Layout Tests ---

static const char* kLayoutMetricsJSON = R"json({
    "metrics": [
        { "name": "PCIe Rd (B)", "formula": "A * 64", "short_name": "PCIeRd", "aggregation": "socket" },
        { "name": "PCIe Wr (B)", "formula": "B * 64", "short_name": "PCIeWr", "aggregation": "socket" },
        { "name": "Total BW (B)", "formula": "(A + B) * 64", "aggregation": "system" }
    ],
    "layout": {
        "sections": [
            { "title": "PCIe Bandwidth", "metrics": ["PCIe Rd (B)", "PCIe Wr (B)"] },
            { "title": "Total", "metrics": ["Total BW (B)"] }
        ]
    }
})json";

TEST(LayoutTest, LayoutParsing)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kLayoutMetricsJSON));

    const auto& layout = config.getLayout();
    ASSERT_EQ(layout.size(), 2u);

    EXPECT_EQ(layout[0].title, "PCIe Bandwidth");
    ASSERT_EQ(layout[0].metrics.size(), 2u);
    EXPECT_EQ(layout[0].metrics[0], "PCIe Rd (B)");
    EXPECT_EQ(layout[0].metrics[1], "PCIe Wr (B)");

    EXPECT_EQ(layout[1].title, "Total");
    ASSERT_EQ(layout[1].metrics.size(), 1u);
    EXPECT_EQ(layout[1].metrics[0], "Total BW (B)");
}

TEST(LayoutTest, LayoutMissingUsesFlat)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    const auto& layout = config.getLayout();
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_EQ(layout[0].title, "");
    ASSERT_EQ(layout[0].metrics.size(), 3u);
    EXPECT_EQ(layout[0].metrics[0], "PCIe Rd (B)");
    EXPECT_EQ(layout[0].metrics[1], "PCIe Wr (B)");
    EXPECT_EQ(layout[0].metrics[2], "Total BW (B)");
}

TEST(LayoutTest, LayoutEmptySectionsUsesFlat)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(R"json({
        "metrics": [
            { "name": "Foo", "formula": "x * 2" }
        ],
        "layout": {
            "sections": []
        }
    })json"));

    const auto& layout = config.getLayout();
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_EQ(layout[0].title, "");
    ASSERT_EQ(layout[0].metrics.size(), 1u);
    EXPECT_EQ(layout[0].metrics[0], "Foo");
}

TEST(LayoutTest, LayoutSingleSection)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(R"json({
        "metrics": [
            { "name": "Read BW", "formula": "R * 64" },
            { "name": "Write BW", "formula": "W * 64" }
        ],
        "layout": {
            "sections": [
                { "title": "Bandwidth", "metrics": ["Read BW", "Write BW"] }
            ]
        }
    })json"));

    const auto& layout = config.getLayout();
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_EQ(layout[0].title, "Bandwidth");
    ASSERT_EQ(layout[0].metrics.size(), 2u);
    EXPECT_EQ(layout[0].metrics[0], "Read BW");
    EXPECT_EQ(layout[0].metrics[1], "Write BW");
}

TEST(LayoutTest, LayoutMalformedSection)
{
    MetricsConfig config;
    // Section missing "title" and "metrics" keys — should not crash, load still succeeds
    ASSERT_TRUE(config.loadFromString(R"json({
        "metrics": [
            { "name": "A", "formula": "x * 1" },
            { "name": "B", "formula": "y * 2" }
        ],
        "layout": {
            "sections": [
                { "other_key": "irrelevant" },
                { "title": "Valid", "metrics": ["A"] }
            ]
        }
    })json"));

    const auto& layout = config.getLayout();
    ASSERT_EQ(layout.size(), 2u);
    // Malformed section: empty title, no metrics
    EXPECT_EQ(layout[0].title, "");
    EXPECT_TRUE(layout[0].metrics.empty());
    // Valid section still parsed
    EXPECT_EQ(layout[1].title, "Valid");
    ASSERT_EQ(layout[1].metrics.size(), 1u);
    EXPECT_EQ(layout[1].metrics[0], "A");
}

TEST(LayoutTest, GetLayoutMetricNamesFlatDedup)
{
    // Two flat sections share "PCIe Rd (B)" — must dedupe.
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(R"json({
        "metrics": [
            { "name": "PCIe Rd (B)", "formula": "A", "aggregation": "socket" },
            { "name": "PCIe Wr (B)", "formula": "B", "aggregation": "socket" },
            { "name": "Total BW (B)", "formula": "A+B", "aggregation": "system" }
        ],
        "layout": {
            "sections": [
                { "title": "Focus", "metrics": ["PCIe Rd (B)", "Total BW (B)"] },
                { "metrics": ["PCIe Rd (B)", "PCIe Wr (B)"] }
            ]
        }
    })json"));

    auto names = config.getLayoutMetricNames();
    EXPECT_EQ(names.size(), 3u);
    EXPECT_TRUE(names.count("PCIe Rd (B)"));
    EXPECT_TRUE(names.count("PCIe Wr (B)"));
    EXPECT_TRUE(names.count("Total BW (B)"));
}

TEST(LayoutTest, GetLayoutMetricNamesMultiRow)
{
    // Must collect names from columns AND system-wide-metrics.
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(R"json({
        "metrics": [
            {"name":"PCIRdCur",       "formula":"A", "aggregation":"socket"},
            {"name":"PCIRdCur Miss",  "formula":"B", "aggregation":"socket"},
            {"name":"ItoM",           "formula":"D", "aggregation":"socket"},
            {"name":"Total Read (B)", "formula":"A+B","aggregation":"system"}
        ],
        "layout": {
            "sections": [
                {
                    "rows": ["Total", "Miss"],
                    "columns": {
                        "PCIRdCur Events": ["PCIRdCur", "PCIRdCur Miss"],
                        "ItoM Events": ["ItoM"]
                    },
                    "system-wide-metrics": ["Total Read (B)"]
                }
            ]
        }
    })json"));

    auto names = config.getLayoutMetricNames();
    EXPECT_EQ(names.size(), 4u);
    EXPECT_TRUE(names.count("PCIRdCur"));
    EXPECT_TRUE(names.count("PCIRdCur Miss"));
    EXPECT_TRUE(names.count("ItoM"));
    EXPECT_TRUE(names.count("Total Read (B)"));
}

TEST(LayoutTest, GetLayoutMetricNamesNoLayoutReturnsAll)
{
    // When layout is omitted, generateFlatLayout() populates every metric
    // so the helper returns all of them.
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    auto names = config.getLayoutMetricNames();
    EXPECT_EQ(names.size(), 3u);
    EXPECT_TRUE(names.count("PCIe Rd (B)"));
    EXPECT_TRUE(names.count("PCIe Wr (B)"));
    EXPECT_TRUE(names.count("Total BW (B)"));
}

// --- Validation Tests ---

TEST(ValidationTest, ValidateAllEventsPresent)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    auto result = config.validateEvents([](const std::string&) { return true; });

    EXPECT_TRUE(result.allValid());
    ASSERT_EQ(result.metrics.size(), 3u);
    for (const auto& m : result.metrics)
    {
        EXPECT_TRUE(m.valid);
        EXPECT_TRUE(m.missingEvents.empty());
    }
}

TEST(ValidationTest, ValidateMissingEvent)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    auto result = config.validateEvents([](const std::string& event) {
        return event != "UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR";
    });

    EXPECT_FALSE(result.allValid());
    // Metric 0 "PCIe Rd (B)": formula uses only IO_PCIRDCUR -> valid
    EXPECT_TRUE(result.metrics[0].valid);
    // Metric 1 "PCIe Wr (B)": formula uses IO_ITOM + IO_ITOMCACHENEAR -> invalid
    EXPECT_FALSE(result.metrics[1].valid);
    ASSERT_EQ(result.metrics[1].missingEvents.size(), 1u);
    EXPECT_EQ(result.metrics[1].missingEvents.count("UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR"), 1u);
    // Metric 2 "Total BW (B)": formula uses all three -> invalid
    EXPECT_FALSE(result.metrics[2].valid);
    ASSERT_EQ(result.metrics[2].missingEvents.size(), 1u);
    EXPECT_EQ(result.metrics[2].missingEvents.count("UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR"), 1u);
}

TEST(ValidationTest, ValidateMultipleMissing)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    auto result = config.validateEvents([](const std::string&) { return false; });

    EXPECT_FALSE(result.allValid());
    ASSERT_EQ(result.metrics.size(), 3u);
    for (const auto& m : result.metrics)
    {
        EXPECT_FALSE(m.valid);
        EXPECT_FALSE(m.missingEvents.empty());
    }
    // Metric 0 has 1 event, metric 1 has 2, metric 2 has 3
    EXPECT_EQ(result.metrics[0].missingEvents.size(), 1u);
    EXPECT_EQ(result.metrics[1].missingEvents.size(), 2u);
    EXPECT_EQ(result.metrics[2].missingEvents.size(), 3u);
}

TEST(ValidationTest, PrintValidatedMetrics)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    std::ostringstream os;
    bool allValid = config.printValidatedMetrics(os, [](const std::string& event) {
        return event != "UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR";
    });

    EXPECT_FALSE(allValid);
    std::string output = os.str();
    EXPECT_NE(output.find("[OK]"), std::string::npos);
    EXPECT_NE(output.find("PCIe Rd (B)"), std::string::npos);
    EXPECT_NE(output.find("[INVALID]"), std::string::npos);
    EXPECT_NE(output.find("PCIe Wr (B)"), std::string::npos);
    EXPECT_NE(output.find("UNC_CHA_TOR_INSERTS.IO_ITOMCACHENEAR"), std::string::npos);
    EXPECT_NE(output.find("1 of 3 metrics valid"), std::string::npos);

    std::ostringstream os2;
    bool allValidTrue = config.printValidatedMetrics(os2, [](const std::string&) { return true; });
    EXPECT_TRUE(allValidTrue);
}

// --- Local Events Tests ---

static const char* kLocalEventsJSON = R"json({
    "events": [
        {
            "EventName": "MY_CUSTOM_EVENT.SUB",
            "Unit": "CHA",
            "EventCode": "0x99",
            "UMask": "0x42",
            "BriefDescription": "A custom event"
        },
        {
            "EventName": "ANOTHER_EVENT.FOO",
            "Unit": "IIO",
            "EventCode": "0x10",
            "UMask": "0x01"
        }
    ],
    "metrics": [
        {
            "name": "Custom Metric",
            "formula": "MY_CUSTOM_EVENT.SUB * 64",
            "aggregation": "socket"
        }
    ]
})json";

TEST(LocalEventsTest, LocalEventsParsing)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kLocalEventsJSON));

    const auto& localEvents = config.getLocalEvents();
    ASSERT_EQ(localEvents.size(), 2u);

    EXPECT_EQ(localEvents[0].first, "MY_CUSTOM_EVENT.SUB");
    EXPECT_EQ(localEvents[0].second.at("Unit"), "CHA");
    EXPECT_EQ(localEvents[0].second.at("EventCode"), "0x99");
    EXPECT_EQ(localEvents[0].second.at("UMask"), "0x42");
    EXPECT_EQ(localEvents[0].second.at("BriefDescription"), "A custom event");
    // EventName should NOT be in the fields map
    EXPECT_EQ(localEvents[0].second.count("EventName"), 0u);

    EXPECT_EQ(localEvents[1].first, "ANOTHER_EVENT.FOO");
    EXPECT_EQ(localEvents[1].second.at("Unit"), "IIO");
}

TEST(LocalEventsTest, LocalEventsEmpty)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kTestMetricsJSON));

    const auto& localEvents = config.getLocalEvents();
    EXPECT_TRUE(localEvents.empty());
}

TEST(LocalEventsTest, LocalEventsValidation)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kLocalEventsJSON));

    // Build a set of known events from local definitions
    std::set<std::string> knownEvents;
    for (const auto& [name, fields] : config.getLocalEvents())
        knownEvents.insert(name);

    auto result = config.validateEvents([&knownEvents](const std::string& event) {
        return knownEvents.count(event) > 0;
    });

    // "Custom Metric" uses MY_CUSTOM_EVENT.SUB which is locally defined
    EXPECT_TRUE(result.allValid());
    ASSERT_EQ(result.metrics.size(), 1u);
    EXPECT_TRUE(result.metrics[0].valid);
}

// --- Multi-Row Layout Tests ---

static const char* kMultiRowJSON = R"json({
    "metrics": [
        {"name":"PCIRdCur",       "formula":"A", "aggregation":"socket"},
        {"name":"PCIRdCur Miss",  "formula":"B", "aggregation":"socket"},
        {"name":"PCIRdCur Hit",   "formula":"C", "aggregation":"socket"},
        {"name":"ItoM",           "formula":"D", "aggregation":"socket"},
        {"name":"Total Read (B)", "formula":"A+B","aggregation":"system"}
    ],
    "layout": {
        "sections": [
            {
                "title": "PCIe Data",
                "rows": ["Total", "Miss", "Hit"],
                "columns": {
                    "PCIRdCur Events": ["PCIRdCur", "PCIRdCur Miss", "PCIRdCur Hit"],
                    "ItoM Events": ["ItoM"]
                },
                "system-wide-metrics": ["Total Read (B)"]
            }
        ]
    }
})json";

TEST(MultiRowLayoutTest, ParsesRowLabels)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kMultiRowJSON));
    const auto& layout = config.getLayout();
    ASSERT_EQ(layout.size(), 1u);
    EXPECT_TRUE(layout[0].isMultiRow());
    ASSERT_EQ(layout[0].rowLabels.size(), 3u);
    EXPECT_EQ(layout[0].rowLabels[0], "Total");
    EXPECT_EQ(layout[0].rowLabels[1], "Miss");
    EXPECT_EQ(layout[0].rowLabels[2], "Hit");
}

TEST(MultiRowLayoutTest, ParsesColumns)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kMultiRowJSON));
    const auto& layout = config.getLayout();
    ASSERT_EQ(layout[0].columns.size(), 2u);
    // Column order preserved
    EXPECT_EQ(layout[0].columns[0].first, "PCIRdCur Events");
    ASSERT_EQ(layout[0].columns[0].second.size(), 3u);
    EXPECT_EQ(layout[0].columns[0].second[0], "PCIRdCur");
    EXPECT_EQ(layout[0].columns[0].second[1], "PCIRdCur Miss");
    EXPECT_EQ(layout[0].columns[0].second[2], "PCIRdCur Hit");
    EXPECT_EQ(layout[0].columns[1].first, "ItoM Events");
    ASSERT_EQ(layout[0].columns[1].second.size(), 1u);
    EXPECT_EQ(layout[0].columns[1].second[0], "ItoM");
}

TEST(MultiRowLayoutTest, ParsesSystemWideMetrics)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kMultiRowJSON));
    const auto& layout = config.getLayout();
    ASSERT_EQ(layout[0].systemWideMetrics.size(), 1u);
    EXPECT_EQ(layout[0].systemWideMetrics[0], "Total Read (B)");
}

TEST(MultiRowLayoutTest, ParsesTitle)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kMultiRowJSON));
    EXPECT_EQ(config.getLayout()[0].title, "PCIe Data");
    // Flat-metrics list should be empty for a multi-row section
    EXPECT_TRUE(config.getLayout()[0].metrics.empty());
}

TEST(MultiRowLayoutTest, FlatSectionIsNotMultiRow)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(kLayoutMetricsJSON));
    const auto& layout = config.getLayout();
    ASSERT_GE(layout.size(), 1u);
    EXPECT_FALSE(layout[0].isMultiRow());
    EXPECT_FALSE(layout[0].metrics.empty());
}

TEST(MultiRowLayoutTest, MixedLayout)
{
    MetricsConfig config;
    ASSERT_TRUE(config.loadFromString(R"json({
        "metrics": [
            {"name":"A","formula":"x","aggregation":"socket"},
            {"name":"B","formula":"y","aggregation":"socket"},
            {"name":"C","formula":"z","aggregation":"socket"}
        ],
        "layout": {
            "sections": [
                {
                    "title": "Multi",
                    "rows": ["Row1", "Row2"],
                    "columns": {
                        "Col Group": ["A", "B"]
                    }
                },
                {
                    "title": "Flat",
                    "metrics": ["C"]
                }
            ]
        }
    })json"));
    const auto& layout = config.getLayout();
    ASSERT_EQ(layout.size(), 2u);
    EXPECT_TRUE(layout[0].isMultiRow());
    EXPECT_EQ(layout[0].title, "Multi");
    EXPECT_FALSE(layout[1].isMultiRow());
    EXPECT_EQ(layout[1].title, "Flat");
    ASSERT_EQ(layout[1].metrics.size(), 1u);
    EXPECT_EQ(layout[1].metrics[0], "C");
}

// --- Multi-Row Rendering Tests (via TableRenderer directly) ---

class MultiRowRenderTest : public ::testing::Test {
protected:
    TableRenderer renderer;
};

TEST_F(MultiRowRenderTest, ColumnCountMatchesHeaders)
{
    // Simulate 2 sockets x 3 sub-rows with 2 column groups
    renderer.setHeaders({"Skt", "", "ColA", "ColB"});
    renderer.addSectionHeader("Section Title");
    // Socket 0
    renderer.addRow({"0", "Total", "100", "200"});
    renderer.addRow({"", "Miss",  "30",  "50"});
    renderer.addRow({"", "Hit",   "70",  "150"});
    // Socket 1
    renderer.addRow({"1", "Total", "110", "210"});
    renderer.addRow({"", "Miss",  "35",  "55"});
    renderer.addRow({"", "Hit",   "75",  "155"});

    std::string result = renderer.renderToString();
    EXPECT_NE(result.find("Section Title"), std::string::npos);
    EXPECT_NE(result.find("ColA"), std::string::npos);
    EXPECT_NE(result.find("ColB"), std::string::npos);
    EXPECT_NE(result.find("Total"), std::string::npos);
    EXPECT_NE(result.find("Miss"), std::string::npos);
    EXPECT_NE(result.find("Hit"), std::string::npos);
    EXPECT_NE(result.find("100"), std::string::npos);
    EXPECT_NE(result.find("200"), std::string::npos);
}

TEST_F(MultiRowRenderTest, EmptyCellForShortColumn)
{
    // "Short Col" has only 1 metric mapped to row 0; rows 1 and 2 get empty cells
    renderer.setHeaders({"Skt", "", "Full Col", "Short Col"});
    renderer.addRow({"0", "Total", "1000", "500"});
    renderer.addRow({"",  "Miss",  "200",  ""});    // empty for Short Col
    renderer.addRow({"",  "Hit",   "800",  ""});    // empty for Short Col

    std::string result = renderer.renderToString();
    EXPECT_NE(result.find("1000"), std::string::npos);
    EXPECT_NE(result.find("500"), std::string::npos);
    EXPECT_NE(result.find("200"), std::string::npos);
    EXPECT_NE(result.find("800"), std::string::npos);
    EXPECT_GT(result.size(), 0u);
}

TEST_F(MultiRowRenderTest, SocketNumberOnlyInFirstSubRow)
{
    renderer.setHeaders({"Skt", "", "Val"});
    renderer.addRow({"0", "Total", "100"});
    renderer.addRow({"",  "Miss",  "30"});
    renderer.addRow({"",  "Hit",   "70"});

    std::string result = renderer.renderToString();
    // The socket number "0" should appear right-aligned in the Skt cell exactly once.
    // Skt column width = max(len("Skt")=3, len("0")=1, len("")=0) + 2 = 5.
    // Right-aligned "0" in a 5-wide cell: "   0 " bordered by the vertical box char.
    std::string pattern = std::string(B_V) + "   0 " + B_V;
    size_t count = 0;
    size_t pos = 0;
    while ((pos = result.find(pattern, pos)) != std::string::npos) {
        ++count;
        ++pos;
    }
    EXPECT_EQ(count, 1u) << "Socket number '0' should appear in exactly one row\n" << result;
}

TEST_F(MultiRowRenderTest, SystemSectionAfterMultiRows)
{
    renderer.setHeaders({"Skt", "", "Events"});
    renderer.addSectionHeader("PCIe Data");
    renderer.addRow({"0", "Total", "1000"});
    renderer.addRow({"",  "Miss",  "200"});
    renderer.addRow({"",  "Hit",   "800"});
    renderer.addSystemSection("System Wide", {{"TotRd", "1000"}, {"TotWr", "500"}});

    std::string result = renderer.renderToString();
    EXPECT_NE(result.find("PCIe Data"), std::string::npos);
    EXPECT_NE(result.find("System Wide"), std::string::npos);
    EXPECT_NE(result.find("TotRd"), std::string::npos);
    EXPECT_NE(result.find("TotWr"), std::string::npos);
    EXPECT_NE(result.find("1000"), std::string::npos);
    EXPECT_NE(result.find("500"), std::string::npos);
}
