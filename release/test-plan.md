# Test plan: Hydrogen

Hydrogen is a documents app (songs, drumkits) whose main job is making
sound. The standard checks run in `haiku-b6-test`; the two feature checks
below say where they run.

## Fixtures

None from the host: the package ships its demo songs in
`/boot/system/data/hydrogen/data/demo_songs`.

The `play` check needs the guest's sound card output. `haiku-b6-test`
(VirtualBox) sends it to a null driver, so `play` runs in the app's QEMU
dev VM (`hydrogen-haiku`, Haiku R1/beta6), which writes every sample its
sound card plays to `~/VMs/hydrogen-haiku/audio.wav`. Install the same
`.hpkg` there and start the installed app, not the dev build.

## Fresh state

Delete `~/config/settings/Hydrogen` (settings, user data, log) before the
first launch. The installed app must not be given `H2_SYS_DATA`.

## Feature checks

### play
VM: `hydrogen-haiku`.
Steps: start `/boot/system/apps/Hydrogen /boot/system/data/hydrogen/data/demo_songs/GM_kit_demo1.h2song`;
the window title shows the song. Note the size of `audio.wav` on the host,
press Space (Play), wait 8 s, press Space again (Stop), note the size again.
Measure the audio written between the two sizes (peak and RMS level).
PASS when: `{p}-play.txt` has `peak <x> dBFS, ok`, written only if the
peak in that span is above -30 dBFS and the RMS above -50 dBFS, and the
OCR of a shot taken while playing shows the song name.

### export
VM: `haiku-b6-test`.
Steps: start the app with `GM_kit_demo1.h2song` as above. Open the
"File" menu, "Export song…". In the "File:" field type
`/boot/home/{p}-export.wav`, leave "Format:" at WAV, click "Export", wait
until the window's progress bar finishes, click "Close". Copy the file to
the host.
PASS when: `{p}-export.txt` has `RIFF … WAVE audio` (from `file` on the
host), at least 10 s long and not silent (peak above -30 dBFS), and the
OCR of the window shot shows "Export song".
