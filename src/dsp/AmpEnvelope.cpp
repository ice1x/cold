#include "dsp/AmpEnvelope.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
// Convert a stage time in seconds to a whole number of samples. Stages are driven
// by this integer count (not a level threshold) so transitions are exact and free
// of floating-point drift over the ramp.
int secondsToSamples(float seconds, double sampleRate)
{
    return static_cast<int>(std::lround(static_cast<double>(seconds) * sampleRate));
}
} // namespace

void AmpEnvelope::setSampleRate(double newSampleRate)
{
    if (newSampleRate > 0.0)
        sampleRate = newSampleRate;
}

void AmpEnvelope::setParameters(float newAttack, float newDecay, float newSustain, float newRelease)
{
    attackSeconds = std::max(0.0f, newAttack);
    decaySeconds = std::max(0.0f, newDecay);
    sustainLevel = std::clamp(newSustain, 0.0f, 1.0f);
    releaseSeconds = std::max(0.0f, newRelease);
}

void AmpEnvelope::noteOn()
{
    const int attackSamples = secondsToSamples(attackSeconds, sampleRate);
    if (attackSamples <= 0)
    {
        currentLevel = 1.0;
        startDecay();
        return;
    }

    stage = Stage::Attack;
    samplesRemaining = attackSamples;
    increment = (1.0 - currentLevel) / static_cast<double>(attackSamples);
}

void AmpEnvelope::startDecay()
{
    const int decaySamples = secondsToSamples(decaySeconds, sampleRate);
    if (decaySamples <= 0)
    {
        currentLevel = sustainLevel;
        stage = Stage::Sustain;
        increment = 0.0;
        samplesRemaining = 0;
        return;
    }

    stage = Stage::Decay;
    samplesRemaining = decaySamples;
    increment =
        (static_cast<double>(sustainLevel) - currentLevel) / static_cast<double>(decaySamples);
}

void AmpEnvelope::noteOff()
{
    if (stage == Stage::Idle)
        return;

    const int releaseSamples = secondsToSamples(releaseSeconds, sampleRate);
    if (releaseSamples <= 0)
    {
        currentLevel = 0.0;
        stage = Stage::Idle;
        increment = 0.0;
        samplesRemaining = 0;
        return;
    }

    stage = Stage::Release;
    samplesRemaining = releaseSamples;
    increment = (0.0 - currentLevel) / static_cast<double>(releaseSamples);
}

void AmpEnvelope::reset()
{
    stage = Stage::Idle;
    currentLevel = 0.0;
    increment = 0.0;
    samplesRemaining = 0;
}

float AmpEnvelope::getNextSample()
{
    switch (stage)
    {
    case Stage::Idle:
        return 0.0f;

    case Stage::Attack:
        currentLevel += increment;
        if (--samplesRemaining <= 0)
        {
            currentLevel = 1.0;
            startDecay();
        }
        break;

    case Stage::Decay:
        currentLevel += increment;
        if (--samplesRemaining <= 0)
        {
            currentLevel = sustainLevel;
            stage = Stage::Sustain;
            increment = 0.0;
        }
        break;

    case Stage::Sustain:
        currentLevel = sustainLevel;
        break;

    case Stage::Release:
        currentLevel += increment;
        if (--samplesRemaining <= 0)
        {
            currentLevel = 0.0;
            stage = Stage::Idle;
            increment = 0.0;
        }
        break;
    }

    return static_cast<float>(currentLevel);
}

} // namespace cold::dsp
