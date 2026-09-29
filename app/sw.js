// Keeps the app shell available offline. App files are fetched fresh when there's a
// connection (so updates show up) and served from the cache when there isn't.
const VERSION = 'peekabyte-v4';
const SHELL = [
  './', 'index.html', 'manifest.webmanifest', 'icon.png',
  'js/main.js', 'js/link.js', 'js/protocol.js', 'js/mirror.js', 'js/sfx.js', 'js/voice.js', 'js/tts-worker.js',
  'js/brain.js', 'js/llm-worker.js', 'js/phrases.js', 'js/awake.js', 'js/connlog.js', 'js/native.js',
];

self.addEventListener('install', (e) => {
  e.waitUntil(caches.open(VERSION).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});

self.addEventListener('activate', (e) => {
  e.waitUntil(caches.keys()
    .then((keys) => Promise.all(keys.filter((k) => k.startsWith('peekabyte-') && k !== VERSION).map((k) => caches.delete(k))))
    .then(() => self.clients.claim()));
});

self.addEventListener('fetch', (e) => {
  const url = new URL(e.request.url);
  if (url.origin !== location.origin || e.request.method !== 'GET') return;
  e.respondWith(fetch(e.request)
    .then((res) => {
      const copy = res.clone();
      caches.open(VERSION).then((c) => c.put(e.request, copy));
      return res;
    })
    .catch(() => caches.match(e.request)));
});
