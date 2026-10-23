/*
 * MelodyLift UI: loading audio, the piano-roll editor, playback and export.
 */
(function () {
  'use strict';
  const VM = window.VM;
  const $ = (id) => document.getElementById(id);

  const ANALYSIS_SR = 16000;
  const PEAKS_PER_SEC = 200;
  const RULER_H = 24;
  const WAVE_H = 46;
  const TOP = RULER_H + WAVE_H;
  const KEY_W = 56;
  const RANGES = { auto: [65, 1100], low: [55, 700], high: [130, 1400] };
  const IN_ARTIFACT = !!(window.claude && typeof window.claude.use === 'function');
  const SETTINGS_KEY = 'melodylift.settings.v1';

  const DEFAULT_PARAMS = {
    range: 'auto',
    confMin: VM.TRANSCRIBE_DEFAULTS.confMin,
    gateDb: VM.TRANSCRIBE_DEFAULTS.gateDb,
    changeCost: VM.TRANSCRIBE_DEFAULTS.changeCost,
    minNoteMs: VM.TRANSCRIBE_DEFAULTS.minNoteMs,
    splitDb: VM.TRANSCRIBE_DEFAULTS.splitDb,
    tuningAuto: true,
    tuningCents: 0,
    keyTonic: 'auto',
    scale: 'major',
    snap: false,
    transpose: 0,
    bpm: 120,
    grid: '16',
    quantize: false,
    strength: 100,
    bends: false,
    bendRange: '2',
    vocalVol: 0.85,
    synthVol: 0.55,
    follow: true,
  };

  const state = {
    name: null,
    buffer: null, // original-rate AudioBuffer for playback
    mono: null, // 16 kHz analysis signal
    peaks: null,
    duration: 0,
    track: null,
    result: null,
    base: [],
    notes: [],
    selected: new Set(),
    undo: [],
    redo: [],
    edited: false,
    lastSettingsUndo: 0,
    view: { pps: 110, rowH: 14, sx: 0, sy: 0 },
    params: Object.assign({}, DEFAULT_PARAMS, loadSettings()),
    position: 0,
    isDemo: false,
  };

  // ------------------------------------------------------------ helpers

  function loadSettings() {
    try {
      const raw = localStorage.getItem(SETTINGS_KEY);
      return raw ? JSON.parse(raw) : {};
    } catch (e) {
      return {};
    }
  }
  function saveSettings() {
    try {
      localStorage.setItem(SETTINGS_KEY, JSON.stringify(state.params));
    } catch (e) { /* storage unavailable */ }
  }

  let toastTimer = 0;
  function toast(msg, ms) {
    const el = $('toast');
    el.textContent = msg;
    el.hidden = false;
    clearTimeout(toastTimer);
    toastTimer = setTimeout(() => (el.hidden = true), ms || 3600);
  }

  function fmtTime(t) {
    t = Math.max(0, t);
    const m = Math.floor(t / 60);
    const s = t - m * 60;
    return m + ':' + (s < 10 ? '0' : '') + s.toFixed(3);
  }
  function fmtBars(t) {
    const beat = 60 / state.params.bpm;
    const totalBeats = t / beat;
    const bar = Math.floor(totalBeats / 4) + 1;
    const b = Math.floor(totalBeats % 4) + 1;
    const sixteenth = Math.floor((totalBeats % 1) * 4) + 1;
    return bar + '.' + b + '.' + sixteenth;
  }
  const isBlack = (p) => [1, 3, 6, 8, 10].indexOf(((p % 12) + 12) % 12) >= 0;
  const clamp = (v, a, b) => Math.max(a, Math.min(b, v));

  function gridSec() {
    return VM.gridSeconds(state.params.bpm, state.params.grid);
  }

  // ------------------------------------------------------- audio engine

  let actx = null;
  let vocalGain, synthGain, synthBus;
  function audio() {
    if (!actx) {
      const Ctx = window.AudioContext || window.webkitAudioContext;
      actx = new Ctx();
      vocalGain = actx.createGain();
      synthGain = actx.createGain();
      synthBus = actx.createBiquadFilter();
      synthBus.type = 'lowpass';
      synthBus.frequency.value = 3200;
      synthBus.connect(synthGain);
      vocalGain.connect(actx.destination);
      synthGain.connect(actx.destination);
      applyVolumes();
    }
    return actx;
  }
  function applyVolumes() {
    if (!actx) return;
    vocalGain.gain.value = state.params.vocalVol;
    synthGain.gain.value = state.params.synthVol * 0.5;
  }

  const voices = new Set();
  function synthNote(pitch, when, dur, vel) {
    const ctx = audio();
    const f = VM.midiToHz(pitch);
    const g = ctx.createGain();
    const o1 = ctx.createOscillator();
    const o2 = ctx.createOscillator();
    o1.type = 'sawtooth';
    o2.type = 'triangle';
    o1.frequency.value = f;
    o2.frequency.value = f * 2;
    o1.detune.value = -4;
    const mix2 = ctx.createGain();
    mix2.gain.value = 0.35;
    o1.connect(g);
    o2.connect(mix2).connect(g);
    g.connect(synthBus);
    const peak = 0.12 + 0.2 * ((vel || 96) / 127);
    const end = when + Math.max(0.04, dur);
    g.gain.setValueAtTime(0, when);
    g.gain.linearRampToValueAtTime(peak, when + 0.012);
    g.gain.setTargetAtTime(peak * 0.7, when + 0.012, 0.12);
    g.gain.setValueAtTime(peak * 0.7, Math.max(when + 0.013, end - 0.001));
    g.gain.setTargetAtTime(0, end, 0.04);
    o1.start(when);
    o2.start(when);
    o1.stop(end + 0.3);
    o2.stop(end + 0.3);
    const v = { o1, o2, g };
    voices.add(v);
    o1.onended = () => {
      voices.delete(v);
      g.disconnect();
    };
  }
  function audition(pitch) {
    const ctx = audio();
    if (ctx.state === 'suspended') ctx.resume();
    synthNote(pitch, ctx.currentTime + 0.005, 0.22, 100);
  }

  const play = {
    on: false,
    src: null,
    ctxStart: 0,
    offset: 0,
    timer: 0,
    scheduledUntil: 0,
  };

  function currentTime() {
    if (!play.on) return state.position;
    return play.offset + (actx.currentTime - play.ctxStart);
  }

  function startPlayback() {
    if (!state.buffer && !state.notes.length) return;
    const ctx = audio();
    if (ctx.state === 'suspended') ctx.resume();
    const end = Math.max(state.duration, lastNoteEnd());
    if (state.position >= end - 0.05) state.position = 0;
    play.offset = state.position;
    play.ctxStart = ctx.currentTime + 0.05;
    if (state.buffer) {
      const src = ctx.createBufferSource();
      src.buffer = state.buffer;
      src.connect(vocalGain);
      src.start(play.ctxStart, Math.min(play.offset, state.buffer.duration));
      play.src = src;
    }
    play.on = true;
    play.scheduledUntil = play.offset;
    // notes already sounding at the start position
    for (const n of state.notes) {
      if (n.start < play.offset && n.end > play.offset + 0.03) {
        synthNote(n.pitch, play.ctxStart, n.end - play.offset, n.vel);
      }
    }
    scheduleAhead();
    play.timer = setInterval(scheduleAhead, 50);
    setPlayIcon(true);
    requestAnimationFrame(tick);
  }

  function scheduleAhead() {
    if (!play.on) return;
    const now = currentTime();
    const until = now + 0.35;
    for (const n of state.notes) {
      if (n.start >= play.scheduledUntil && n.start < until) {
        synthNote(n.pitch, play.ctxStart + (n.start - play.offset), n.end - n.start, n.vel);
      }
    }
    play.scheduledUntil = Math.max(play.scheduledUntil, until);
    if (now > Math.max(state.duration, lastNoteEnd()) + 0.3) stopPlayback(true);
  }

  function stopPlayback(toEnd) {
    if (!play.on) return;
    state.position = toEnd ? 0 : currentTime();
    play.on = false;
    clearInterval(play.timer);
    if (play.src) {
      try { play.src.stop(); } catch (e) { /* already stopped */ }
      play.src.disconnect();
      play.src = null;
    }
    const t = actx.currentTime;
    for (const v of voices) {
      v.g.gain.cancelScheduledValues(t);
      v.g.gain.setTargetAtTime(0, t, 0.015);
      try { v.o1.stop(t + 0.1); v.o2.stop(t + 0.1); } catch (e) { /* not started yet */ }
    }
    setPlayIcon(false);
    updateClock();
    draw();
  }

  function togglePlay() {
    if (play.on) stopPlayback(false);
    else startPlayback();
  }

  function seek(t) {
    const wasOn = play.on;
    if (wasOn) stopPlayback(false);
    state.position = Math.max(0, t);
    if (wasOn) startPlayback();
    updateClock();
    draw();
  }

  function setPlayIcon(on) {
    $('playIcon').innerHTML = on
      ? '<path d="M5 4h4v12H5zM11 4h4v12h-4z" fill="currentColor"/>'
      : '<path d="M6 4l10 6-10 6z" fill="currentColor"/>';
    $('playBtn').setAttribute('aria-label', on ? 'Pause' : 'Play');
  }

  function tick() {
    if (!play.on) return;
    const t = currentTime();
    if (state.params.follow) {
      const x = timeToX(t);
      if (x > W - 60 || x < KEY_W) {
        state.view.sx = Math.max(0, t * state.view.pps - (W - KEY_W) * 0.15);
      }
    }
    updateClock();
    draw();
    requestAnimationFrame(tick);
  }

  function updateClock() {
    const t = currentTime();
    $('clock').textContent = fmtTime(t);
    $('clockBars').textContent = fmtBars(t);
  }

  function lastNoteEnd() {
    let e = 0;
    for (const n of state.notes) e = Math.max(e, n.end);
    return e;
  }

  // ------------------------------------------------------- loading audio

  function decode(arrayBuffer) {
    const ctx = audio();
    return new Promise((resolve, reject) => {
      const p = ctx.decodeAudioData(arrayBuffer, resolve, reject);
      if (p && p.then) p.then(resolve, reject);
    });
  }

  async function toAnalysisRate(buf) {
    const len = Math.ceil(buf.duration * ANALYSIS_SR);
    try {
      const Off = window.OfflineAudioContext || window.webkitOfflineAudioContext;
      const off = new Off(1, len, ANALYSIS_SR);
      const src = off.createBufferSource();
      src.buffer = buf;
      src.connect(off.destination);
      src.start();
      const rendered = await off.startRendering();
      return new Float32Array(rendered.getChannelData(0));
    } catch (e) {
      return manualResample(buf, len);
    }
  }

  // Fallback for browsers that refuse a 16 kHz offline context.
  function manualResample(buf, len) {
    const n = buf.length;
    const mono = new Float32Array(n);
    for (let c = 0; c < buf.numberOfChannels; c++) {
      const d = buf.getChannelData(c);
      for (let i = 0; i < n; i++) mono[i] += d[i] / buf.numberOfChannels;
    }
    // anti-alias: two passes of a simple low-pass at ~7 kHz
    for (let pass = 0; pass < 2; pass++) {
      const a = Math.exp((-2 * Math.PI * 7000) / buf.sampleRate);
      let y = 0;
      for (let i = 0; i < n; i++) { y = (1 - a) * mono[i] + a * y; mono[i] = y; }
    }
    const out = new Float32Array(len);
    const ratio = buf.sampleRate / ANALYSIS_SR;
    for (let i = 0; i < len; i++) {
      const p = i * ratio;
      const k = Math.floor(p);
      const f = p - k;
      out[i] = (mono[k] || 0) * (1 - f) + (mono[k + 1] || 0) * f;
    }
    return out;
  }

  function computePeaks(x) {
    const block = Math.round(ANALYSIS_SR / PEAKS_PER_SEC);
    const n = Math.ceil(x.length / block);
    const peaks = new Float32Array(n);
    let top = 1e-6;
    for (let i = 0; i < n; i++) {
      let m = 0;
      const end = Math.min(x.length, (i + 1) * block);
      for (let j = i * block; j < end; j++) { const v = Math.abs(x[j]); if (v > m) m = v; }
      peaks[i] = m;
      if (m > top) top = m;
    }
    for (let i = 0; i < n; i++) peaks[i] /= top;
    return peaks;
  }

  async function loadFile(file) {
    if (!file) return;
    showProgress('Decoding ' + file.name + '…', 0);
    try {
      const buf = await decode(await file.arrayBuffer());
      await loadBuffer(buf, file.name, false);
    } catch (e) {
      hideProgress();
      toast('That file could not be decoded. Try WAV, MP3, M4A, FLAC or OGG.');
      console.error(e);
    }
  }

  async function loadBuffer(buf, name, isDemo) {
    stopPlayback(false);
    state.position = 0;
    state.buffer = buf;
    state.name = name;
    state.isDemo = isDemo;
    state.duration = buf.duration;
    showProgress('Preparing audio…', 0);
    const mono = await toAnalysisRate(buf);
    VM.highpass(mono, ANALYSIS_SR, 60);
    state.mono = mono;
    state.peaks = computePeaks(mono);
    state.undo = [];
    state.redo = [];
    state.edited = false;
    state.selected.clear();
    state.notes = [];
    updateFileInfo();
    await analyse();
    fitView();
    updateClock();
  }

  function updateFileInfo() {
    const el = $('fileInfo');
    el.textContent = '';
    if (!state.name) { el.textContent = 'No audio loaded'; return; }
    const s = document.createElement('strong');
    s.textContent = state.name;
    el.appendChild(s);
    el.appendChild(document.createTextNode(
      ' · ' + fmtTime(state.duration).replace(/\.\d+$/, '') +
      (state.isDemo ? ' · synthesized demo vocal' : '')));
  }

  let analyseToken = 0;
  async function analyse() {
    if (!state.mono) return;
    const token = ++analyseToken;
    const [fmin, fmax] = RANGES[state.params.range] || RANGES.auto;
    showProgress('Tracking pitch…', 0);
    const track = await VM.trackPitchAsync(state.mono, ANALYSIS_SR, { fmin, fmax }, (f) => {
      if (token === analyseToken) showProgress('Tracking pitch…', f);
    });
    if (token !== analyseToken) return;
    state.track = track;
    hideProgress();
    transcribe();
  }

  function showProgress(label, frac) {
    $('progress').hidden = false;
    $('progressLabel').textContent = label;
    $('progressFill').style.width = Math.round(frac * 100) + '%';
  }
  function hideProgress() {
    $('progress').hidden = true;
  }

  // ------------------------------------------------- transcription chain

  function transcribe() {
    if (!state.track) return;
    const p = state.params;
    const res = VM.transcribe(state.track, {
      confMin: p.confMin,
      gateDb: p.gateDb,
      changeCost: p.changeCost,
      minNoteMs: p.minNoteMs,
      splitDb: p.splitDb,
      tuningCents: p.tuningAuto ? null : p.tuningCents,
    });
    state.result = res;
    state.base = res.notes;
    if (p.tuningAuto) {
      p.tuningCents = Math.round(res.prep.tuning * 100);
      syncControl('tuningCents');
    }
    updateKeyOptions();
    postProcess();
  }

  function currentKey() {
    const p = state.params;
    if (p.keyTonic === 'auto') {
      const k = state.result && state.result.key;
      if (k) return { tonic: k.tonic, scale: k.mode === 'major' ? 'major' : 'minor', auto: true };
      return { tonic: 0, scale: 'major', auto: true };
    }
    return { tonic: +p.keyTonic, scale: p.scale, auto: false };
  }

  function postProcess() {
    if (!state.result) return;
    const p = state.params;
    const key = currentKey();
    const notes = VM.postProcess(state.base, {
      snap: p.snap ? { tonic: key.tonic, scale: key.scale } : null,
      transpose: p.transpose,
      bpm: p.bpm,
      grid: p.quantize ? p.grid : 'off',
      strength: p.strength / 100,
    });
    replaceNotes(notes, true);
  }

  function replaceNotes(notes, fromSettings) {
    const now = Date.now();
    if (fromSettings) {
      if (state.edited) toast('Settings changed, so your manual edits were replaced. Press Ctrl+Z to get them back.', 5000);
      if (state.notes.length && now - state.lastSettingsUndo > 700) pushUndo();
      state.lastSettingsUndo = now;
      state.edited = false;
    }
    state.notes = notes;
    const ids = new Set(notes.map((n) => n.id));
    for (const id of Array.from(state.selected)) if (!ids.has(id)) state.selected.delete(id);
    afterNotesChanged();
  }

  function afterNotesChanged() {
    const has = state.notes.length > 0;
    $('exportBtn').disabled = !has;
    $('exportBtn2').disabled = !has;
    $('dragMidi').classList.toggle('disabled', !has);
    updateStats();
    updateSelectionInfo();
    draw();
  }

  // ----------------------------------------------------------- undo/redo

  function snapshot() {
    return state.notes.map((n) => Object.assign({}, n));
  }
  function pushUndo() {
    state.undo.push(snapshot());
    if (state.undo.length > 200) state.undo.shift();
    state.redo = [];
  }
  function undo() {
    if (!state.undo.length) return;
    state.redo.push(snapshot());
    state.notes = state.undo.pop();
    state.edited = true;
    afterNotesChanged();
  }
  function redo() {
    if (!state.redo.length) return;
    state.undo.push(snapshot());
    state.notes = state.redo.pop();
    state.edited = true;
    afterNotesChanged();
  }
  function markEdited() {
    state.edited = true;
    state.notes.sort((a, b) => a.start - b.start);
    afterNotesChanged();
  }

  // ------------------------------------------------------ status display

  function updateStats() {
    const el = $('stats');
    if (!state.result) { el.textContent = '—'; return; }
    const n = state.notes;
    const parts = [n.length + (n.length === 1 ? ' note' : ' notes')];
    if (n.length) {
      let lo = 127, hi = 0;
      for (const x of n) { lo = Math.min(lo, x.pitch); hi = Math.max(hi, x.pitch); }
      parts.push(VM.noteName(lo) + '–' + VM.noteName(hi));
    }
    const k = currentKey();
    parts.push('key ' + VM.NOTE_NAMES[k.tonic] + ' ' + scaleLabel(k.scale) + (k.auto ? ' (detected)' : ''));
    const c = Math.round(state.result.prep.tuning * 100);
    parts.push('A4 ' + (440 * Math.pow(2, c / 1200)).toFixed(1) + ' Hz');
    el.textContent = parts.join(' · ');
  }

  function scaleLabel(s) {
    const o = $('scale').querySelector('option[value="' + s + '"]');
    return o ? o.textContent.toLowerCase() : s;
  }

  function updateKeyOptions() {
    const sel = $('keyTonic');
    const k = state.result && state.result.key;
    const autoLabel = 'Auto' + (k ? ' (' + k.name + ')' : '');
    if (!sel.options.length) {
      const o = document.createElement('option');
      o.value = 'auto';
      sel.appendChild(o);
      VM.NOTE_NAMES.forEach((nm, i) => {
        const opt = document.createElement('option');
        opt.value = String(i);
        opt.textContent = nm;
        sel.appendChild(opt);
      });
    }
    sel.options[0].textContent = autoLabel;
    sel.value = String(state.params.keyTonic);
    $('scale').disabled = state.params.keyTonic === 'auto';
  }

  function updateSelectionInfo() {
    const sel = state.notes.filter((n) => state.selected.has(n.id));
    const box = $('selectionInfo');
    if (!sel.length) { box.hidden = true; return; }
    box.hidden = false;
    if (sel.length === 1) {
      const n = sel[0];
      $('selText').textContent = VM.noteName(n.pitch) + ' · ' + fmtTime(n.start) + ' (' + fmtBars(n.start) + ') · ' +
        Math.round((n.end - n.start) * 1000) + ' ms';
      $('selVel').value = n.vel;
    } else {
      $('selText').textContent = sel.length + ' notes selected';
      $('selVel').value = Math.round(sel.reduce((s, n) => s + n.vel, 0) / sel.length);
    }
  }

  // --------------------------------------------------------- piano roll

  const canvas = $('roll');
  const g2 = canvas.getContext('2d');
  let W = 0, H = 0, dpr = 1;
  let colors = {};

  function readColors() {
    const cs = getComputedStyle(document.documentElement);
    const get = (n) => cs.getPropertyValue(n).trim();
    colors = {
      bg: get('--roll-bg'), black: get('--roll-black'), line: get('--roll-line'), c: get('--roll-c'),
      bar: get('--roll-bar'), wave: get('--wave'), keyWhite: get('--key-white'), keyBlack: get('--key-black'),
      keyText: get('--key-text'), note: get('--accent'), noteText: get('--note-text'), curve: get('--curve'),
      playhead: get('--playhead'), panel: get('--panel'), fg: get('--fg'), muted: get('--muted'), lineUi: get('--line'),
      mono: get('--font-mono') || 'monospace',
    };
  }

  function resize() {
    const r = canvas.parentElement.getBoundingClientRect();
    dpr = window.devicePixelRatio || 1;
    W = Math.max(100, r.width);
    H = Math.max(100, r.height);
    canvas.width = Math.round(W * dpr);
    canvas.height = Math.round(H * dpr);
    clampView();
    draw();
  }

  const timeToX = (t) => KEY_W + t * state.view.pps - state.view.sx;
  const xToTime = (x) => (x - KEY_W + state.view.sx) / state.view.pps;
  const pitchToY = (p) => TOP + (127 - p) * state.view.rowH - state.view.sy;
  const yToPitch = (y) => 127 - Math.floor((y - TOP + state.view.sy) / state.view.rowH);

  function contentEnd() {
    return Math.max(state.duration, lastNoteEnd(), 8);
  }

  function clampView() {
    const v = state.view;
    v.pps = clamp(v.pps, 4, 1600);
    v.rowH = clamp(v.rowH, 6, 40);
    const maxY = Math.max(0, 128 * v.rowH - (H - TOP));
    v.sy = clamp(v.sy, 0, maxY);
    const maxX = Math.max(0, contentEnd() * v.pps + 120 - (W - KEY_W));
    v.sx = clamp(v.sx, 0, maxX);
  }

  function fitView() {
    const v = state.view;
    const notes = state.notes;
    const dur = contentEnd();
    v.pps = dur <= 40 ? (W - KEY_W - 30) / dur : 90;
    if (notes.length) {
      let lo = 127, hi = 0;
      for (const n of notes) { lo = Math.min(lo, n.pitch); hi = Math.max(hi, n.pitch); }
      const span = hi - lo + 7;
      v.rowH = clamp((H - TOP - 10) / span, 8, 22);
      const mid = (lo + hi) / 2;
      v.sy = (127 - mid) * v.rowH - (H - TOP) / 2;
    } else {
      v.sy = (127 - 66) * v.rowH - (H - TOP) / 2;
    }
    v.sx = 0;
    clampView();
    draw();
  }

  function zoomAt(factor, x) {
    const v = state.view;
    const t = xToTime(x == null ? KEY_W + (W - KEY_W) / 2 : x);
    v.pps *= factor;
    v.pps = clamp(v.pps, 4, 1600);
    v.sx = t * v.pps - ((x == null ? KEY_W + (W - KEY_W) / 2 : x) - KEY_W);
    clampView();
    draw();
  }

  let drawQueued = false;
  function draw() {
    if (drawQueued) return;
    drawQueued = true;
    requestAnimationFrame(() => {
      drawQueued = false;
      paint();
    });
  }

  function paint() {
    const v = state.view;
    const c = g2;
    c.setTransform(dpr, 0, 0, dpr, 0, 0);
    c.fillStyle = colors.bg;
    c.fillRect(0, 0, W, H);

    // --- note grid area
    c.save();
    c.beginPath();
    c.rect(KEY_W, TOP, W - KEY_W, H - TOP);
    c.clip();

    const pTop = clamp(yToPitch(TOP), 0, 127);
    const pBot = clamp(yToPitch(H), 0, 127);
    for (let p = pTop; p >= pBot; p--) {
      const y = pitchToY(p);
      if (isBlack(p)) {
        c.fillStyle = colors.black;
        c.fillRect(KEY_W, y, W - KEY_W, v.rowH);
      }
      c.fillStyle = p % 12 === 0 ? colors.c : colors.line;
      c.fillRect(KEY_W, Math.round(y + v.rowH) - 0.5, W - KEY_W, 1);
    }

    const t0 = xToTime(KEY_W);
    const t1 = xToTime(W);
    drawBeatLines(c, t0, t1, TOP, H);

    // pitch curve (what was actually sung)
    const res = state.result;
    if (res) {
      const { midi, tuning } = res.prep;
      const hop = state.track.hopSec;
      const tr = state.params.transpose;
      const i0 = Math.max(0, Math.floor(t0 / hop) - 1);
      const i1 = Math.min(midi.length - 1, Math.ceil(t1 / hop) + 1);
      c.strokeStyle = colors.curve;
      c.globalAlpha = 0.85;
      c.lineWidth = 1.5;
      c.lineJoin = 'round';
      c.beginPath();
      let pen = false;
      for (let i = i0; i <= i1; i++) {
        const m = midi[i];
        if (isNaN(m)) { pen = false; continue; }
        const x = timeToX(i * hop);
        const y = pitchToY(m - tuning + tr) + v.rowH / 2;
        if (pen) c.lineTo(x, y); else c.moveTo(x, y);
        pen = true;
      }
      c.stroke();
      c.globalAlpha = 1;
    }

    // notes
    c.font = '500 ' + Math.min(11, v.rowH - 3) + 'px ' + colors.mono;
    c.textBaseline = 'middle';
    for (const n of state.notes) {
      if (n.end < t0 || n.start > t1) continue;
      const x0 = timeToX(n.start);
      const x1 = timeToX(n.end);
      const y = pitchToY(n.pitch);
      if (y > H || y + v.rowH < TOP) continue;
      const sel = state.selected.has(n.id);
      const w = Math.max(2, x1 - x0 - 1);
      c.globalAlpha = sel ? 1 : 0.8 + 0.2 * (n.vel / 127);
      c.fillStyle = colors.note;
      roundRect(c, x0, y + 1, w, v.rowH - 2, Math.min(3, v.rowH / 4));
      c.fill();
      c.globalAlpha = 1;
      if (sel) {
        c.strokeStyle = colors.fg;
        c.lineWidth = 2;
        roundRect(c, x0, y + 1, w, v.rowH - 2, Math.min(3, v.rowH / 4));
        c.stroke();
      }
      if (w > 30 && v.rowH >= 11) {
        c.fillStyle = colors.noteText;
        c.fillText(VM.noteName(n.pitch), x0 + 4, y + v.rowH / 2 + 0.5);
      }
    }
    c.restore();

    // --- waveform lane
    c.fillStyle = colors.panel;
    c.fillRect(KEY_W, RULER_H, W - KEY_W, WAVE_H);
    if (state.peaks) {
      c.fillStyle = colors.wave;
      const mid = RULER_H + WAVE_H / 2;
      const half = WAVE_H / 2 - 4;
      for (let x = KEY_W; x < W; x++) {
        const a = xToTime(x) * PEAKS_PER_SEC;
        const b = xToTime(x + 1) * PEAKS_PER_SEC;
        let m = 0;
        for (let i = Math.max(0, Math.floor(a)); i < Math.min(state.peaks.length, Math.max(Math.floor(a) + 1, Math.floor(b))); i++) {
          if (state.peaks[i] > m) m = state.peaks[i];
        }
        if (m > 0.004) c.fillRect(x, mid - m * half, 1, Math.max(1, 2 * m * half));
      }
    }
    c.fillStyle = colors.lineUi;
    c.fillRect(0, TOP - 1, W, 1);

    // --- ruler
    c.fillStyle = colors.panel;
    c.fillRect(KEY_W, 0, W - KEY_W, RULER_H);
    drawRuler(c, t0, t1);
    c.fillStyle = colors.lineUi;
    c.fillRect(0, RULER_H - 1, W, 1);

    // --- playhead
    const ph = timeToX(currentTime());
    if (ph >= KEY_W && ph <= W) {
      c.fillStyle = colors.playhead;
      c.fillRect(Math.round(ph) - 0.5, 0, 1.5, H);
      c.beginPath();
      c.moveTo(ph - 5, 0);
      c.lineTo(ph + 5, 0);
      c.lineTo(ph, 7);
      c.fill();
    }

    // --- keyboard
    c.save();
    c.beginPath();
    c.rect(0, TOP, KEY_W, H - TOP);
    c.clip();
    c.fillStyle = colors.keyWhite;
    c.fillRect(0, TOP, KEY_W, H - TOP);
    c.font = '500 ' + Math.min(10, v.rowH - 2) + 'px ' + colors.mono;
    for (let p = pTop; p >= pBot; p--) {
      const y = pitchToY(p);
      if (isBlack(p)) {
        c.fillStyle = colors.keyBlack;
        c.fillRect(0, y, KEY_W * 0.62, v.rowH);
      } else if (p % 12 === 0 || p % 12 === 5) {
        c.fillStyle = colors.c;
        c.fillRect(0, Math.round(y + v.rowH) - 0.5, KEY_W, 1);
      }
      if (p % 12 === 0 && v.rowH >= 7) {
        c.fillStyle = colors.keyText;
        c.textAlign = 'right';
        c.fillText(VM.noteName(p), KEY_W - 5, y + v.rowH / 2 + 0.5);
        c.textAlign = 'left';
      }
    }
    if (hoverPitch != null) {
      c.fillStyle = colors.note;
      c.globalAlpha = 0.35;
      c.fillRect(0, pitchToY(hoverPitch), KEY_W, v.rowH);
      c.globalAlpha = 1;
    }
    c.restore();
    c.fillStyle = colors.lineUi;
    c.fillRect(KEY_W - 1, TOP, 1, H - TOP);

    // corner
    c.fillStyle = colors.panel;
    c.fillRect(0, 0, KEY_W - 1, TOP - 1);
    c.fillStyle = colors.muted;
    c.font = '500 10px ' + colors.mono;
    c.textBaseline = 'middle';
    c.fillText(state.params.bpm + ' bpm', 6, RULER_H / 2);
    c.fillText('audio', 6, RULER_H + WAVE_H / 2);
  }

  function roundRect(c, x, y, w, h, r) {
    c.beginPath();
    c.moveTo(x + r, y);
    c.lineTo(x + w - r, y);
    c.quadraticCurveTo(x + w, y, x + w, y + r);
    c.lineTo(x + w, y + h - r);
    c.quadraticCurveTo(x + w, y + h, x + w - r, y + h);
    c.lineTo(x + r, y + h);
    c.quadraticCurveTo(x, y + h, x, y + h - r);
    c.lineTo(x, y + r);
    c.quadraticCurveTo(x, y, x + r, y);
    c.closePath();
  }

  function drawBeatLines(c, t0, t1, yTop, yBot) {
    const beat = 60 / state.params.bpm;
    const pps = state.view.pps;
    const g = gridSec();
    if (g > 0 && g * pps >= 7) {
      c.fillStyle = colors.line;
      for (let k = Math.floor(t0 / g); k * g <= t1; k++) {
        c.fillRect(Math.round(timeToX(k * g)) - 0.5, yTop, 1, yBot - yTop);
      }
    }
    if (beat * pps >= 5) {
      for (let k = Math.max(0, Math.floor(t0 / beat)); k * beat <= t1; k++) {
        c.fillStyle = k % 4 === 0 ? colors.bar : colors.c;
        c.fillRect(Math.round(timeToX(k * beat)) - 0.5, yTop, 1, yBot - yTop);
      }
    } else {
      for (let k = Math.max(0, Math.floor(t0 / (beat * 4))); k * beat * 4 <= t1; k++) {
        c.fillStyle = colors.bar;
        c.fillRect(Math.round(timeToX(k * beat * 4)) - 0.5, yTop, 1, yBot - yTop);
      }
    }
  }

  function drawRuler(c, t0, t1) {
    const beat = 60 / state.params.bpm;
    const barPx = beat * 4 * state.view.pps;
    let every = 1;
    while (barPx * every < 44) every *= 2;
    c.font = '500 10px ' + colors.mono;
    c.textBaseline = 'middle';
    c.fillStyle = colors.muted;
    for (let k = Math.max(0, Math.floor(t0 / (beat * 4))); k * beat * 4 <= t1; k++) {
      const x = timeToX(k * beat * 4);
      if (k % every === 0) {
        c.fillRect(Math.round(x) - 0.5, RULER_H - 9, 1, 9);
        c.fillText(String(k + 1), x + 4, RULER_H / 2 - 1);
      } else {
        c.fillRect(Math.round(x) - 0.5, RULER_H - 4, 1, 4);
      }
    }
  }

  // ------------------------------------------------------- interaction

  let drag = null;
  let hoverPitch = null;

  function hitNote(x, y) {
    if (x < KEY_W || y < TOP) return null;
    const p = yToPitch(y);
    const t = xToTime(x);
    const slack = 3 / state.view.pps;
    for (let i = state.notes.length - 1; i >= 0; i--) {
      const n = state.notes[i];
      if (n.pitch === p && t >= n.start - slack && t <= n.end + slack) {
        const edge = timeToX(n.end) - x < 7 && timeToX(n.end) - timeToX(n.start) > 10;
        return { note: n, edge };
      }
    }
    return null;
  }

  function localPos(e) {
    const r = canvas.getBoundingClientRect();
    return { x: e.clientX - r.left, y: e.clientY - r.top };
  }

  function snapTime(t, e) {
    const g = gridSec();
    if (!g || (e && e.altKey)) return t;
    return Math.round(t / g) * g;
  }

  canvas.addEventListener('pointerdown', (e) => {
    canvas.focus();
    const { x, y } = localPos(e);
    canvas.setPointerCapture(e.pointerId);

    if (y < TOP && x >= KEY_W) {
      drag = { mode: 'seek' };
      seek(Math.max(0, xToTime(x)));
      return;
    }
    if (x < KEY_W && y >= TOP) {
      audition(yToPitch(y));
      drag = { mode: 'keys', last: yToPitch(y) };
      return;
    }
    const hit = hitNote(x, y);
    if (hit) {
      const n = hit.note;
      if (e.shiftKey) {
        if (state.selected.has(n.id)) state.selected.delete(n.id); else state.selected.add(n.id);
      } else if (!state.selected.has(n.id)) {
        state.selected.clear();
        state.selected.add(n.id);
      }
      const sel = state.notes.filter((m) => state.selected.has(m.id));
      drag = {
        mode: hit.edge ? 'resize' : 'move',
        x0: x, y0: y,
        anchor: n,
        orig: sel.map((m) => ({ n: m, start: m.start, end: m.end, pitch: m.pitch })),
        moved: false,
        lastPitch: n.pitch,
      };
      if (!hit.edge) audition(n.pitch);
      updateSelectionInfo();
      draw();
      return;
    }
    if (!e.shiftKey) state.selected.clear();
    drag = { mode: 'pan', x0: x, y0: y, sx: state.view.sx, sy: state.view.sy };
    updateSelectionInfo();
    draw();
  });

  canvas.addEventListener('pointermove', (e) => {
    const { x, y } = localPos(e);
    if (!drag) {
      const hit = hitNote(x, y);
      canvas.style.cursor = y < TOP && x >= KEY_W ? 'text' : hit ? (hit.edge ? 'ew-resize' : 'grab') : x < KEY_W ? 'pointer' : 'default';
      const hp = x < KEY_W && y >= TOP ? yToPitch(y) : null;
      if (hp !== hoverPitch) { hoverPitch = hp; draw(); }
      return;
    }
    if (drag.mode === 'seek') { seek(Math.max(0, xToTime(x))); return; }
    if (drag.mode === 'keys') {
      const p = yToPitch(y);
      if (p !== drag.last) { drag.last = p; audition(p); }
      return;
    }
    if (drag.mode === 'pan') {
      state.view.sx = drag.sx - (x - drag.x0);
      state.view.sy = drag.sy - (y - drag.y0);
      clampView();
      draw();
      return;
    }
    const dx = x - drag.x0;
    const dy = y - drag.y0;
    if (!drag.moved && Math.abs(dx) < 3 && Math.abs(dy) < 3) return;
    if (!drag.moved) { pushUndo(); drag.moved = true; }
    const dt = dx / state.view.pps;
    const a = drag.orig.find((o) => o.n === drag.anchor);
    if (drag.mode === 'move') {
      canvas.style.cursor = 'grabbing';
      const dp = Math.round(-dy / state.view.rowH);
      const newStart = Math.max(0, snapTime(a.start + dt, e));
      const shift = newStart - a.start;
      const minStart = Math.min.apply(null, drag.orig.map((o) => o.start));
      const sh = Math.max(shift, -minStart);
      for (const o of drag.orig) {
        o.n.start = o.start + sh;
        o.n.end = o.end + sh;
        o.n.pitch = clamp(o.pitch + dp, 0, 127);
      }
      if (drag.anchor.pitch !== drag.lastPitch) {
        drag.lastPitch = drag.anchor.pitch;
        audition(drag.anchor.pitch);
      }
    } else {
      const g = gridSec();
      const minLen = Math.min(0.03, g || 0.03);
      const newEnd = Math.max(a.start + minLen, snapTime(a.end + dt, e));
      const delta = newEnd - a.end;
      for (const o of drag.orig) o.n.end = Math.max(o.n.start + minLen, o.end + delta);
    }
    state.edited = true;
    updateSelectionInfo();
    draw();
  });

  function endDrag() {
    if (drag && drag.moved) markEdited();
    drag = null;
  }
  canvas.addEventListener('pointerup', endDrag);
  canvas.addEventListener('pointercancel', endDrag);
  canvas.addEventListener('pointerleave', () => {
    if (hoverPitch != null) { hoverPitch = null; draw(); }
  });

  canvas.addEventListener('dblclick', (e) => {
    const { x, y } = localPos(e);
    if (x < KEY_W || y < TOP) return;
    const hit = hitNote(x, y);
    pushUndo();
    if (hit) {
      state.notes = state.notes.filter((n) => n !== hit.note);
      state.selected.delete(hit.note.id);
    } else {
      const g = gridSec() || 0.25;
      const t = Math.max(0, gridSec() && !e.altKey ? Math.floor(xToTime(x) / g) * g : xToTime(x));
      const p = yToPitch(y);
      const n = { id: VM.newNoteId(), pitch: p, detPitch: p, pitchRaw: p, start: t, end: t + Math.max(g, 0.1), vel: 96, src: null };
      state.notes.push(n);
      state.selected.clear();
      state.selected.add(n.id);
      audition(p);
    }
    markEdited();
  });

  canvas.addEventListener('wheel', (e) => {
    e.preventDefault();
    const { x } = localPos(e);
    const unit = e.deltaMode === 1 ? 16 : e.deltaMode === 2 ? H : 1;
    const dx = e.deltaX * unit;
    const dy = e.deltaY * unit;
    if (e.ctrlKey || e.metaKey) {
      zoomAt(Math.exp(-dy * 0.0025), x);
      return;
    }
    if (e.altKey) {
      const v = state.view;
      const centerPitch = 127 - (v.sy + (H - TOP) / 2) / v.rowH;
      v.rowH *= Math.exp(-dy * 0.002);
      v.rowH = clamp(v.rowH, 6, 40);
      v.sy = (127 - centerPitch) * v.rowH - (H - TOP) / 2;
    } else if (e.shiftKey) {
      state.view.sx += dy || dx;
    } else {
      state.view.sx += dx;
      state.view.sy += dy;
    }
    clampView();
    draw();
  }, { passive: false });

  function selectedNotes() {
    return state.notes.filter((n) => state.selected.has(n.id));
  }

  document.addEventListener('keydown', (e) => {
    const tag = (e.target && e.target.tagName) || '';
    if (/INPUT|SELECT|TEXTAREA/.test(tag) && e.target.type !== 'range' && e.target.type !== 'checkbox') return;
    const mod = e.ctrlKey || e.metaKey;
    if (e.code === 'Space') { e.preventDefault(); togglePlay(); return; }
    if (mod && e.key.toLowerCase() === 'z') { e.preventDefault(); if (e.shiftKey) redo(); else undo(); return; }
    if (mod && e.key.toLowerCase() === 'y') { e.preventDefault(); redo(); return; }
    if (mod && e.key.toLowerCase() === 'a') {
      e.preventDefault();
      state.notes.forEach((n) => state.selected.add(n.id));
      updateSelectionInfo();
      draw();
      return;
    }
    if (e.key === 'Home') { seek(0); state.view.sx = 0; clampView(); draw(); return; }
    if (e.key === '+' || e.key === '=') { zoomAt(1.25); return; }
    if (e.key === '-' || e.key === '_') { zoomAt(0.8); return; }
    if (e.key === 'Escape') { state.selected.clear(); updateSelectionInfo(); draw(); return; }
    const sel = selectedNotes();
    if (!sel.length) return;
    if (e.key === 'Delete' || e.key === 'Backspace') {
      e.preventDefault();
      pushUndo();
      state.notes = state.notes.filter((n) => !state.selected.has(n.id));
      state.selected.clear();
      markEdited();
    } else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
      e.preventDefault();
      pushUndo();
      const d = (e.key === 'ArrowUp' ? 1 : -1) * (e.shiftKey ? 12 : 1);
      for (const n of sel) n.pitch = clamp(n.pitch + d, 0, 127);
      if (sel.length === 1) audition(sel[0].pitch);
      markEdited();
    } else if (e.key === 'ArrowLeft' || e.key === 'ArrowRight') {
      e.preventDefault();
      pushUndo();
      const step = (gridSec() || 0.01) * (e.key === 'ArrowRight' ? 1 : -1);
      const minStart = Math.min.apply(null, sel.map((n) => n.start));
      const s = Math.max(step, -minStart);
      for (const n of sel) { n.start += s; n.end += s; }
      markEdited();
    }
  });

  // ----------------------------------------------------------- controls

  const controls = Array.from(document.querySelectorAll('[data-param]'));

  function fmtParam(name, v) {
    switch (name) {
      case 'confMin': return Math.round(v * 100) + '%';
      case 'gateDb': return v + ' dB';
      case 'changeCost': return (+v).toFixed(1);
      case 'minNoteMs': return v + ' ms';
      case 'splitDb': return +v === 0 ? 'off' : v + ' dB';
      case 'tuningCents': return (v > 0 ? '+' : '') + v + ' ct';
      case 'transpose': return (v > 0 ? '+' : '') + v + ' st';
      case 'strength': return v + '%';
      default: return String(v);
    }
  }

  function syncControl(name) {
    const el = document.querySelector('[data-param="' + name + '"]');
    if (!el) return;
    const v = state.params[name];
    if (el.type === 'checkbox') el.checked = !!v;
    else el.value = v;
    const out = $(name + 'Out');
    if (out) out.textContent = fmtParam(name, v);
    if (name === 'tuningCents' || name === 'tuningAuto') {
      $('tuningCents').disabled = !!state.params.tuningAuto;
    }
    if (name === 'quantize' || name === 'grid') {
      $('strength').closest('.field').classList.toggle('disabled', !state.params.quantize);
    }
  }

  function readControl(el) {
    if (el.type === 'checkbox') return el.checked;
    if (el.type === 'range' || el.type === 'number') {
      const v = parseFloat(el.value);
      return isNaN(v) ? state.params[el.dataset.param] : v;
    }
    return el.value;
  }

  function applyLevel(level) {
    switch (level) {
      case 'pitch': analyse(); break;
      case 'notes': transcribe(); break;
      case 'post': postProcess(); break;
      case 'audio': applyVolumes(); break;
      case 'view': draw(); break;
      default: break;
    }
  }

  for (const el of controls) {
    const name = el.dataset.param;
    syncControl(name);
    const evt = el.type === 'range' ? 'input' : 'change';
    el.addEventListener(evt, () => {
      let v = readControl(el);
      if (name === 'bpm') v = clamp(v, 30, 300);
      state.params[name] = v;
      syncControl(name);
      if (name === 'keyTonic') $('scale').disabled = v === 'auto';
      saveSettings();
      applyLevel(el.dataset.level);
      if (name === 'bpm' || name === 'grid') { updateClock(); updateSelectionInfo(); draw(); }
    });
  }

  document.querySelectorAll('[data-transpose]').forEach((b) => {
    b.addEventListener('click', () => {
      state.params.transpose = clamp(state.params.transpose + +b.dataset.transpose, -24, 24);
      syncControl('transpose');
      saveSettings();
      postProcess();
    });
  });

  $('selVel').addEventListener('change', () => {
    const v = clamp(parseInt($('selVel').value, 10) || 96, 1, 127);
    pushUndo();
    for (const n of selectedNotes()) n.vel = v;
    markEdited();
  });

  let taps = [];
  $('tapBtn').addEventListener('click', () => {
    const now = performance.now();
    taps = taps.filter((t) => now - t < 2500);
    taps.push(now);
    if (taps.length >= 2) {
      const iv = [];
      for (let i = 1; i < taps.length; i++) iv.push(taps[i] - taps[i - 1]);
      const avg = iv.slice(-6).reduce((a, b) => a + b, 0) / Math.min(6, iv.length);
      state.params.bpm = Math.round((60000 / avg) * 10) / 10;
      syncControl('bpm');
      saveSettings();
      postProcess();
      updateClock();
    }
  });

  $('playBtn').addEventListener('click', togglePlay);
  $('toStartBtn').addEventListener('click', () => { seek(0); state.view.sx = 0; clampView(); draw(); });
  $('zoomInBtn').addEventListener('click', () => zoomAt(1.25));
  $('zoomOutBtn').addEventListener('click', () => zoomAt(0.8));
  $('zoomFitBtn').addEventListener('click', fitView);

  $('fileInput').addEventListener('change', (e) => {
    loadFile(e.target.files[0]);
    e.target.value = '';
  });
  $('demoBtn').addEventListener('click', loadDemo);

  // drag & drop
  const wrap = $('rollWrap');
  let dragDepth = 0;
  document.addEventListener('dragenter', (e) => {
    if (!e.dataTransfer || Array.from(e.dataTransfer.types || []).indexOf('Files') < 0) return;
    dragDepth++;
    $('dropHint').hidden = false;
  });
  document.addEventListener('dragleave', () => {
    dragDepth = Math.max(0, dragDepth - 1);
    if (!dragDepth) $('dropHint').hidden = true;
  });
  document.addEventListener('dragover', (e) => {
    if (e.dataTransfer && Array.from(e.dataTransfer.types || []).indexOf('Files') >= 0) e.preventDefault();
  });
  document.addEventListener('drop', (e) => {
    dragDepth = 0;
    $('dropHint').hidden = true;
    const f = e.dataTransfer && e.dataTransfer.files && e.dataTransfer.files[0];
    if (f) {
      e.preventDefault();
      loadFile(f);
    }
  });

  // ------------------------------------------------------------- record

  let recorder = null;
  let recTimer = 0;
  async function toggleRecord() {
    if (recorder) { recorder.stop(); return; }
    if (!navigator.mediaDevices || !window.MediaRecorder) {
      toast('Recording is not supported in this browser. Record your vocal elsewhere and open the file.');
      return;
    }
    let stream;
    try {
      stream = await navigator.mediaDevices.getUserMedia({
        audio: { echoCancellation: false, noiseSuppression: true, autoGainControl: false },
      });
    } catch (e) {
      toast('Microphone access was blocked. Allow it in your browser settings, or open a recorded file instead.');
      return;
    }
    stopPlayback(false);
    const chunks = [];
    recorder = new MediaRecorder(stream);
    recorder.ondataavailable = (ev) => ev.data.size && chunks.push(ev.data);
    recorder.onstop = async () => {
      stream.getTracks().forEach((t) => t.stop());
      clearInterval(recTimer);
      recorder = null;
      $('recBtn').classList.remove('recording');
      $('recLabel').textContent = 'Record';
      const blob = new Blob(chunks, { type: chunks[0] ? chunks[0].type : 'audio/webm' });
      try {
        const buf = await decode(await blob.arrayBuffer());
        await loadBuffer(buf, 'Recording ' + new Date().toLocaleTimeString(), false);
      } catch (e) {
        toast('The recording could not be decoded.');
      }
    };
    recorder.start();
    const t0 = Date.now();
    $('recBtn').classList.add('recording');
    $('recLabel').textContent = 'Stop 0:00';
    recTimer = setInterval(() => {
      const s = Math.floor((Date.now() - t0) / 1000);
      $('recLabel').textContent = 'Stop ' + Math.floor(s / 60) + ':' + String(s % 60).padStart(2, '0');
    }, 250);
  }
  $('recBtn').addEventListener('click', toggleRecord);

  // ------------------------------------------------------------- export

  function exportName() {
    const base = (state.name || 'vocal').replace(/\.[^.]+$/, '').replace(/[\\/:*?"<>|]+/g, '_').trim() || 'vocal';
    return base + ' - melody';
  }

  function midiBytes() {
    const notes = state.notes.slice().sort((a, b) => a.start - b.start);
    const p = state.params;
    let bends = null;
    const range = +p.bendRange || 2;
    if (p.bends && state.result) {
      bends = VM.buildBendEvents(notes, (n) => VM.bendCurve(n, state.result.prep), range);
    }
    return VM.writeMidi({ notes, bpm: p.bpm, ppq: 480, name: exportName(), bends, bendRange: range });
  }

  let downloadsPromise = null;
  function downloadsCap() {
    if (!IN_ARTIFACT) return Promise.resolve(null);
    if (!downloadsPromise) downloadsPromise = window.claude.use('downloads').catch(() => null);
    return downloadsPromise;
  }

  async function exportMidi() {
    if (!state.notes.length) return;
    const bytes = midiBytes();
    const name = exportName() + '.mid';
    const dl = await downloadsCap();
    if (dl) {
      try {
        await dl.save({ filename: exportName() + '.zip', data: makeZip([{ name, data: bytes }]) });
        toast('Saved. Unzip it and drag the .mid file into your DAW.');
      } catch (e) {
        if (e && e.code !== 'declined') toast('The file could not be saved here (' + (e.code || 'error') + ').');
      }
      return;
    }
    if (IN_ARTIFACT) {
      toast('Saving files is not available in this view.');
      return;
    }
    const url = URL.createObjectURL(new Blob([bytes], { type: 'audio/midi' }));
    const a = document.createElement('a');
    a.href = url;
    a.download = name;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 4000);
    toast('Exported ' + name);
  }
  $('exportBtn').addEventListener('click', exportMidi);
  $('exportBtn2').addEventListener('click', exportMidi);

  // Chrome lets a page drag a generated file straight into a DAW.
  let dragUrl = null;
  $('dragMidi').addEventListener('dragstart', (e) => {
    if (!state.notes.length) { e.preventDefault(); return; }
    if (dragUrl) URL.revokeObjectURL(dragUrl);
    dragUrl = URL.createObjectURL(new Blob([midiBytes()], { type: 'audio/midi' }));
    const name = exportName() + '.mid';
    e.dataTransfer.effectAllowed = 'copy';
    e.dataTransfer.setData('DownloadURL', 'audio/midi:' + name + ':' + dragUrl);
    e.dataTransfer.setData('text/uri-list', dragUrl);
  });

  // Minimal store-only ZIP writer (for hosts that only accept .zip saves).
  function crc32(bytes) {
    let c, crc = 0xffffffff;
    for (let i = 0; i < bytes.length; i++) {
      c = (crc ^ bytes[i]) & 0xff;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crc = (crc >>> 8) ^ c;
    }
    return (crc ^ 0xffffffff) >>> 0;
  }
  function makeZip(files) {
    const enc = new TextEncoder();
    const parts = [];
    const central = [];
    let offset = 0;
    for (const f of files) {
      const nameBytes = enc.encode(f.name);
      const crc = crc32(f.data);
      const local = new DataView(new ArrayBuffer(30));
      local.setUint32(0, 0x04034b50, true);
      local.setUint16(4, 20, true);
      local.setUint16(6, 0x0800, true);
      local.setUint32(14, crc, true);
      local.setUint32(18, f.data.length, true);
      local.setUint32(22, f.data.length, true);
      local.setUint16(26, nameBytes.length, true);
      parts.push(new Uint8Array(local.buffer), nameBytes, f.data);
      const cen = new DataView(new ArrayBuffer(46));
      cen.setUint32(0, 0x02014b50, true);
      cen.setUint16(4, 20, true);
      cen.setUint16(6, 20, true);
      cen.setUint16(8, 0x0800, true);
      cen.setUint32(16, crc, true);
      cen.setUint32(20, f.data.length, true);
      cen.setUint32(24, f.data.length, true);
      cen.setUint16(28, nameBytes.length, true);
      cen.setUint32(42, offset, true);
      central.push(new Uint8Array(cen.buffer), nameBytes);
      offset += 30 + nameBytes.length + f.data.length;
    }
    const cenSize = central.reduce((s, a) => s + a.length, 0);
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true);
    end.setUint16(8, files.length, true);
    end.setUint16(10, files.length, true);
    end.setUint32(12, cenSize, true);
    end.setUint32(16, offset, true);
    return new Blob(parts.concat(central, [new Uint8Array(end.buffer)]), { type: 'application/zip' });
  }

  // --------------------------------------------------------------- demo

  // A hummed "Ode to Joy" with vibrato, scoops and breath noise, so the
  // editor opens with something real to look at.
  function demoBuffer() {
    const ctx = audio();
    const sr = ctx.sampleRate;
    const bpm = 96;
    const beat = 60 / bpm;
    const E = 64, F = 65, G = 67, D = 62, C = 60;
    const phrase1 = [[E, 1], [E, 1], [F, 1], [G, 1], [G, 1], [F, 1], [E, 1], [D, 1], [C, 1], [C, 1], [D, 1], [E, 1]];
    const tune = phrase1.concat([[E, 1.5], [D, 0.5], [D, 2]], phrase1, [[D, 1.5], [C, 0.5], [C, 2]]);
    const lead = beat * 4;
    const total = Math.ceil((lead + tune.reduce((s, n) => s + n[1], 0) * beat + 1) * sr);
    const x = new Float32Array(total);
    let t = lead;
    let seed = 7;
    const rnd = () => ((seed = (seed * 16807) % 2147483647) / 2147483647) * 2 - 1;
    // vowel "ah" formants
    const formants = [[730, 90], [1090, 110], [2440, 160]];
    const gainAt = (f) => formants.reduce((s, [fc, bw], i) => s + (1 / (i + 1)) / (1 + ((f - fc) / bw) ** 2), 0.04);
    for (const [pitch, beats] of tune) {
      const dur = beats * beat * 0.92;
      const a = Math.round(t * sr);
      const len = Math.round(dur * sr);
      let phase = 0;
      const f0 = VM.midiToHz(pitch + 0.08);
      for (let i = 0; i < len; i++) {
        const tt = i / sr;
        const vib = 0.35 * Math.sin(2 * Math.PI * 5.3 * tt) * Math.min(1, Math.max(0, (tt - 0.15) / 0.3));
        const scoop = -0.7 * Math.exp(-tt / 0.045);
        const f = f0 * Math.pow(2, (vib + scoop) / 12);
        phase += (2 * Math.PI * f) / sr;
        const env = Math.min(1, tt / 0.03) * Math.min(1, (dur - tt) / 0.05);
        let v = 0;
        for (let h = 1; h <= 20 && f * h < 5000; h++) v += gainAt(f * h) * Math.sin(h * phase);
        x[a + i] += env * (v * 0.22 + rnd() * 0.004);
      }
      t += beats * beat;
    }
    const buf = ctx.createBuffer(1, total, sr);
    buf.getChannelData(0).set(x);
    return { buf, bpm };
  }

  async function loadDemo() {
    const { buf, bpm } = demoBuffer();
    state.params.bpm = bpm;
    syncControl('bpm');
    await loadBuffer(buf, 'Demo – Ode to Joy (hummed)', true);
  }

  // --------------------------------------------------------------- boot

  function boot() {
    readColors();
    updateKeyOptions();
    if (IN_ARTIFACT) {
      $('recBtn').hidden = true;
      $('dragMidi').hidden = true;
      $('exportNote').hidden = false;
    }
    if (!('draggable' in document.createElement('span')) || /Mobi|Android/i.test(navigator.userAgent)) {
      $('dragMidi').hidden = true;
    }
    new ResizeObserver(resize).observe(canvas.parentElement);
    resize();
    const mq = window.matchMedia('(prefers-color-scheme: dark)');
    const onTheme = () => { readColors(); draw(); };
    if (mq.addEventListener) mq.addEventListener('change', onTheme);
    new MutationObserver(onTheme).observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });
    if (document.fonts && document.fonts.ready) document.fonts.ready.then(() => draw());
    afterNotesChanged();
    loadDemo().catch((e) => {
      hideProgress();
      console.error(e);
    });
  }

  boot();
})();
