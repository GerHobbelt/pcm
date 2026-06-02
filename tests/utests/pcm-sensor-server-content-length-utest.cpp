// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation

// Regression test for the Content-Length integer-conversion / unbounded
// allocation issue in src/pcm-sensor-server.cpp.
//
// HTTPHeader::headerValueAsNumber() used to parse the Content-Length value
// with std::stoll() and return it directly as a size_t. A negative value such
// as "-1" therefore wrapped to SIZE_MAX, and arbitrarily large positive values
// were accepted as attacker-controlled allocation lengths (CWE-190/CWE-400).
//
// The fixed implementation rejects negative values and values above
// kMaxRequestBodyBytes before converting to size_t. This test exercises that
// helper directly and verifies:
//   1. "-1" is rejected instead of wrapping to a huge size_t,
//   2. an oversized positive Content-Length is rejected,
//   3. a valid, in-bounds value is accepted and returned unchanged.

#include <stdexcept>
#include <string>

// Pull the real HTTPHeader implementation out of pcm-sensor-server.cpp without
// bringing in its main(). The same mechanism is already used by
// tests/pcm-sensor-server-fuzz.cpp and the overflow unit test.
#define UNIT_TEST 1
#include "../../src/pcm-sensor-server.cpp"
#undef UNIT_TEST

#include <gtest/gtest.h>

TEST(PcmSensorServerContentLengthTest, NegativeContentLengthIsRejected)
{
    HTTPHeader const h( "Content-Length", "-1" );
    // Must throw rather than wrapping -1 to SIZE_MAX.
    EXPECT_THROW(h.headerValueAsNumber(), std::runtime_error);
}

TEST(PcmSensorServerContentLengthTest, OversizedContentLengthIsRejected)
{
    long long const tooLarge = kMaxRequestBodyBytes + 1;
    HTTPHeader const h( "Content-Length", std::to_string( tooLarge ) );
    EXPECT_THROW(h.headerValueAsNumber(), std::runtime_error);
}

TEST(PcmSensorServerContentLengthTest, ValidContentLengthIsAccepted)
{
    HTTPHeader const h( "Content-Length", "1024" );
    EXPECT_EQ(static_cast<size_t>( 1024 ), h.headerValueAsNumber());
}

TEST(PcmSensorServerContentLengthTest, MaximumContentLengthIsAccepted)
{
    HTTPHeader const h( "Content-Length", std::to_string( kMaxRequestBodyBytes ) );
    EXPECT_EQ(static_cast<size_t>( kMaxRequestBodyBytes ), h.headerValueAsNumber());
}
