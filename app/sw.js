/* Q3RF termosztĂˇt â€“ offline gyorsĂ­tĂłtĂˇr (a Bluetooth-vezĂ©rlĂ©s internet nĂ©lkĂĽl is menjen) */
const CACHE = "q3rf-v3";
const FILES = ["./", "index.html", "mqtt.min.js", "manifest.json", "icon-192.png", "icon-512.png"];

self.addEventListener("install", e => {
  e.waitUntil(caches.open(CACHE).then(c => c.addAll(FILES)).then(() => self.skipWaiting()));
});
self.addEventListener("activate", e => {
  e.waitUntil(caches.keys()
    .then(keys => Promise.all(keys.filter(k => k !== CACHE).map(k => caches.delete(k))))
    .then(() => self.clients.claim()));
});
/* hĂˇlĂłzat elĹ‘szĂ¶r (friss verziĂł), ha nincs net: gyorsĂ­tĂłtĂˇr */
self.addEventListener("fetch", e => {
  if (e.request.method !== "GET" || new URL(e.request.url).origin !== location.origin) return;
  e.respondWith(
    fetch(e.request)
      .then(r => { const copy = r.clone(); caches.open(CACHE).then(c => c.put(e.request, copy)); return r; })
      .catch(() => caches.match(e.request, { ignoreSearch: true }))
  );
});
