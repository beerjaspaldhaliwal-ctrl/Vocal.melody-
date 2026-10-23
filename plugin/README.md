# MelodyLift plugin (VST3 / AU)

Two tools in one plugin, built for pop songwriting:

- **Generator**: writes chord progressions, catchy hooks, basslines and
  riffs in eight pop styles, plays them back, and lets you drag each part
  into FL Studio. In the spirit of tools like MIDI Wizard.
- **Vocal to MIDI**: put it on your vocal track, press **Capture**, play the
  song, and the sung melody appears as MIDI on your song's bars.

It installs as two plugins from the same code:

| Plugin | Where it goes in FL Studio | Best for |
| --- | --- | --- |
| **MelodyLift Generator** (instrument) | Channel Rack | Writing hooks, chords, bass, riffs. Plays them with the song |
| **MelodyLift** (effect) | Mixer insert of your vocal | Capturing a vocal as MIDI (the Generator tab is there too) |

## The generator

Pick a **Genre**, a **Key** and a **Length**, then press **Generate**.

| Genre | What it writes |
| --- | --- |
| Pop Hit | Radio-pop progressions (I–V–vi–IV family), arching hooks with a repeated motif, 8th-note bass |
| Melodic Rap / R&B | Minor keys with 7th and 9th chords, narrow falling flows with repeated notes and triplets, 808 bass, bell riffs |
| Punjabi Pop | Harmonic minor with the raised 7th, high-starting hooks with murki turns and grace notes, bhangra-style bass, tumbi riffs |
| Desi / UK Drill | Dark minor progressions with the major V, sparse falling hooks, sliding 808 rhythms, answer phrases |
| Afrobeats | Bouncy syncopated hooks, 3-3-2 rhythms, marimba-style riffs, off-beat chord skanks |
| Dance Pop / EDM | Anthem progressions, busy plucky hooks, off-beat bass and stabs, 16th arps |
| Emotional Ballad | Wider rising melodies, 7th chords, broken-chord piano, walking bass |
| Trap Melody | Minor loops, triplet flows, 808s, bell riffs |

The genres capture each style's musical traits (scales, rhythms, chord
habits, ornaments). They do not copy any artist's songs.

### How it writes a hook

1. Choose a proven chord progression for the genre and key, voiced with smooth
   voice leading.
2. Write a two-bar rhythmic motif from the genre's rhythm vocabulary (density,
   syncopation and triplets set by your controls).
3. Build the phrase plan: motif, answer, motif, resolution (call and
   response). Repeats re-fit the motif to the new chords.
4. Choose every note by scoring candidates: chord tones on strong beats,
   mostly steps with the occasional signature leap (followed by a step
   back), a contour that fits the genre, and an ending that resolves.
5. Write 24 complete hooks and keep the one with the best hook score
   (chord fit, singability, range, a single peak, resolution). The score is
   shown in the status bar.
6. Add genre ornaments (grace notes, murki turns, falls) and humanize.

### Controls

| Control | Effect |
| --- | --- |
| Generate | New chords, hook, bass and riff (locked layers stay) |
| New hook | New hook over the same chords |
| Variation | Same rhythm, new notes: a related version of the hook |
| New chords | New progression; the hook re-fits to it |
| Catchiness | How strongly the hook repeats its motif |
| Density / Syncopation | Fewer, longer notes ↔ busy flows; on the beat ↔ off-beat bounce |
| Variation (slider) | How much the repeats change |
| Ornaments | Grace notes, turns, falls (strongest in Punjabi Pop) |
| Range / Register | How wide and how high the hook sits |
| Humanize / Swing | Velocity and timing feel |
| Use my vocal's key | Takes the key detected from your captured vocal |

Moving a control re-renders the **same idea** with the new setting, so you
can shape a hook you like. The dice buttons are what make new ideas.

Each layer card (Melody, Chords, Bass, Riff) has:
**mute** (speaker), **lock** (padlock: kept by Generate), **New** (re-roll
this layer) and **Drag** (drag this layer into FL). Click a card to edit that
layer in the piano roll; the others are shown faded behind it.

**Preview** loops the pattern with built-in sounds (lead, pad, 808, pluck).
**Drag all** drops all four layers into FL as a multi-track MIDI file.

### Chords for your vocal

On the Vocal to MIDI tab, after capturing or loading a vocal, press
**Chords for vocal**. MelodyLift detects the key, picks the best chord for
each bar under your melody, and writes bass and a riff around it.

## Download

Builds are produced automatically by GitHub Actions
(**Actions** tab → **Plugin** workflow → latest run → *Artifacts*):

| File | What it is |
| --- | --- |
| `MelodyLift-Windows` → `MelodyLift-1.0.0-Windows-Setup.exe` | Installer. Puts both plugins in `C:\Program Files\Common Files\VST3` |
| `MelodyLift-Windows` → `MelodyLift-Windows-VST3.zip` | Both plugins without the installer, plus the standalone app |
| `MelodyLift-macOS` | VST3, AU and standalone apps (Apple Silicon + Intel) |
| `MelodyLift-Linux` | VST3 and standalone apps |

Pushing a tag like `v1.1.0` also attaches these files to a GitHub Release.

The builds are not code-signed. On Windows, SmartScreen may say "Windows
protected your PC": click **More info → Run anyway**. On macOS, run
`xattr -dr com.apple.quarantine` on each plugin after copying it to
`~/Library/Audio/Plug-Ins/`.

## Set up in FL Studio

1. Run the installer (or copy both `.vst3` folders to
   `C:\Program Files\Common Files\VST3`).
2. In FL Studio: **Options → Manage plugins → Find installed plugins**.
3. **Generator**: in the Channel Rack click **+** and choose
   **MelodyLift Generator**. Press play in FL and the pattern plays with your
   song, looping. Drag the parts you like into the Playlist or onto other
   channels' piano rolls.
4. **Vocal to MIDI**: route your vocal to a mixer insert, click an empty
   effect slot on that insert and choose **MelodyLift**.
5. To drive your own synths live instead of dragging MIDI, set **MIDI out**
   to *Generated pattern*. Each layer goes out on its own channel (melody 1,
   chords 2, bass 3, riff 4). In FL, set an **Output port** in the plugin's
   wrapper settings and the same port as the **Input port** of each target
   instrument.

## Vocal to MIDI

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
   - **MIDI out** (optional): set *MIDI out* to *Vocal notes* and the
     plugin plays the notes in time with the song. In FL, open the
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

- `Source/core/MelodyCore.*` – vocal detection engine, plain C++ (no JUCE)
- `Source/core/Generator.*` – the pop generator, plain C++ (no JUCE)
- `tests/CoreTests.cpp` – unit tests for both
- `Source/Synth.h` – preview synth
- `Source/PluginProcessor.*` – capture on the song timeline, background
  analysis, MIDI out, project state
- `Source/PluginEditor.*`, `Source/PianoRoll.*` – the UI
- `tests/PluginTests.cpp` – drives the real processor the way a DAW does
- `installer/MelodyLift.iss` – Windows installer (Inno Setup)

JUCE is licensed under AGPLv3 or a commercial JUCE licence. Selling
closed-source builds of this plugin requires a JUCE licence.
