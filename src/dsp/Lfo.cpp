#include "dsp/Lfo.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kSeed = 0x1234567u;
constexpr double kFadeCycles = 4.0; // |fade| = 1 spans this many LFO cycles
constexpr double kExpCurve = 5.0;   // steepness of the Exponential wave
} // namespace

void Lfo::setSampleRate(double newSampleRate)
{
    if (newSampleRate > 0.0)
    {
        sampleRate = newSampleRate;
        phaseInc = static_cast<double>(rateHz) / sampleRate;
    }
}

void Lfo::setShape(LfoShape newShape)
{
    shape = newShape;
}

void Lfo::setTrigMode(LfoTrigMode mode)
{
    trigMode = mode;
}

void Lfo::setCustomShape(const float* points, int count)
{
    customPoints.assign(points, points + std::max(0, count));
    if (!customPoints.empty())
        shape = LfoShape::Custom;
}

void Lfo::setRate(float hz)
{
    rateHz = hz; // negative = backward
    phaseInc = static_cast<double>(rateHz) / sampleRate;
}

void Lfo::setSlew(float newSlew)
{
    slew = std::clamp(newSlew, 0.0f, 1.0f);
}

void Lfo::setFade(float newFade)
{
    fade = std::clamp(newFade, -1.0f, 1.0f);
}

void Lfo::setDepth(float newDepth)
{
    depth = std::clamp(newDepth, 0.0f, 1.0f);
}

void Lfo::setPhaseOffset(float newPhase)
{
    phaseOffset = std::clamp(newPhase, 0.0f, 1.0f);
}

void Lfo::reset()
{
    phase = phaseOffset;
    rngState = kSeed;
    heldRandom = nextRandom();
    prevRandom = heldRandom;
    fadeCounter = 0.0;
    latched = 0.0f;
    running = true;
}

void Lfo::retrigger()
{
    phase = phaseOffset;
}

void Lfo::noteTrigger()
{
    fadeCounter = 0.0; // the fade restarts on every trig

    // A restarting mode draws a fresh random value, so Random is new on every hit
    // (glide starts from wherever the output is now).
    const bool restarts = trigMode == LfoTrigMode::Trig || trigMode == LfoTrigMode::OneShot ||
                          trigMode == LfoTrigMode::Half;
    if (restarts && shape == LfoShape::Random)
    {
        prevRandom = shapeValue(phase);
        heldRandom = nextRandom();
    }

    switch (trigMode)
    {
    case LfoTrigMode::Free:
        break; // keep free-running
    case LfoTrigMode::Trig:
        phase = phaseOffset;
        break;
    case LfoTrigMode::Hold:
        latched = shapeValue(phase) * depth * fadeGain(); // sample the free-running value now
        break;
    case LfoTrigMode::OneShot:
    case LfoTrigMode::Half:
        phase = phaseOffset;
        running = true;
        break;
    }
}

double Lfo::divisionToHz(double bpm, double beatsPerCycle)
{
    if (beatsPerCycle <= 0.0)
        return 0.0;
    return (bpm / 60.0) / beatsPerCycle;
}

double Lfo::tempoRateHz(double speed, double multiplier, double bpm)
{
    // speed x multiplier = 128 -> one cycle per bar; a 4/4 bar lasts 240 / bpm seconds.
    if (bpm <= 0.0)
        return 0.0;
    return speed * multiplier / 128.0 * (bpm / 240.0);
}

float Lfo::fadeGain() const
{
    if (std::abs(fade) < 1.0e-6f || rateHz == 0.0f)
        return 1.0f;
    const double fadeSamples = std::abs(static_cast<double>(fade)) * kFadeCycles * sampleRate /
                               std::abs(static_cast<double>(rateHz));
    const double t = std::min(1.0, fadeCounter / fadeSamples);
    return static_cast<float>(fade < 0.0f ? t : 1.0 - t);
}

void Lfo::stepRandom()
{
    prevRandom = heldRandom;
    heldRandom = nextRandom();
}

float Lfo::nextRandom()
{
    // xorshift32 -> bipolar float in [-1, 1]; deterministic for reproducible tests.
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    const auto unit = static_cast<double>(rngState) / static_cast<double>(0xFFFFFFFFu);
    return static_cast<float>(2.0 * unit - 1.0);
}

float Lfo::shapeValue(double p) const
{
    switch (shape)
    {
    case LfoShape::Sine:
        return static_cast<float>(std::sin(2.0 * kPi * p));
    case LfoShape::Triangle:
        return p < 0.5 ? static_cast<float>(4.0 * p - 1.0) : static_cast<float>(3.0 - 4.0 * p);
    case LfoShape::SawUp:
        return static_cast<float>(2.0 * p - 1.0);
    case LfoShape::SawDown:
        return static_cast<float>(1.0 - 2.0 * p);
    case LfoShape::Square:
        return p < 0.5 ? 1.0f : -1.0f;
    case LfoShape::Random:
    {
        if (slew <= 0.0f)
            return heldRandom;
        // Glide from the previous value over the first `slew` of the step.
        const double progress = rateHz >= 0.0f ? p : 1.0 - p;
        const double t = std::min(1.0, progress / static_cast<double>(slew));
        return prevRandom + (heldRandom - prevRandom) * static_cast<float>(t);
    }
    case LfoShape::Exponential:
        return static_cast<float>((std::exp(-kExpCurve * p) - std::exp(-kExpCurve)) /
                                  (1.0 - std::exp(-kExpCurve)));
    case LfoShape::Ramp:
        return static_cast<float>(p);
    case LfoShape::Custom:
    {
        if (customPoints.empty())
            return 0.0f;
        const auto count = static_cast<double>(customPoints.size());
        const double x = std::clamp(p, 0.0, 1.0) * count;
        const auto i0 = static_cast<int>(x) % static_cast<int>(count);
        const int i1 = (i0 + 1) % static_cast<int>(count);
        const auto frac = static_cast<float>(x - std::floor(x));
        return customPoints[static_cast<size_t>(i0)] * (1.0f - frac) +
               customPoints[static_cast<size_t>(i1)] * frac;
    }
    }
    return 0.0f;
}

void Lfo::advancePhase()
{
    const double before = phase;
    phase += phaseInc;

    // Half: stop (and hold) when the run reaches the middle of the cycle.
    if (trigMode == LfoTrigMode::Half && running)
    {
        const bool crossed =
            phaseInc >= 0.0 ? (before < 0.5 && phase >= 0.5) : (before > 0.5 && phase <= 0.5);
        if (crossed)
        {
            phase = 0.5;
            running = false;
            latched = shapeValue(phase) * depth * fadeGain();
            return;
        }
    }

    // Wrap in either direction (a negative rate runs backwards).
    if (phase >= 1.0 || phase < 0.0)
    {
        phase += phase >= 1.0 ? -1.0 : 1.0;
        stepRandom();
        if (trigMode == LfoTrigMode::OneShot)
            running = false; // one cycle done -> freeze the last value
    }
}

float Lfo::getNextSample()
{
    if (trigMode == LfoTrigMode::Hold)
    {
        advancePhase(); // keep the internal phase moving so the next trig samples afresh
        return latched;
    }
    if ((trigMode == LfoTrigMode::OneShot || trigMode == LfoTrigMode::Half) && !running)
        return latched;

    const float out = shapeValue(phase) * depth * fadeGain();
    latched = out;
    fadeCounter += 1.0;
    advancePhase();
    return out;
}

float Lfo::peek() const
{
    const bool stopped =
        (trigMode == LfoTrigMode::OneShot || trigMode == LfoTrigMode::Half) && !running;
    if (trigMode == LfoTrigMode::Hold || stopped)
        return latched;
    return shapeValue(phase) * depth * fadeGain();
}

float Lfo::advance(int numSamples)
{
    const float out = getNextSample();
    for (int i = 1; i < numSamples; ++i)
        getNextSample();
    return out;
}

} // namespace cold::dsp
