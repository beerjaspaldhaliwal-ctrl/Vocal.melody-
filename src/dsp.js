/*
 * DSP core: FFT, filtering and YIN pitch tracking.
 * Plain script (no modules) so the app also runs straight from file://.
 * Everything is exported on the global `VM` namespace.
 */
(function (root) {
  'use strict';
  const VM = (root.VM = root.VM || {});

  // ---------------------------------------------------------------- FFT

  function makeFFT(n) {
    const levels = Math.round(Math.log2(n));
    if (1 << levels !== n) throw new Error('FFT size must be a power of two');
    const cos = new Float64Array(n / 2);
    const sin = new Float64Array(n / 2);
    for (let i = 0; i < n / 2; i++) {
      cos[i] = Math.cos((2 * Math.PI * i) / n);
      sin[i] = Math.sin((2 * Math.PI * i) / n);
    }
    const rev = new Uint32Array(n);
    for (let i = 0; i < n; i++) {
      let r = 0;
      for (let b = 0; b < levels; b++) r = (r << 1) | ((i >>> b) & 1);
      rev[i] = r;
    }

    // In-place forward transform, X[k] = sum x[j] e^{-2πijk/n}.
    function forward(re, im) {
      for (let i = 0; i < n; i++) {
        const j = rev[i];
        if (j > i) {
          let t = re[i]; re[i] = re[j]; re[j] = t;
          t = im[i]; im[i] = im[j]; im[j] = t;
        }
      }
      for (let size = 2; size <= n; size <<= 1) {
        const half = size >> 1;
        const step = n / size;
        for (let i = 0; i < n; i += size) {
          for (let j = i, k = 0; j < i + half; j++, k += step) {
            const l = j + half;
            const tre = re[l] * cos[k] + im[l] * sin[k];
            const tim = im[l] * cos[k] - re[l] * sin[k];
            re[l] = re[j] - tre;
            im[l] = im[j] - tim;
            re[j] += tre;
            im[j] += tim;
          }
        }
      }
    }

    return { n, forward };
  }

  // ------------------------------------------------------------ filters

  // RBJ biquad high-pass, applied in place. Removes rumble / DC that would
  // otherwise bias the YIN difference function.
  function highpass(x, sr, freq, q) {
    q = q || 0.707;
    const w0 = (2 * Math.PI * freq) / sr;
    const alpha = Math.sin(w0) / (2 * q);
    const cw = Math.cos(w0);
    const a0 = 1 + alpha;
    const b0 = (1 + cw) / 2 / a0;
    const b1 = -(1 + cw) / a0;
    const b2 = (1 + cw) / 2 / a0;
    const a1 = (-2 * cw) / a0;
    const a2 = (1 - alpha) / a0;
    let x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    for (let i = 0; i < x.length; i++) {
      const x0 = x[i];
      const y0 = b0 * x0 + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
      x2 = x1; x1 = x0; y2 = y1; y1 = y0;
      x[i] = y0;
    }
    return x;
  }

  // -------------------------------------------------------------- YIN

  function nextPow2(v) {
    let p = 1;
    while (p < v) p <<= 1;
    return p;
  }

  /**
   * Creates an incremental YIN pitch tracker so long files can be analysed
   * in slices without freezing the UI.
   *
   * Returns { frames, step(count) -> boolean done, result() }.
   * result(): { sr, hop, hopSec, times, f0, conf, rmsDb } where f0 = 0 means
   * no periodicity was found, and conf is 1 - aperiodicity (0..1).
   */
  function createPitchTracker(signal, sr, opts) {
    opts = opts || {};
    const fmin = opts.fmin || 65;
    const fmax = opts.fmax || 1100;
    const hop = opts.hop || Math.round(sr * 0.01);
    const threshold = opts.threshold || 0.15;

    const tauMin = Math.max(2, Math.floor(sr / fmax));
    const tauMax = Math.ceil(sr / fmin);
    const W = Math.max(2 * tauMax, 256);
    const N = W + tauMax + 2;
    const M = nextPow2(N);
    const fft = makeFFT(M);

    // Pad so frame i is centred on sample i * hop.
    const padL = W >> 1;
    const total = signal.length + padL + N;
    const x = new Float32Array(total);
    x.set(signal, padL);

    // Prefix sum of squares for the energy terms of the difference function.
    const sq = new Float64Array(total + 1);
    for (let i = 0; i < total; i++) sq[i + 1] = sq[i] + x[i] * x[i];

    const frames = Math.max(1, Math.ceil(signal.length / hop));
    const f0 = new Float32Array(frames);
    const conf = new Float32Array(frames);
    const rmsDb = new Float32Array(frames);
    const envDb = new Float32Array(frames); // short-window loudness for onsets
    const times = new Float32Array(frames);

    const re = new Float64Array(M);
    const im = new Float64Array(M);
    const cr = new Float64Array(M);
    const ci = new Float64Array(M);
    const d = new Float64Array(tauMax + 2);
    const dn = new Float64Array(tauMax + 2);

    let cursor = 0;

    function analyseFrame(fi) {
      const s = fi * hop;
      times[fi] = (fi * hop) / sr;

      const e1 = sq[s + W] - sq[s];
      const rms = Math.sqrt(e1 / W);
      rmsDb[fi] = rms > 1e-9 ? 20 * Math.log10(rms) : -180;
      const c0 = s + padL - (hop >> 1);
      const es = (sq[c0 + hop] - sq[c0]) / hop;
      envDb[fi] = es > 1e-18 ? 10 * Math.log10(es) : -180;
      if (rms < 1e-6) {
        f0[fi] = 0;
        conf[fi] = 0;
        return;
      }

      // r(tau) = sum_{j<W} x[s+j] x[s+j+tau] via one complex FFT holding both
      // the window (real part) and the extended segment (imag part).
      for (let j = 0; j < M; j++) {
        re[j] = j < W ? x[s + j] : 0;
        im[j] = j < N ? x[s + j] : 0;
      }
      fft.forward(re, im);
      for (let k = 0; k < M; k++) {
        const m = (M - k) & (M - 1);
        const zr = re[k], zi = im[k], mr = re[m], mi = im[m];
        const ar = (zr + mr) * 0.5, ai = (zi - mi) * 0.5;
        const br = (zi + mi) * 0.5, bi = (mr - zr) * 0.5;
        // conj(A) * B, then conjugated again for the inverse-by-forward trick
        cr[k] = ar * br + ai * bi;
        ci[k] = -(ar * bi - ai * br);
      }
      fft.forward(cr, ci);
      // cr[tau] / M is now the cross-correlation r(tau)

      d[0] = 0;
      dn[0] = 1;
      let running = 0;
      for (let tau = 1; tau <= tauMax + 1; tau++) {
        const e2 = sq[s + tau + W] - sq[s + tau];
        const v = Math.max(0, e1 + e2 - (2 * cr[tau]) / M);
        d[tau] = v;
        running += v;
        dn[tau] = running > 0 ? (v * tau) / running : 1;
      }

      // Absolute threshold: first dip below threshold, walked down to its
      // local minimum. Fall back to the global minimum (low confidence).
      let best = -1;
      for (let tau = tauMin; tau <= tauMax; tau++) {
        if (dn[tau] < threshold) {
          while (tau + 1 <= tauMax && dn[tau + 1] < dn[tau]) tau++;
          best = tau;
          break;
        }
      }
      if (best < 0) {
        let mv = Infinity;
        for (let tau = tauMin; tau <= tauMax; tau++) {
          if (dn[tau] < mv) { mv = dn[tau]; best = tau; }
        }
      }

      let tauRefined = best;
      if (best > 1 && best < tauMax + 1) {
        const y0 = dn[best - 1], y1 = dn[best], y2 = dn[best + 1];
        const den = y0 - 2 * y1 + y2;
        if (Math.abs(den) > 1e-12) {
          const off = (0.5 * (y0 - y2)) / den;
          if (Math.abs(off) < 1) tauRefined = best + off;
        }
      }
      const c = Math.max(0, Math.min(1, 1 - dn[best]));
      f0[fi] = tauRefined > 0 ? sr / tauRefined : 0;
      conf[fi] = c;
    }

    return {
      frames,
      step(count) {
        const end = Math.min(frames, cursor + count);
        for (; cursor < end; cursor++) analyseFrame(cursor);
        return cursor >= frames;
      },
      progress() {
        return cursor / frames;
      },
      result() {
        return { sr, hop, hopSec: hop / sr, times, f0, conf, rmsDb, envDb, fmin, fmax };
      },
    };
  }

  /** Runs the tracker in slices, yielding to the event loop between them. */
  function trackPitchAsync(signal, sr, opts, onProgress) {
    const tracker = createPitchTracker(signal, sr, opts);
    return new Promise((resolve) => {
      function tick() {
        const t0 = Date.now();
        let done = false;
        while (!done && Date.now() - t0 < 30) done = tracker.step(200);
        if (onProgress) onProgress(tracker.progress());
        if (done) resolve(tracker.result());
        else setTimeout(tick, 0);
      }
      tick();
    });
  }

  function trackPitch(signal, sr, opts) {
    const tracker = createPitchTracker(signal, sr, opts);
    tracker.step(Infinity);
    return tracker.result();
  }

  VM.makeFFT = makeFFT;
  VM.highpass = highpass;
  VM.createPitchTracker = createPitchTracker;
  VM.trackPitch = trackPitch;
  VM.trackPitchAsync = trackPitchAsync;
})(typeof window !== 'undefined' ? window : globalThis);
