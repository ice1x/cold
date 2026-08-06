#include "dsp/MorphFilter.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kSmoothingSeconds = 0.005; // 5 ms one-pole for modulated params
} // namespace

void MorphFilter::prepare(double newSampleRate)
{
    if (newSampleRate > 0.0)
        sampleRate = newSampleRate;

    smoothCoeff = 1.0 - std::exp(-1.0 / (kSmoothingSeconds * sampleRate));

    // Snap the smoothed followers to their targets so there is no start-up ramp.
    smCutoff = targetCutoff;
    smResonance = targetResonance;
    smType = targetType;
    reset();
}

void MorphFilter::setCutoff(float hz)
{
    const auto maxHz = static_cast<float>(0.49 * sampleRate);
    targetCutoff = std::clamp(hz, 1.0f, maxHz);
}

void MorphFilter::setResonance(float q)
{
    targetResonance = std::clamp(q, 0.05f, 40.0f);
}

void MorphFilter::setFilterType(float type)
{
    targetType = std::clamp(type, 0.0f, 1.0f);
}

void MorphFilter::reset()
{
    ic1eq = 0.0;
    ic2eq = 0.0;
}

float MorphFilter::morph(const Outputs& o, float type)
{
    // Anchors in morph order; type in [0,1] maps onto the 3 segments between them.
    const float anchors[4] = {o.lp, o.bp, o.hp, o.notch};
    const float t = std::clamp(type, 0.0f, 1.0f);
    const float seg = t * 3.0f;
    int i = static_cast<int>(seg);
    if (i > 2)
        i = 2;
    const float frac = seg - static_cast<float>(i);
    return (1.0f - frac) * anchors[i] + frac * anchors[i + 1];
}

MorphFilter::Outputs MorphFilter::computeOutputs(float x)
{
    // Advance the smoothed parameter followers, then derive coefficients from them.
    smCutoff += (static_cast<double>(targetCutoff) - smCutoff) * smoothCoeff;
    smResonance += (static_cast<double>(targetResonance) - smResonance) * smoothCoeff;
    smType += (static_cast<double>(targetType) - smType) * smoothCoeff;

    const double g = std::tan(kPi * smCutoff / sampleRate);
    const double k = 1.0 / smResonance;
    const double a1 = 1.0 / (1.0 + g * (g + k));
    const double a2 = g * a1;
    const double a3 = g * a2;

    const double in = static_cast<double>(x);
    const double v3 = in - ic2eq;
    const double v1 = a1 * ic1eq + a2 * v3;
    const double v2 = ic2eq + a2 * ic1eq + a3 * v3;
    ic1eq = 2.0 * v1 - ic1eq;
    ic2eq = 2.0 * v2 - ic2eq;

    Outputs o;
    o.lp = static_cast<float>(v2);
    o.bp = static_cast<float>(v1);
    o.hp = static_cast<float>(in - k * v1 - v2);
    o.notch = static_cast<float>(in - k * v1);
    return o;
}

MorphFilter::Outputs MorphFilter::processAll(float x)
{
    return computeOutputs(x);
}

float MorphFilter::process(float x)
{
    const auto o = computeOutputs(x);
    return morph(o, static_cast<float>(smType));
}

} // namespace cold::dsp
