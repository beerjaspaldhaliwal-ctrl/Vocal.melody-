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

Pick a **Genre**, a **Song section**, a **Key** and a **Length**, then press
**Generate**.

| Scene | Genres | What they write |
| --- | --- | --- |
| Hollywood / Western Pop | Pop Hit, Synth-Pop, Dance Pop / EDM, Emotional Ballad | Radio-pop progressions, arching hooks with a repeated motif, anthem choruses, 80s plucks, piano ballads |
| R&B | 90s R&B, Neo-Soul, Alt R&B, Trap-Soul | 7th and 9th chords (ii–V–I, IVmaj7–iii7), laid-back timing, melisma runs, blue notes, falling tails, slides |
| Hip-Hop | Melodic Rap, Trap Melody, Desi / UK Drill | Narrow repeated-note flows, triplets, 808s, dark minor with the major V |
| Punjabi | Punjabi Pop, Punjabi Romantic | Harmonic or natural minor, high-starting hooks, murki turns, meend slides, tumbi riffs, bhangra bass |
| Bollywood | Bollywood Romantic, Bollywood Dance, Sufi Pop | Long rising lines with meend and gamak-style vibrato, raga-flavoured scales, dhol-driven dance hooks, Sufi high openings and runs |
| Afro | Afrobeats | Bouncy 3-3-2 syncopation, marimba riffs, off-beat skanks |

**Scale / raga** offers the western modes plus Yaman (Lydian), Kafi
(Dorian), Khamaj (Mixolydian), Bhairavi (Phrygian) and Bhairav.

**Song section** shapes the hook the way a topliner would:

| Section | Effect |
| --- | --- |
| Verse | Lower, more conversational, more notes, less repetition |
| Pre-Chorus | A rising line that builds toward the chorus |
| Chorus | Higher and wider, longer notes, strongest repetition |
| Post-Chorus Hook | One catchy bar, said twice |

The genres capture each style's musical language (scales, phrase shapes,
rhythms, chord habits, ornaments, timing). They do not copy songs. The
phrase library is a set of generic phrase shapes written for this plugin,
and every hook is newly composed over your chords.

### Vocal feel (humanized, sung-sounding melodies)

**Vocal feel** turns a MIDI line into something that phrases like a singer:

- **Scoops** up into notes and **meend** slides between connected notes
  (strongest in Bollywood, Punjabi and Sufi)
- **Vibrato** that starts after the note settles, wider for Bollywood
  (gamak-like)
- **Falls** off phrase ends (R&B, rap)
- **R&B runs** (melisma), **blue-note** slides, **murki** turns (via Ornaments)
- **Pocket**: R&B and Neo-Soul sit slightly behind the beat, Afrobeats a
  touch ahead
- **Dynamics** that swell toward each phrase's peak and ease off at the end

The piano roll shows these on the melody notes (a hook for a scoop or slide,
a wave for vibrato, a tail for a fall). With **Export vocal expression** on,
they go into the MIDI file as pitch bends (bend range ±2), so a vocal
synth, sampler or lead plays them back. **Melody sound** switches the
preview between a lead synth and an "aah" vocal voice.

### How it writes a hook

1. Choose a proven chord progression for the genre and key, voiced with smooth
   voice leading.
2. Write a two-bar motif: either a phrase shape from the genre's phrase
   library (how often is set by **Genre phrases**) or a rhythm built from the
   genre's rhythm vocabulary (density, syncopation and triplets set by your
   controls).
3. Build the phrase plan: motif, answer, motif, resolution (call and
   response). Repeats re-fit the motif to the new chords.
4. Choose every note by scoring candidates: chord tones on strong beats,
   mostly steps with the occasional signature leap (followed by a step
   back), a contour that fits the genre, and an ending that resolves.
5. Write 24 complete hooks and keep the one with the best hook score
   (chord fit, singability, range, a single peak, resolution). The score is
   shown in the status bar.
6. Add genre ornaments (runs, blue notes, murki turns, grace notes), then
   the vocal feel (scoops, slides, vibrato, falls, pocket, dynamics) and
   humanize.

### Controls

| Control | Effect |
| --- | --- |
| Generate | New chords, hook, bass and riff (locked layers stay) |
| New hook | New hook over the same chords |
| Variation | Same rhythm, new notes: a related version of the hook |
| New chords | New progression; the hook re-fits to it |
| Catchiness | How strongly the hook repeats its motif |
| Genre phrases | How often the hook grows from the genre's phrase library instead of a fresh rhythm |
| Song section | Verse, Pre-Chorus, Chorus or Post-Chorus Hook |
| Vocal feel | Scoops, slides, vibrato, falls, pocket and dynamics |
| Density / Syncopation | Fewer, longer notes ↔ busy flows; on the beat ↔ off-beat bounce |
| Variation (slider) | How much the repeats change |
| Ornaments | Runs, blue notes, murki, grace notes, falls |
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
