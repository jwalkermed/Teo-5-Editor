TEO-5 Editor: editor/librarian for the Oberheim TEO-5
Apple Silicon and Intel Macs (universal binary), macOS 11 or later.

Copyright (C) 2026 The TEO-5 Editor project. Free software under the GNU Affero General Public License v3
or later, WITHOUT ANY WARRANTY; see LICENSE.txt. Third-party software: see THIRD-PARTY-NOTICES.md.
Source code: https://github.com/jwalkermed/Teo-5-Editor
The full manual is TEO-5-Editor-Users-Guide.pdf in this folder.

INSTALL
  Open Terminal, then type "bash " (with a space) and drag install.command into the window. Press Return:

      bash /path/to/TEO5Editor/install.command

  It installs:
    TEO-5 Editor.vst3       -> /Library/Audio/Plug-Ins/VST3
    TEO-5 Editor.component  -> /Library/Audio/Plug-Ins/Components    (Audio Unit)
    TEO-5 Editor.app        -> /Applications                         (standalone)

  It may ask for your password if a folder needs admin rights.
  (Double-clicking install.command may be blocked because the download isn't notarized. Running it from
  Terminal as above avoids that.)

FIRST RUN WITH THE TEO-5
  1. TEO-5: GLOBAL > MIDI Param Send = NRPN, MIDI Param Receive = NRPN, MIDI SysEx = On,
     MIDI SysEx Cable = the connection you use, MIDI Program Send = On.
  2. Open TEO-5 Editor (standalone is simplest for a first test). Click the MIDI tab on the right end cheek,
     choose the TEO-5 as input and output, then click "Find TEO-5".
  3. Turn a knob on the synth: the matching knob on screen should move. Turn Cutoff on screen: the synth follows.
  4. LIBRARY tab (left end cheek): Fetch > Bank 1, then click a program to load it.
  Don't use WRITE or "Send bank" until the steps above work, and try them on a scratch slot first.

IF THE SYNTH DOESN'T RESPOND TO THE EDITOR
  The MIDI drawer has a monitor at the bottom that lists every message sent and received.
  - Click "Send test note": the TEO-5 should play middle C. The monitor should show "OUT Note on...".
    "OUT (no port!)" means no output is open: pick the TEO-5 under Output.
  - Click "Find TEO-5": the monitor should show "OUT SysEx 6 bytes" followed by "IN SysEx ..." from the synth.

IF THE VST3 DOESN'T SHOW UP IN YOUR DAW
  A VST3 copied by hand from the download (e.g. dragged in Finder) keeps macOS's download quarantine flag.
  The DAW's plug-in scanner then stalls on a hidden security prompt and leaves the plug-in out.
  - Run install.command again. It clears the flag, removes old copies, and finishes by loading the VST3
    itself: "OK: the VST3 loads" means the plug-in is fine and the DAW only needs to rescan.
  - Ableton Live: Settings > Plug-Ins > "Use VST3 Plug-In System Folders" on, then Option-click "Rescan".
  - Bitwig: Settings > Plug-ins > Rescan. If TEO-5 Editor is listed as failed or blocked, re-enable it.
