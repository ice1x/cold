#include "dsp/ModMatrix.h"

#include <algorithm>
#include <cmath>

namespace cold::dsp
{

void ModMatrix::addRoute(ModDestination destination, float amount)
{
    routes.push_back({destination, amount});
}

void ModMatrix::clearRoutes()
{
    routes.clear();
}

ModMatrix::ModValues ModMatrix::apply(float baseCutoffHz, float baseType, float sourceValue) const
{
    float cutoff = baseCutoffHz;
    float type = baseType;
    float pitch = 0.0f;
    float sampleOffset = 0.0f;

    for (const auto& route : routes)
    {
        const float mod = route.amount * sourceValue;
        switch (route.destination)
        {
        case ModDestination::FilterCutoff:
            cutoff *= std::pow(2.0f, mod); // exponential: `amount` is in octaves
            break;
        case ModDestination::FilterType:
            type += mod; // linear offset
            break;
        case ModDestination::Pitch:
            pitch += mod; // additive semitones
            break;
        case ModDestination::SampleSelect:
            sampleOffset += mod; // additive slot/layer offset
            break;
        }
    }

    return {std::max(1.0f, cutoff), std::clamp(type, 0.0f, 1.0f), pitch, sampleOffset};
}

} // namespace cold::dsp
