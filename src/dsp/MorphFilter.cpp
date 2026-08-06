#include "dsp/MorphFilter.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846;
}

void MorphFilter::prepare(double newSampleRate)
{
    if (newSampleRate > 0.0)
        sampleRate = newSampleRate;
    reset();
    coeffsDirty = true;
}

void MorphFilter::setCutoff(float hz)
{
    // Keep tan() finite and the filter stable below Nyquist.
    const auto maxHz = static_cast<float>(0.49 * sampleRate);
    cutoffHz = std::clamp(hz, 1.0f, maxHz);
    coeffsDirty = true;
}

void MorphFilter::setResonance(float q)
{
    resonanceQ = std::clamp(q, 0.05f, 40.0f);
    coeffsDirty = true;
}

void MorphFilter::reset()
{
    ic1eq = 0.0;
    ic2eq = 0.0;
}

void MorphFilter::updateCoefficients()
{
    g = std::tan(kPi * static_cast<double>(cutoffHz) / sampleRate);
    k = 1.0 / static_cast<double>(resonanceQ);
    a1 = 1.0 / (1.0 + g * (g + k));
    a2 = g * a1;
    a3 = g * a2;
    coeffsDirty = false;
}

MorphFilter::Outputs MorphFilter::processAll(float x)
{
    if (coeffsDirty)
        updateCoefficients();

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

} // namespace cold::dsp
