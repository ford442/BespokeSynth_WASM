/**
 * Cross-origin isolation for hosts that cannot send COOP/COEP headers (GitHub Pages and the
 * like). Off by default; opt in at build time with BESPOKE_COI_SW=1 (webpack.config.js), see
 * docs/DEPLOYMENT_GUIDE.md. Real headers are better: they work on the first load and in every
 * browser that isolates, and this file then does nothing.
 *
 * The same file runs twice. As a page script (first in <head>) it registers itself as a service
 * worker and reloads once that worker controls the page. As the service worker it re-serves every
 * response with COOP/COEP/CORP added, which makes the page crossOriginIsolated, so the AudioWorklet
 * engine (SharedArrayBuffer) can run instead of the SDL2 fallback.
 */
/* global self, window, document, navigator, sessionStorage */
if (typeof window === 'undefined') {
  self.addEventListener('install', () => self.skipWaiting());
  self.addEventListener('activate', (event) => event.waitUntil(self.clients.claim()));
  self.addEventListener('fetch', (event) => {
    const request = event.request;
    // Only valid for same-origin requests (DevTools issues these).
    if (request.cache === 'only-if-cached' && request.mode !== 'same-origin') return;
    event.respondWith(
      fetch(request).then((response) => {
        // Opaque (no-cors, cross-origin) responses cannot be changed.
        if (response.status === 0) return response;
        const headers = new Headers(response.headers);
        headers.set('Cross-Origin-Opener-Policy', 'same-origin');
        headers.set('Cross-Origin-Embedder-Policy', 'require-corp');
        // Only same-origin and CORS responses get here; CORS ones are not checked against CORP.
        headers.set('Cross-Origin-Resource-Policy', 'same-origin');
        return new Response(response.body, { status: response.status, statusText: response.statusText, headers });
      }),
    );
  });
} else {
  (() => {
    const RELOADED = 'bespokesynth.coiReload';
    const flag = (value) => {
      try {
        if (value === undefined) return sessionStorage.getItem(RELOADED) === '1';
        if (value) sessionStorage.setItem(RELOADED, '1');
        else sessionStorage.removeItem(RELOADED);
      } catch {
        // Storage blocked: at worst one extra reload is skipped.
      }
      return false;
    };
    if (window.crossOriginIsolated) {
      flag(false);
      return;
    }
    // Reloaded once already and still not isolated (browser policy, old worker): stay on SDL2.
    if (flag() || !window.isSecureContext || !('serviceWorker' in navigator)) return;
    const script = document.currentScript && document.currentScript.src;
    if (!script) return;
    navigator.serviceWorker.register(script).then(
      () => {
        const reload = () => {
          flag(true);
          window.location.reload();
        };
        if (navigator.serviceWorker.controller) reload();
        else navigator.serviceWorker.addEventListener('controllerchange', reload, { once: true });
      },
      (error) => console.warn('COOP/COEP service worker not registered:', error),
    );
  })();
}
