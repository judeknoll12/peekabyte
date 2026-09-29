// Keeps the phone's screen on while the pet is connected. In a browser on an iPhone, the page
// (and with it the Bluetooth link) is suspended as soon as the screen locks, so a pet you're
// just watching would lose its phone after 30 seconds. The iPhone app keeps Bluetooth up
// while locked, so there it's only a preference.

import { call, isNative } from './native.js';

let lock = null;
let wanted = false;

export const awake = { supported: isNative || 'wakeLock' in navigator, active: false, error: '', native: isNative };

export async function keepAwake(on) {
  wanted = on;
  if (isNative) {
    try {
      await call('app.keepAwake', { on });
      awake.active = on;
      awake.error = '';
    } catch (e) {
      awake.error = String(e?.message || e);
    }
    return;
  }
  if (on) return grab();
  const l = lock;
  lock = null;
  awake.active = false;
  try { await l?.release(); } catch { /* already released */ }
}

async function grab() {
  if (!wanted || !awake.supported || document.hidden || (lock && !lock.released)) return;
  try {
    const l = await navigator.wakeLock.request('screen');
    if (!wanted) { l.release().catch(() => {}); return; }
    lock = l;
    awake.active = true;
    awake.error = '';
    l.addEventListener('release', () => { if (lock === l) { lock = null; awake.active = false; } });
  } catch (e) {
    awake.active = false;
    awake.error = e?.name === 'NotAllowedError' ? 'not allowed right now (Low Power Mode can block it)' : String(e?.message || e);
  }
}

// The lock is dropped whenever the page is hidden; take it again on return. Some browsers
// only grant it during a tap, so try again on the next one too.
if (!isNative) {
  document.addEventListener('visibilitychange', () => { if (!document.hidden) grab(); });
  document.addEventListener('pointerdown', () => { if (wanted && !awake.active) grab(); }, true);
}
