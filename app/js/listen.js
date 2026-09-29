// Talking to the pet with your voice. In the iPhone app the phone's own speech recognizer
// does the listening (on the phone itself when it can, see ios/Peekabyte/Listen.swift);
// in a browser that has one, the browser's speech recognition does.

import { call, isNative, on } from './native.js';

const WebRec = window.SpeechRecognition || window.webkitSpeechRecognition;

export class Listener {
  constructor() {
    this.kind = null;          // 'native' | 'web' | null
    this.why = '';             // why listening isn't available
    this.active = false;
    this.session = null;
    this.ready = this.detect();
    if (isNative) {
      on('mic.partial', (d) => this.session?.partial(d.text || ''));
      on('mic.level', (d) => this.session?.level(d.level || 0));
      on('mic.final', (d) => this.session?.finish(d.text || ''));
      on('mic.error', (d) => this.session?.fail(d.message || 'The microphone stopped'));
    }
  }

  async detect() {
    if (isNative) {
      try {
        const st = await call('mic.state');
        this.kind = st.available ? 'native' : null;
        this.onDevice = !!st.onDevice;
        this.permission = st.permission;
        if (!st.available) this.why = st.why || "Speech recognition isn't available on this iPhone.";
      } catch {
        this.why = 'Reinstall the latest iPhone app to talk with your voice.';
      }
    } else if (WebRec) {
      this.kind = 'web';
    } else {
      this.why = "This browser can't listen. Type instead, or use the iPhone app.";
    }
    return this.kind;
  }

  get available() { return !!this.kind; }

  // Listen for one thing said. Resolves with the words (empty if nothing was said).
  listen({ hints = [], onPartial, onLevel, silenceMs = 1300, waitMs = 8000, maxMs = 20000 } = {}) {
    if (this.active) this.cancel();
    this.active = true;
    return new Promise((resolve, reject) => {
      let done = false;
      const session = {
        text: '',
        partial: (t) => { session.text = t; onPartial?.(t); },
        level: (l) => onLevel?.(l),
        finish: (t) => {
          if (done) return;
          done = true;
          this.active = false;
          this.session = null;
          resolve((t || session.text || '').trim());
        },
        fail: (message) => {
          if (done) return;
          done = true;
          this.active = false;
          this.session = null;
          reject(new Error(message));
        },
      };
      this.session = session;
      if (this.kind === 'native') {
        call('mic.start', { hints: hints.filter(Boolean).slice(0, 50), silenceMs, waitMs, maxMs })
          .catch((e) => session.fail(e.message));
      } else if (this.kind === 'web') {
        this.listenWeb(session, { silenceMs, waitMs, maxMs, onLevel });
      } else {
        session.fail(this.why || "Can't listen here");
      }
    });
  }

  listenWeb(session, { silenceMs, waitMs, maxMs, onLevel }) {
    const rec = new WebRec();
    rec.lang = navigator.language?.startsWith('en') ? navigator.language : 'en-US';
    rec.interimResults = true;
    rec.continuous = true;
    rec.maxAlternatives = 1;
    let quiet = null;
    const endSoon = (ms) => { clearTimeout(quiet); quiet = setTimeout(() => { try { rec.stop(); } catch { /* ended */ } }, ms); };
    const cap = setTimeout(() => { try { rec.stop(); } catch { /* ended */ } }, maxMs);
    endSoon(waitMs);
    rec.onresult = (e) => {
      let text = '';
      for (let i = 0; i < e.results.length; i++) text += e.results[i][0].transcript;
      session.partial(text);
      onLevel?.(0.5 + Math.random() * 0.3);
      endSoon(silenceMs);
    };
    rec.onerror = (e) => {
      if (e.error === 'no-speech' || e.error === 'aborted') return;
      clearTimeout(cap);
      clearTimeout(quiet);
      session.fail(e.error === 'not-allowed' || e.error === 'service-not-allowed'
        ? 'Microphone access is turned off for this page' : `Couldn't listen (${e.error})`);
    };
    rec.onend = () => { clearTimeout(cap); clearTimeout(quiet); onLevel?.(0); session.finish(session.text); };
    this.webRec = rec;
    try { rec.start(); } catch (e) { session.fail(e.message); }
  }

  // Stop listening now and use what was heard so far.
  stop() {
    if (!this.active) return;
    if (this.kind === 'native') call('mic.stop').catch(() => {});
    else try { this.webRec?.stop(); } catch { /* ended */ }
  }

  // Stop and throw away what was heard.
  cancel() {
    const s = this.session;
    if (this.kind === 'native') call('mic.cancel').catch(() => {});
    else try { this.webRec?.abort(); } catch { /* ended */ }
    s?.finish('');
    this.active = false;
  }

  // Done with the microphone for a while: lets the phone hand its audio back to music etc.
  release() {
    if (this.kind === 'native') call('mic.release').catch(() => {});
  }

  openSettings() {
    if (isNative) call('app.openSettings').catch(() => {});
  }
}
