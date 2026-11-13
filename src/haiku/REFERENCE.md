# Native Haiku GUI: reference and spec

Hydrogen, Copyright(c) 2008-2026 The hydrogen development team.
Haiku port: Copyright(c) 2026 idumych (https://github.com/idumych)

What the native (BeOS-API) GUI must match, measured from upstream's Qt GUI
(`main`, 2.0.0-pre-alpha) built from this tree and running on Haiku R1/beta6,
1280×800, demo song `GM_kit_demo1.h2song`. The screenshots themselves are kept
out of the repository; this file records what was measured from them.

The layout, content and sizes come from upstream. Menus, wording, shortcuts,
alerts and settings follow the Haiku Human Interface Guidelines; every such
difference is listed under [Haiku conventions](#haiku-conventions).

## Main window

One window holding, top to bottom: menu bar, toolbar, song editor, a splitter,
then the pattern editor beside the instrument rack. Upstream's window is
about 1005×750 before zooming.

| Part | Measured |
|---|---|
| Menu bar | 20 px high |
| Toolbar | 40 px high: select/draw tools, mode buttons, time display (`00:00:00.000`, ~145 px), rewind, record, play (with menu), stop, forward, loop, metronome, BPM field (115.00, ~95 px), tap tempo, MIDI activity, CPU, and view buttons (playlist, director, mixer, rack) |
| Song editor, left column | 202 px wide: three header rows (timeline/automation ~50 px, then two ~25 px rows of tool buttons), then the pattern list |
| Song editor pattern rows | 18 px pitch; current pattern marked with a ▷ and highlighted |
| Song editor grid | 16 px per bar; ruler numbers every 4 bars (1, 5, 9…); cells coloured per pattern along a blue→yellow→red ramp |
| Pattern editor header | pattern name centred (~18 px), then a tool row: tools, size `4 / 4`, resolution menu (`1/8 - eighth`) |
| Instrument list | 200 px wide: name 162 px, then **M** (mute) and **S** (solo) buttons 19 px each; kit name (`GMRockKit`) as a 23 px header |
| Pattern editor rows | 21 px pitch (20 px + 1 px separator); current instrument highlighted |
| Pattern editor grid | 144 px per beat (576 px per 4/4 bar); notes drawn as dots on the row's centre line, filled for full velocity, hollow for quieter notes; velocity lane below |
| Instrument rack | ~300 px wide, tabs **Param.**, **Comp.**, **Library**; instrument name banner; knob groups: ADSR; gain + mute group, auto-stop note, apply velocity; filter cutoff/resonance; pitch, coarse, fine, random; MIDI output channel + note; hi-hat pressure group, min/max range |

## Mixer window

Separate window. One 56 px strip per instrument: play, mute, solo,
pan knob, four FX send knobs, a level readout (`0.00`), a fader with meter and
the instrument name written vertically. Then four FX slots (`No plugin`,
bypass, edit, return knob) and the master section: mute, level readout,
master fader with meter, humanize velocity/timing and swing knobs, **FX** and
**Peak** buttons.

## Preferences window

Tabs: General, Audio System, MIDI System, OSC, Appearance, Shortcuts.
Audio: driver, host API, device, buffer size (1024), latency target, sample
rate (44100), track output, JACK options, *Apply and restart driver*,
polyphony (256), metronome volume, resampling. MIDI: driver, input, output,
*Apply and restart driver*, a link to the MIDI control dialog.

Upstream's Qt dialog keeps its own driver lists and doesn't offer "Midi Kit";
the native one must list Media Kit and Midi Kit, and leave out JACK, host API
and device fields that don't apply on Haiku.

## Menus (upstream)

- **Project:** New (Ctrl+N), Open (Ctrl+O), Open Demo (Ctrl+D), Open Recent ▸ /
  Song Properties / Save (Ctrl+S), Save As... (Ctrl+Shift+S) / Export MIDI File
  (Ctrl+M), Export Song (Ctrl+E), Export LilyPond File (Ctrl+L) / Quit (Ctrl+Q)
- **Undo:** Undo (Ctrl+Z), Redo (Ctrl+Y), Undo History
- **Pattern:** New, Open / Properties / Save, Save As... / Duplicate / Online Import
- **Drumkit:** New, Open / Properties / Save, Save As... / Export, Import, Online Import
- **View:** Playlist Editor, Director (Alt+D), Mixer (Alt+M), Rack (Alt+I),
  Automation Path (Alt+A), Playback Track / Full screen (Alt+F)
- **Options:** MIDI Control, Preferences (Alt+P)
- **Debug:** Show Audio Engine Info, Show Filesystem Info, Log Level ▸, Open Log File
- **Info:** User Manual (Ctrl+?), About, Report Bug, Donate

## Haiku conventions

Proposed differences from upstream (each can be vetoed):

1. **Project → File**, with "About Hydrogen…", "Settings…" (Alt+,) and "Quit"
   (Alt+Q) at its bottom, as many Haiku apps do.
2. **Undo → Edit:** Undo (Alt+Z), Redo (**Alt+Shift+Z** instead of Ctrl+Y),
   Undo history.
3. **Options → Settings:** "MIDI control…" stays as its own item; "Preferences"
   becomes "Settings…" in File.
4. **Info → Help:** "User guide", "Report a bug", "Donate"; About moves to File.
5. **Sentence case and `…`** everywhere: "Song properties…", "Save as…",
   "Export MIDI file…", "Open demo…", "Playlist editor", "Automation path".
6. **Shortcuts** use the Command key (Alt on Haiku). Three upstream shortcuts
   collide with reserved ones and change: Export LilyPond file loses Ctrl+L
   (reserved for Replace and find), Automation path becomes Alt+Shift+A
   (Alt+A is Select all), Full screen becomes Alt+Shift+F (Alt+F is Find).
7. **Settings** are stored in `~/config/settings/Hydrogen/` instead of
   `~/.config/hydrogen/`, and user data (songs, kits, patterns, playlists) in
   its `data` folder instead of `~/.local/share/hydrogen/` (`~/config/data`
   is part of the read-only package tree). The log goes to
   `~/config/var/log/Hydrogen.log`.
8. **Remembered window frames** for the main window, mixer and other panels.

## Not yet captured

Rack Comp./Library tabs, playlist editor, director, sample editor, song and
pattern properties, export dialogs, MIDI control dialog, About.
