---
name: cold-install
description: Install the built Cold AU/VST3 into the macOS plug-ins folders, validate the AU with auval, and clear the AU cache so Logic Pro re-scans. Use when asked to install the plugin or get it into Logic.
---

# cold-install

Get a freshly built Cold into Logic Pro. **Logic only loads AU**, so the AU
validation step is the gate.

## 1. Install the components

```sh
AU=$(find build -name 'Cold.component' | head -1)
VST3=$(find build -name 'Cold.vst3' | head -1)
cp -R "$AU"   ~/Library/Audio/Plug-Ins/Components/
cp -R "$VST3" ~/Library/Audio/Plug-Ins/VST3/      # optional, for other DAWs
```

## 1b. Re-sign the installed bundle (REQUIRED — see gotcha)

`cp -R` invalidates the build's ad-hoc code signature, so `dlopen` of the copied
bundle is killed with `CODESIGNING / Code Signature Invalid` and `auval` crashes
(SIGKILL) the instant it opens the AU — the report shows `* * PASS` then dies at
`TESTING OPEN TIMES`. Re-sign ad-hoc after copying, every time:

```sh
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/Components/Cold.component
codesign --force --deep -s - ~/Library/Audio/Plug-Ins/VST3/Cold.vst3   # if installed
codesign -v ~/Library/Audio/Plug-Ins/Components/Cold.component && echo "signature OK"
```

## 2. Validate the AU

```sh
# Cold registers as aumu / Cld1 / Cold (from CMakeLists PLUGIN_CODE + manufacturer).
# `auval -a | grep -i cold` also lists it, but re-scans every AU and is slow after
# an AudioUnitCache clear — prefer the direct form:
auval -v aumu Cld1 Cold
```

A clean `AU VALIDATION SUCCEEDED` is required before testing in Logic. If it dies
at `TESTING OPEN TIMES` with no verdict, the signature is the cause — redo step 1b.

## 3. Force Logic to re-scan (if it cached a bad build)

```sh
rm -rf ~/Library/Caches/AudioUnitCache
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

Then relaunch Logic Pro; it re-scans AUs on startup.

## Notes

- Cold is an **instrument** (`aumu`) — add it on a software-instrument track.
- If `auval` fails, fix the plugin before touching Logic — don't debug in the DAW.
- Follow with `cold-logic-smoke` for the manual in-DAW verification.
