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
9. **Canvas colours:** the editors (grids, rows, velocity lane) draw in
   Hydrogen's colour theme from the core's preferences by default; "Use
   system colors in editors" switches them to
   colours derived from the Haiku system colours (Settings > Appearance).
   Controls always use the system look. Mute and solo stay red and yellow
   in both.
10. **Pattern editor header:** standard Haiku controls with labels ("Size:",
   "Grid:") instead of upstream's unlabelled LCD spin boxes.
11. **Zoom** with Command+wheel over the grid (HIG), in steps of ×2.
12. **Velocity:** drag across the bars with the primary button (upstream
   draws with the secondary button in its select mode; on Haiku the
   secondary button is for context menus).
13. **Song editor:** click toggles a cell and dragging toggles every cell
   crossed (upstream's draw mode); one stroke is one undo step. No lasso,
   move or copy/paste of cells yet. "Clear pattern sequence" is undoable, so
   it doesn't ask first. New patterns get a name directly ("Pattern N")
   until the pattern properties dialog is ported; renaming opens a small
   window instead of an inline field. The playback-track, timeline (tempo
   markers, tags) and automation strips aren't ported yet, so the ruler is
   25 px instead of upstream's 50 px header.
14. **Rack:** tabs "Parameters", "Components", "Library" (upstream: "Param.",
   "Comp." — the HIG asks for no abbreviations). The parameters sit in
   labelled boxes with standard spinners and checkboxes instead of
   upstream's drawn panel; knobs follow upstream's Rotary (44×26, 100 px of
   drag is the whole range, wheel, arrow keys) but reset with a
   double-click instead of Ctrl+click, and Shift drags finely (upstream:
   Ctrl drags faster). The filter's on/off button is an "Active" checkbox.
   Like upstream, parameter changes are not undoable; renaming is.
15. **Library:** double-click loads a kit, opens a song (asking about
   unsaved changes, which upstream doesn't) or adds a pattern, as well as
   the context menu (upstream has no double-click). Items are grouped by
   Session / Custom / User / System without upstream's sub-folder levels,
   and there is no info pane, properties, duplicate, delete, import or
   export yet.
16. **Components tab:** each component is a box with its header, the layer
   preview (20 px numbered strip, 16 px rows, upstream's colours), a layer
   toolbar, "Sample selection" (sentence case of upstream's "Sample Sel."
   options), the waveform and the layer's pitch. Structural edits are
   undoable as upstream's copy-and-replace actions; gains, mute, solo,
   pitch and velocity-range drags apply directly. Samples are chosen in a
   Haiku file panel showing only supported audio formats; adding several
   at once spreads their velocity ranges evenly (upstream leaves that to
   a checkbox in its file browser, which also offers renaming the
   component or instrument after the file — not ported). Reordering
   layers by dragging, dropping files onto the preview and the "Edit
   layer" button (sample editor) aren't ported yet. Renaming a component
   opens a small window instead of an inline field.
17. **Mixer:** follows upstream's strips (56×254), FX slots and master
   section, drawn in the canvas colours. Differences: strip meters use the
   fader's full height (upstream's are only 20 px tall, which looks like a
   bug — it uses the fader's width), double-click sets a fader to unity
   (1.0) instead of Ctrl+click setting the 1.5 maximum. Each effect
   slot's "Edit…" opens its effect window (item 23). The mixer settings
   dialog (pan law) isn't ported yet; MIDI learn is item 26. On
   Haiku, LADSPA_PATH defaults to the system and user lib/ladspa folders.
18. **Settings window** (File > Settings…, Alt+,; upstream's Preferences
   dialog): tabs General, Audio, MIDI, Appearance. Settings apply as they
   change, with Revert (back to how the window opened) and Defaults, per
   the HIG; driver, buffer size, sample rate and MIDI ports apply with
   "Apply and restart driver", as upstream. The options of upstream's MIDI
   control dialog (note mapping and channel, note-off, clock and transport
   in/out, feedback, channels) are on the MIDI tab. Left out: the language
   menu (Haiku apps follow the system language), the Shortcuts tab (Haiku's
   standard shortcuts), fonts, Qt style, layout, scaling and colour theme
   editing (the system look covers them), JACK/PortAudio/OSC options (not
   built on Haiku) and the beat counter (not ported yet). The
   MIDI action table (MIDI learn) isn't ported yet.
19. **Properties window** (File > Song properties…, Pattern > Properties…
   and "Properties…" in the pattern list's menu; upstream's song and
   pattern properties dialogs): name, version, author, licence menu with
   the licence text, notes, tags (comma-separated) and the file. Apply
   changes them as one undoable step; a pattern name that's taken gets a
   number, as upstream. The licence text is hidden for "Unspecified".
20. **Export song** (File > Export song…, Alt+E): file and Choose…, format
   (only those the core was built with), sample rate and depth for
   uncompressed formats, compression for FLAC/Opus/Ogg/MP3, export to a
   single track, separate tracks ("song-Kick.flac" …, instruments without
   notes skipped) or both, resampling, timeline tempo and Rubberband
   options, a progress bar. Each export is its own core export session,
   ended when it's done, so the audio driver comes back between exports;
   Close is disabled while writing (the DiskWriter can't be interrupted).
   Fixed from upstream: its progress bar never moved (min instead of max)
   and a second export in the same dialog left the song in song mode.
   Left out: the licence summary shown before exporting.
21. **Export MIDI file** (File > Export MIDI file…, Alt+M): file, SMF
   format (SMF1 one track, SMF1 one track per instrument, SMF0) and
   humanization; lists any time signatures that had to be changed.

22. **Sample editor** (double-click a layer's waveform in the Components
   tab, as upstream): the file's two channels with start, loop and end
   markers (drag the lines or their S/L/E tabs; moving one pushes the
   others along), a one-frame-per-pixel detail view with zoom, start /
   loop / end / mode / loop count fields, Rubberband length in beats,
   pitch and crispness (shown only when librubberband or the rubberband
   tool is there), and the result with its volume and pan envelopes
   (click adds a point, right-click removes one, end points move only up
   and down). Edits are rendered at once; Play and Play original preview
   with a moving playhead; Apply replaces the instrument as one undoable
   step, Revert goes back, closing with changes asks. Differences: the
   editor isn't modal and has no undo steps of its own (Revert instead);
   it follows undo in the main window when it has no changes; it
   remembers its frame. Fixed from upstream: the crispness menu did
   nothing, "Off" was ignored at a length of 4 beats, spinners of pushed
   markers went stale, and switching Rubberband on also transposed by the
   core's default 1 semitone. Not ported: editing the playback track.

23. **Effect window** (Edit… on a mixer effect slot; upstream's LADSPA FX
   properties): a "Plugin:" menu of the installed LADSPA plugins by name
   ("No plugin" removes it; replaces upstream's selector dialog with its
   plugin groups and its Remove button), an "Active" check box (upstream's
   Activate/Deactivate button), the plugin's label and library, a fader
   per input control port with its value (integer ports snap,
   double-click restores the plugin's default, arrow keys step), a check
   box for toggle ports (upstream left them as faders, "not implemented
   yet"), and a meter per output port, polled every 100 ms. One window
   per slot, sized to its controls up to the screen width, remembering
   its frame; it follows changes made elsewhere (another song, Bypass).

24. **Drumkit menu** (upstream's, minus shortcut-less session items):
   - *New…* asks, then replaces the song's kit with an empty one (undoable).
   - *Open ▸* lists the installed kits, system kits first, the song's kit
     ticked (upstream: a dialog with the sound library tree). Undoable.
   - *Properties…* (DrumkitWindow): General tab with name, version,
     author, licence + text, notes, tags, image with preview, Choose…
     and Clear, image licence; Instrument types tab (types must be
     unique, duplicates marked). Apply is one undoable step; notes of an
     instrument that just got a type take it over, as upstream. Notes are
     shown and saved as plain text (upstream wrapped them in Qt HTML on
     every save). A new image is copied into the cache as upstream; no
     image is ever deleted (upstream could delete the one it copied).
     The song isn't saved behind the user's back (upstream saved it).
   - *Save* writes a user or custom kit to its folder; a new or system
     kit goes to *Save as…*, the same window with a Folder field
     (following the name, default in the user's drumkits). Written from
     a copy, so the song's kit keeps its samples' paths.
   - *Export…* saves the kit as it is in the song to an .h2drumkit file
     with the chosen name (upstream made a folder of that name and put
     the archive inside).
   - *Import…* installs an .h2drumkit into the user's drumkits and loads
     it (undoable). Fixed: the kit was entered into the sound library a
     second time under another spelling of its folder.
   - Left out: the licence table tab, the licence checks before export,
     Online import (not ported yet), Save to session (NSM, not on Haiku).

25. **Autosave** (upstream's): "Autosaves per hour" on the General tab of
   Settings (0 is off, at most 360). A modified song is saved to
   ".<name>.autosave.h2song" next to it, or in the user's songs folder if
   that isn't writable; an untitled one beside Filesystem::emptyPath().
   Opening a song, or starting, offers to recover an autosave newer than
   the song ("Don't recover" / "Recover"); recovered songs show as
   modified and keep their own file name. File > New starts fresh
   (removes the untitled autosave), as upstream. Differences: a saved
   song's autosave is removed (upstream left it behind, merely older),
   and playlists aren't autosaved (the playlist editor isn't ported).

26. **MIDI learn** (upstream's MidiLearnable / MidiSenseWidget and MIDI
   action table). Shift-click a learnable control to open "Bind MIDI
   event" (a floating window: the action, its bindings, "Waiting for MIDI
   input…" naming the action channel if one is set, Clear bindings,
   Cancel); the next incoming note, CC, program change or MMC message is
   bound, as one undoable step. Tooltips of learnable controls add
   "MIDI: <ACTION>" and a "Bound to …" line per binding, or "Not bound
   (Shift-click to bind)". Learnable, with upstream's actions: mixer
   strip mute, solo, pan, FX sends, volume (STRIP_* / PAN_ABSOLUTE /
   EFFECT_LEVEL_ABSOLUTE with the strip index); master mute, volume,
   humanize velocity and timing, swing; transport previous bar, record,
   play/pause, stop, next bar. Settings > MIDI actions lists every
   binding: Learn (takes the next event for that row), event, number
   (note/CC), action, its parameters (instrument with "Selected", FX,
   component, layer, pattern, song, factor), Remove, and "Add binding";
   rows only become bindings once complete; edits are undoable and the
   tab resyncs when the window is activated. Differences: a Cancel
   button; TIMING_CLOCK_TICK isn't offered (driven by the clock). Fixed
   in the core (with tests): bindings of different strips were treated
   as duplicates (MidiAction::isEquivalentTo ignored the instrument) and
   removing one removed all; removal used an erased iterator. Testing
   tool: h2midisend cc|note|pc NUMBER [VALUE] [CHANNEL].

27. **Export LilyPond file** (File > Export LilyPond file…, Alt+L, like
   upstream's Ctrl+L): upstream's "experimental" note as an alert with
   Cancel/Continue, then a save panel (name from the song, ".ly" added),
   stopping playback, as upstream; the core's LilyPond writer. It doesn't
   report failure, so an empty or missing file is reported instead.

## Not yet captured

Playlist editor, director, MIDI control dialog.
