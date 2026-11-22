# DSP notes

Math and design notes for the framework-agnostic DSP classes.

## Morphing multimode filter (`dsp/MorphFilter`)

Use a **TPT / Zavalishin state-variable filter** (Cytomic "SVF" topology). One
filter computes low-pass, band-pass, high-pass and notch outputs **simultaneously**
from the same internal state, which makes continuous morphing cheap and stable
even while the cutoff is modulated.

Per sample (cutoff `fc`, resonance/Q `q`, sample rate `fs`):

```
g  = tan(pi * fc / fs)
k  = 1 / q
a1 = 1 / (1 + g*(g + k))
a2 = g * a1
a3 = g * a2

v3 = x - ic2
v1 = a1*ic1 + a2*v3
v2 = ic2 + a2*ic1 + a3*v3
ic1 = 2*v1 - ic1     // integrator states
ic2 = 2*v2 - ic2

lp = v2
bp = v1
hp = x - k*v1 - v2
notch = x - k*v1            // = lp + hp
```

### Continuous type morph

Expose a single `filterType` parameter in `[0, 1]` that **blends** between the
outputs along a path, e.g. `LP → BP → HP → Notch`. Map the parameter onto adjacent
pairs and crossfade:

```
seg   = filterType * (N-1)        // N anchor types
i     = floor(seg)
frac  = seg - i
out   = (1-frac)*type[i] + frac*type[i+1]
```

Equal-power or linear crossfade between the two nearest anchor outputs gives a
smooth, click-free morph. Because it's just a parameter into a blend, `filterType`
is itself **modulatable** (an LFO can target it) — a key requested feature.

Smooth `cutoff`, `resonance` and `filterType` with one-pole parameter smoothing to
avoid zipper noise when modulated.

## Amp envelope (`dsp/AmpEnvelope`)

ADSR (allow A=0 / S=1 for an AHD/AD-style hold + decay). Sample-accurate stage transitions; retrigger resets to
attack. Keep it allocation-free and branch-light for the audio thread.

## LFO (`dsp/Lfo`)

Shapes: sine, triangle, saw (up/down), square, sample-and-hold (random). Params:
rate (Hz or host-synced division), depth, phase offset, retrigger mode, one-shot vs
free-running. Output normalized to a known range (e.g. `[-1, 1]`) so the mod matrix
can scale it per destination.

## Sample playback (`dsp/SampleVoice`)

- **Pitch ratio**: `ratio = 2^((note - rootNote)/12) * (fileSr / hostSr)`.
- **Repitch / varispeed mode** (default): advance the read position by `ratio`
  each output sample; loops play faster/slower with pitch (classic sampler).
- **Looping**: `OneShot`, `Forward` (wrap `loopStart..loopEnd`), `PingPong`.
- **Window**: `start` and `length` clamp the playable region.
- **Interpolation**: linear for the PoC; upgrade to Hermite/sinc later.
- Mix the voice through its amp env then its morph filter before summing.
