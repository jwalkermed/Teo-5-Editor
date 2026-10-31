# Changelog

All notable changes to TEO-5 Editor, newest first. Each version's download is on the
[releases page](https://github.com/jwalkermed/Teo-5-Editor/releases).

## 1.0.5 (2026-10-30)

**Changed**
- **RECORD** on the front panel is now the sequencer's step entry (REC): it opens the sequencer drawer with REC on.
  Pressing it again, or closing the drawer, turns REC off. To record on the TEO-5 itself, use the synth's own
  RECORD button; the steps still fill in live in the drawer.

**Fixed**
- **GLOBAL** and **PROGRAM** light while their drawers are open.

## 1.0.4 (2026-10-30)

**New**
- **Front panel guide:** click the **?** at the top left of the panel for a guide to everything on the front panel:
  knobs, buttons and LEDs, the display, choosing programs, assigning modulation with a right-click, the mod matrix
  drawer, the other drawers and the keyboard shortcuts. Close it with ?, CLOSE or Esc. The user's guide describes it.

**Fixed**
- The editor reopens at the size you last gave it, after closing and reopening the plug-in window and after
  reloading a project (the size is saved with the project).

## 1.0.3 (not released)

The front panel guide was built as 1.0.3 but never published; it is part of 1.0.4.

## 1.0.2 (2026-10-30)

**New**
- **User's guide** (`TEO-5-Editor-Users-Guide.pdf`, in the zip and on the release page): installing, connecting,
  the front panel and every drawer, with screenshots.

**Changed**
- MIDI drawer: the "Map dump layout", program-name and "Locate name" controls are gone (the editor knows the
  TEO-5's program format), and "Also route through host MIDI" is hidden on the Mac, where it isn't needed. The MIDI
  monitor is larger.

## 1.0.1 (2026-10-30)

**Changed**
- The MOD section's SRC and DEST buttons, which did nothing, are replaced by one button that opens and closes the
  mod matrix.

## 1.0.0 (2026-10-30)

First public release.

- Renamed to **TEO-5 Editor**. Projects saved with the earlier "TEO5Editor" name still load it.
- Universal binary for Apple Silicon and Intel Macs (macOS 11 or later).
- Licensed under the GNU AGPL v3 or later; third-party software is listed in `THIRD-PARTY-NOTICES.md`.

## Before 1.0 (0.2 – 0.8, October 2026)

Development versions, not published. In summary:

- **Front panel** with two-way sync to the TEO-5 (NRPN), program changes with the edit buffer fetched, COMPARE,
  WRITE (to a User location you choose), BANK and PATCH buttons, arrow-key program stepping, TAP TEMPO, PANIC, INIT
  (loads the Basic Program) and an OLED that shows each value next to the one stored in the program.
- **Knobs:** fine adjustment with Shift, double-click returns to the program's stored value.
- **Library:** fetch the TEO-5's User and Factory banks, your own banks, drag and drop between them (move, swap,
  Option-drag to copy), favourites, tags, search, undo/redo, .syx import and export, sending changes or whole banks
  back to the TEO-5 with "not sent" marks. Factory banks are read-only.
- **Mod matrix drawer:** all 16 slots, an LED per slot to switch it off and on, restore a slot to the program's
  setting, undo/redo, and **right-click modulation** from any panel control that is a modulation destination or
  source.
- **Sequencer drawer:** a step editor for the program's 64-step, 5-note sequence: length, rests, ties, velocities,
  transpose, step entry from the on-screen keyboard, the DAW track or the TEO-5's keys, copy/paste, undo/redo,
  following the TEO-5's own recording, and play/stop that follows the DAW's transport.
- **GLOBAL and PROGRAM drawers** for the TEO-5's menu settings, and a **MIDI drawer** for ports and a MIDI monitor.
- **In a DAW:** a saved project sends its program to the TEO-5 when opened, and the display shows the DAW's tempo
  when the TEO-5 follows external clock.
