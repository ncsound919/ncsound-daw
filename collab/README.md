# NCSound Remote — collaborative mix sessions

A phone remote for a mix session. The engineer keeps the session on the desk;
guests join with an 8-character code and are approved per device as **engineer**,
**mixer** or **viewer**. Faders, pans, mutes, solos, record arm and transport are
kept in sync in both directions, with console-style touch locks so two people
cannot fight over the same control.

```
phone (GuestSession) ──ws──▶ relay ◀──ws── host (HostSession) ──▶ DAW backend
```

The relay routes messages; it never applies a mix change. The host validates every
guest op against the protocol, the guest's role and the live track list before it
reaches the DAW. See `src/protocol.ts` for the wire format and `src/host/HostSession.ts`
for the permission and lock rules.

## Run it

```bash
cd "VST DAW/collab"
npm install
npm start
```

Then open **http://localhost:8787/host** on the computer. The console prints the
LAN address; open that on a phone on the same Wi-Fi. Enter the session code and
the engineer approves the device on the host console.

`npm start` builds the two browser bundles and serves:

| Path | What |
| --- | --- |
| `/host` | desktop host console (simulated mixer, approvals, code, guests) |
| `/` | phone mixer remote |
| `/api/addresses` | LAN addresses the host page uses to show the phone URL |

Tests and checks:

```bash
npm test        # end-to-end: approvals, deltas, locks, roles, resume, rate limits
npm run typecheck
```

## What is implemented

- 8-character code from an unambiguous alphabet, 15-minute expiry, rotate with
  `NEW`; sloppy input (`abcd-efgh`, `O` for `0`, `I`/`L` for `1`) is normalised.
- Per-device approval with three roles; deny and kick revoke the seat.
- Resume tokens: a phone that locks or drops its socket reconnects to the same
  seat without asking the host again. Revealing the token in a second tab takes
  the seat over and kicks the old one.
- Host reconnect keeps the session, guests and revision number.
- Versioned deltas with resync on a gap; unversioned 20 Hz meters.
- Optimistic local faders on the phone, coalesced to ~30 Hz, rolled back with a
  notice when the host rejects a change.
- Touch locks: the last person to grab a fader owns it for 1.2 s after release.
- Validation of shape, ranges, track existence and role before `backend.apply`.
- Upgrade-time Host and Origin allowlists (DNS-rebinding and CSWSH defence), an
  Origin allowlist option for proxied deployments, and a per-IP upgrade rate
  limit with a hard connection cap.
- Per-IP brute-force limiting on bad codes, pending-request cap, rate limit per
  guest, heartbeat pings for dead phone sockets.
- Mobile-first phone UI with large touch faders, pan, mute/solo/arm, transport,
  role-aware controls and connection state.

## Wiring a real DAW

`src/host/demo.ts` is the reference host: it holds a real `HostSession`, uses the
real approvals and publishes real deltas. Replacing it with the NCSound Hub bridge
means implementing one interface:

```ts
const backend: HostBackend = {
  getState: () => ({
    title: sessionName,
    transport: { playing, recording, beats, bpm },
    tracks: engineTracks(), // id, name, color, volume, pan, mute, solo, armed
  }),
  apply: (op) => bridge.send(op), // message thread only
};
```

`apply` runs on the host's message chain, never on the audio callback. Cross into
JUCE through a lock-free FIFO and smooth the parameter on the audio thread, exactly
as the comments in `HostSession.ts` describe. Publish local moves with
`host.publish({ tracks: [{ id, volume }] })` and call `host.meters(...)` at ~20 Hz.

The current host console is a simulator so the session layer can be exercised
without the native app. The native NCSound Hub side (a WebSocket client or a local
bridge process plus a `Share` control in the mixer) is not wired yet.

## Security notes

- Codes are short by design and are only the first factor: every device still
  needs host approval. Wrong codes are rate limited per IP.
- The relay is a LAN/dev server over `ws://`. Exposing it beyond the studio needs
  TLS (`wss://`) and a real origin/allow-list policy; do not port-forward the dev
  server as-is.
- Guest frames are untrusted: `validateOp` + `allowed` + track existence run before
  `backend.apply`, and the relay only forwards frames from seated guests.
- Resume tokens live in `sessionStorage`; closing the tab clears them.

## Layout

```
src/protocol.ts          messages, state, validation, roles, code normalisation
src/server/relay.ts      sessions, codes, approvals, resume, kick/end, limits
src/server/index.ts      relay + static phone/host pages + LAN discovery
src/host/HostSession.ts  host authority: approval, locks, validation, deltas
src/host/demo.ts         desktop host console (simulated DAW backend)
src/guest/GuestSession.ts phone-side store: optimistic controls, resume, meters
src/phone/main.ts        mobile mixer UI
src/ui/fader.ts          shared pointer/keyboard fader
tests/e2e.ts             end-to-end session test
```
