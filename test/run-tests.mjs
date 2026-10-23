// Node test-suite: synthesises sung melodies and checks the transcription.
import assert from 'node:assert/strict';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const here = path.dirname(fileURLToPath(import.meta.url));
for (const f of ['dsp.js', 'notes.js', 'midi.js']) await import(path.join(here, '..', 'src', f));
const VM = globalThis.VM;

const SR = 16000;

// Vowel-like tone: harmonics with a formant-ish rolloff, vibrato, attack/release.
function synth(melody, { vibrato = 0.3, detuneCents = 0, noise = 0.003, seed = 1 } = {}) {
  const total = Math.ceil(Math.max(...melody.map((n) => n.start + n.dur)) * SR + SR * 0.3);
  const x = new Float32Array(total);
  let s = seed;
  const rnd = () => ((s = (s * 16807) % 2147483647) / 2147483647) * 2 - 1;
  for (const n of melody) {
    const f0 = VM.midiToHz(n.pitch + detuneCents / 100);
    const a = Math.round(n.start * SR), len = Math.round(n.dur * SR);
    let phase = 0;
    for (let i = 0; i < len; i++) {
      const t = i / SR;
      const vib = vibrato * Math.sin(2 * Math.PI * 5.5 * t) * Math.min(1, t / 0.25);
      const scoop = n.scoop ? n.scoop * Math.exp(-t / 0.04) : 0;
      const f = f0 * Math.pow(2, (vib + scoop) / 12);
      phase += (2 * Math.PI * f) / SR;
      const env = Math.min(1, t / 0.02) * Math.min(1, (n.dur - t) / 0.03) * (n.amp || 0.5);
      let v = 0;
      for (let h = 1; h <= 12; h++) if (f * h < SR / 2) v += Math.sin(h * phase) / Math.pow(h, 1.1);
      x[a + i] += env * v * 0.4;
    }
  }
  for (let i = 0; i < total; i++) x[i] += noise * rnd();
  return x;
}

function run(melody, params, synthOpts) {
  const x = synth(melody, synthOpts);
  VM.highpass(x, SR, 60);
  const track = VM.trackPitch(x, SR, { fmin: 65, fmax: 1100 });
  return VM.transcribe(track, params);
}

function checkMelody(melody, res, tol = 0.06) {
  const got = res.notes.map((n) => [n.pitch, +n.start.toFixed(3), +(n.end - n.start).toFixed(3)]);
  assert.equal(res.notes.length, melody.length, 'note count: ' + JSON.stringify(got));
  melody.forEach((m, i) => {
    const n = res.notes[i];
    assert.equal(n.pitch, m.pitch, `note ${i} pitch: ` + JSON.stringify(got));
    assert.ok(Math.abs(n.start - m.start) < tol, `note ${i} start ${n.start} vs ${m.start}`);
    assert.ok(Math.abs(n.end - (m.start + m.dur)) < tol * 1.5, `note ${i} end ${n.end} vs ${m.start + m.dur}`);
  });
}

const tests = [];
const test = (name, fn) => tests.push({ name, fn });

test('simple melody with vibrato', () => {
  const mel = [60, 62, 64, 65, 67, 65, 64, 62].map((p, i) => ({ pitch: p, start: 0.2 + i * 0.4, dur: 0.33 }));
  checkMelody(mel, run(mel));
});

test('legato line (no gaps), scoops into notes', () => {
  const mel = [57, 60, 64, 62, 59, 57].map((p, i) => ({ pitch: p, start: 0.1 + i * 0.35, dur: 0.35, scoop: -1 }));
  checkMelody(mel, run(mel));
});

test('low male range and high female range', () => {
  const low = [43, 45, 47, 48].map((p, i) => ({ pitch: p, start: 0.2 + i * 0.45, dur: 0.4 }));
  checkMelody(low, run(low));
  const high = [79, 81, 83, 84].map((p, i) => ({ pitch: p, start: 0.2 + i * 0.45, dur: 0.4 }));
  checkMelody(high, run(high));
});

test('long low notes and noisy input stay single notes', () => {
  const mel = [40, 43, 41].map((p, i) => ({ pitch: p, start: 0.2 + i * 1.1, dur: 1.0 }));
  checkMelody(mel, run(mel, {}, { noise: 0.02, vibrato: 0.5 }), 0.08);
});

test('repeated same-pitch syllables split by energy dips', () => {
  const mel = [0, 1, 2, 3].map((i) => ({ pitch: 64, start: 0.2 + i * 0.3, dur: 0.3 }));
  // make syllables touch: amplitude envelopes fade in/out create dips
  const res = run(mel);
  assert.equal(res.notes.length, 4, JSON.stringify(res.notes.map((n) => [n.pitch, n.start, n.end])));
  assert.ok(res.notes.every((n) => n.pitch === 64));
});

test('auto tuning compensates a 35-cent sharp singer', () => {
  const mel = [60, 64, 67, 72].map((p, i) => ({ pitch: p, start: 0.2 + i * 0.4, dur: 0.35 }));
  const res = run(mel, {}, { detuneCents: 35 });
  assert.ok(Math.abs(res.prep.tuning - 0.35) < 0.06, 'tuning ' + res.prep.tuning);
  checkMelody(mel, res);
});

test('key detection', () => {
  const mel = [57, 59, 60, 62, 64, 65, 67, 69, 64, 60, 57].map((p, i) => ({ pitch: p, start: 0.2 + i * 0.3, dur: 0.25 }));
  const res = run(mel);
  assert.ok(['A minor', 'C major'].includes(res.key.name), res.key.name);
});

test('post-processing: transpose, scale snap, quantize', () => {
  const notes = [
    { id: 1, pitch: 61, detPitch: 61, pitchRaw: 61.3, start: 0.03, end: 0.48, vel: 90, src: { start: 0.03, end: 0.48 } },
    { id: 2, pitch: 64, detPitch: 64, pitchRaw: 64, start: 0.52, end: 0.97, vel: 90, src: { start: 0.52, end: 0.97 } },
  ];
  const out = VM.postProcess(notes, { snap: { tonic: 0, scale: 'major' }, transpose: 12, bpm: 120, grid: '8', strength: 1 });
  assert.deepEqual(out.map((n) => n.pitch), [74, 76]);
  assert.deepEqual(out.map((n) => [n.start, n.end]), [[0, 0.5], [0.5, 1]]);
  assert.equal(notes[0].pitch, 61, 'input untouched');
  assert.ok(Math.abs(VM.gridSeconds(120, '8t') - 1 / 6) < 1e-9);
});

test('MIDI writer produces a valid file', () => {
  const bytes = VM.writeMidi({
    notes: [{ pitch: 60, start: 0, end: 0.5, vel: 100 }, { pitch: 62, start: 0.5, end: 1, vel: 80 }],
    bpm: 120, ppq: 480, name: 'Test',
    bends: [{ t: 0, v: 0.5 }, { t: 0.5, v: 0 }], bendRange: 2,
  });
  const s = (a, b) => String.fromCharCode(...bytes.slice(a, b));
  assert.equal(s(0, 4), 'MThd');
  assert.equal(s(14, 18), 'MTrk');
  const len = (bytes[18] << 24) | (bytes[19] << 16) | (bytes[20] << 8) | bytes[21];
  assert.equal(len, bytes.length - 22);
  assert.deepEqual([...bytes.slice(-4)], [0, 0xff, 0x2f, 0]);
  // parse events back
  let i = 22, tick = 0, status = 0;
  const notesOn = [];
  while (i < bytes.length) {
    let d = 0, b;
    do { b = bytes[i++]; d = (d << 7) | (b & 127); } while (b & 128);
    tick += d;
    status = bytes[i++];
    if (status === 0xff) { i++; let l = 0; do { b = bytes[i++]; l = (l << 7) | (b & 127); } while (b & 128); i += l; }
    else if ((status & 0xf0) === 0xc0) i += 1;
    else { if ((status & 0xf0) === 0x90) notesOn.push([tick, bytes[i]]); i += 2; }
  }
  assert.deepEqual(notesOn, [[0, 60], [480, 62]]);
});

test('silence produces no notes', () => {
  const track = VM.trackPitch(new Float32Array(SR), SR, {});
  assert.equal(VM.transcribe(track).notes.length, 0);
});

test('performance: 60 s of audio', () => {
  const mel = Array.from({ length: 150 }, (_, i) => ({ pitch: 55 + ((i * 7) % 15), start: 0.1 + i * 0.4, dur: 0.35 }));
  const x = synth(mel);
  const t0 = Date.now();
  const track = VM.trackPitch(x, SR, {});
  const res = VM.transcribe(track);
  const ms = Date.now() - t0;
  console.log(`    (${(x.length / SR).toFixed(0)} s analysed in ${ms} ms, ${res.notes.length} notes)`);
  assert.ok(ms < 8000);
  assert.equal(res.notes.length, mel.length);
});

let failed = 0;
for (const t of tests) {
  try { t.fn(); console.log('ok   ' + t.name); }
  catch (e) { failed++; console.log('FAIL ' + t.name + '\n     ' + e.message); }
}
console.log(`\n${tests.length - failed}/${tests.length} passed`);
process.exit(failed ? 1 : 0);
