// Keeps the phone's screen on while the pet is connected. iPhones suspend the page (and with
// it the Bluetooth link) as soon as the screen locks, so a pet you're just watching would
// lose its phone after 30 seconds.

let lock = null;
let wanted = false;

export const awake = { supported: 'wakeLock' in navigator, active: false, error: '' };

export async function keepAwake(on) {
  wanted = on;
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
document.addEventListener('visibilitychange', () => { if (!document.hidden) grab(); });
document.addEventListener('pointerdown', () => { if (wanted && !awake.active) grab(); }, true);
