# VERIFY — items not confirmed by the TEO-5 MIDI Implementation

Source used: *TEO-5 MIDI Implementation* (User's Guide appendix, 16 pp.), text saved in
`docs/TEO5-MIDI-Implementation.txt`. Value names come from the TEO-5 User's Guide v1.0 (March 2024).
Every item below is marked `verify = true` in `Source/Params/ParamTable.cpp` or has a `// TODO: verify`.

## Spec inconsistencies / typos
| Item | What the spec says | What we assume |
|---|---|---|
| NRPN 34 Filter Cutoff | 0-1024, plus a separate NRPN 35 "Filter Cutoff MSB 0-7" | **Measured:** 34 carries the full value (14-bit NRPN); range **0-1023**, stored in bytes 34-35. |
| NRPN 36 Filter Res | 0-265 | **Measured: 0-255.** |
| NRPN 38 Filter State | 0-512, plus NRPN 39 "Filter State MSB 0-127" | **Measured: 0-511**, bytes 38-39. |
| NRPN 20 Noise Type | 0-127 (CC 108 says 0-1) | **Measured: 0-1.** |
| NRPN 60/61 LFO Amt, 75/76 Env Amt, 121-136 Mod Amt | 0-256 / 0-255 | **Measured: 0-254**; for the mod amounts 127 = none (unused slots store 127). |
| NRPN 81-88 Env Attack/Decay/Release | 0-256 | **Measured: 0-255.** |
| NRPN 10 Osc 2 Saw | 0-1 | Sending it changed nothing in the edit buffer on a real TEO-5. Unexplained. |
| NRPN 43 FX Select | 0-12 (13 values), but only 12 effect types are documented | Names in manual order; 13th entry labelled "Type 13". Order unconfirmed. |
| NRPN 4188 Seq Play/Stop | 4188 | **Measured: 4198.** The TEO-5 sends 4198 (CC 99 = 32, CC 98 = 102) when its PLAY is pressed and ignores 4188. |
| NRPN 157 Clock BPM | 30-250 (CC 3 says 15-127) | CC fallback needs a mapping, TBD. |
| NRPN 155 Scale | 0-64 (CC 114 says 0-65) | **Measured 0-65**: 0 = Global, 1-65 = the user's guide's 65 tunings. Global Scale (4117) is 0-64. |
| Global NRPN 4120 Velocity Curve | 0-7 | **Measured 0-6** (the user's guide lists Curve 1-7). |
| **Bank numbers** | Request dump: 0-15; Program Data Dump: bank 0-9, program 0-99; CC 32 bank select: 1-4 user, 5-8 factory, 9-12 add-on | **Confirmed on hardware:** CC 32 = bank-1 (+16 for Factory, so 0-31), Program Change = program-1 (0-15); SysEx dumps use the same bank/program numbers (User 512 → bank 4, program 11). |
| **Program size** | "4096 bytes expanded to 4690 MIDI bytes" | **Confirmed on hardware:** dumps carry 4688 packed bytes = **4102** unpacked (edit buffer message 4693 bytes, program dump 4695). Programs are stored and re-sent verbatim. |
| Global NRPN 4099 MIDI Clock Mode | Values 0-3 (Off/Out/In/In Thru), but the prose lists 6 modes | **0-5 in the prose's order**; 5 = In No S/S measured, the rest inferred. |
| Global NRPN 4096/4097 tuning | Coarse 0-100, Fine 0-24 | **Measured: Coarse 0-24, Fine 0-100** (ranges swapped). |
| Global NRPN 4102/4103/4107 value order | NRPN/CC/Off; MIDI/USB/All | **Measured: Off/CC/NRPN; Off/MIDI/USB/All** (the prose's order). |
| Global dump | 50 nibbles for 25 globals | **Measured: 32 packed-MS-bit bytes = 28 values; byte i = NRPN 4096 + i.** |
| Device inquiry TEO-5 ID | `0b1011010` = 0x5A | Assumed 0x5A for all SysEx headers. The editor now adopts the family byte the synth reports. |
| **Oberheim manufacturer ID** | `0000 0010` = 0x02 | **Wrong. Confirmed on hardware:** the TEO-5 answers the device inquiry with **0x10** (Oberheim's MIDI ID). Default changed to 0x10; IDs are learned from the inquiry reply. |

## Undocumented semantics
- **Step sequence (measured on hardware, 496 programs).** Byte 189 = length (1-64); 190-229 rest flags and 230-269 tie flags (5 lanes x 64 bits each, bit = step % 8 of byte step / 8); 270-589 pitches and 590-909 velocities (5 lanes x 64 steps). A tied step is stored as a rest with the tie flag set on the step before. Read/write round-trips all 496 programs byte for byte. Not yet checked on the synth:
  - Whether the TEO-5 accepts a changed sequence in an edit-buffer dump without stopping playback. The sequencer drawer sends one ~150 ms after each edit.
  - The playhead assumes the sequencer steps at the Clock Divide rate (1/16 = four steps per beat) and starts at the DAW's bar 1; with Live's MIDI clock the synth follows the DAW, but whether it honours Song Position when starting mid-song is unknown.
  - While the panel's RECORD is on, the drawer reads the edit buffer twice a second to show the step the synth just recorded.
- **Program data layout (measured on hardware).** Not in the spec. On a real TEO-5 each program parameter is stored at the byte matching its NRPN number (Cutoff and Filter State as two bytes, little-endian), and the name takes bytes 159-178. The editor has this built in (`ProgramLayout::builtIn`), so no probe is needed. **MIDI drawer → Map dump layout…** can still re-measure it; its result goes to `~/Library/TEO5Editor/program-layout.json`. Layouts saved by older versions are ignored because that probe mistook some interlocking oscillator waveform switches for each other.
- **Bank/program addressing**: confirmed from a hardware capture (see the table above). Still unconfirmed: whether the TEO-5 *accepts* CC 32 0-31 from the editor (it transmits them), and the request address for factory programs (assumed bank 16-31).
- **Writing programs to memory.** The spec documents the Program Data Dump format but doesn't say whether the TEO-5 *stores* a received program dump in memory. WRITE, "Write to TEO-5 memory" and "Send bank to TEO-5" assume it does. Try this on a scratch location first. The bank send waits ~250 ms between programs; that pacing is also unconfirmed.
- **Programs per bank.** The librarian assumes 16 programs per panel bank and 16 banks (from the panel's 1-16 buttons and the factory program list).
- **Large SysEx on virtual ports.** In testing, a JUCE virtual MIDI *source* stopped delivering after a few dozen 4.7 KB dumps in quick succession. Real hardware doesn't use that path, but if dumps from the TEO-5 stall during the layout probe, this is the first thing to look at. The probe retries each request 3 times.
- **Bipolar centres.** Osc Detune 0-63 (centre 32?), Env Amt 0-256 (centre 128?), LFO Amt 0-256 (unipolar or bipolar?), Mod Amt 0-255 (centre 128?).
- **Osc Freq 0-63** units/default (semitones? which value is 8').
- **Env Routing (NRPN 89) value order**: assumed 0 = Env1 Filter / Env2 Amp, 1 = Aux / Filter+Amp, 2 = Aux / Filter+Gate.
- **LFO Dest / Env 1 Dest 0-61**: assumed to be the first 62 entries of the mod destination list (which has 65).
- Unison Key Mode (153) order Low/High/Last is the user's guide's order (not measured); **Transpose (156)** 0-4 as -2..+2 oct; **Unison Voices (98)** 0-5 meaning.
- **LFO Freq Sync (68/69)** 0-15 division names; **FX Sync Rate (48)** 0-10 names.
- **Panel controls without a documented number:**
  - MISC **Rate** knob: currently bound to Osc 1 Glide (NRPN 22). May need to drive both Osc 1/2 Glide (22/23).
  - OSCILLATORS **Pulse Width** knob: bound to Osc 1 PW (NRPN 5). May need to drive both Osc 1/2 PW (5/6).
  - **Hold** button: no NRPN/CC listed (Arp Relatch NRPN 187 is a different setting).
  - **BP** LED / Filter Bandpass (NRPN 37): set in the synth's PROGRAM menu (user's guide p. 22); the editor's PROGRAM drawer has it.
  - **Master Volume** knob: sent as CC 7 (documented as "Volume knob"). Not part of the program.
  - **Tap Tempo, Compare, Write, Global, Program, Manual**: panel functions with no MIDI message.
