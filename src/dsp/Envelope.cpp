#include "dsp/Envelope.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
int secondsToSamples(float seconds, double sampleRate)
{
    return static_cast<int>(std::lround(static_cast<double>(seconds) * sampleRate));
}
} // namespace

void Envelope::setSampleRate(double newSampleRate)
{
    if (newSampleRate > 0.0)
        sampleRate = newSampleRate;
}

void Envelope::setParameters(float newDelay, float newAttack, float newHold, float newDecay,
                             float newSustain, float newRelease)
{
    delaySeconds = std::max(0.0f, newDelay);
    attackSeconds = std::max(0.0f, newAttack);
    holdSeconds = std::max(0.0f, newHold);
    decaySeconds = std::max(0.0f, newDecay);
    sustainLevel = std::clamp(newSustain, 0.0f, 1.0f);
    releaseSeconds = std::max(0.0f, newRelease);
}

void Envelope::setCurves(float newAttackCurve, float newDecayCurve, float newReleaseCurve)
{
    attackCurve = std::clamp(newAttackCurve, -1.0f, 1.0f);
    decayCurve = std::clamp(newDecayCurve, -1.0f, 1.0f);
    releaseCurve = std::clamp(newReleaseCurve, -1.0f, 1.0f);
}

void Envelope::setVelocitySensitivity(float sensitivity)
{
    velocitySensitivity = std::clamp(sensitivity, 0.0f, 1.0f);
}

float Envelope::shape(double phase, float curve)
{
    // phase in [0,1]; returns a monotonic mapping with shape(0)=0, shape(1)=1.
    // curve 0 => linear; >0 convex (slow start); <0 concave (fast start).
    const double p = std::clamp(phase, 0.0, 1.0);
    if (std::abs(curve) < 1.0e-6f)
        return static_cast<float>(p);
    const double c = static_cast<double>(curve) * 6.0; // map [-1,1] -> [-6,6]
    return static_cast<float>((std::exp(c * p) - 1.0) / (std::exp(c) - 1.0));
}

void Envelope::beginRamp(double target, float curve, int samples)
{
    rampStart = currentLevel;
    rampTarget = target;
    rampCurve = curve;
    rampSamples = samples;
    rampPos = 0;
}

void Envelope::enterDelay()
{
    stageSamples = secondsToSamples(delaySeconds, sampleRate);
    if (stageSamples <= 0)
    {
        enterAttack();
        return;
    }
    stage = Stage::Delay;
    stagePos = 0;
    currentLevel = 0.0;
}

void Envelope::enterAttack()
{
    const int samples = secondsToSamples(attackSeconds, sampleRate);
    if (samples <= 0)
    {
        currentLevel = 1.0;
        enterHold();
        return;
    }
    stage = Stage::Attack;
    beginRamp(1.0, attackCurve, samples);
}

void Envelope::enterHold()
{
    stageSamples = secondsToSamples(holdSeconds, sampleRate);
    if (stageSamples <= 0)
    {
        enterDecay();
        return;
    }
    stage = Stage::Hold;
    stagePos = 0;
    currentLevel = 1.0;
}

void Envelope::enterDecay()
{
    const int samples = secondsToSamples(decaySeconds, sampleRate);
    if (samples <= 0)
    {
        currentLevel = sustainLevel;
        stage = Stage::Sustain;
        return;
    }
    stage = Stage::Decay;
    beginRamp(sustainLevel, decayCurve, samples);
}

void Envelope::enterRelease()
{
    const int samples = secondsToSamples(releaseSeconds, sampleRate);
    if (samples <= 0)
    {
        currentLevel = 0.0;
        stage = Stage::Idle;
        return;
    }
    stage = Stage::Release;
    beginRamp(0.0, releaseCurve, samples);
}

void Envelope::noteOn(float velocity)
{
    velScale = 1.0f - velocitySensitivity * (1.0f - std::clamp(velocity, 0.0f, 1.0f));
    enterDelay();
}

void Envelope::noteOff()
{
    if (stage != Stage::Idle)
        enterRelease();
}

void Envelope::reset()
{
    stage = Stage::Idle;
    currentLevel = 0.0;
    velScale = 1.0f;
    rampPos = 0;
    stagePos = 0;
}

float Envelope::getNextSample()
{
    switch (stage)
    {
    case Stage::Idle:
        return 0.0f;

    case Stage::Delay:
        currentLevel = 0.0;
        if (++stagePos >= stageSamples)
            enterAttack();
        break;

    case Stage::Attack:
    case Stage::Decay:
    case Stage::Release:
    {
        ++rampPos;
        const double phase = static_cast<double>(rampPos) / static_cast<double>(rampSamples);
        currentLevel =
            rampStart + (rampTarget - rampStart) * static_cast<double>(shape(phase, rampCurve));
        if (rampPos >= rampSamples)
        {
            currentLevel = rampTarget;
            if (stage == Stage::Attack)
                enterHold();
            else if (stage == Stage::Decay)
                stage = Stage::Sustain;
            else
                stage = Stage::Idle;
        }
        break;
    }

    case Stage::Hold:
        currentLevel = 1.0;
        if (++stagePos >= stageSamples)
            enterDecay();
        break;

    case Stage::Sustain:
        currentLevel = sustainLevel;
        break;
    }

    return static_cast<float>(currentLevel) * velScale;
}

} // namespace cold::dsp
