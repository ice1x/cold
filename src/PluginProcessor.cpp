#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "dsp/SampleVoice.h"
#include "io/BatteryKit.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace cold
{

namespace
{
constexpr auto kGainId = "gain";
constexpr auto kPolyphonyId = "polyphony";
constexpr auto kLoopModeId = "loopMode";
constexpr auto kLoopStartId = "loopStart";
constexpr auto kLoopEndId = "loopEnd";
constexpr auto kRootNoteId = "rootNote";
constexpr auto kAttackId = "ampAttack";
constexpr auto kDecayId = "ampDecay";
constexpr auto kSustainId = "ampSustain";
constexpr auto kReleaseId = "ampRelease";
constexpr auto kFilterCutoffId = "filterCutoff";
constexpr auto kFilterResonanceId = "filterResonance";
constexpr auto kFilterTypeId = "filterType";
constexpr auto kLfoShapeId = "lfoShape";
constexpr auto kLfoRateId = "lfoRate";
constexpr auto kLfoDepthId = "lfoDepth";
constexpr auto kLfoSyncId = "lfoSync";
constexpr auto kLfoDivisionId = "lfoDivision";
constexpr auto kLfoToCutoffId = "lfoToCutoff";
constexpr auto kLfoToTypeId = "lfoToType";
constexpr auto kLfoToPitchId = "lfoToPitch";
constexpr auto kLfoToSampleId = "lfoToSampleSelect";
constexpr auto kLfoTrigModeId = "lfoTrigMode";
constexpr auto kTrackId = "track";
constexpr auto kAmpDelayId = "ampDelay";
constexpr auto kAmpHoldId = "ampHold";
constexpr auto kAmpAtkCurveId = "ampAttackCurve";
constexpr auto kAmpDecCurveId = "ampDecayCurve";
constexpr auto kAmpRelCurveId = "ampReleaseCurve";
constexpr auto kAmpVelId = "ampVelocity";
constexpr auto kModAttackId = "modAttack";
constexpr auto kModDecayId = "modDecay";
constexpr auto kModSustainId = "modSustain";
constexpr auto kModReleaseId = "modRelease";
constexpr auto kModToCutoffId = "modToCutoff";
constexpr auto kModToTypeId = "modToType";
constexpr auto kPitchEnvAmtId = "pitchEnvAmount";
constexpr auto kPitchEnvDecayId = "pitchEnvDecay";
constexpr auto kVelToPitchId = "velToPitch";
constexpr auto kCompOnId = "compOn";
constexpr auto kCompThreshId = "compThreshold";
constexpr auto kCompRatioId = "compRatio";
constexpr auto kCompAttackId = "compAttack";
constexpr auto kCompReleaseId = "compRelease";
constexpr auto kCompMakeupId = "compMakeup";
constexpr auto kSatOnId = "satOn";
constexpr auto kSatDriveId = "satDrive";
constexpr auto kSatMixId = "satMix";
constexpr auto kCrushOnId = "crushOn";
constexpr auto kCrushBitsId = "crushBits";
constexpr auto kCrushRateId = "crushRate";
constexpr auto kMidiAddressId = "midiAddressing";
constexpr auto kMapNoteId = "mapNote";

// Each cell (track) owns kNumLayers dedicated sample slots for layering: cell t's
// layer l is slot (t * kNumLayers + l). The bank is these ranges laid end to end,
// so it is an internal store — there is no separate shared "sample bank" any more.
constexpr int kNumSlots = dsp::MultiEngine::kNumTracks * ColdAudioProcessor::kNumLayers;

// Bank slot backing cell `track`'s layer `layer`.
constexpr int slotForLayer(int track, int layer)
{
    return track * ColdAudioProcessor::kNumLayers + layer;
}

// Host-sync divisions (parallel to the lfoDivision choice list), in quarter-note
// beats per LFO cycle: "1/16" = 0.25 beats ... "4 bars" = 16 beats.
constexpr std::array<double, 7> kDivisionBeats{16.0, 8.0, 4.0, 2.0, 1.0, 0.5, 0.25};

// A time range in seconds with a perceptual skew (more resolution near zero).
juce::NormalisableRange<float> timeRange(float maxSeconds)
{
    juce::NormalisableRange<float> range(0.0f, maxSeconds, 0.0001f);
    range.setSkewForCentre(0.2f);
    return range;
}

// Per-track / per-layer parameter IDs: "t{track}_{base}" and "t{track}_lyr{layer}_{base}".
juce::String trackParamId(int track, const char* base)
{
    return "t" + juce::String(track) + "_" + base;
}
juce::String layerParamId(int track, int layer, const char* base)
{
    return "t" + juce::String(track) + "_lyr" + juce::String(layer) + "_" + base;
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout ColdAudioProcessor::createParameterLayout()
{
    using juce::AudioParameterBool;
    using juce::AudioParameterChoice;
    using juce::AudioParameterFloat;
    using juce::AudioParameterInt;

    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kGainId, 1}, "Gain", juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f),
        0.0f));

    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{kPolyphonyId, 1}, "Polyphony",
                                                   1, 16, 8));

    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLoopModeId, 1}, "Loop Mode",
        juce::StringArray{"One-Shot", "Forward", "Ping-Pong"}, 0));

    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLoopStartId, 1},
                                                     "Loop Start", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLoopEndId, 1}, "Loop End",
                                                     0.0f, 1.0f, 1.0f));

    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{kRootNoteId, 1}, "Root Note",
                                                   0, 127, 60));

    // Amp envelope and the morph filter (cutoff/resonance/type) are PER-TRACK (each
    // track/cell is an independent sound) — created in the per-track loop below.

    // LFO: shape, free rate (Hz) or host-tempo sync + division, depth, and the two
    // modulation amounts — to cutoff (octaves) and, distinctively, to filter type.
    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLfoShapeId, 1}, "LFO Shape",
        juce::StringArray{"Sine", "Triangle", "Saw Up", "Saw Down", "Square", "Random", "Custom"},
        0));
    juce::NormalisableRange<float> rateRange(0.01f, 40.0f, 0.01f);
    rateRange.setSkewForCentre(2.0f);
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLfoRateId, 1}, "LFO Rate",
                                                     rateRange, 2.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kLfoDepthId, 1}, "LFO Depth",
                                                     0.0f, 1.0f, 1.0f));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID{kLfoSyncId, 1},
                                                          "LFO Sync", false));
    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLfoDivisionId, 1}, "LFO Division",
        juce::StringArray{"4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16"}, 4));
    // How the LFO reacts to each note: Free (ignore), Trig (restart phase), Hold
    // (latch its value at the trig), One-Shot (one cycle per note, then freeze).
    layout.add(std::make_unique<AudioParameterChoice>(
        juce::ParameterID{kLfoTrigModeId, 1}, "LFO Trig Mode",
        juce::StringArray{"Free", "Trig", "Hold", "One-Shot"}, 0));
    // The LFO designer: eight step levels forming the Custom waveform (used when
    // LFO Shape = Custom). Defaults sample one sine cycle so Custom starts musical.
    constexpr std::array<float, kNumCustomSteps> kStepDefaults{0.0f, 0.707f,  1.0f,  0.707f,
                                                               0.0f, -0.707f, -1.0f, -0.707f};
    for (int i = 0; i < kNumCustomSteps; ++i)
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{"lfoStep" + juce::String(i + 1), 1},
            "LFO Step " + juce::String(i + 1), -1.0f, 1.0f, kStepDefaults[static_cast<size_t>(i)]));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToCutoffId, 1}, "LFO -> Cutoff",
        juce::NormalisableRange<float>(-4.0f, 4.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToTypeId, 1}, "LFO -> Filter Type",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToPitchId, 1}, "LFO -> Pitch",
        juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
    // LFO/mod -> sample-select: a signed slot/layer offset applied at the trig.
    // Each track is fed the whole bank, so this roams across the loaded slots.
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kLfoToSampleId, 1}, "LFO -> Sample Select",
        juce::NormalisableRange<float>(-8.0f, 8.0f, 0.01f), 0.0f));

    // Amp envelope extras (delay/hold, per-segment curves, velocity) are PER-TRACK
    // too — created in the per-track loop below.

    // Modulation envelope + its routing to the filter (env -> cutoff / type).
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModAttackId, 1},
                                                     "Mod Attack", timeRange(5.0f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModDecayId, 1}, "Mod Decay",
                                                     timeRange(5.0f), 0.2f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModSustainId, 1},
                                                     "Mod Sustain", 0.0f, 1.0f, 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(juce::ParameterID{kModReleaseId, 1},
                                                     "Mod Release", timeRange(5.0f), 0.1f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kModToCutoffId, 1}, "Mod Env -> Cutoff",
        juce::NormalisableRange<float>(-6.0f, 6.0f, 0.01f), 0.0f));
    layout.add(std::make_unique<AudioParameterFloat>(
        juce::ParameterID{kModToTypeId, 1}, "Mod Env -> Filter Type",
        juce::NormalisableRange<float>(-1.0f, 1.0f, 0.01f), 0.0f));

    // Selected cell for editing / the Load buttons. All cells play; MIDI channel N
    // routes to cell N-1. Each cell plays its own layer slots.
    layout.add(std::make_unique<AudioParameterInt>(juce::ParameterID{kTrackId, 1}, "Track", 0,
                                                   dsp::MultiEngine::kNumTracks - 1, 0));

    // MIDI addressing: Channel (channel N -> track N-1) or Note Map (one channel,
    // note number picks the track like a GM drum map). In Note Map the sample
    // plays at its natural pitch; the base note maps to track 0.
    layout.add(std::make_unique<AudioParameterChoice>(juce::ParameterID{kMidiAddressId, 1},
                                                      "MIDI Addressing",
                                                      juce::StringArray{"Channel", "Note Map"}, 0));

    // Multilayer one-shot stacks: per track, a layer mode (Select = one sample per
    // trig; Stack = all layers together) plus kNumLayers layers. Each layer is a
    // bank slot (-1 = off) with per-layer gain and tune. Defaults keep track t on
    // its own slot t in layer 0 so Stack behaves like the single-sample case until
    // the user adds layers.
    for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
    {
        const auto tp = "T" + juce::String(t + 1) + " ";
        layout.add(std::make_unique<AudioParameterChoice>(
            juce::ParameterID{trackParamId(t, "layerMode"), 1}, tp + "Layer Mode",
            juce::StringArray{"Single", "Stack", "Round-Robin", "Velocity"}, 0));

        // Choke / mute group (0 = off, 1..8). Cells sharing a non-zero group cut each
        // other off: firing one releases every voice in the group (classic hi-hat
        // choke). Battery-style "MuteGroup".
        layout.add(std::make_unique<AudioParameterInt>(
            juce::ParameterID{trackParamId(t, "chokeGroup"), 1}, tp + "Choke Group", 0, 8, 0));

        // Reverse playback: the cell plays its sample backwards (reverse one-shot).
        layout.add(std::make_unique<AudioParameterBool>(
            juce::ParameterID{trackParamId(t, "reverse"), 1}, tp + "Reverse", false));

        // Per-track amp envelope (each track/cell is an independent sound).
        layout.add(
            std::make_unique<AudioParameterFloat>(juce::ParameterID{trackParamId(t, kAttackId), 1},
                                                  tp + "Amp Attack", timeRange(5.0f), 0.001f));
        layout.add(
            std::make_unique<AudioParameterFloat>(juce::ParameterID{trackParamId(t, kDecayId), 1},
                                                  tp + "Amp Decay", timeRange(5.0f), 0.0f));
        layout.add(
            std::make_unique<AudioParameterFloat>(juce::ParameterID{trackParamId(t, kSustainId), 1},
                                                  tp + "Amp Sustain", 0.0f, 1.0f, 1.0f));
        layout.add(
            std::make_unique<AudioParameterFloat>(juce::ParameterID{trackParamId(t, kReleaseId), 1},
                                                  tp + "Amp Release", timeRange(5.0f), 0.05f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kAmpDelayId), 1}, tp + "Amp Delay", timeRange(5.0f),
            0.0f));
        layout.add(
            std::make_unique<AudioParameterFloat>(juce::ParameterID{trackParamId(t, kAmpHoldId), 1},
                                                  tp + "Amp Hold", timeRange(5.0f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kAmpAtkCurveId), 1}, tp + "Amp Attack Curve", -1.0f,
            1.0f, 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kAmpDecCurveId), 1}, tp + "Amp Decay Curve", -1.0f,
            1.0f, 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kAmpRelCurveId), 1}, tp + "Amp Release Curve", -1.0f,
            1.0f, 0.0f));
        layout.add(
            std::make_unique<AudioParameterFloat>(juce::ParameterID{trackParamId(t, kAmpVelId), 1},
                                                  tp + "Amp Velocity", 0.0f, 1.0f, 0.0f));

        // Per-track morph filter: cutoff (log-ish skew), resonance/Q, LP->BP->HP->
        // Notch morph type. Cutoff defaults wide open so a fresh cell is bright.
        juce::NormalisableRange<float> cutoffRange(20.0f, 20000.0f, 1.0f);
        cutoffRange.setSkewForCentre(1000.0f);
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kFilterCutoffId), 1}, tp + "Filter Cutoff",
            cutoffRange, 20000.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kFilterResonanceId), 1}, tp + "Filter Resonance",
            juce::NormalisableRange<float>(0.1f, 20.0f, 0.01f, 0.5f), 0.707f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kFilterTypeId), 1}, tp + "Filter Type", 0.0f, 1.0f,
            0.0f));

        // Per-track pitch envelope (drum "blip"): semitone amount + decay time.
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kPitchEnvAmtId), 1}, tp + "Pitch Env Amount",
            juce::NormalisableRange<float>(-48.0f, 48.0f, 0.01f), 0.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kPitchEnvDecayId), 1}, tp + "Pitch Env Decay",
            timeRange(5.0f), 0.05f));

        // Per-track velocity -> pitch (Battery-style): semitones at full velocity.
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kVelToPitchId), 1}, tp + "Velocity to Pitch",
            juce::NormalisableRange<float>(-48.0f, 48.0f, 0.01f), 0.0f));

        // Per-cell Note Map key: the MIDI note that triggers this cell (Note Map
        // mode). Default is a chromatic map from C1 (36) so cells are addressable
        // out of the box; a .kit import sets each cell's own rootKey (GM-drum style).
        layout.add(std::make_unique<AudioParameterInt>(
            juce::ParameterID{trackParamId(t, kMapNoteId), 1}, tp + "Note Map Key", 0, 127,
            juce::jmin(127, 36 + t)));

        // Per-track compressor (punch/glue on the cell's summed output).
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{trackParamId(t, kCompOnId), 1}, tp + "Comp On", false));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kCompThreshId), 1}, tp + "Comp Threshold",
            juce::NormalisableRange<float>(-60.0f, 0.0f, 0.1f), -12.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kCompRatioId), 1}, tp + "Comp Ratio",
            juce::NormalisableRange<float>(1.0f, 20.0f, 0.01f, 0.5f), 4.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kCompAttackId), 1}, tp + "Comp Attack",
            juce::NormalisableRange<float>(0.0001f, 0.1f, 0.0001f, 0.4f), 0.005f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kCompReleaseId), 1}, tp + "Comp Release",
            juce::NormalisableRange<float>(0.005f, 1.0f, 0.001f, 0.4f), 0.1f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kCompMakeupId), 1}, tp + "Comp Makeup",
            juce::NormalisableRange<float>(0.0f, 24.0f, 0.1f), 0.0f));

        // Per-track saturator (drive/warmth). Drive in dB of pre-gain into the tanh.
        layout.add(std::make_unique<juce::AudioParameterBool>(
            juce::ParameterID{trackParamId(t, kSatOnId), 1}, tp + "Sat On", false));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kSatDriveId), 1}, tp + "Sat Drive",
            juce::NormalisableRange<float>(0.0f, 36.0f, 0.1f), 6.0f));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kSatMixId), 1}, tp + "Sat Mix",
            juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f), 1.0f));

        // Per-track lo-fi bit crusher / sample-rate reducer (vintage grit).
        layout.add(std::make_unique<AudioParameterBool>(
            juce::ParameterID{trackParamId(t, kCrushOnId), 1}, tp + "Crush On", false));
        layout.add(std::make_unique<AudioParameterFloat>(
            juce::ParameterID{trackParamId(t, kCrushBitsId), 1}, tp + "Crush Bits",
            juce::NormalisableRange<float>(1.0f, 16.0f, 0.1f), 16.0f));
        layout.add(std::make_unique<AudioParameterInt>(
            juce::ParameterID{trackParamId(t, kCrushRateId), 1}, tp + "Crush Rate", 1, 64, 1));

        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto lp = tp + "L" + juce::String(l + 1) + " ";
            // A layer plays its own dedicated slot (slotForLayer(t, l)); it is loaded
            // via loadSampleToLayer, so there is no per-layer slot-select parameter.
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "gain"), 1}, lp + "Gain", 0.0f, 1.0f, 1.0f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "tune"), 1}, lp + "Tune",
                juce::NormalisableRange<float>(-24.0f, 24.0f, 0.01f), 0.0f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "pan"), 1}, lp + "Pan", -1.0f, 1.0f, 0.0f));
            // Optional per-layer filter (overrides the track filter for this layer).
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{layerParamId(t, l, "filterOn"), 1}, lp + "Filter On", false));
            juce::NormalisableRange<float> layerCutoff(20.0f, 20000.0f, 1.0f);
            layerCutoff.setSkewForCentre(1000.0f);
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "cutoff"), 1}, lp + "Cutoff", layerCutoff,
                20000.0f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "ftype"), 1}, lp + "Filter Type", 0.0f, 1.0f,
                0.0f));
            // Optional per-layer amp envelope (the core of layering). Off = the layer
            // keeps the cell's shared envelope; on = its own DAHDSR. Defaults mirror
            // the cell envelope so turning it on is a neutral starting point.
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{layerParamId(t, l, "ampOn"), 1}, lp + "Env On", false));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "ampDelay"), 1}, lp + "Env Delay",
                timeRange(5.0f), 0.0f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "ampAttack"), 1}, lp + "Env Attack",
                timeRange(5.0f), 0.001f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "ampHold"), 1}, lp + "Env Hold",
                timeRange(5.0f), 0.0f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "ampDecay"), 1}, lp + "Env Decay",
                timeRange(5.0f), 0.0f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "ampSustain"), 1}, lp + "Env Sustain", 0.0f,
                1.0f, 1.0f));
            layout.add(std::make_unique<AudioParameterFloat>(
                juce::ParameterID{layerParamId(t, l, "ampRelease"), 1}, lp + "Env Release",
                timeRange(5.0f), 0.05f));
            // Per-layer mute / solo (stack mixing). Mute silences the layer; if any
            // layer in the cell is soloed, only soloed layers sound.
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{layerParamId(t, l, "mute"), 1}, lp + "Mute", false));
            layout.add(std::make_unique<juce::AudioParameterBool>(
                juce::ParameterID{layerParamId(t, l, "solo"), 1}, lp + "Solo", false));
        }
    }

    return layout;
}

ColdAudioProcessor::ColdAudioProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "PARAMS", createParameterLayout()), bank(kNumSlots)
{
    gainParam = apvts.getRawParameterValue(kGainId);
    polyphonyParam = apvts.getRawParameterValue(kPolyphonyId);
    loopModeParam = apvts.getRawParameterValue(kLoopModeId);
    loopStartParam = apvts.getRawParameterValue(kLoopStartId);
    loopEndParam = apvts.getRawParameterValue(kLoopEndId);
    rootNoteParam = apvts.getRawParameterValue(kRootNoteId);
    lfoShapeParam = apvts.getRawParameterValue(kLfoShapeId);
    lfoRateParam = apvts.getRawParameterValue(kLfoRateId);
    lfoDepthParam = apvts.getRawParameterValue(kLfoDepthId);
    lfoSyncParam = apvts.getRawParameterValue(kLfoSyncId);
    lfoDivisionParam = apvts.getRawParameterValue(kLfoDivisionId);
    lfoToCutoffParam = apvts.getRawParameterValue(kLfoToCutoffId);
    lfoToTypeParam = apvts.getRawParameterValue(kLfoToTypeId);
    lfoToPitchParam = apvts.getRawParameterValue(kLfoToPitchId);
    lfoToSampleParam = apvts.getRawParameterValue(kLfoToSampleId);
    lfoTrigModeParam = apvts.getRawParameterValue(kLfoTrigModeId);
    for (int i = 0; i < kNumCustomSteps; ++i)
        lfoStepParams[static_cast<size_t>(i)] =
            apvts.getRawParameterValue("lfoStep" + juce::String(i + 1));
    trackParam = apvts.getRawParameterValue(kTrackId);

    // Each track is fed the whole bank as its sample-select list; size the shared
    // windowed-pointer storage to match.
    slotPtrs.resize(static_cast<size_t>(bank.numSlots()));
    slotNumFrames.assign(static_cast<size_t>(bank.numSlots()), 0);
    modAttackParam = apvts.getRawParameterValue(kModAttackId);
    modDecayParam = apvts.getRawParameterValue(kModDecayId);
    modSustainParam = apvts.getRawParameterValue(kModSustainId);
    modReleaseParam = apvts.getRawParameterValue(kModReleaseId);
    modToCutoffParam = apvts.getRawParameterValue(kModToCutoffId);
    modToTypeParam = apvts.getRawParameterValue(kModToTypeId);
    midiAddressParam = apvts.getRawParameterValue(kMidiAddressId);

    for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
    {
        const auto tt = static_cast<size_t>(t);
        layerModeParam[tt] = apvts.getRawParameterValue(trackParamId(t, "layerMode"));
        chokeGroupParam[tt] = apvts.getRawParameterValue(trackParamId(t, "chokeGroup"));
        reverseParam[tt] = apvts.getRawParameterValue(trackParamId(t, "reverse"));
        attackParam[tt] = apvts.getRawParameterValue(trackParamId(t, kAttackId));
        decayParam[tt] = apvts.getRawParameterValue(trackParamId(t, kDecayId));
        sustainParam[tt] = apvts.getRawParameterValue(trackParamId(t, kSustainId));
        releaseParam[tt] = apvts.getRawParameterValue(trackParamId(t, kReleaseId));
        ampDelayParam[tt] = apvts.getRawParameterValue(trackParamId(t, kAmpDelayId));
        ampHoldParam[tt] = apvts.getRawParameterValue(trackParamId(t, kAmpHoldId));
        ampAtkCurveParam[tt] = apvts.getRawParameterValue(trackParamId(t, kAmpAtkCurveId));
        ampDecCurveParam[tt] = apvts.getRawParameterValue(trackParamId(t, kAmpDecCurveId));
        ampRelCurveParam[tt] = apvts.getRawParameterValue(trackParamId(t, kAmpRelCurveId));
        ampVelParam[tt] = apvts.getRawParameterValue(trackParamId(t, kAmpVelId));
        filterCutoffParam[tt] = apvts.getRawParameterValue(trackParamId(t, kFilterCutoffId));
        filterResonanceParam[tt] = apvts.getRawParameterValue(trackParamId(t, kFilterResonanceId));
        filterTypeParam[tt] = apvts.getRawParameterValue(trackParamId(t, kFilterTypeId));
        pitchEnvAmtParam[tt] = apvts.getRawParameterValue(trackParamId(t, kPitchEnvAmtId));
        pitchEnvDecayParam[tt] = apvts.getRawParameterValue(trackParamId(t, kPitchEnvDecayId));
        velToPitchParam[tt] = apvts.getRawParameterValue(trackParamId(t, kVelToPitchId));
        mapNoteParam[tt] = apvts.getRawParameterValue(trackParamId(t, kMapNoteId));
        compOnParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCompOnId));
        compThreshParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCompThreshId));
        compRatioParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCompRatioId));
        compAttackParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCompAttackId));
        compReleaseParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCompReleaseId));
        compMakeupParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCompMakeupId));
        satOnParam[tt] = apvts.getRawParameterValue(trackParamId(t, kSatOnId));
        satDriveParam[tt] = apvts.getRawParameterValue(trackParamId(t, kSatDriveId));
        satMixParam[tt] = apvts.getRawParameterValue(trackParamId(t, kSatMixId));
        crushOnParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCrushOnId));
        crushBitsParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCrushBitsId));
        crushRateParam[tt] = apvts.getRawParameterValue(trackParamId(t, kCrushRateId));
        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto ll = static_cast<size_t>(l);
            layerGainParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "gain"));
            layerTuneParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "tune"));
            layerPanParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "pan"));
            layerFilterOnParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "filterOn"));
            layerCutoffParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "cutoff"));
            layerTypeParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "ftype"));
            layerAmpOnParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "ampOn"));
            layerAmpDelayParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "ampDelay"));
            layerAmpAttackParam[tt][ll] =
                apvts.getRawParameterValue(layerParamId(t, l, "ampAttack"));
            layerAmpHoldParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "ampHold"));
            layerAmpDecayParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "ampDecay"));
            layerAmpSustainParam[tt][ll] =
                apvts.getRawParameterValue(layerParamId(t, l, "ampSustain"));
            layerAmpReleaseParam[tt][ll] =
                apvts.getRawParameterValue(layerParamId(t, l, "ampRelease"));
            layerMuteParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "mute"));
            layerSoloParam[tt][ll] = apvts.getRawParameterValue(layerParamId(t, l, "solo"));
        }
    }
    layerScratch.reserve(static_cast<size_t>(kNumLayers));
}

void ColdAudioProcessor::prepareToPlay(double sampleRate, int)
{
    const juce::ScopedLock sl(engineLock);
    multi.prepare(sampleRate);
    lastPolyphony = -1; // force a re-apply of polyphony on the next block
    updateEngineParameters();
}

void ColdAudioProcessor::releaseResources() {}

bool ColdAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void ColdAudioProcessor::applySharedParamsToTrack(int track, bool polyChanged)
{
    auto& e = multi.track(track);

    if (polyChanged)
        e.setPolyphony(static_cast<int>(polyphonyParam->load()));

    const auto mode =
        static_cast<dsp::SampleVoice::LoopMode>(static_cast<int>(loopModeParam->load()));
    e.setLoopMode(mode);
    e.setReverse(reverseParam[static_cast<size_t>(track)]->load() > 0.5f);

    // Loop points are relative to the track's base slot (slot == track index).
    const int frames = slotNumFrames[static_cast<size_t>(track)];
    if (frames > 0)
    {
        const int start = static_cast<int>(loopStartParam->load() * static_cast<float>(frames));
        const int end = static_cast<int>(loopEndParam->load() * static_cast<float>(frames));
        if (end > start)
            e.setLoopPoints(start, end);
    }

    const auto tt = static_cast<size_t>(track);
    e.setAmpEnvelope(attackParam[tt]->load(), decayParam[tt]->load(), sustainParam[tt]->load(),
                     releaseParam[tt]->load());
    e.setAmpEnvelopeDAH(ampDelayParam[tt]->load(), ampHoldParam[tt]->load());
    e.setAmpCurves(ampAtkCurveParam[tt]->load(), ampDecCurveParam[tt]->load(),
                   ampRelCurveParam[tt]->load());
    e.setAmpVelocity(ampVelParam[tt]->load());
    e.setModEnvelope(modAttackParam->load(), modDecayParam->load(), modSustainParam->load(),
                     modReleaseParam->load());
    e.setModRouting(modToCutoffParam->load(), modToTypeParam->load());
    e.setPitchEnvelope(pitchEnvAmtParam[tt]->load(), pitchEnvDecayParam[tt]->load());
    e.setVelToPitch(velToPitchParam[tt]->load());
    e.setCompressor(compOnParam[tt]->load() > 0.5f, compThreshParam[tt]->load(),
                    compRatioParam[tt]->load(), compAttackParam[tt]->load(),
                    compReleaseParam[tt]->load(), compMakeupParam[tt]->load());
    e.setSaturator(satOnParam[tt]->load() > 0.5f,
                   std::pow(10.0f, satDriveParam[tt]->load() / 20.0f), // dB -> linear pre-gain
                   satMixParam[tt]->load());
    e.setBitCrusher(crushOnParam[tt]->load() > 0.5f, crushBitsParam[tt]->load(),
                    static_cast<int>(crushRateParam[tt]->load()));
    e.setFilter(filterCutoffParam[tt]->load(), filterResonanceParam[tt]->load(),
                filterTypeParam[tt]->load());

    // LFO: free rate in Hz, or host-tempo synced via the selected division.
    const auto shape = static_cast<dsp::LfoShape>(static_cast<int>(lfoShapeParam->load()));
    float rateHz = lfoRateParam->load();
    if (lfoSyncParam->load() > 0.5f)
    {
        const auto idx =
            static_cast<size_t>(std::clamp(static_cast<int>(lfoDivisionParam->load()), 0,
                                           static_cast<int>(kDivisionBeats.size()) - 1));
        rateHz = static_cast<float>(dsp::Lfo::divisionToHz(hostBpm, kDivisionBeats[idx]));
    }
    e.setLfo(shape, rateHz, lfoDepthParam->load());
    e.setLfoTrigMode(static_cast<dsp::LfoTrigMode>(static_cast<int>(lfoTrigModeParam->load())));
    // The Custom shape is driven by the designer's step levels.
    if (shape == dsp::LfoShape::Custom)
    {
        std::array<float, kNumCustomSteps> steps{};
        for (int i = 0; i < kNumCustomSteps; ++i)
            steps[static_cast<size_t>(i)] = lfoStepParams[static_cast<size_t>(i)]->load();
        e.setLfoCustomShape(steps.data(), kNumCustomSteps);
    }

    e.clearModRoutes();
    if (std::abs(lfoToCutoffParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::FilterCutoff, lfoToCutoffParam->load());
    if (std::abs(lfoToTypeParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::FilterType, lfoToTypeParam->load());
    if (std::abs(lfoToPitchParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::Pitch, lfoToPitchParam->load());
    if (std::abs(lfoToSampleParam->load()) > 1.0e-4f)
        e.addModRoute(dsp::ModDestination::SampleSelect, lfoToSampleParam->load());

    // Multilayer: Single (one sample per trig — layer 0, or LFO->sample roaming the
    // cell's loaded layers), Stack (all loaded layers at once), RoundRobin (one loaded
    // layer per trig, cycling) or Velocity (one loaded layer chosen by hit strength).
    // Each layer plays its own dedicated slot, so `sampleIndex` is the LOCAL index l
    // into the track's per-cell sample list built by rebuildTrackSamples. A layer with
    // no sample loaded contributes nothing.
    const auto layerMode =
        static_cast<dsp::Engine::LayerMode>(static_cast<int>(layerModeParam[tt]->load()));
    e.setLayerMode(layerMode);
    if (layerMode == dsp::Engine::LayerMode::Stack ||
        layerMode == dsp::Engine::LayerMode::RoundRobin ||
        layerMode == dsp::Engine::LayerMode::Velocity)
    {
        // Solo takes priority: if any layer in the cell is soloed, only soloed layers
        // sound. Otherwise every non-muted loaded layer sounds.
        bool anySolo = false;
        for (int l = 0; l < kNumLayers; ++l)
            if (layerSoloParam[tt][static_cast<size_t>(l)]->load() > 0.5f)
                anySolo = true;

        layerScratch.clear();
        for (int l = 0; l < kNumLayers; ++l)
        {
            const auto ll = static_cast<size_t>(l);
            if (!bank.isSlotLoaded(slotForLayer(track, l)))
                continue; // empty layer -> silent
            const bool muted = layerMuteParam[tt][ll]->load() > 0.5f;
            const bool soloed = layerSoloParam[tt][ll]->load() > 0.5f;
            if (muted || (anySolo && !soloed))
                continue; // muted, or another layer is soloed -> excluded from the mix
            dsp::Engine::Layer layer;
            layer.sampleIndex = l;
            layer.gain = layerGainParam[tt][ll]->load();
            layer.tuneSemitones = layerTuneParam[tt][ll]->load();
            layer.enabled = true;
            layer.pan = layerPanParam[tt][ll]->load();
            layer.filterOverride = layerFilterOnParam[tt][ll]->load() > 0.5f;
            layer.cutoffHz = layerCutoffParam[tt][ll]->load();
            layer.resonance = 0.707f;
            layer.type = layerTypeParam[tt][ll]->load();
            // Per-layer amp envelope (core of layering): its own DAHDSR when Env On.
            layer.ampOverride = layerAmpOnParam[tt][ll]->load() > 0.5f;
            layer.ampDelay = layerAmpDelayParam[tt][ll]->load();
            layer.ampAttack = layerAmpAttackParam[tt][ll]->load();
            layer.ampHold = layerAmpHoldParam[tt][ll]->load();
            layer.ampDecay = layerAmpDecayParam[tt][ll]->load();
            layer.ampSustain = layerAmpSustainParam[tt][ll]->load();
            layer.ampRelease = layerAmpReleaseParam[tt][ll]->load();
            layerScratch.push_back(layer);
        }
        e.setLayers(layerScratch);
    }
}

void ColdAudioProcessor::updateEngineParameters()
{
    // Shared sound params drive every track (each keeps its own voice pool + filter
    // + LFO state). Polyphony is destructive (resets the pool) so it is guarded.
    const int poly = static_cast<int>(polyphonyParam->load());
    const bool polyChanged = poly != lastPolyphony;
    if (polyChanged)
        lastPolyphony = poly;

    for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
        applySharedParamsToTrack(t, polyChanged);
}

void ColdAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const juce::ScopedLock sl(engineLock);

    const int numChannels = buffer.getNumChannels();
    const int numSamples = buffer.getNumSamples();
    buffer.clear();

    // Capture the host tempo for LFO sync (falls back to the last known BPM).
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
            if (auto bpm = pos->getBpm())
                hostBpm = *bpm;

    updateEngineParameters();

    // Channel-pointer view for the engine (up to a normal stereo pair).
    float* channels[2] = {nullptr, nullptr};
    for (int ch = 0; ch < numChannels && ch < 2; ++ch)
        channels[ch] = buffer.getWritePointer(ch);
    const int engineChannels = juce::jmin(numChannels, 2);

    const int rootNote = static_cast<int>(rootNoteParam->load());

    // Render in segments split on MIDI event boundaries for sample-accurate trigs.
    // Notes route to a track either by MIDI channel (Digitakt-style: channel N ->
    // track N-1) or, in Note Map mode, by note number like a GM drum map.
    const bool noteMap = midiAddressParam->load() > 0.5f;

    // Fire cell `t`, applying its choke / mute group first: if it belongs to a
    // non-zero group, release every voice in that group (itself included) so only one
    // sound in the group rings at a time — classic hi-hat choke.
    auto fireCell = [this](int t, int note, float vel, int root)
    {
        const int group = static_cast<int>(chokeGroupParam[static_cast<size_t>(t)]->load());
        if (group > 0)
            for (int tt = 0; tt < dsp::MultiEngine::kNumTracks; ++tt)
                if (static_cast<int>(chokeGroupParam[static_cast<size_t>(tt)]->load()) == group)
                    multi.stopAllNotes(tt);
        multi.noteOn(t, note, vel, root);
    };

    int pos = 0;
    for (const auto meta : midi)
    {
        const int eventTime = juce::jlimit(0, numSamples, meta.samplePosition);
        if (eventTime > pos)
        {
            multi.renderNextBlock(channels, engineChannels, pos, eventTime - pos);
            pos = eventTime;
        }

        const auto msg = meta.getMessage();
        const bool isOn = msg.isNoteOn() && msg.getFloatVelocity() > 0.0f;
        const bool isOff = msg.isNoteOff() || msg.isNoteOn(); // note-on vel 0 is a note-off

        if (noteMap && (isOn || isOff))
        {
            // The note number triggers every cell whose Note Map key matches it, so
            // cells can sit on arbitrary (non-consecutive) notes like a GM drum map,
            // and several cells can share a note. Each plays at its natural pitch
            // (trigger at rootNote so the playback ratio is 1).
            const int note = msg.getNoteNumber();
            for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
            {
                if (static_cast<int>(mapNoteParam[static_cast<size_t>(t)]->load()) != note)
                    continue;
                if (isOn)
                    fireCell(t, rootNote, msg.getFloatVelocity(), rootNote);
                else
                    multi.noteOff(t, rootNote);
            }
        }
        else if (isOn)
        {
            const int t = dsp::MultiEngine::trackForChannel(msg.getChannel());
            if (t >= 0)
                fireCell(t, msg.getNoteNumber(), msg.getFloatVelocity(), rootNote);
        }
        else if (isOff)
            multi.routeNoteOff(msg.getChannel(), msg.getNoteNumber());
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
            multi.reset();
    }

    if (pos < numSamples)
        multi.renderNextBlock(channels, engineChannels, pos, numSamples - pos);

    const float gainLinear = juce::Decibels::decibelsToGain(gainParam->load(), -60.0f);
    buffer.applyGain(gainLinear);
}

void ColdAudioProcessor::rebuildTrackSamples()
{
    // Each cell owns kNumLayers dedicated slots: track t's sample list is exactly
    // its own layers [slotForLayer(t, 0) .. slotForLayer(t, kNumLayers-1)], so the
    // LOCAL sample index l is layer l. Base index 0 is layer 0 (what Single plays);
    // an LFO->sample-select roams the cell's loaded layers (round-robin). Each slot
    // carries its own [start, length) window; empty layers are invalid refs.
    for (int t = 0; t < dsp::MultiEngine::kNumTracks; ++t)
    {
        std::vector<dsp::Engine::SampleRef> refs(static_cast<size_t>(kNumLayers));
        for (int l = 0; l < kNumLayers; ++l)
        {
            const int k = slotForLayer(t, l);
            const auto& s = bank.slot(k);
            auto& ptrs = slotPtrs[static_cast<size_t>(k)];
            ptrs.clear();

            if (!s.isLoaded())
            {
                slotNumFrames[static_cast<size_t>(k)] = 0;
                continue; // leave refs[l] as a default (invalid) SampleRef
            }

            const int start = s.effectiveStart();
            const int length = s.effectiveLength();
            for (const auto& ch : s.data.channels)
                ptrs.push_back(ch.data() + start);
            slotNumFrames[static_cast<size_t>(k)] = length;

            refs[static_cast<size_t>(l)] = {ptrs.data(), s.data.numChannels(), length,
                                            s.data.sampleRate};
        }

        auto& e = multi.track(t);
        e.setSampleList(refs);
        e.setBaseSampleIndex(0); // layer 0 is the cell's primary sample
    }
    lastPolyphony = -1; // setSampleList reset the pools; force a param re-apply
}

bool ColdAudioProcessor::loadSampleToSlot(int slot, const juce::File& file)
{
    const juce::ScopedLock sl(engineLock);
    if (!bank.loadSlot(slot, file))
        return false;
    // A slot belongs to one cell's layer set, so rebuild the tracks' sample views.
    rebuildTrackSamples();
    return true;
}

bool ColdAudioProcessor::loadSampleToLayer(int track, int layer, const juce::File& file)
{
    if (track < 0 || track >= dsp::MultiEngine::kNumTracks || layer < 0 || layer >= kNumLayers)
        return false;
    return loadSampleToSlot(slotForLayer(track, layer), file);
}

bool ColdAudioProcessor::loadSampleToTrack(int track, const juce::File& file)
{
    return loadSampleToLayer(track, 0, file); // the cell's primary sample (layer 0)
}

bool ColdAudioProcessor::loadSampleFile(const juce::File& file)
{
    return loadSampleToTrack(selectedTrack(), file);
}

int ColdAudioProcessor::loadFolder(const juce::File& directory)
{
    if (!directory.isDirectory())
        return 0;

    auto files = directory.findChildFiles(juce::File::findFiles, false, "*.wav;*.aiff;*.aif;*.mp3");
    files.sort();

    int loaded = 0;
    for (const auto& file : files)
    {
        if (loaded >= dsp::MultiEngine::kNumTracks)
            break; // one sample per cell (its layer 0)
        if (loadSampleToTrack(loaded, file))
            ++loaded;
    }
    return loaded;
}

int ColdAudioProcessor::importBatteryKit(const juce::File& kitFile, const juce::File& sampleFolder)
{
    const auto kit = io::parseBatteryKitFile(kitFile);
    if (!kit.valid())
        return 0;

    auto setParam = [this](const juce::String& id, float value)
    {
        if (auto* p = apvts.getParameter(id))
            p->setValueNotifyingHost(p->convertTo0to1(value));
    };

    // Resolve a Battery-stored sample name to a real file under the folder
    // (recursive). Handles exact and truncated names via io::bestKitSampleMatch —
    // Battery often truncates the stored name. Returns a non-existent File if
    // nothing matches. Enumerated once per call (kits hold a few dozen cells).
    auto resolveSample = [&sampleFolder](const juce::String& name) -> juce::File
    {
        if (name.isEmpty() || !sampleFolder.isDirectory())
            return {};
        const auto files = sampleFolder.findChildFiles(juce::File::findFiles, true, "*");
        juce::StringArray names;
        for (const auto& f : files)
            names.add(f.getFileName());
        const int idx = io::bestKitSampleMatch(name, names);
        return idx >= 0 ? files[idx] : juce::File{};
    };

    const int n = std::min(static_cast<int>(kit.cells.size()), dsp::MultiEngine::kNumTracks);
    bool mappedAnyKey = false;
    for (int t = 0; t < n; ++t)
    {
        const auto& cell = kit.cells[static_cast<size_t>(t)];

        if (const auto file = resolveSample(cell.file); file.existsAsFile())
            loadSampleToTrack(t, file); // the cell's primary sample (layer 0)

        // Per-track amp AHDSR (delay 0; curves left linear).
        setParam(trackParamId(t, kAmpDelayId), 0.0f);
        setParam(trackParamId(t, kAttackId), cell.attackSeconds());
        setParam(trackParamId(t, kAmpHoldId), cell.holdSeconds());
        setParam(trackParamId(t, kDecayId), cell.decaySeconds());
        setParam(trackParamId(t, kSustainId), cell.sustainLevel());
        setParam(trackParamId(t, kReleaseId), cell.releaseSeconds());

        // Cell tune / pan / volume land on the track's first layer.
        setParam(layerParamId(t, 0, "tune"), cell.tuneSemitones());
        setParam(layerParamId(t, 0, "pan"), cell.coldPan());
        setParam(layerParamId(t, 0, "gain"), cell.coldGain());

        // Each cell answers to its own root key (GM-drum style, non-consecutive).
        if (cell.rootKey >= 0)
        {
            setParam(trackParamId(t, kMapNoteId), static_cast<float>(cell.rootKey));
            mappedAnyKey = true;
        }
    }

    // If any cell carried a key, switch to Note Map so the kit plays by its keys.
    if (mappedAnyKey)
        setParam(kMidiAddressId, 1.0f);

    return n;
}

void ColdAudioProcessor::selectTrack(int track)
{
    track = juce::jlimit(0, dsp::MultiEngine::kNumTracks - 1, track);
    if (auto* p = apvts.getParameter(kTrackId))
        p->setValueNotifyingHost(p->convertTo0to1(static_cast<float>(track)));
}

int ColdAudioProcessor::selectedTrack() const
{
    return trackParam != nullptr ? static_cast<int>(trackParam->load()) : 0;
}

void ColdAudioProcessor::auditionCell(int track)
{
    if (track < 0 || track >= dsp::MultiEngine::kNumTracks)
        return;
    const juce::ScopedLock sl(engineLock);
    // Settle the engine parameters first so the pool is sized and configured — a
    // fresh load marks polyphony dirty, and the note must survive the next block's
    // param apply (which would otherwise reset the pool out from under it).
    updateEngineParameters();
    const int root = rootNoteParam != nullptr ? static_cast<int>(rootNoteParam->load()) : 60;
    multi.noteOn(track, root, 1.0f, root);
}

void ColdAudioProcessor::releaseCell(int track)
{
    if (track < 0 || track >= dsp::MultiEngine::kNumTracks)
        return;
    const juce::ScopedLock sl(engineLock);
    // One-shots ring out on their own (a quick click still plays the whole hit);
    // only sustaining (looping) cells are gated by releasing the note.
    const auto mode = static_cast<dsp::SampleVoice::LoopMode>(
        static_cast<int>(loopModeParam != nullptr ? loopModeParam->load() : 0.0f));
    if (mode == dsp::SampleVoice::LoopMode::OneShot)
        return;
    const int root = rootNoteParam != nullptr ? static_cast<int>(rootNoteParam->load()) : 60;
    multi.noteOff(track, root);
}

int ColdAudioProcessor::numSlots() const
{
    return bank.numSlots();
}

juce::String ColdAudioProcessor::loadedSampleName() const
{
    const auto& s = bank.slot(slotForLayer(selectedTrack(), 0)); // the cell's primary sample
    return s.isLoaded() ? s.name : juce::String("(no sample)");
}

juce::String ColdAudioProcessor::slotSampleName(int slot) const
{
    const auto& s = bank.slot(slot);
    return s.isLoaded() ? s.name : juce::String();
}

dsp::WaveformPeaks ColdAudioProcessor::slotWaveform(int slot, int numBins) const
{
    const auto& s = bank.slot(slot);
    if (!s.isLoaded())
        return {};

    const int numCh = s.data.numChannels();
    std::vector<const float*> ch(static_cast<size_t>(numCh));
    for (int c = 0; c < numCh; ++c)
        ch[static_cast<size_t>(c)] = s.data.channels[static_cast<size_t>(c)].data();

    return dsp::computeWaveformPeaks(ch.data(), numCh, 0, s.data.numFrames(), numBins);
}

int ColdAudioProcessor::slotTotalFrames(int slot) const
{
    return bank.slot(slot).numFrames();
}

int ColdAudioProcessor::slotWindowStart(int slot) const
{
    return bank.slot(slot).effectiveStart();
}

int ColdAudioProcessor::slotWindowLength(int slot) const
{
    return bank.slot(slot).effectiveLength();
}

void ColdAudioProcessor::setSlotStartFrames(int slot, int startFrames)
{
    const juce::ScopedLock sl(engineLock);
    bank.setSlotStart(slot, startFrames);
    rebuildTrackSamples(); // re-offset the tracks' sample views to the new window
}

void ColdAudioProcessor::setSlotLengthFrames(int slot, int lengthFrames)
{
    const juce::ScopedLock sl(engineLock);
    bank.setSlotLength(slot, lengthFrames);
    rebuildTrackSamples();
}

juce::AudioProcessorEditor* ColdAudioProcessor::createEditor()
{
    return new ColdAudioProcessorEditor(*this);
}

void ColdAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    // Wrapper: the APVTS params and the bank live as sibling children so the bank
    // never leaks into (and duplicates within) the parameter tree across saves.
    juce::ValueTree root("ColdState");
    root.appendChild(apvts.copyState(), nullptr);

    juce::ValueTree bankTree("bank");
    for (int i = 0; i < bank.numSlots(); ++i)
    {
        const auto& s = bank.slot(i);
        if (!s.isLoaded() || s.path.isEmpty())
            continue;
        juce::ValueTree slotTree("slot");
        slotTree.setProperty("index", i, nullptr);
        slotTree.setProperty("path", s.path, nullptr);
        slotTree.setProperty("start", s.start, nullptr);
        slotTree.setProperty("length", s.length, nullptr);
        bankTree.appendChild(slotTree, nullptr);
    }
    root.appendChild(bankTree, nullptr);

    if (auto xml = root.createXml())
        copyXmlToBinary(*xml, destData);
}

void ColdAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary(data, sizeInBytes);
    if (xml == nullptr)
        return;

    auto root = juce::ValueTree::fromXml(*xml);
    if (!root.isValid())
        return;

    if (auto params = root.getChildWithName(apvts.state.getType()); params.isValid())
        apvts.replaceState(params);

    if (auto bankTree = root.getChildWithName("bank"); bankTree.isValid())
    {
        for (int c = 0; c < bankTree.getNumChildren(); ++c)
        {
            const auto slotTree = bankTree.getChild(c);
            const int index = slotTree.getProperty("index", -1);
            const juce::String path = slotTree.getProperty("path", juce::String()).toString();
            if (index < 0 || path.isEmpty())
                continue;
            if (bank.loadSlot(index, juce::File(path)))
            {
                bank.setSlotStart(index, slotTree.getProperty("start", 0));
                bank.setSlotLength(index, slotTree.getProperty("length", 0));
            }
        }
    }

    // Rebuild every track's sample-select list from the restored bank.
    const juce::ScopedLock sl(engineLock);
    rebuildTrackSamples();
}

} // namespace cold

// Entry point used by all plugin formats.
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new cold::ColdAudioProcessor();
}
