# Changelog

All notable changes to TEO-5 Editor, newest first. Each version's download is on the
[releases page](https://github.com/jwalkermed/Teo-5-Editor/releases).

## 1.0.16 (2026-10-31)

**Fixed**
- The TEO-5 could be left showing "Receiving SysEx" after Live started, ignoring the editor's knobs and program
  changes. The editor now sends an End of SysEx byte whenever it opens the synth's MIDI port (and with PANIC), which
  releases a synth stuck that way.
- Copies of the plug-in that Live only loads to scan it (after an install, for example) no longer send anything to
  the synth. The editor starts talking to the TEO-5 only once Live runs it or you open its window. The installer's
  load test and the screenshot tool never talk to it.

**Added**
- A MIDI log: every message the editor sends and receives is also written to ~/Library/TEO5Editor/midi-log.txt
  (with the time and which process sent it), so a problem like the one above can be traced.

## 1.0.15 (2026-10-31)

**Fixed** (from a code review)
- While WRITE's Save pop-up is open, the library's toolbar (Undo, Redo, New Bank, Import, Fetch, the send buttons) is
  greyed out, so the library can't change under the slot you chose. If the chosen slot's bank goes away anyway, the
  choice is dropped and Confirm does nothing until you pick another slot. Before, this could save into the wrong bank
  or crash the editor.
- Saving into a TEO-5 User slot, Save all changes and the other sends now refuse when no MIDI output is selected, and
  say so. Before, the programs went nowhere but the slots were marked as being on the synth (no NOT SENT).

## 1.0.14 (2026-10-31)

**Changed**
- The editor no longer re-reads the synth's sound every two seconds (added in 1.0.10 to follow the TEO-5's own
  GLOBAL > Basic Program, which the synth doesn't announce over MIDI). That constant SysEx traffic filled the MIDI
  monitor, made the TEO-5 show its SysEx screen, and could keep a new editor from landing on User 1-01. To start from
  the Basic Program, use INIT on the editor's panel.

## 1.0.13 (2026-10-31)

**Changed**
- The Save program pop-up (WRITE) now sits right next to the library drawer instead of in the middle of the panel,
  and never covers the library at any editor size.
- Right-click > **Write to TEO-5 memory** in the library now saves the same way as WRITE on the panel: the library
  becomes a slot picker for that program, with the same pop-up, audition and Confirm/Cancel. (The old bank and
  program dialog is gone.)
- The editor can now be dragged up to 3840 px wide, for 4K screens (it was limited to 150% of its normal size).

## 1.0.12 (2026-10-31)

**Changed**
- A new editor (a new track, not a saved project) now opens on **User bank 1, program 01**: it selects that program
  on the TEO-5 and reads it into the panel. This replaces 1.0.10's look-up of the synth's current sound in the
  library. Saved projects still restore their own sound.
- The envelope routing button (below the ENV 1 / ENV 2 lights) is now the same size as the other buttons.

## 1.0.11 (2026-10-31)

**Changed**
- **NOT SENT** now means "different from what's on the TEO-5". Swapping programs back to where they were, or undoing
  the change, clears the mark; Redo brings it back. The library remembers what the synth holds in each User slot
  (from the last fetch, send or WRITE) and compares with that. Undoing a change that was already sent marks the slot
  again, since the synth now has the newer version.

**Fixed**
- The OLED flickered between its normal page and "TEO-5 / <program name>" every few seconds. Programs whose stored
  values are outside a control's range (for example Reverb Size 255) looked changed every time the editor re-read the
  synth, so it loaded them again and again. Such values now count as equal, and a sound is never loaded twice in a row.

## 1.0.10 (2026-10-31)

**Changed**
- **WRITE** now saves through the library. Pressing it opens the library with the panel's program selected, and a
  Save pop-up appears over the panel. Click a slot in a TEO-5 User bank or in MY BANKS to choose where the program
  goes. The program stored in that slot goes into the TEO-5's edit buffer, so you can play it from your MIDI
  controller or the synth's keys to hear what you'd replace. If the panel's program isn't in a User bank or one of
  your banks, User 1 slot 01 is chosen to start with. **Confirm** saves (a TEO-5 User slot is written to the synth's
  memory; MY BANKS keeps it in the library only), closes the library and shows the saved program on the panel.
  **Cancel** puts your edit back and closes the library.
- Library: **+ Current** is replaced by **Save all changes**, which writes every NOT SENT program to the TEO-5 after
  listing what it overwrites on the synth (Confirm or Cancel). **+ Bank** is now **New Bank**. The up-arrow "write to
  TEO-5" icon is gone from the rows (Write to TEO-5 memory is still in the right-click menu).
- Closing the library with LIBRARY while changes aren't sent asks whether to send them first.
- Mod matrix: the top row is now **Clear all**, **Revert**, **Undo**, **Redo**, then the number of active slots.

**Added**
- Every bank in MY BANKS ends with an Empty slot to save into.
- A **?** left of LIBRARY opens a guide to the library.
- Mod matrix **Clear all** (empties all 16 slots) and **Revert** (all 16 back as stored in the program). Both are one
  Undo step.
- The OLED shows **EDITED** in red under the program name whenever the sound differs from the stored program.
- When the editor opens on a new track, BANK, FACTORY, the program buttons and the display show where the synth's
  sound comes from, found among the banks the library has fetched (the TEO-5 doesn't report it). A sound edited on
  the synth is found by its name and shows EDITED; one the library doesn't know shows "---". A saved project still
  restores its own program.

**Fixed**
- Loading the Basic Program on the TEO-5 itself (GLOBAL > Basic Program) now shows on the panel, the OLED and the
  mod matrix. The synth sends nothing when you do this, so while the editor is open it re-reads the synth's sound
  every two seconds; your own edits are never overwritten by this.

## 1.0.9 (2026-10-30)

**Fixed**
- Changing program while the sequence plays (BANK, FACTORY, the 1–16 buttons, PATCH ‹ ›, the arrow keys, the
  sequencer drawer's bank and PATCH controls, or on the TEO-5 itself) stops the TEO-5's sequencer, and **PLAY** now
  goes off with it instead of staying lit. Loading a program without a program change (REVERT, COMPARE, INIT, the
  library) still keeps the sequence playing.

## 1.0.8 (2026-10-30)

**Changed**
- A new instance of the editor (in a new Live set, or after restarting Live) opens at the size you last gave the
  editor window, instead of the default size. A saved project still opens at the size it was saved with.

## 1.0.7 (2026-10-30)

**Changed**
- **REVERT** asks first when you have edits, since they can't be brought back. Before any program has been loaded it
  says there is nothing to revert to, instead of fetching the synth's current (edited) sound.

**Fixed**
- Loading a program into the edit buffer (REVERT, COMPARE, INIT, or a program from the library) no longer leaves the
  sequencer stopped: the TEO-5 stops it whenever a program arrives, so the editor sends Play again if PLAY was on.

## 1.0.6 (2026-10-30)

**New**
- **REVERT** button under MOD: drops all your edits and goes back to the program as it was loaded (or as last
  written), sent to the TEO-5's edit buffer. Nothing is written to memory. Hover over it for a tooltip.
- The **MOD** button has a tooltip saying what it does.

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
