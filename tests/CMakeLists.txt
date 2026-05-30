# Catch2 unit-test target for the framework-agnostic DSP/IO code.
# DSP suites need no plugin host; file-decode suites (M1) link JUCE.
add_executable(ColdTests
    test_main.cpp)

target_link_libraries(ColdTests PRIVATE
    cold_dsp
    Catch2::Catch2WithMain)

target_compile_features(ColdTests PRIVATE cxx_std_20)

include(Catch)
catch_discover_tests(ColdTests)
