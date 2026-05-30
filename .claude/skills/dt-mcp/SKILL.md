---
name: dt-mcp
description: Build, run, and test the Cold MCP server and the in-plugin control bridge that lets LLM/voice tools control the plugin and recommend tuning. Use when working on M7 / MCP control tasks.
---

# dt-mcp

Work on the MCP control surface (see [docs/MCP.md](../../../docs/MCP.md)). Two
parts: the **in-plugin control server** (JUCE, in `src/`) and the **MCP server**
(`mcp/`, TypeScript/Node).

## In-plugin control server

- Localhost-only OSC (`juce::OSCReceiver/Sender`) or a small WebSocket.
- Reads/writes the APVTS and forwards note triggers to `Engine`.
- **Never block the audio thread**: cross thread boundaries with atomics / a
  lock-free FIFO. Test the message handling via `dt-test` (no audio host needed).

## MCP server (`mcp/`)

```sh
cd mcp
npm install
npm run build
npm start            # serves MCP over stdio
```

Tools to implement (incrementally): `list_parameters`, `get_parameter`,
`set_parameter`, `load_sample`, `trigger_note`, `get_state`, `recommend_tuning`.

## Register with an MCP client

Point the LLM/voice client at the built server, e.g. for Claude Code:

```sh
claude mcp add cold -- node /absolute/path/to/cold/mcp/dist/index.js
```

## Verify

1. Start Cold (Standalone or in Logic) so its control server is listening.
2. Start the MCP server; from the client, call `get_state`, then `set_parameter`
   (e.g. set cutoff) and confirm the change is audible / reflected in the UI.
3. Try `recommend_tuning` and confirm it returns usable state for the LLM and can
   apply suggestions.

## Safety

Bind to localhost only; clamp all writes through the APVTS ranges; no remote
exposure by default.
