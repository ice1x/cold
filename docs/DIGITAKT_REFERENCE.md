# Digitakt reference → roadmap mapping

Inventory of Elektron Digitakt features and where each lands in Cold's roadmap
([../TASKS.md](../TASKS.md)). Cold intentionally deviates from the Digitakt in a
few places — those are called out.

## Core sampler

| Digitakt feature | Cold | Milestone |
| --- | --- | --- |
| Sample playback from RAM | Yes | M1 |
| Load samples (wav/aiff) | Yes, + mp3 | M1 |
| Sample start / length | Yes | M4 |
| Looping (loop point) | Yes | M1/M4 |
| Pitch (repitch/varispeed) | Yes — default playback mode | M1 |
| **Mono per track** | **No — Cold is polyphonic (chords)** | M1 |
| 8 audio tracks | Yes | M5 |
| Sample slots / +Drive bank | Sample bank (slots) | M4 |
| Sample chop / slice | Backlog | later |

## Sound shaping

| Digitakt feature | Cold | Milestone |
| --- | --- | --- |
| Multimode filter (LP/BP/HP + base-width) | **Continuous morph LP→BP→HP→Notch** | M2 |
| Amp envelope (AHD-style) | Amp ADSR (emulates AHD) | M1 |
| Filter envelope | Separate filter env | backlog |
| Overdrive | Backlog | later |
| Bit reduction / sample-rate reduction | Backlog | later |

## Modulation

| Digitakt feature | Cold | Milestone |
| --- | --- | --- |
| LFO per track | Yes | M3 |
| LFO destinations | cutoff, **filter type (morph)**, +more | M3 |
| Multiple LFOs | Backlog | later |

## Sequencer

| Digitakt feature | Cold | Milestone |
| --- | --- | --- |
| 16-step sequencer, host sync | Yes | M6 |
| Parameter locks (p-locks) | Yes | M6 |
| Patterns & chains | Backlog | later |
| Trig conditions / probability | Backlog | later |
| Micro-timing / retrig | Backlog | later |

## FX

| Digitakt feature | Cold | Milestone |
| --- | --- | --- |
| Send delay / reverb | Backlog | later |
| Master compressor / overdrive | Backlog | later |

## Cold-only additions (not in Digitakt)

- **Polyphony** per track (chords with samples).
- **Classic repitch/varispeed** loop mode alongside Digitakt-style looping.
- **MCP control surface** for LLM/voice control and tuning recommendations
  ([MCP.md](MCP.md)).
