#include <catch2/catch_test_macros.hpp>

#include "dsp/Logger.h"

using cold::dsp::Logger;

TEST_CASE("Logger retains lines in order and stamps them", "[logger][dsp][unit]")
{
    auto& log = Logger::instance();
    log.clear();
    log.setCapacity(500);

    const auto before = log.sequence();
    log.log("first");
    log.log("second");

    auto lines = log.snapshot();
    REQUIRE(lines.size() >= 2);
    // The two we just pushed are the last two, in order.
    REQUIRE(lines[lines.size() - 2].find("first") != std::string::npos);
    REQUIRE(lines.back().find("second") != std::string::npos);
    // Stamped with the "[+..ms #..]" prefix.
    REQUIRE(lines.back().rfind("[+", 0) == 0);
    REQUIRE(log.sequence() == before + 2);
}

TEST_CASE("Logger drops the oldest lines past its capacity", "[logger][dsp][unit]")
{
    auto& log = Logger::instance();
    log.clear();
    log.setCapacity(3);

    for (int i = 0; i < 10; ++i)
        log.log("line" + std::to_string(i));

    auto lines = log.snapshot();
    REQUIRE(lines.size() == 3); // only the last 3 are retained
    REQUIRE(lines.front().find("line7") != std::string::npos);
    REQUIRE(lines.back().find("line9") != std::string::npos);

    log.setCapacity(500); // restore for other tests
}

TEST_CASE("Logger clear empties the ring", "[logger][dsp][unit]")
{
    auto& log = Logger::instance();
    log.setCapacity(500);
    log.log("something");
    log.clear();
    REQUIRE(log.snapshot().empty());
}
