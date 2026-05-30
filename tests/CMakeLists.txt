# Catch2 unit-test target for the framework-agnostic DSP code (no plugin host).
add_executable(ColdTests
    test_main.cpp)

target_link_libraries(ColdTests PRIVATE
    cold_dsp
    Catch2::Catch2WithMain)

target_compile_features(ColdTests PRIVATE cxx_std_20)

include(Catch)
catch_discover_tests(ColdTests)

# ----------------------------------------------------------------------------
# JUCE-linked test target for the thin I/O layer (file decode). Separate from
# ColdTests so the framework-agnostic suites stay JUCE-free and fast.
# ----------------------------------------------------------------------------
add_executable(ColdIoTests
    test_sample_loader.cpp)

target_link_libraries(ColdIoTests PRIVATE
    cold_io
    Catch2::Catch2WithMain
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_features(ColdIoTests PRIVATE cxx_std_20)

catch_discover_tests(ColdIoTests)
