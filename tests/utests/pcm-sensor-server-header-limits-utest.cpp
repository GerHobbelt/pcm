// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2009-2025, Intel Corporation

// Regression tests for the unbounded HTTP header consumption issue in the
// server-side request parser (src/pcm-sensor-server.cpp, the
// operator>>( basic_socketstream&, HTTPRequest& ) header loop, CWE-770).
//
// An unauthenticated remote client could previously drive unbounded memory
// growth by sending either a very large number of distinct headers or one
// folded header whose value was extended with whitespace-continuation lines
// without limit. The parser now enforces three ceilings before storing or
// extending header data:
//   * kMaxHeaderCount      - maximum number of distinct headers
//   * kMaxHeaderLineBytes  - maximum size of a single CRLF-terminated header line
//   * kMaxTotalHeaderBytes - maximum cumulative header bytes per request
//
// Each test below drives the real request parser through a socketpair and
// verifies that an over-limit request is rejected with a std::runtime_error,
// while a well-formed request within the limits is accepted.

#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <string>
#include <thread>

// Pull the real request parser out of pcm-sensor-server.cpp without bringing
// in its main(). The same mechanism is used by the other sensor-server tests.
#define UNIT_TEST 1
#include "../../src/pcm-sensor-server.cpp"
#undef UNIT_TEST

#include <gtest/gtest.h>

namespace {

// Write the full request payload to the peer end of the socketpair from a
// background thread, then shut the write direction down so the parser sees a
// clean end-of-stream if it ever reaches it. Writing from a separate thread
// avoids deadlocking on payloads larger than the socket's send buffer.
class RequestWriter {
    RequestWriter( const RequestWriter& ) = delete;
    RequestWriter& operator=( const RequestWriter& ) = delete;
public:
    RequestWriter( int fd, std::string payload )
        : fd_( fd ), payload_( std::move( payload ) ),
          thread_( [this]() { run(); } ) {}

    ~RequestWriter() {
        if ( thread_.joinable() )
            thread_.join();
        ::close( fd_ );
    }

private:
    void run() {
        size_t off = 0;
        while ( off < payload_.size() ) {
            ssize_t n = ::write( fd_, payload_.data() + off, payload_.size() - off );
            if ( n <= 0 ) {
                if ( n < 0 && ( errno == EINTR ) )
                    continue;
                break; // peer closed or error; nothing more we can do
            }
            off += static_cast<size_t>( n );
        }
        ::shutdown( fd_, SHUT_WR );
    }

    int fd_;
    std::string payload_;
    std::thread thread_;
};

// Drive the parser over a socketpair with the given raw request bytes and
// return whether parsing threw (the rejection path). server_fd is closed by
// the socketstream destructor; the writer thread owns client_fd's write side.
void parseThrows( const std::string& request, bool& threw ) {
    int sv[2];
    ASSERT_EQ( 0, ::socketpair( AF_UNIX, SOCK_STREAM, 0, sv ) )
        << "socketpair failed: " << std::strerror( errno );

    RequestWriter writer( sv[1], request );

    socketstream rs( sv[0] );
    HTTPRequest req;
    threw = false;
    try {
        rs >> req;
    } catch ( std::exception const& ) {
        threw = true;
    }
    // sv[0] is closed by rs' destructor; sv[1] is closed by RequestWriter
    // after its thread finishes and joins.
}

} // namespace

// A well-formed request that stays within every limit must be accepted.
TEST( PcmSensorServerHeaderLimitsTest, AcceptsRequestWithinLimits ) {
    std::string req = "GET / HTTP/1.1\r\nHost: 127.0.0.1\r\n";
    for ( size_t i = 0; i < kMaxHeaderCount - 1; ++i ) {
        req += "X-Pad-" + std::to_string( i ) + ": a\r\n";
    }
    req += "\r\n";
    bool threw = false;
    parseThrows( req, threw );
    EXPECT_FALSE( threw )
        << "Request with " << kMaxHeaderCount
        << " headers (at the limit) should be accepted.";
}

// More than kMaxHeaderCount distinct headers must be rejected (header flood).
TEST( PcmSensorServerHeaderLimitsTest, RejectsTooManyDistinctHeaders ) {
    std::string req = "GET / HTTP/1.1\r\n";
    for ( size_t i = 0; i < kMaxHeaderCount + 50; ++i ) {
        req += "X-Pad-" + std::to_string( i ) + ": a\r\n";
    }
    req += "\r\n";
    bool threw = false;
    parseThrows( req, threw );
    EXPECT_TRUE( threw )
        << "A request exceeding kMaxHeaderCount distinct headers must be rejected.";
}

// A single folded header extended past kMaxHeaderLineBytes / the cumulative
// kMaxTotalHeaderBytes cap via whitespace-continuation lines must be rejected.
TEST( PcmSensorServerHeaderLimitsTest, RejectsOversizedFoldedHeader ) {
    std::string req = "GET / HTTP/1.1\r\nX-Fold: a\r\n";
    // Each continuation line begins with a space (folding) and adds bytes to
    // the same logical header value. Enough lines to blow past the total cap.
    const std::string cont = " " + std::string( 4096, 'a' ) + "\r\n";
    const size_t lines = ( kMaxTotalHeaderBytes / cont.size() ) + 8;
    for ( size_t i = 0; i < lines; ++i ) {
        req += cont;
    }
    req += "\r\n";
    bool threw = false;
    parseThrows( req, threw );
    EXPECT_TRUE( threw )
        << "An unbounded folded header must be rejected once it exceeds the byte caps.";
}

// Many headers whose cumulative size exceeds kMaxTotalHeaderBytes but whose
// count stays under kMaxHeaderCount must still be rejected by the byte cap.
TEST( PcmSensorServerHeaderLimitsTest, RejectsOversizedTotalHeaderBytes ) {
    std::string req = "GET / HTTP/1.1\r\n";
    // ~2 KB per header * 60 headers ~= 120 KB > kMaxTotalHeaderBytes (64 KB),
    // while staying under kMaxHeaderCount.
    const std::string value( 2048, 'a' );
    for ( size_t i = 0; i < 60; ++i ) {
        req += "X-Big-" + std::to_string( i ) + ": " + value + "\r\n";
    }
    req += "\r\n";
    bool threw = false;
    parseThrows( req, threw );
    EXPECT_TRUE( threw )
        << "Cumulative header bytes exceeding kMaxTotalHeaderBytes must be rejected.";
}
