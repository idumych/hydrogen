# Test plan: Hydrogen

Hydrogen is a documents app (songs, drumkits) whose main job is making
sound. Every check runs in `hydrogen-release`: a QEMU VM with a clean
Haiku R1/beta6 install and nothing else, SSH alias `hydrogen-release`. The
shared `haiku-b6-test` can't be used: it runs a pre-beta6 nightly
(r1~beta5_hrev59741) with 39 MB free, and its sound card goes to a null
driver.

## Fixtures

None from the host: the package ships its demo songs in
`/boot/system/data/hydrogen/data/demo_songs`.

The `play` check needs the guest's sound card output: QEMU writes every
sample the guest plays to `~/VMs/hydrogen-release/audio.wav` on the host.

## Fresh state

Delete `~/config/settings/Hydrogen` (settings, user data, log) before the
first launch. The installed app must not be given `H2_SYS_DATA`.

## Feature checks

### play
Steps: start `/boot/system/apps/Hydrogen /boot/system/data/hydrogen/data/demo_songs/GM_kit_demo1.h2song`;
the window title shows the song. Note the size of `audio.wav` on the host,
click the "Play/pause" button (▶, after the "Elapsed time" display), wait
8 s, click the "Stop" button (■, right of it), note the size again.
(Upstream's Space shortcut for play/stop isn't in the port yet.)
Measure the audio written between the two sizes (peak and RMS level).
PASS when: `{p}-play.txt` has `peak <x> dBFS, ok`, written only if the
peak in that span is above -30 dBFS and the RMS above -50 dBFS, and the
OCR of a shot taken while playing shows the song name.

### export
Steps: start the app with `GM_kit_demo1.h2song` as above. Open the
"File" menu, "Export song…". In the "File:" field type
`/boot/home/{p}-export.wav`; the format follows the extension, so
"Format:" changes from the default FLAC to WAV. Leave the other settings
at their defaults (44100 Hz, 8 bit, a single track), click "Export", wait
until the file stops growing, click "Close". Copy the file to the host.
PASS when: `{p}-export.txt` has `RIFF … WAVE audio` (from `file` on the
host), at least 10 s long and not silent (peak above -30 dBFS), and the
OCR of the window shot shows "Export song".

## Log

Loading a song while the Media Kit node runs makes the audio thread miss a
few buffers (upstream logs each as an `(E)` line). `log-clean` passes when
the log has no error words and every lock timeout behind a missed buffer
names `Hydrogen::setSong`, i.e. none happened during playback.
