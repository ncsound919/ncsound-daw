/**
 * Dev / LAN server: relay + phone remote + desktop host console.
 * In production the relay lives on a small VPS with TLS (wss://); the host
 * console in this build is a simulator that documents the HostBackend contract.
 */
import { readFile } from 'node:fs/promises';
import { networkInterfaces } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import type { IncomingMessage, ServerResponse } from 'node:http';
import { createRelay } from './relay';

const here = path.dirname(fileURLToPath(import.meta.url));
const publicDir = path.resolve(here, '../../public');

const MIME: Record<string, string> = {
  '.html': 'text/html; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8',
  '.css': 'text/css; charset=utf-8',
  '.svg': 'image/svg+xml',
  '.png': 'image/png',
  '.ico': 'image/x-icon',
  '.json': 'application/json; charset=utf-8',
};

function lanAddresses(): string[] {
  const addresses: string[] = [];
  for (const list of Object.values(networkInterfaces())) {
    for (const net of list ?? []) {
      if (net.family === 'IPv4' && !net.internal) addresses.push(net.address);
    }
  }
  return addresses;
}

function json(res: ServerResponse, status: number, body: unknown): void {
  res.writeHead(status, { 'content-type': 'application/json; charset=utf-8', 'cache-control': 'no-store' });
  res.end(JSON.stringify(body));
}

async function serve(req: IncomingMessage, res: ServerResponse): Promise<void> {
  const url = new URL(req.url ?? '/', 'http://relay.local');

  if (url.pathname === '/api/health') return json(res, 200, { ok: true });
  if (url.pathname === '/api/addresses') {
    return json(res, 200, { addresses: lanAddresses(), origin: `http://${req.headers.host ?? 'localhost'}` });
  }

  const requested = url.pathname === '/' ? '/index.html' : url.pathname === '/host' ? '/host.html' : url.pathname;
  const file = path.resolve(publicDir, `.${decodeURIComponent(requested)}`);
  if (file !== publicDir && !file.startsWith(publicDir + path.sep)) {
    res.writeHead(403);
    res.end('Forbidden');
    return;
  }

  try {
    const body = await readFile(file);
    const type = MIME[path.extname(file).toLowerCase()] ?? 'application/octet-stream';
    res.writeHead(200, {
      'content-type': type,
      'cache-control': type.startsWith('text/html') ? 'no-store' : 'no-cache',
      'x-content-type-options': 'nosniff',
    });
    res.end(body);
  } catch {
    res.writeHead(404, { 'content-type': 'text/plain; charset=utf-8' });
    res.end('Not found — run `npm run build` first.');
  }
}

const port = Number(process.env.PORT ?? 8787);
const host = process.env.HOST ?? '0.0.0.0';

const relay = await createRelay({ port, host, httpHandler: serve });
const addresses = lanAddresses();

console.log('');
console.log('  NCSOUND REMOTE');
console.log('  ─────────────────────────────────────────────');
console.log(`  host console   http://localhost:${relay.port}/host`);
if (addresses.length) {
  for (const address of addresses) {
    console.log(`  phone          http://${address}:${relay.port}`);
  }
  console.log('                 (same Wi-Fi, then enter the session code)');
} else {
  console.log(`  phone          http://localhost:${relay.port}`);
}
console.log('');

let closing = false;
const shutdown = async () => {
  if (closing) return;
  closing = true;
  await relay.close();
  process.exit(0);
};
process.on('SIGINT', shutdown);
process.on('SIGTERM', shutdown);
