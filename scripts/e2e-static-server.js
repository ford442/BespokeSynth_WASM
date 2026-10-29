#!/usr/bin/env node
/**
 * Static file server for dist/: Playwright smoke tests and `npm run serve`.
 * Sends COOP/COEP so SharedArrayBuffer + AudioWorklet SAB ring can be tested.
 * Paths under /no-coi/ are served without isolation headers, to exercise the
 * SDL2 fallback a non-isolated production host would get.
 */
const http = require('http');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const root = path.resolve(__dirname, '..', 'dist');
const port = Number(process.env.PORT || process.argv[2] || 9876);

const TYPES = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'application/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.wasm': 'application/wasm',
  '.json': 'application/json',
  '.png': 'image/png',
  '.map': 'application/json',
  '.data': 'application/octet-stream',
};

const server = http.createServer((req, res) => {
  let urlPath = decodeURIComponent((req.url || '/').split('?')[0]);
  const isolated = !urlPath.startsWith('/no-coi/');
  if (!isolated) urlPath = urlPath.slice('/no-coi'.length);
  let filePath = path.join(root, urlPath === '/' ? 'index.html' : urlPath);
  if (!filePath.startsWith(root)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }
  if (!fs.existsSync(filePath) || fs.statSync(filePath).isDirectory()) {
    filePath = path.join(root, 'index.html');
  }
  const ext = path.extname(filePath);
  const headers = {
    'Content-Type': TYPES[ext] || 'application/octet-stream',
    'Cache-Control': 'no-store',
  };
  if (isolated) {
    headers['Cross-Origin-Opener-Policy'] = 'same-origin';
    headers['Cross-Origin-Embedder-Policy'] = 'require-corp';
    headers['Cross-Origin-Resource-Policy'] = 'same-origin';
  }
  fs.readFile(filePath, (err, data) => {
    if (err) {
      res.writeHead(404, headers);
      res.end('Not found');
      return;
    }
    res.writeHead(200, headers);
    res.end(data);
  });
});

server.listen(port, () => {
  console.log(`Serving ${root} on http://localhost:${port} (COOP/COEP enabled)`);
});
