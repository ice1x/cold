include(Catch)

# ----------------------------------------------------------------------------
# Shared, header-only test fixtures (deterministic signal + file writers).
# Available to every JUCE-linked suite via "support/SignalFixtures.h".
# ----------------------------------------------------------------------------
add_library(cold_test_support INTERFACE)
target_include_directories(cold_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})

# Common JUCE warning/config flags for the linked suites.
set(COLD_TEST_JUCE_FLAGS
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# UNIT — framework-agnostic DSP (no JUCE, no plugin host). Fast inner loop.
# ----------------------------------------------------------------------------
add_executable(ColdTests
    unit/test_dsp_sanity.cpp
    unit/test_amp_envelope.cpp
    unit/test_lfo.cpp
    unit/test_mod_matrix.cpp
    unit/test_morph_filter.cpp
    unit/test_sample_voice.cpp
    unit/test_engine.cpp)
target_link_libraries(ColdTests PRIVATE
    cold_dsp
    Catch2::Catch2WithMain)
target_compile_features(ColdTests PRIVATE cxx_std_20)
catch_discover_tests(ColdTests PROPERTIES LABELS "unit")

# ----------------------------------------------------------------------------
# UNIT — JUCE-backed I/O layer (SampleLoader decode), in isolation.
# ----------------------------------------------------------------------------
add_executable(ColdIoTests
    unit/test_sample_loader.cpp
    unit/test_sample_bank.cpp)
target_link_libraries(ColdIoTests PRIVATE
    cold_io
    cold_test_support
    Catch2::Catch2WithMain
    ${COLD_TEST_JUCE_FLAGS})
target_compile_features(ColdIoTests PRIVATE cxx_std_20)
catch_discover_tests(ColdIoTests PROPERTIES LABELS "unit")

# ----------------------------------------------------------------------------
# INTEGRATION — multiple units in a realistic workflow (filesystem + multi-
# format bank load, stream/asset paths). One level above the unit cases.
# ----------------------------------------------------------------------------
add_executable(ColdIntegrationTests
    integration/test_sample_loader_integration.cpp)
target_link_libraries(ColdIntegrationTests PRIVATE
    cold_io
    cold_test_support
    Catch2::Catch2WithMain
    ${COLD_TEST_JUCE_FLAGS})
target_compile_features(ColdIntegrationTests PRIVATE cxx_std_20)
catch_discover_tests(ColdIntegrationTests PROPERTIES LABELS "integration")

# ----------------------------------------------------------------------------
# E2E — the full plugin processor driven as a host drives it. Compiles the
# processor/editor sources directly (no plugin wrapper) so the AudioProcessor
# can be instantiated and exercised headless.
# ----------------------------------------------------------------------------
add_executable(ColdE2ETests
    e2e/test_processor_e2e.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_SOURCE_DIR}/src/PluginEditor.cpp)
target_include_directories(ColdE2ETests PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ColdE2ETests PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0)
target_link_libraries(ColdE2ETests PRIVATE
    cold_dsp
    cold_io
    cold_test_support
    juce::juce_audio_utils
    Catch2::Catch2WithMain
    ${COLD_TEST_JUCE_FLAGS})
target_compile_features(ColdE2ETests PRIVATE cxx_std_20)
catch_discover_tests(ColdE2ETests PROPERTIES LABELS "e2e")
