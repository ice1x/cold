#pragma once

// Framework-agnostic (no JUCE) master stereo-width (mid/side) control.
//
// One knob scales the stereo image: the signal is split into mid = (L+R)/2 and
// side = (L-R)/2, the side is scaled by `width`, then recombined (L = mid+side,
// R = mid-side). width 1 leaves the image untouched, 0 collapses to mono (L=R=mid),
// and >1 widens (more side). A mono input (L==R, so side==0) is unaffected at any
// width. Mono buffers pass through unchanged. Bypassed by default.

namespace cold::dsp
{

class StereoWidth
{
public:
    // width scales the side (difference) component: 0 = mono, 1 = unchanged, up to
    // 2 = doubled width. Takes effect on the next processed sample.
    void setWidth(float newWidth);

    void setEnabled(bool shouldBeEnabled) { isEnabled = shouldBeEnabled; }
    [[nodiscard]] bool enabled() const noexcept { return isEnabled; }

    // Adjust the width of `channels` in place. Only a true stereo pair (numChannels
    // >= 2) is processed — the first two channels are treated as L/R; a mono buffer
    // is left untouched. A no-op while disabled.
    void processBlock(float* const* channels, int numChannels, int numSamples);

private:
    float width = 1.0f;
    bool isEnabled = false;
};

} // namespace cold::dsp
