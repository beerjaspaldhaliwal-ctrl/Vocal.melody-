# MelodyLift: vocal to MIDI

Upload a vocal (or sing/hum into the mic) and MelodyLift writes the melody as
MIDI notes you can edit and drop into any DAW (FL Studio, Ableton, Logic,
Cubase, Studio One, Reaper…).

Everything runs locally in your browser. No upload, no server, no account.

**Want it inside FL Studio?** There is a VST3/AU plugin version in
[`plugin/`](plugin/README.md): capture the vocal straight from your mixer
track and drag the MIDI into the playlist.

## Use it

- **Open `dist/melodylift.html`** in Chrome, Edge, Firefox or Safari. It is a
  single self-contained file that works offline. (Or open `index.html`, which
  loads the source files directly.)
- Click **Open vocal** (or drag a file onto the piano roll). WAV, MP3, M4A,
  FLAC and OGG all work.
- Check the notes against your vocal with the **Vocal / MIDI** volume sliders
  and **Space** to play.
- Set the song **tempo** so bars line up, pick a grid, optionally **quantize**.
- **Export MIDI**, or in Chrome drag the **Drag MIDI** chip straight onto a DAW
  track. The MIDI file starts at 0:00 of the audio, so if your vocal stem was
  exported from bar 1 it lines up at bar 1.

Tip: results are best on an isolated vocal (a stem or acapella). For a full
mix, separate the vocal first with a stem splitter (e.g. Demucs, UVR, or your
DAW's stem separation).

## What it does

| Stage | How |
| --- | --- |
| Pitch tracking | FFT-accelerated YIN with parabolic interpolation, 10 ms hop, 16 kHz analysis, rumble high-pass |
| Voicing | Periodicity (clarity) + loudness gate relative to the loudest part, median cleanup of octave blips |
| Tuning | Detects singers who are sharp/flat against A440 (circular mean of cents) |
| Note decoding | Viterbi (HMM) over semitone states. Vibrato, scoops and drift stay inside one note instead of flickering |
| Repeated notes | Splits same-pitch syllables on short-window energy dips |
| Cleanup | Short transition blips are absorbed into neighbouring notes |
| Velocity | From each note's loudness |
| Key | Krumhansl-Schmuckler key detection; optional snap-to-scale (7 scales) |
| Timing | Tempo + tap tempo, grids 1/4 to 1/32 incl. triplets, quantize strength |
| Export | Standard MIDI file (PPQ 480, tempo + 4/4 meta), optional pitch-bend curve for vibrato/slides with RPN bend range |

### Editor

- Click a note to select (Shift-click for more, Ctrl/Cmd+A for all)
- Drag to move (snaps to the grid, hold **Alt** to move freely), drag the right
  edge to resize
- Double-click empty space to add a note, double-click a note to delete it
- ↑/↓ moves selected notes by a semitone (Shift: octave), ←/→ nudges in time
- Delete/Backspace removes; Ctrl/Cmd+Z undo, Ctrl/Cmd+Shift+Z or Ctrl+Y redo
- Mouse wheel scrolls, Shift+wheel scrolls sideways, Ctrl/Cmd+wheel zooms
  time, Alt+wheel zooms pitch; click the ruler to move the playhead; click
  the keyboard to hear a pitch

### Settings that matter most

- **Note stability**: raise it if vibrato or slides are chopped into extra
  notes; lower it for fast runs and melisma.
- **Pitch clarity** / **Silence threshold**: raise them if breaths, reverb
  tails or bleed create junk notes.
- **Syllable split**: lower it if repeated words on one pitch are merged.

## Development

```
npm test        # synthetic-vocal test suite (Node 18+)
npm run build   # rebuild dist/ single-file versions
```

Source lives in `src/` (`dsp.js` pitch tracking, `notes.js` transcription,
`midi.js` MIDI writer, `app.js` UI) and `css/style.css`. No dependencies.

## Limits and roadmap

- Monophonic: it transcribes one melody line (a lead vocal), not chords or
  harmonies sung together.
- The web app runs in the browser; for use inside a DAW see the
  [plugin](plugin/README.md).
