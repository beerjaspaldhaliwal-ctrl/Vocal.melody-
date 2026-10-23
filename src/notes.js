/*
 * Melody transcription: turns a frame-wise pitch track into discrete notes.
 *
 *   pitch track -> voicing + cleanup -> tuning estimate
 *               -> Viterbi decoding over semitone states (stable notes,
 *                  vibrato/scoops do not create note flicker)
 *               -> syllable splitting on energy dips (repeated notes)
 *               -> short-note cleanup
 *   then post-processing: scale snap, transpose, quantize.
 */
(function (root) {
  'use strict';
  const VM = (root.VM = root.VM || {});

  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const SCALES = {
    major: [0, 2, 4, 5, 7, 9, 11],
    minor: [0, 2, 3, 5, 7, 8, 10],
    harmonicMinor: [0, 2, 3, 5, 7, 8, 11],
    dorian: [0, 2, 3, 5, 7, 9, 10],
    mixolydian: [0, 2, 4, 5, 7, 9, 10],
    pentatonicMajor: [0, 2, 4, 7, 9],
    pentatonicMinor: [0, 3, 5, 7, 10],
    chromatic: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11],
  };
  // Krumhansl-Kessler key profiles
  const KK_MAJOR = [6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88];
  const KK_MINOR = [6.33, 2.68, 3.52, 5.38, 2.6, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17];

  const DEFAULTS = {
    confMin: 0.55, // minimum periodicity ("clarity") for a frame to count as sung
    gateDb: -45, // frames quieter than loudest - gate are treated as silence
    changeCost: 10, // Viterbi penalty for changing note (higher = steadier notes)
    silenceCost: 7, // Viterbi penalty for entering/leaving silence
    sigma: 0.45, // pitch tolerance in semitones for a frame to fit a note
    minNoteMs: 70, // notes shorter than this get absorbed or dropped
    splitDb: 6, // energy dip that splits repeated syllables (0 = off)
    tuningCents: null, // null = auto-detect reference tuning
  };

  function noteName(n) {
    return NOTE_NAMES[((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  }
  function hzToMidi(f) {
    return 69 + 12 * Math.log2(f / 440);
  }
  function midiToHz(m) {
    return 440 * Math.pow(2, (m - 69) / 12);
  }

  function percentile(arr, p) {
    const a = Array.from(arr).sort((x, y) => x - y);
    if (!a.length) return 0;
    return a[Math.min(a.length - 1, Math.max(0, Math.round((p / 100) * (a.length - 1))))];
  }

  function median(values) {
    const a = values.slice().sort((x, y) => x - y);
    const n = a.length;
    if (!n) return NaN;
    return n % 2 ? a[(n - 1) / 2] : 0.5 * (a[n / 2 - 1] + a[n / 2]);
  }

  /**
   * Voicing decision + pitch cleanup. Returns the per-frame data every later
   * stage works on: midi (NaN when unvoiced), weight, energy envelope.
   */
  function prepare(track, params) {
    const p = Object.assign({}, DEFAULTS, params);
    const T = track.f0.length;
    const peakDb = percentile(track.rmsDb, 99);
    const gate = peakDb + p.gateDb;

    const raw = new Float32Array(T).fill(NaN);
    for (let i = 0; i < T; i++) {
      const f = track.f0[i];
      if (f > 0 && track.conf[i] >= p.confMin && track.rmsDb[i] >= gate &&
          f >= track.fmin * 0.97 && f <= track.fmax * 1.03) {
        raw[i] = hzToMidi(f);
      }
    }

    // 5-frame median over voiced neighbours removes octave blips and jitter.
    const midi = new Float32Array(T).fill(NaN);
    const win = [];
    for (let i = 0; i < T; i++) {
      if (isNaN(raw[i])) continue;
      win.length = 0;
      for (let k = i - 2; k <= i + 2; k++) if (k >= 0 && k < T && !isNaN(raw[k])) win.push(raw[k]);
      const med = median(win);
      midi[i] = Math.abs(raw[i] - med) > 0.8 ? med : raw[i];
    }

    // Drop isolated voiced specks (< 3 frames) – usually noise or breaths.
    for (let i = 0; i < T; ) {
      if (isNaN(midi[i])) { i++; continue; }
      let j = i;
      while (j < T && !isNaN(midi[j])) j++;
      if (j - i < 3) for (let k = i; k < j; k++) midi[k] = NaN;
      i = j;
    }

    const src = track.envDb || track.rmsDb;
    const env = new Float32Array(T);
    for (let i = 0; i < T; i++) {
      let s = 0, c = 0;
      for (let k = i - 1; k <= i + 1; k++) if (k >= 0 && k < T) { s += src[k]; c++; }
      env[i] = s / c;
    }

    let tuning;
    if (p.tuningCents == null) tuning = estimateTuning(midi, track.conf);
    else tuning = p.tuningCents / 100;

    return { track, midi, env, peakDb, tuning, params: p };
  }

  /** Circular mean of the fractional MIDI pitch -> reference offset (semitones). */
  function estimateTuning(midi, weights) {
    let sx = 0, sy = 0;
    for (let i = 0; i < midi.length; i++) {
      const m = midi[i];
      if (isNaN(m)) continue;
      const w = weights ? weights[i] : 1;
      const a = 2 * Math.PI * m;
      sx += w * Math.cos(a);
      sy += w * Math.sin(a);
    }
    if (sx === 0 && sy === 0) return 0;
    let off = Math.atan2(sy, sx) / (2 * Math.PI);
    if (off >= 0.5) off -= 1;
    if (off < -0.5) off += 1;
    return off;
  }

  /**
   * Viterbi decoding over {silence, note lo..hi}. Uniform switching costs let
   * us do it in O(frames * states).
   */
  function decode(prep) {
    const { midi, tuning, params: p } = prep;
    const T = midi.length;
    let lo = Infinity, hi = -Infinity;
    for (let i = 0; i < T; i++) {
      const m = midi[i];
      if (!isNaN(m)) { lo = Math.min(lo, m - tuning); hi = Math.max(hi, m - tuning); }
    }
    const path = new Int16Array(T).fill(-1);
    if (!isFinite(lo)) return path;
    lo = Math.max(0, Math.floor(lo) - 1);
    hi = Math.min(127, Math.ceil(hi) + 1);
    const K = hi - lo + 2; // state 0 = silence, state s = note lo + s - 1

    const unvoicedNotePenalty = 2.5;
    const voicedSilencePenalty = 3;
    const cap = 12;
    const inv2s2 = 1 / (2 * p.sigma * p.sigma);

    let prev = new Float64Array(K);
    let cur = new Float64Array(K);
    const back = new Int16Array(T * K);
    prev.fill(-1e9);
    prev[0] = 0;

    for (let t = 0; t < T; t++) {
      // best and second-best note state of the previous frame
      let b1 = -Infinity, b1i = -1, b2 = -Infinity, b2i = -1;
      for (let s = 1; s < K; s++) {
        const v = prev[s];
        if (v > b1) { b2 = b1; b2i = b1i; b1 = v; b1i = s; } else if (v > b2) { b2 = v; b2i = s; }
      }
      const m = midi[t];
      const voiced = !isNaN(m);
      const off = t * K;

      // silence
      {
        const stay = prev[0];
        const enter = b1 - p.silenceCost;
        if (stay >= enter) { cur[0] = stay; back[off] = 0; } else { cur[0] = enter; back[off] = b1i; }
        cur[0] += voiced ? -voicedSilencePenalty : 0;
      }
      for (let s = 1; s < K; s++) {
        const other = s === b1i ? b2 : b1;
        let best = prev[s], from = s;
        if (other - p.changeCost > best) {
          best = other - p.changeCost;
          from = s === b1i ? b2i : b1i;
        }
        const fromSil = prev[0] - p.silenceCost;
        if (fromSil > best) { best = fromSil; from = 0; }
        let e;
        if (voiced) {
          const dv = m - tuning - (lo + s - 1);
          e = -Math.min(dv * dv * inv2s2, cap);
        } else e = -unvoicedNotePenalty;
        cur[s] = best + e;
        back[off + s] = from;
      }
      const tmp = prev; prev = cur; cur = tmp;
    }

    let st = 0, bv = -Infinity;
    for (let s = 0; s < K; s++) if (prev[s] > bv) { bv = prev[s]; st = s; }
    for (let t = T - 1; t >= 0; t--) {
      path[t] = st === 0 ? -1 : lo + st - 1;
      st = back[t * K + st];
    }
    return path;
  }

  /** Splits a frame range wherever the energy dips by >= splitDb and recovers. */
  function splitOnDips(env, a, b, splitDb) {
    const cuts = [];
    if (!(splitDb > 0)) return cuts;
    let peak = -Infinity, minIdx = -1, minVal = Infinity;
    for (let j = a; j < b; j++) {
      const v = env[j];
      if (minIdx < 0) {
        if (v > peak) peak = v;
        if (peak - v >= splitDb) { minIdx = j; minVal = v; }
      } else {
        if (v < minVal) { minVal = v; minIdx = j; }
        if (v - minVal >= splitDb) {
          if (minIdx - a >= 3 && b - minIdx >= 3) cuts.push(minIdx);
          peak = v; minIdx = -1; minVal = Infinity;
        }
      }
    }
    return cuts;
  }

  let noteIdCounter = 1;
  function nextId() {
    return noteIdCounter++;
  }

  /** Full transcription. Returns { prep, notes, key }. */
  function transcribe(track, params) {
    const prep = prepare(track, params);
    const p = prep.params;
    const { midi, env, tuning } = prep;
    const hopSec = track.hopSec;
    const path = decode(prep);
    const T = path.length;

    // runs of identical note state
    const segs = [];
    for (let i = 0; i < T; ) {
      if (path[i] < 0) { i++; continue; }
      let j = i;
      while (j < T && path[j] === path[i]) j++;
      const cuts = splitOnDips(env, i, j, p.splitDb);
      let a = i;
      for (const c of cuts.concat([j])) {
        segs.push({ a, b: c });
        a = c;
      }
      i = j;
    }

    let notes = [];
    for (const sg of segs) {
      const vals = [];
      let eSum = 0;
      for (let k = sg.a; k < sg.b; k++) {
        if (!isNaN(midi[k])) vals.push(midi[k] - tuning);
        eSum += Math.pow(10, env[k] / 20);
      }
      if (vals.length < Math.max(2, 0.3 * (sg.b - sg.a))) continue;
      // trim unvoiced frames at the edges
      let a = sg.a, b = sg.b;
      while (a < b && isNaN(midi[a])) a++;
      while (b > a && isNaN(midi[b - 1])) b--;
      const med = median(vals);
      const pitch = Math.round(med);
      const meanDb = 20 * Math.log10(eSum / (sg.b - sg.a) + 1e-12);
      notes.push({
        id: nextId(),
        pitch,
        detPitch: pitch,
        pitchRaw: med,
        start: a * hopSec,
        end: b * hopSec,
        db: meanDb,
        vel: 0,
        src: { start: a * hopSec, end: b * hopSec, a, b },
      });
    }

    notes = absorbShortNotes(notes, p.minNoteMs / 1000, hopSec);

    // velocity from loudness relative to the phrase peak
    if (notes.length) {
      const top = Math.max.apply(null, notes.map((n) => n.db));
      for (const n of notes) {
        const rel = Math.max(-30, n.db - top); // 0 .. -30 dB
        n.vel = Math.round(Math.max(30, Math.min(127, 112 + rel * 2.6)));
      }
    }

    return { prep, notes, key: detectKey(notes) };
  }

  /** Short notes (scoops, transitions) merge into a touching neighbour or go. */
  function absorbShortNotes(notes, minDur, hopSec) {
    const out = notes.slice();
    const touch = hopSec * 2.5;
    for (;;) {
      let idx = -1, shortest = minDur;
      for (let i = 0; i < out.length; i++) {
        const d = out[i].end - out[i].start;
        if (d < shortest) { shortest = d; idx = i; }
      }
      if (idx < 0) break;
      const n = out[idx];
      const prev = idx > 0 && n.start - out[idx - 1].end <= touch ? out[idx - 1] : null;
      const next = idx < out.length - 1 && out[idx + 1].start - n.end <= touch ? out[idx + 1] : null;
      let target = null;
      if (prev && next) {
        target = Math.abs(prev.pitchRaw - n.pitchRaw) <= Math.abs(next.pitchRaw - n.pitchRaw) ? prev : next;
      } else target = prev || next;
      if (target) {
        if (target === prev) { target.end = n.end; target.src.end = n.src.end; target.src.b = n.src.b; }
        else { target.start = n.start; target.src.start = n.src.start; target.src.a = n.src.a; }
      }
      out.splice(idx, 1);
    }
    return out;
  }

  function pearson(a, b) {
    const n = a.length;
    let ma = 0, mb = 0;
    for (let i = 0; i < n; i++) { ma += a[i]; mb += b[i]; }
    ma /= n; mb /= n;
    let num = 0, da = 0, db = 0;
    for (let i = 0; i < n; i++) {
      num += (a[i] - ma) * (b[i] - mb);
      da += (a[i] - ma) ** 2;
      db += (b[i] - mb) ** 2;
    }
    return da && db ? num / Math.sqrt(da * db) : 0;
  }

  /** Krumhansl-Schmuckler key finding on duration-weighted pitch classes. */
  function detectKey(notes) {
    const hist = new Array(12).fill(0);
    for (const n of notes) hist[((n.pitch % 12) + 12) % 12] += n.end - n.start;
    if (!hist.some((v) => v > 0)) return null;
    let best = null;
    for (let tonic = 0; tonic < 12; tonic++) {
      for (const mode of ['major', 'minor']) {
        const prof = mode === 'major' ? KK_MAJOR : KK_MINOR;
        const rot = hist.map((_, i) => hist[(i + tonic) % 12]);
        const r = pearson(rot, prof);
        if (!best || r > best.score) best = { tonic, mode, score: r };
      }
    }
    best.name = NOTE_NAMES[best.tonic] + (best.mode === 'major' ? ' major' : ' minor');
    return best;
  }

  function inScale(pitch, tonic, scale) {
    return SCALES[scale].indexOf((((pitch - tonic) % 12) + 12) % 12) >= 0;
  }

  function gridSeconds(bpm, grid) {
    if (!grid || grid === 'off') return 0;
    const triplet = /t$/.test(grid);
    const div = parseInt(grid, 10);
    return (60 / bpm) * (4 / div) * (triplet ? 2 / 3 : 1);
  }

  /**
   * Applies scale snapping, transposition and quantization. Never mutates the
   * input notes.
   * opts: { snap: {tonic, scale} | null, transpose, bpm, grid, strength (0..1) }
   */
  function postProcess(notes, opts) {
    opts = opts || {};
    let out = notes.map((n) => Object.assign({}, n, { src: n.src }));

    if (opts.snap && opts.snap.scale && opts.snap.scale !== 'chromatic') {
      const { tonic, scale } = opts.snap;
      for (const n of out) {
        if (inScale(n.pitch, tonic, scale)) continue;
        const dir = n.pitchRaw >= n.pitch ? 1 : -1;
        let found = null;
        for (let d = 1; d <= 2 && found == null; d++) {
          if (inScale(n.pitch + dir * d, tonic, scale)) found = n.pitch + dir * d;
          else if (inScale(n.pitch - dir * d, tonic, scale)) found = n.pitch - dir * d;
        }
        if (found != null) n.pitch = found;
      }
    }

    const tr = opts.transpose | 0;
    if (tr) for (const n of out) n.pitch = Math.max(0, Math.min(127, n.pitch + tr));

    const g = opts.bpm ? gridSeconds(opts.bpm, opts.grid) : 0;
    const strength = opts.strength == null ? 1 : opts.strength;
    if (g > 0 && strength > 0) {
      for (const n of out) {
        const qs = Math.round(n.start / g) * g;
        let qe = Math.round(n.end / g) * g;
        if (qe <= qs) qe = qs + g;
        n.start = n.start + (qs - n.start) * strength;
        n.end = n.end + (qe - n.end) * strength;
      }
      out.sort((a, b) => a.start - b.start);
      // keep it monophonic: two notes landing on the same slot -> keep longer
      const kept = [];
      for (const n of out) {
        const last = kept[kept.length - 1];
        if (last && n.start <= last.start + 1e-6) {
          if (n.src.end - n.src.start > last.src.end - last.src.start) kept[kept.length - 1] = n;
          continue;
        }
        if (last && last.end > n.start) last.end = n.start;
        kept.push(n);
      }
      out = kept;
    }
    return out;
  }

  /**
   * Expressive pitch curve for a note (for MIDI pitch bend), in semitones
   * relative to the detected note, on the note's current timeline.
   */
  function bendCurve(note, prep) {
    if (!note.src || note.src.a == null) return [];
    const { midi, tuning, track } = prep;
    const shift = note.start - note.src.start;
    const pts = [];
    for (let k = note.src.a; k < note.src.b; k++) {
      if (isNaN(midi[k])) continue;
      const t = k * track.hopSec + shift;
      if (t < note.start - 1e-6 || t >= note.end) continue;
      pts.push({ t, v: midi[k] - tuning - note.detPitch });
    }
    return pts;
  }

  VM.NOTE_NAMES = NOTE_NAMES;
  VM.SCALES = SCALES;
  VM.TRANSCRIBE_DEFAULTS = DEFAULTS;
  VM.noteName = noteName;
  VM.hzToMidi = hzToMidi;
  VM.midiToHz = midiToHz;
  VM.estimateTuning = estimateTuning;
  VM.transcribe = transcribe;
  VM.detectKey = detectKey;
  VM.postProcess = postProcess;
  VM.gridSeconds = gridSeconds;
  VM.bendCurve = bendCurve;
  VM.inScale = inScale;
  VM.newNoteId = nextId;
})(typeof window !== 'undefined' ? window : globalThis);
