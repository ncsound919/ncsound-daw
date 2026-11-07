import assert from 'node:assert/strict';
import { request as httpRequest } from 'node:http';
import { WebSocket as WsClient, type ClientOptions } from 'ws';
import { createRelay } from '../src/server/relay';
import { HostSession, type HostBackend } from '../src/host/HostSession';
import { GuestSession } from '../src/guest/GuestSession';
import type { Op, SessionState } from '../src/protocol';

const sleep = (ms: number) => new Promise((r) => setTimeout(r, ms));
async function until(cond: () => boolean, label: string, ms = 3000) {
  const t0 = Date.now();
  while (!cond()) { if (Date.now() - t0 > ms) throw new Error(`timeout: ${label}`); await sleep(10); }
}

type RawResult = { ws?: WsClient; status?: number; error?: Error };

/** Attempt a handshake and report either the open socket or the HTTP status. */
function openRaw(target: string, options: ClientOptions = {}): Promise<RawResult> {
  return new Promise((resolve) => {
    const ws = new WsClient(target, options);
    let settled = false;
    const done = (result: RawResult) => { if (!settled) { settled = true; resolve(result); } };
    ws.once('open', () => done({ ws }));
    ws.once('unexpected-response', (_req, res) => { res.resume(); done({ status: res.statusCode }); });
    ws.once('error', (error) => done({ error }));
  });
}

function httpStatus(port: number, path: string, headers: Record<string, string>): Promise<number> {
  return new Promise((resolve, reject) => {
    const req = httpRequest({ host: '127.0.0.1', port, path, headers }, (res) => {
      res.resume();
      resolve(res.statusCode ?? 0);
    });
    req.on('error', reject);
    req.end();
  });
}

// Fake DAW: what the JUCE bridge would be in production.
function fakeDaw() {
  const state: Omit<SessionState, 'rev'> = {
    title: 'Test session',
    transport: { playing: false, recording: false, beats: 0, bpm: 120 },
    tracks: ['kick', 'bass', 'vox'].map((n, i) => ({
      id: `t${i + 1}`, name: n, color: '#4aa', volume: 0.75, pan: 0, mute: false, solo: false, armed: false,
    })),
  };
  const applied: Op[] = [];
  const backend: HostBackend = {
    getState: () => state,
    apply(op) {
      applied.push(op);
      if (op.k === 'param' || op.k === 'toggle') (state.tracks.find((t) => t.id === op.track) as any)[op.param] = op.value;
      if (op.k === 'transport' && op.cmd === 'play') state.transport.playing = true;
      return true;
    },
  };
  return { state, backend, applied };
}

const relay = await createRelay({ port: 0, maxFailuresPerMin: 5 });
const url = `ws://127.0.0.1:${relay.port}`;

// --- handshake hardening: Origin/Host allowlists and upgrade rate limiting
const noOrigin = await openRaw(url);
assert.ok(noOrigin.ws, `loopback client without Origin should connect (${noOrigin.status ?? noOrigin.error?.message})`);
noOrigin.ws.close();

const badOrigin = await openRaw(url, { origin: 'http://evil.example' });
assert.equal(badOrigin.status, 403, 'a foreign Origin must be refused');

const badPort = await openRaw(url, { origin: `http://127.0.0.1:${relay.port + 1}` });
assert.equal(badPort.status, 403, 'an Origin on another port must be refused');

const nullOrigin = await openRaw(url, { origin: 'null' });
assert.equal(nullOrigin.status, 403, 'an opaque Origin must be refused');

const badHost = await openRaw(url, { headers: { Host: 'evil.example' } });
assert.equal(badHost.status, 403, 'a foreign Host header must be refused');

const goodOrigin = await openRaw(url, { origin: `http://127.0.0.1:${relay.port}` });
assert.ok(goodOrigin.ws, 'a same-origin browser handshake must connect');
goodOrigin.ws.close();

assert.equal(await httpStatus(relay.port, '/health', { Host: 'evil.example' }), 403, 'HTTP must reject a foreign Host');
assert.equal(await httpStatus(relay.port, '/health', { Host: `127.0.0.1:${relay.port}` }), 200, 'HTTP must accept this host');
console.log('✓ foreign Origin/Host refused; same-origin and loopback native clients connect');

const proxied = await createRelay({ port: 0, allowedOrigins: ['https://remote.example'] });
const proxiedUrl = `ws://127.0.0.1:${proxied.port}`;
const proxiedOk = await openRaw(proxiedUrl, { origin: 'https://remote.example' });
assert.ok(proxiedOk.ws, 'an explicitly allowed Origin must connect');
proxiedOk.ws.close();
const proxiedNo = await openRaw(proxiedUrl, { origin: 'https://evil.example' });
assert.equal(proxiedNo.status, 403, 'an unlisted Origin must be refused even with an allowlist');
await proxied.close();
console.log('✓ explicit Origin allowlist works');

const limitedRelay = await createRelay({ port: 0, maxUpgradesPerMin: 2 });
const limitedUrl = `ws://127.0.0.1:${limitedRelay.port}`;
const first = await openRaw(limitedUrl);
const second = await openRaw(limitedUrl);
assert.ok(first.ws && second.ws, 'the first two upgrades are allowed');
first.ws.close();
second.ws.close();
const third = await openRaw(limitedUrl);
assert.equal(third.status, 429, 'the third upgrade is rate limited');
await limitedRelay.close();
console.log('✓ upgrade attempts are rate limited per IP');

const daw = fakeDaw();
const roleByName: Record<string, 'engineer' | 'mixer' | 'viewer' | null> = { eng: 'engineer', mix: 'mixer', view: 'viewer', nope: null };

const host = new HostSession({ url, backend: daw.backend, onJoinRequest: ({ name }) => roleByName[name] ?? null });
const { code } = await host.start();
assert.match(code, /^[0-9A-HJKMNP-TV-Z]{8}$/);
console.log('✓ host created session, code', code);

// --- join with a sloppy human-typed code (lowercase, dash, O for 0)
const typed = (code.slice(0, 4) + '-' + code.slice(4)).toLowerCase().replace(/0/g, 'o');
const mix = new GuestSession(url); mix.join(typed, 'mix');
const eng = new GuestSession(url); eng.join(code, 'eng');
await until(() => mix.getView().status === 'live' && !!mix.getView().state, 'mixer live');
await until(() => eng.getView().status === 'live' && !!eng.getView().state, 'engineer live');
assert.equal(mix.getView().role, 'mixer');
assert.equal(mix.getView().state!.tracks.length, 3);
console.log('✓ approval flow + initial snapshot (sloppy code accepted)');

// --- mixer moves a fader: host applies, other guest sees delta
mix.setControl('t1', 'volume', 0.5);
await until(() => daw.state.tracks[0].volume === 0.5, 'host applied fader');
await until(() => eng.getView().state!.tracks[0].volume === 0.5, 'engineer saw delta');
console.log('✓ fader change reached host and was broadcast');

// --- touch lock: engineer can't grab the fader the mixer is holding
eng.setControl('t1', 'volume', 0.9);
await until(() => !!eng.getView().notice, 'lock notice');
assert.equal(daw.state.tracks[0].volume, 0.5);
await until(() => eng.getView().state!.tracks[0].volume === 0.5, 'engineer snapped back');
mix.release('t1', 'volume'); await sleep(60);
eng.setControl('t1', 'volume', 0.9);
await until(() => daw.state.tracks[0].volume === 0.9, 'engineer moves after release');
console.log('✓ touch lock blocks a contested fader, releases cleanly');

// --- role enforcement + validation
const before = daw.applied.length;
mix.send({ k: 'transport', cmd: 'record' });                       // mixers may not record
mix.send({ k: 'toggle', track: 't2', param: 'armed', value: true }); // ...or arm
mix.send({ k: 'param', track: 't2', param: 'volume', value: 99 } as Op);       // out of range
mix.send({ k: 'param', track: 'nope', param: 'volume', value: 0.2 });          // unknown track
mix.send({ k: 'loadPlugin', path: '/etc/passwd' } as unknown as Op);           // not an op at all
await sleep(200);
assert.equal(daw.applied.length, before, 'no forbidden/invalid op may reach the backend');
console.log('✓ forbidden, out-of-range, unknown-track and non-existent ops never reach the DAW');

const view = new GuestSession(url); view.join(code, 'view');
await until(() => view.getView().status === 'live' && !!view.getView().state, 'viewer live');
view.setControl('t3', 'mute', true); await sleep(150);
assert.equal(daw.state.tracks[2].mute, false);
console.log('✓ viewer is read-only');

// --- engineer-only ops
eng.send({ k: 'toggle', track: 't2', param: 'armed', value: true });
await until(() => daw.state.tracks[1].armed === true, 'engineer arm');

// --- host-originated changes + meters
host.publish({ transport: { beats: 8 } });
await until(() => eng.getView().state!.transport.beats === 8, 'host delta');
host.meters({ t1: [0.4, 0.5] });
await until(() => eng.getLevels().t1?.[0] === 0.4, 'meters');
console.log('✓ host-side changes and meters stream to guests');

// --- guest reconnect with resume token: no second approval
const gid = (mix as any).creds.guestId;
(mix as any).ws.close();
await until(() => mix.getView().status === 'reconnecting', 'mixer sees drop');
await until(() => mix.getView().status === 'live' && !!mix.getView().state, 'mixer resumed', 5000);
assert.equal((mix as any).creds.guestId, gid);
console.log('✓ dropped phone resumed its seat without re-approval');

// --- host drops and resumes
(host as any).ws.close();
await until(() => eng.getView().status === 'host-offline', 'guests see host offline');
await until(() => eng.getView().status === 'live', 'host resumed', 5000);
eng.setControl('t3', 'pan', -0.5);
await until(() => daw.state.tracks[2].pan === -0.5, 'ops flow after host resume');
console.log('✓ host reconnect keeps the session and guests');

// --- denial, kick
const nope = new GuestSession(url); nope.join(code, 'nope');
await until(() => nope.getView().status === 'denied', 'denied');
const kicked = (view as any).creds.guestId; host.kick(kicked);
await until(() => view.getView().status === 'kicked', 'kicked');
console.log('✓ deny and kick work');

// --- brute force: wrong codes get rate limited per IP
const bad = new GuestSession(url);
for (let i = 0; i < 6; i++) { bad.join('ZZZZZZZZ', 'x'); await sleep(60); }
await until(() => /Too many/.test(bad.getView().error ?? ''), 'rate limited');
const late = new GuestSession(url); late.join(code, 'eng'); await sleep(200);
assert.notEqual(late.getView().status, 'pending', 'a rate-limited IP can’t join even with a good code');
console.log('✓ code guessing is rate limited');

host.end();
await until(() => eng.getView().status === 'ended', 'session ended');
await relay.close();
console.log('\nall checks passed');
process.exit(0);
