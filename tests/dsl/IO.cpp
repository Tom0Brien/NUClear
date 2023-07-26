/*
 * Copyright (C) 2013      Trent Houliston <trent@houliston.me>, Jake Woods <jake.f.woods@gmail.com>
 *               2014-2017 Trent Houliston <trent@houliston.me>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
 * documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
 * WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
 * COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
 * OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */

#include <array>
#include <catch.hpp>

#include "test_util/TestBase.hpp"

// Windows can't do this test as it doesn't have file descriptors
#ifndef _WIN32

    #include <unistd.h>

    #include <nuclear>

namespace {

/// @brief Events that occur during the test
std::vector<std::string> events;  // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

class TestReactor : public NUClear::Reactor {
public:
    TestReactor(std::unique_ptr<NUClear::Environment> environment) : Reactor(std::move(environment)) {

        std::array<int, 2> fds{-1, -1};

        if (::pipe(fds.data()) < 0) {
            events.push_back("Pipe creation failed");
            return;
        }

        in  = fds[0];
        out = fds[1];
        events.push_back("Pipe created");

        on<IO>(in.get(), IO::READ).then([this](const IO::Event& e) {
            // Read from our fd
            char c{0};
            auto bytes = ::read(e.fd, &c, 1);

            events.push_back("Read " + std::to_string(bytes) + " bytes (" + c + ") from pipe");

            if (c == 'o') {
                powerplant.shutdown();
            }
        });

        writer = on<IO>(out.get(), IO::WRITE).then([this](const IO::Event& e) {
            // Send data into our fd
            const char c       = "Hello"[char_no++];
            const ssize_t sent = ::write(e.fd, &c, 1);

            events.push_back("Wrote " + std::to_string(sent) + " bytes (" + c + ") to pipe");

            if (char_no == 5) {
                writer.unbind();
            }
        });
    }

    NUClear::util::FileDescriptor in{};
    NUClear::util::FileDescriptor out{};
    int char_no{0};
    ReactionHandle writer{};
};
}  // namespace

TEST_CASE("Testing the IO extension", "[api][io]") {

    NUClear::PowerPlant::Configuration config;
    config.thread_count = 1;
    NUClear::PowerPlant plant(config);
    plant.install<TestReactor>();
    plant.start();

    std::vector<std::string> expected = {
        "Pipe created",
        "Wrote 1 bytes (H) to pipe",
        "Read 1 bytes (H) from pipe",
        "Wrote 1 bytes (e) to pipe",
        "Read 1 bytes (e) from pipe",
        "Wrote 1 bytes (l) to pipe",
        "Read 1 bytes (l) from pipe",
        "Wrote 1 bytes (l) to pipe",
        "Read 1 bytes (l) from pipe",
        "Wrote 1 bytes (o) to pipe",
        "Read 1 bytes (o) from pipe",
    };

    // Make an info print the diff in an easy to read way if we fail
    INFO(test_util::diff_string(expected, events));

    // Check the events fired in order and only those events
    REQUIRE(events == expected);
}

#else   // _WIN32

TEST_CASE("Testing the IO extension", "[api][io]") {
    SKIP("IO extension is not supported on Windows")
}

#endif  // _WIN32
