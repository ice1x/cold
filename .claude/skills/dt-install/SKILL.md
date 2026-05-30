---
name: dt-install
description: Install the built Cold AU/VST3 into the macOS plug-ins folders, validate the AU with auval, and clear the AU cache so Logic Pro re-scans. Use when asked to install the plugin or get it into Logic.
---

# dt-install

Get a freshly built Cold into Logic Pro. **Logic only loads AU**, so the AU
validation step is the gate.

## 1. Install the components

```sh
AU=$(find build -name 'Cold.component' | head -1)
VST3=$(find build -name 'Cold.vst3' | head -1)
cp -R "$AU"   ~/Library/Audio/Plug-Ins/Components/
cp -R "$VST3" ~/Library/Audio/Plug-Ins/VST3/      # optional, for other DAWs
```

## 2. Validate the AU

```sh
# List to find the type/subtype/manufacturer codes Cold registered with:
auval -a | grep -i cold
# Full validation (substitute the codes printed above, e.g. aumu Cold Cldx):
auval -v aumu <subtype> <manufacturer>
```

A clean `AU VALIDATION SUCCEEDED` is required before testing in Logic.

## 3. Force Logic to re-scan (if it cached a bad build)

```sh
rm -rf ~/Library/Caches/AudioUnitCache
killall -9 AudioComponentRegistrar 2>/dev/null || true
```

Then relaunch Logic Pro; it re-scans AUs on startup.

## Notes

- Cold is an **instrument** (`aumu`) — add it on a software-instrument track.
- If `auval` fails, fix the plugin before touching Logic — don't debug in the DAW.
- Follow with `dt-logic-smoke` for the manual in-DAW verification.
