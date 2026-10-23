/*
 * Standard MIDI File (type 0) writer.
 */
(function (root) {
  'use strict';
  const VM = (root.VM = root.VM || {});

  function vlq(n) {
    n = Math.max(0, Math.round(n));
    const bytes = [n & 0x7f];
    n >>= 7;
    while (n > 0) {
      bytes.unshift((n & 0x7f) | 0x80);
      n >>= 7;
    }
    return bytes;
  }

  function u32(n) {
    return [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255];
  }

  function textBytes(str) {
    const out = [];
    for (const ch of unescape(encodeURIComponent(str))) out.push(ch.charCodeAt(0));
    return out;
  }

  /**
   * opts: {
   *   notes: [{pitch, start, end, vel}], bpm, ppq = 480, name, channel = 0,
   *   program = null (GM program number, optional),
   *   bends: [{t, v}] semitone offsets (optional), bendRange = 2
   * }
   * Returns a Uint8Array.
   */
  function writeMidi(opts) {
    const ppq = opts.ppq || 480;
    const bpm = opts.bpm || 120;
    const ch = (opts.channel || 0) & 15;
    const toTick = (sec) => Math.max(0, Math.round((sec * bpm * ppq) / 60));

    // order within a tick: meta/setup < note off < bend < note on
    const ev = [];
    const usPerQuarter = Math.round(60000000 / bpm);
    const name = textBytes(opts.name || 'Vocal Melody');
    ev.push({ tick: 0, ord: 0, data: [0xff, 0x03, ...vlq(name.length), ...name] });
    ev.push({ tick: 0, ord: 0, data: [0xff, 0x51, 0x03, (usPerQuarter >> 16) & 255, (usPerQuarter >> 8) & 255, usPerQuarter & 255] });
    ev.push({ tick: 0, ord: 0, data: [0xff, 0x58, 0x04, 4, 2, 24, 8] });
    if (opts.program != null) ev.push({ tick: 0, ord: 0, data: [0xc0 | ch, opts.program & 127] });

    if (opts.bends && opts.bends.length) {
      const range = Math.max(1, Math.min(24, Math.round(opts.bendRange || 2)));
      // RPN 0 = pitch bend sensitivity, then null RPN
      for (const [cc, v] of [[101, 0], [100, 0], [6, range], [38, 0], [101, 127], [100, 127]]) {
        ev.push({ tick: 0, ord: 0, data: [0xb0 | ch, cc, v] });
      }
      for (const b of opts.bends) {
        const val = Math.max(0, Math.min(16383, Math.round(8192 + (b.v / range) * 8191)));
        ev.push({ tick: toTick(b.t), ord: 2, data: [0xe0 | ch, val & 127, (val >> 7) & 127] });
      }
    }

    for (const n of opts.notes) {
      const a = toTick(n.start);
      const b = Math.max(a + 1, toTick(n.end));
      const p = Math.max(0, Math.min(127, n.pitch | 0));
      const v = Math.max(1, Math.min(127, n.vel | 0 || 96));
      ev.push({ tick: a, ord: 3, data: [0x90 | ch, p, v] });
      ev.push({ tick: b, ord: 1, data: [0x80 | ch, p, 0] });
    }

    ev.sort((x, y) => x.tick - y.tick || x.ord - y.ord);

    const track = [];
    let last = 0;
    for (const e of ev) {
      track.push(...vlq(e.tick - last), ...e.data);
      last = e.tick;
    }
    track.push(0, 0xff, 0x2f, 0x00);

    const header = [0x4d, 0x54, 0x68, 0x64, ...u32(6), 0, 0, 0, 1, (ppq >> 8) & 255, ppq & 255];
    const trk = [0x4d, 0x54, 0x72, 0x6b, ...u32(track.length)];
    const out = new Uint8Array(header.length + trk.length + track.length);
    out.set(header, 0);
    out.set(trk, header.length);
    out.set(track, header.length + trk.length);
    return out;
  }

  /**
   * Thins a dense bend curve: emit only meaningful changes, reset to centre
   * after each note so the next note starts clean.
   */
  function buildBendEvents(notes, curveFor, range) {
    const out = [];
    for (const n of notes) {
      const pts = curveFor(n);
      if (!pts.length) continue;
      let lastV = null;
      let lastT = -1;
      for (const p of pts) {
        const v = Math.max(-range, Math.min(range, p.v));
        if (lastV === null || Math.abs(v - lastV) >= 0.03 || p.t - lastT >= 0.08) {
          out.push({ t: Math.max(n.start, p.t), v });
          lastV = v;
          lastT = p.t;
        }
      }
      out.push({ t: n.end, v: 0 });
    }
    return out;
  }

  VM.writeMidi = writeMidi;
  VM.buildBendEvents = buildBendEvents;
})(typeof window !== 'undefined' ? window : globalThis);
