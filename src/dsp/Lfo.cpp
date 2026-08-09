#include "dsp/Lfo.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr std::uint32_t kSeed = 0x1234567u;
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

void Lfo::setRate(float hz)
{
    rateHz = std::max(0.0f, hz);
    phaseInc = static_cast<double>(rateHz) / sampleRate;
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
}

void Lfo::retrigger()
{
    phase = phaseOffset;
}

double Lfo::divisionToHz(double bpm, double beatsPerCycle)
{
    if (beatsPerCycle <= 0.0)
        return 0.0;
    return (bpm / 60.0) / beatsPerCycle;
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
        return heldRandom;
    }
    return 0.0f;
}

float Lfo::getNextSample()
{
    const float out = shapeValue(phase) * depth;

    phase += phaseInc;
    if (phase >= 1.0)
    {
        phase -= 1.0;
        // A new sample-and-hold value at each cycle boundary.
        heldRandom = nextRandom();
    }

    return out;
}

} // namespace cold::dsp
