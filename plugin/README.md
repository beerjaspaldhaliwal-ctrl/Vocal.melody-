# MelodyLift plugin (VST3 / AU)

MelodyLift as a plugin that runs inside your DAW. Put it on your vocal track,
press **Capture**, play the song, and the vocal melody appears as MIDI notes
on your song's bars. Then drag the MIDI straight into FL Studio.

It uses the same detection engine as the web app (YIN pitch tracking +
Viterbi note decoding), rewritten in C++.

## Download

Builds are produced automatically by GitHub Actions
(**Actions** tab → **Plugin** workflow → latest run → *Artifacts*):

| File | What it is |
| --- | --- |
| `MelodyLift-Windows` → `MelodyLift-1.0.0-Windows-Setup.exe` | Installer. Puts `MelodyLift.vst3` in `C:\Program Files\Common Files\VST3` |
| `MelodyLift-Windows` → `MelodyLift-Windows-VST3.zip` | The same plugin without the installer, plus the standalone app |
| `MelodyLift-macOS` | VST3, AU and standalone app (Apple Silicon + Intel) |
| `MelodyLift-Linux` | VST3 and standalone app |

Pushing a tag like `v1.0.0` also attaches these files to a GitHub Release.

The builds are not code-signed. On Windows, SmartScreen may say "Windows
protected your PC": click **More info → Run anyway**. On macOS, run
`xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/VST3/MelodyLift.vst3`
(and the same for the `.component`) after copying the plugin.

## Set up in FL Studio

1. Run the installer (or copy `MelodyLift.vst3` to
   `C:\Program Files\Common Files\VST3`).
2. In FL Studio: **Options → Manage plugins → Find installed plugins**.
   MelodyLift appears under effects.
3. Route your vocal to a mixer insert, click an empty effect slot on that
   insert and choose **MelodyLift**.

## Use it

1. Click **Capture** (it turns red), then play the song or pattern in FL.
   MelodyLift records the vocal at its song position.
2. Stop playback. The vocal is analysed automatically and the notes appear on
   your bars. You can capture different sections in any order. Each one
   lands in its place.
   Alternatively click **Load audio** or drop a WAV/MP3/FLAC file from FL's
   browser onto the plugin. A file is placed at the start of the song (bar 1).
3. Adjust the settings on the right if needed; the notes update instantly.
4. Get the MIDI into FL:
   - **Drag MIDI**: drag the chip onto the **Playlist** at bar 1 (or onto a
     channel's piano roll). Because the file starts at 0:00 of the song, the
     notes land where they were sung.
   - **Save MIDI**: writes a `.mid` file anywhere.
   - **MIDI out** (optional): turn on *Send notes to MIDI out on playback*
     and the plugin plays the notes in time with the song. In FL, open the
     plugin's wrapper settings (gear icon), set a MIDI **Output port**, and set
     the same number as the **Input port** of the instrument you want to play.

### Editing

- Click a note to select it (Shift-click for more, Ctrl+A for all)
- Drag to move (snaps to the grid, hold **Alt** for free movement), drag the
  right edge to resize
- Double-click empty space to add a note, double-click a note to delete it
- ↑/↓ moves by a semitone (Shift: octave), ←/→ nudges by the grid
- Delete removes, Ctrl+Z / Ctrl+Shift+Z (or the buttons) undo and redo
- Mouse wheel scrolls, Shift+wheel scrolls sideways, Ctrl+wheel zooms time,
  Alt+wheel zooms pitch, **Fit** shows everything

Notes, edits, settings and the pitch analysis are saved with your FL project.

### Settings

| Setting | Use it when |
| --- | --- |
| Voice range | Very low (bass) or very high voices |
| Pitch clarity / Silence threshold | Breaths, reverb tails or bleed turn into junk notes: raise them |
| Note stability | Vibrato or slides are chopped into extra notes: raise it. Fast runs are smoothed over: lower it |
| Shortest note | Remove (raise) or keep (lower) very short notes |
| Syllable split | Repeated words on one pitch are merged: lower it |
| Detect tuning | The singer is consistently sharp or flat against A440 |
| Key / Scale / Snap | Force every note into a key |
| Transpose | Move the melody into another octave or key |
| Grid / Quantize | Snap timing to 1/4 … 1/32 (incl. triplets), with strength |
| Pitch bends | Export vibrato and slides as pitch-bend data, for a bend range of ±2 or ±12 |

## Notes and limits

- Monophonic: one melody line (lead vocal), not harmonies or chords.
- Best on a dry, isolated vocal. On a full mix, separate the vocal first.
- Timing uses the project tempo at capture time. Songs with tempo changes
  will drift after the change.
- Captures up to 12 minutes of song time.

## Build it yourself

Requirements: CMake 3.22+, a C++17 compiler (Visual Studio 2022 on Windows,
Xcode on macOS). JUCE 8 is downloaded automatically.

```
cmake -S plugin -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release
```

The plugin ends up in `build/MelodyLift_artefacts/Release/VST3/`.

Linux needs: `libasound2-dev libfreetype-dev libfontconfig1-dev libx11-dev
libxrandr-dev libxinerama-dev libxcursor-dev libxext-dev libgl-dev`.
Add `-DMELODYLIFT_PLUGIN_TESTS=ON` to also build the end-to-end tests (they
need a display; use `xvfb-run` on a headless machine).

### Layout

- `Source/core/` – detection engine, plain C++ (no JUCE), unit tested in
  `tests/CoreTests.cpp`
- `Source/PluginProcessor.*` – capture on the song timeline, background
  analysis, MIDI out, project state
- `Source/PluginEditor.*`, `Source/PianoRoll.*` – the UI
- `tests/PluginTests.cpp` – drives the real processor the way a DAW does
- `installer/MelodyLift.iss` – Windows installer (Inno Setup)

JUCE is licensed under AGPLv3 or a commercial JUCE licence. Selling
closed-source builds of this plugin requires a JUCE licence.
