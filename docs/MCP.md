# MCP control surface

Cold can be driven by LLM / voice tools through the Model Context Protocol (MCP):
an assistant can set parameters, load samples, trigger notes, read the current
state, and **recommend tuning** — all by voice.

## Two pieces

```
   LLM / voice tool ──(MCP, stdio/JSON-RPC)──▶  mcp/ (Node MCP server)
                                                    │
                                          (OSC / WebSocket, localhost)
                                                    ▼
                                   Cold plugin control server (in JUCE processor)
                                                    │
                                              APVTS parameters / Engine
```

1. **Plugin control server** (in `src/`, JUCE): a lightweight localhost endpoint
   (OSC via `juce::OSCReceiver/Sender`, or a small WebSocket) that the audio plugin
   exposes. It reads/writes the APVTS and forwards note triggers to the `Engine`.
   It must be **thread-safe** and never block the audio thread (use atomics /
   lock-free FIFOs to hand messages to/from the audio callback).

2. **MCP server** (`mcp/`, TypeScript + `@modelcontextprotocol/sdk`): a separate
   process that translates MCP tool calls into control-server messages. This is what
   an LLM/voice client connects to.

## MCP tools (planned)

| Tool | Purpose |
| --- | --- |
| `list_parameters` | Enumerate parameters (id, name, range, current value). |
| `get_parameter` | Read one parameter's value. |
| `set_parameter` | Set a parameter (e.g. "set track 1 cutoff to 40%"). |
| `load_sample` | Load a file/slot into a track. |
| `trigger_note` | Play a note (audition / voice control). |
| `get_state` | Full snapshot (tracks, samples, params) for reasoning. |
| `recommend_tuning` | Return current state + context so the LLM can suggest tuning; optionally apply suggested values via `set_parameter`. |

`recommend_tuning` is intentionally LLM-driven: the tool surfaces the state and the
assistant reasons about good filter/envelope/LFO settings, then applies them.

## Safety

- Control server binds to **localhost only**; no remote exposure by default.
- Parameter writes are clamped to valid ranges by the APVTS.
- The audio thread is never blocked: messages cross thread boundaries via lock-free
  queues.

## Milestone

The MCP surface is milestone **M7** in [../TASKS.md](../TASKS.md), added after the
core sampler (M1) so there are real parameters worth controlling. A minimal slice
(`set_parameter` + `get_state`) can be pulled earlier if voice control is wanted
sooner.
