# ----------------------------------------------------------------------------
# Framework-agnostic DSP library (no JUCE) — unit-testable from tests/.
# Sources are added here as the engine grows (M1+).
# ----------------------------------------------------------------------------
add_library(cold_dsp STATIC
    dsp/Envelope.cpp
    dsp/Lfo.cpp
    dsp/ModMatrix.cpp
    dsp/MorphFilter.cpp
    dsp/SampleVoice.cpp
    dsp/Engine.cpp
    dsp/MultiEngine.cpp
    dsp/Waveform.cpp)
target_include_directories(cold_dsp PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cold_dsp PUBLIC cxx_std_20)

# ----------------------------------------------------------------------------
# Thin JUCE-backed I/O layer (file decode). Kept separate from the DSP layer
# so it can be unit-tested via a JUCE-linked test target while the DSP classes
# stay framework-agnostic. Sample sources/banks are added here (M1, M4).
# ----------------------------------------------------------------------------
add_library(cold_io STATIC
    io/SampleLoader.cpp
    io/SampleBank.cpp
    io/BatteryKit.cpp)
target_include_directories(cold_io PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(cold_io PUBLIC cxx_std_20)
target_link_libraries(cold_io
    PUBLIC
    cold_dsp
    juce::juce_audio_formats
    PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# ----------------------------------------------------------------------------
# The plugin: AU + VST3 + Standalone instrument.
# ----------------------------------------------------------------------------
juce_add_plugin(Cold
    PLUGIN_NAME "Cold"
    PRODUCT_NAME "Cold"
    COMPANY_NAME "Cold"
    BUNDLE_ID com.cold.cold
    PLUGIN_MANUFACTURER_CODE Cold
    PLUGIN_CODE Cld1
    FORMATS AU VST3 Standalone
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

target_sources(Cold PRIVATE
    PluginProcessor.cpp
    PluginEditor.cpp)

target_compile_definitions(Cold PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(Cold PRIVATE
    cold_dsp
    cold_io
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
    juce::juce_recommended_warning_flags)
