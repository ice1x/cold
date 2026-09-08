#pragma once

// Framework-agnostic (no JUCE) single-note sample player.
//
// Renders one voice of a track's polyphonic pool: it reads a decoded sample at a
// fractional position and repitches it (classic varispeed) so the pitch changes
// the playback speed. Looping, the amp envelope and the morph filter are layered
// on in later M1/M2 tasks; this class owns the read position and pitch math.

#include "dsp/Envelope.h"
#include "dsp/MorphFilter.h"

#include <array>

namespace cold::dsp
{

class SampleVoice
{
public:
    // Sample playback / looping behavior.
    //   OneShot  — play once to the end, then free the voice.
    //   Forward  — repeat the [loopStart, loopEnd) region indefinitely.
    //   PingPong — bounce between loopStart and loopEnd, reversing at each.
    // In every looping mode the traversal speed follows pitch (the loop plays
    // faster/slower with the note), matching classic varispeed samplers.
    enum class LoopMode
    {
        OneShot,
        Forward,
        PingPong
    };

    // Equal-tempered playback ratio: the factor by which the sample is sped up so
    // that `midiNote` sounds relative to `rootNote` (the note of native pitch).
    [[nodiscard]] static double noteToRatio(int midiNote, int rootNote);

    // Host (output) sample rate. Call before rendering; safe to call again on a
    // sample-rate change (does not disturb an in-flight note's pitch mapping).
    void prepare(double hostSampleRate);

    // Point the voice at a decoded sample. Non-owning: `channels` (an array of
    // `numChannels` pointers, each to `numFrames` floats) and its data must
    // outlive playback. `sourceSampleRate` is the sample's native rate.
    void setSample(const float* const* channels, int numChannels, int numFrames,
                   double sourceSampleRate);

    // Forget the current sample and stop the voice.
    void clearSample();

    // Select the loop behavior. Takes effect on the next rendered block.
    void setLoopMode(LoopMode mode);

    // Set the loop region in source frames. Clamped to [0, numFrames]; ignored if
    // it does not describe a positive-length window (endFrame > startFrame).
    void setLoopPoints(int startFrame, int endFrame);

    // Play the sample backwards: the note starts at the end of the playable region
    // and reads toward the start (classic reverse one-shot). Takes effect on the next
    // note. Loops still cycle, just traversed in the reverse direction.
    void setReversed(bool shouldReverse);
    [[nodiscard]] bool reversed() const noexcept { return playReversed; }

    [[nodiscard]] LoopMode loopMode() const noexcept { return mode; }
    [[nodiscard]] int loopStart() const noexcept { return loopStartFrame; }
    [[nodiscard]] int loopEnd() const noexcept { return loopEndFrame; }

    // Amp envelope shaping this voice (curved DAHDSR under the hood). setAmpEnvelope
    // sets the ADSR core (delay/hold default to 0); the granular setters add the
    // extra DAHDSR stages, per-segment curves, and velocity sensitivity.
    void setAmpEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                        float releaseSeconds);
    void setAmpEnvelopeDAH(float delaySeconds, float holdSeconds);
    void setAmpCurves(float attackCurve, float decayCurve, float releaseCurve);
    void setAmpVelocity(float sensitivity);
    [[nodiscard]] const Envelope& ampEnvelope() const noexcept { return ampEnv; }

    // Second (modulation) envelope routed to the filter: env -> cutoff (in octaves)
    // and env -> morph type (offset). The envelope only runs when a routing amount
    // is non-zero, so an unconfigured voice is unaffected.
    void setModEnvelope(float attackSeconds, float decaySeconds, float sustainLevel,
                        float releaseSeconds);
    void setModRouting(float toCutoffOctaves, float toTypeOffset);
    [[nodiscard]] bool modEnvelopeActive() const noexcept { return useModEnv; }

    // Per-voice morph filter, applied after the amp envelope (sample -> amp env ->
    // morph filter -> out). Calling setFilter enables it; it is bypassed by
    // default so a bare voice renders the raw sample. cutoff in Hz, resonance/Q,
    // type in [0, 1] (LP -> BP -> HP -> Notch).
    void setFilter(float cutoffHz, float resonance, float type);
    void setFilterEnabled(bool enabled);
    [[nodiscard]] bool filterEnabled() const noexcept { return useFilter; }

    // Dedicated band filters, independent of the morph filter above: a low-cut
    // (high-pass at `cutoffHz`, removes lows) and a high-cut (low-pass, removes
    // highs). Both off by default. Used to band-limit a layer — e.g. high-pass the
    // top layer so only the sub layer carries the bottom. Applied after the morph
    // filter (low-cut then high-cut).
    void setLowCut(bool enabled, float cutoffHz);
    void setHighCut(bool enabled, float cutoffHz);

    // Block-rate filter modulation as a RELATIVE delta around this voice's own base
    // filter: cutoff in octaves, type as a linear offset. Unlike setFilter it does
    // not touch the base, so stacked layers keep independent base filters under a
    // shared LFO. Composes additively with the per-sample mod envelope.
    void setFilterLfoMod(float cutoffOctaves, float typeOffset);

    // Pitch modulation in semitones (from an LFO / mod source), applied on top of
    // the note's base playback ratio. Takes effect immediately for the next
    // samples; 0 restores the plain note pitch. Block-rate in practice (the engine
    // sets it once per rendered block, like the filter modulation).
    void setPitchModSemitones(float semitones);

    // Static per-note tune in semitones (e.g. a layer's tune in a stack). Stacks
    // additively with the pitch modulation and survives block-rate mod updates, so
    // the two are independent channels onto the same playback ratio.
    void setTuneSemitones(float semitones);

    // Per-note pitch envelope (the classic drum "pitch blip"): at note-on the pitch
    // is offset by `amountSemitones` and decays back to the note pitch over
    // `decaySeconds`. Runs per sample, stacking onto tune + pitch mod. amount 0
    // disables it. Signed amount: positive sweeps down from above, negative up.
    void setPitchEnvelope(float amountSemitones, float decaySeconds);
    [[nodiscard]] bool pitchEnvelopeActive() const noexcept { return usePitchEnv; }

    // Velocity -> pitch (Battery-style modulation): at note-on the pitch is offset
    // by `semitones * velocity` (velocity in [0, 1]), so harder hits play higher
    // (or lower, for a negative amount). Captured per note like a static tune; 0
    // disables it. Independent of the tune and pitch-mod channels.
    void setVelToPitch(float semitones);

    // Velocity -> filter cutoff: at note-on the base cutoff is shifted by
    // `octaves * velocity`, so harder hits open (or, negative, close) the filter.
    // Captured per note; 0 disables it. Applies on top of the LFO / mod-env filter
    // modulation. No effect when the filter is off.
    void setVelToCutoff(float octaves);

    // Stereo placement in [-1, 1] (L..R). Linear balance law: centre (0) leaves
    // both channels at unity (unchanged fan-out), a hard side silences the other.
    // Used to spread a stack's layers across the panorama.
    void setPan(float pan);

    // Begin a note. `velocity` in [0, 1] scales output gain (a stand-in until the
    // amp envelope lands). `rootNote` is the MIDI note of native pitch.
    void startNote(int midiNote, float velocity = 1.0f, int rootNote = 60);

    // Immediately free the voice (no release tail yet).
    void stopNote();

    // Return to the idle state, keeping the assigned sample.
    void reset();

    [[nodiscard]] bool isActive() const noexcept { return active; }
    [[nodiscard]] int currentMidiNote() const noexcept { return midiNote; }
    [[nodiscard]] double readPosition() const noexcept { return position; }

    // Per-output-sample advance through the source (pitch ratio × rate correction).
    [[nodiscard]] double increment() const noexcept { return step; }

    // Add this voice's contribution into `out[ch][startSample .. startSample+num)`.
    // Additive so a whole voice pool sums into a shared buffer. A mono source fans
    // out to every output channel; excess source channels beyond the output are
    // dropped. When the read position runs off the end the voice frees itself.
    void renderNextBlock(float* const* out, int numOutChannels, int startSample, int numSamples);

private:
    [[nodiscard]] float readInterpolated(int channel, double pos) const;
    void updateStep(); // recompute step from baseStep + pitchMod + tuneMod

    const float* const* sourceChannels = nullptr;
    int sourceNumChannels = 0;
    int sourceNumFrames = 0;
    double sourceSampleRate = 0.0;

    double hostSampleRate = 44100.0;

    bool active = false;
    int midiNote = -1;
    float gain = 1.0f;
    double position = 0.0; // fractional read index into the source
    double baseStep = 1.0; // note ratio × rate correction (before pitch mod)
    double step = 1.0;     // input frames consumed per output sample (magnitude)
    float pitchMod = 0.0f; // pitch modulation in semitones (mod source, block-rate)
    float tuneMod = 0.0f;  // static per-note tune in semitones (e.g. a layer's tune)
    float panLeft = 1.0f;  // left/right channel gains from the pan (unity at centre)
    float panRight = 1.0f;
    int direction = 1;         // +1 forward, -1 reverse (ping-pong / reverse playback)
    bool playReversed = false; // start at the end and read toward the start

    LoopMode mode = LoopMode::OneShot;
    int loopStartFrame = 0;
    int loopEndFrame = 0;

    Envelope ampEnv;
    Envelope modEnv;
    Envelope pitchEnv; // decaying 1->0; scales the pitch-envelope amount

    float pitchEnvAmount = 0.0f; // semitones at full envelope
    bool usePitchEnv = false;

    float velToPitchAmount = 0.0f;  // semitones at full velocity (velocity -> pitch)
    float velPitchMod = 0.0f;       // captured per note: velToPitchAmount * velocity
    float velToCutoffAmount = 0.0f; // octaves at full velocity (velocity -> cutoff)
    float velCutoffMod = 0.0f;      // captured per note: velToCutoffAmount * velocity

    // Amp envelope parameter store (delay/hold added on top of the ADSR core).
    float ampDelay = 0.0f;
    float ampAttack = 0.0f;
    float ampHold = 0.0f;
    float ampDecay = 0.0f;
    float ampSustain = 1.0f;
    float ampRelease = 0.0f;

    // One filter per output channel (independent state); index 0/1 for L/R.
    std::array<MorphFilter, 2> filters;
    bool useFilter = false;
    // Dedicated band filters (low-cut = high-pass, high-cut = low-pass), read from
    // their pure HP / LP SVF outputs so they are independent of the morph filter.
    std::array<MorphFilter, 2> lowCutFilters;
    std::array<MorphFilter, 2> highCutFilters;
    bool useLowCut = false;
    bool useHighCut = false;

    // Base filter targets captured from setFilter, modulated per sample by modEnv.
    float baseCutoff = 1000.0f;
    float baseType = 0.0f;
    float modToCutoff = 0.0f; // octaves at full envelope
    float modToType = 0.0f;   // morph-type offset at full envelope
    bool useModEnv = false;

    // Block-rate LFO filter modulation (relative deltas around the base).
    float lfoCutoffOct = 0.0f;
    float lfoTypeOff = 0.0f;
    bool hasLfoFilterMod = false;
};

} // namespace cold::dsp
