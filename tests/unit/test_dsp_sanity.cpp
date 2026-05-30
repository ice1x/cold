#include <catch2/catch_test_macros.hpp>

// Scaffold sanity test (M0): proves the Catch2 + CTest wiring builds and runs.
// Real DSP suites (SampleVoice, MorphFilter, AmpEnvelope, Lfo, Engine) replace
// this from M1 onward.
TEST_CASE("scaffold sanity", "[scaffold]")
{
    REQUIRE(1 + 1 == 2);
}
