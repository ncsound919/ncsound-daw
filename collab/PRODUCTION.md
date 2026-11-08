# Collab production deploy

The relay speaks plain `ws://` by design. In production it sits behind a
TLS-terminating reverse proxy that speaks `wss://` to clients. This doc is
the checklist that makes the LAN dev server safe to expose.

## Architecture

```
phone/host ──wss://──▶ Caddy (TLS, edge rate limit) ──ws://──▶ relay :8787
```

`Dockerfile` + `Caddyfile` in this directory are the reference deployment.
The relay never binds the public interface directly in production.

## Steps

1. Build and run the relay image:

   ```bash
   docker build -t ncsound-collab -f collab/Dockerfile collab
   docker run -d --name ncsound-collab -p 127.0.0.1:8787:8787 \
     -e ALLOWED_HOSTS=daw.example.com \
     -e ALLOWED_ORIGINS=https://daw.example.com \
     ncsound-collab
   ```

2. Point Caddy at it (replace the domain in `Caddyfile`):

   ```bash
   caddy run --config Caddyfile
   ```

   Caddy provisions and renews Let's Encrypt certificates automatically.

3. Verify:

   ```bash
   curl -fsS https://daw.example.com/api/health   # {"ok":true,"version":...}
   curl -fsS http://127.0.0.1:8787/api/version
   ```

4. Host from the desktop console, approve guests, and confirm a phone on
   mobile data (not just Wi-Fi) can join with the 8-character code.

## Env vars

| Var | Default | Meaning |
|-----|---------|---------|
| `PORT` | `8787` | Relay bind port |
| `HOST` | `0.0.0.0` | Relay bind host (container: keep `0.0.0.0`; restrict at proxy) |
| `ALLOWED_HOSTS` | — | Comma list of public hostnames for Host/Origin checks |
| `ALLOWED_ORIGINS` | — | Comma list of exact public origins, e.g. `https://daw.example.com` |
| `ALLOW_MISSING_ORIGIN` | `false` | Trust native clients that omit Origin (non-browser). Only for loopback |
| `NODE_ENV` | `production` | Image default |

## Security notes (production)

- Do **not** port-forward `8787` directly. All public traffic goes through
  the TLS proxy, which also enforces the Host allowlist.
- Codes are short by design (8 chars) and are only the first factor: every
  device still needs host approval. Response to wrong codes is rate-limited
  per IP; brute force of the whole code space is impractical before the
  per-IP block kicks in.
- Guest frames are untrusted: `validateOp` + role `allowed()` + track
  existence run before `backend.apply`, and the relay only forwards frames
  from seated guests.
- Resume tokens live in `sessionStorage`; closing the tab clears them.

## Known limits (by design, not oversights)

- **Sessions are in-memory.** Restarting the relay drops sessions; the host
  must re-create the session and guests re-join. Persistent session state
  (Redis/file store) is a follow-up.
- **The host console is a simulator.** The native NCSound bridge
  (`HostBackend` implementation in the webview) is the remaining production
  work; see the "Wiring a real DAW" section in `README.md`.
- Single relay instance. Scale-out (sticky sessions or a shared session
  store) is out of scope until more than one relay is needed.
