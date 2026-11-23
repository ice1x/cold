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

ModMatrix::FilterMod ModMatrix::filterMod(float sourceValue) const
{
    float cutoffOctaves = 0.0f;
    float typeOffset = 0.0f;
    for (const auto& route : routes)
    {
        if (route.destination == ModDestination::FilterCutoff)
            cutoffOctaves += route.amount * sourceValue;
        else if (route.destination == ModDestination::FilterType)
            typeOffset += route.amount * sourceValue;
    }
    return {cutoffOctaves, typeOffset};
}

float ModMatrix::sum(ModDestination destination, float sourceValue) const
{
    float total = 0.0f;
    for (const auto& route : routes)
        if (route.destination == destination)
            total += route.amount * sourceValue;
    return total;
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
        case ModDestination::Resonance:
        case ModDestination::Level:
        case ModDestination::Pan:
        case ModDestination::DelaySend:
        case ModDestination::ReverbSend:
        case ModDestination::LfoSpeed:
        case ModDestination::LfoDepth:
            break; // read by the engine through sum()
        }
    }

    return {std::max(1.0f, cutoff), std::clamp(type, 0.0f, 1.0f), pitch, sampleOffset};
}

} // namespace cold::dsp
