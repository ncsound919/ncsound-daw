/**
 * Minimal typed binding for the JUCE 8 WebBrowserComponent native-integration
 * protocol (see modules/juce_gui_extra/native/javascript/index.js). We only
 * implement the bits we need: native function invocation and arbitrary event
 * subscription.
 */

import type { NativeRoute } from "./nativeRoutes.generated";

type CompletePayload = { promiseId: number; result: unknown };

interface JuceBackend {
  addEventListener: (eventId: string, cb: (data: unknown) => void) => void;
  emitEvent: (eventId: string, data: unknown) => void;
}

interface JuceGlobal {
  backend: JuceBackend;
  initialisationData?: Record<string, unknown>;
}

declare global {
  interface Window {
    __JUCE__?: JuceGlobal;
  }
}

export function isEmbedded(): boolean {
  return typeof window !== "undefined" && !!window.__JUCE__?.backend;
}

/**
 * A native call that never completes -- the backend threw, the window was
 * reloaded mid-call, or a completion event was dropped -- used to leave its
 * entry in `pending` forever and leave the caller awaiting a promise that could
 * never settle. Every call now has a deadline and always clears its entry.
 *
 * The default is deliberately generous: some native calls (a local LLM prompt,
 * an offline bounce) legitimately run for a minute or more. Pass a per-call
 * timeout via `callNativeFunction` where a tighter bound makes sense.
 */
export const DEFAULT_NATIVE_TIMEOUT_MS = 180_000;

type PendingCall = {
  resolve: (value: unknown) => void;
  reject: (reason: Error) => void;
  timer: ReturnType<typeof setTimeout>;
};

let lastPromiseId = 0;
const pending = new Map<number, PendingCall>();

// The backend object we attached the completion listener to. Tracked by
// identity rather than with a one-shot `bound` flag: if `window.__JUCE__` is
// replaced -- a WebView reload, or a re-injected backend -- a flag would leave
// us listening to an object nobody posts to any more, and every subsequent
// call would hang until its timeout.
let boundBackend: JuceBackend | null = null;
let backendWatchdog: ReturnType<typeof setInterval> | null = null;

function stopBackendWatchdogWhenIdle(): void {
  if (pending.size !== 0 || backendWatchdog === null) return;
  clearInterval(backendWatchdog);
  backendWatchdog = null;
}

function watchBackendWhileCallsArePending(): void {
  if (backendWatchdog !== null) return;
  backendWatchdog = setInterval(() => {
    if (pending.size && window.__JUCE__?.backend !== boundBackend) {
      abortPendingNativeCalls(
        window.__JUCE__?.backend ? "JUCE backend replaced" : "JUCE backend disconnected",
      );
    }
  }, 50);
}

function settle(promiseId: number): PendingCall | undefined {
  const call = pending.get(promiseId);
  if (!call) return undefined;
  clearTimeout(call.timer);
  pending.delete(promiseId);
  stopBackendWatchdogWhenIdle();
  return call;
}

function bindCompletionHandler(): void {
  const backend = window.__JUCE__?.backend;
  if (!backend || backend === boundBackend) return;

  // A different backend means the old one is gone (WebView reload): nothing
  // will ever complete the calls still waiting on it, so fail them now rather
  // than leaving callers hanging until their timeout.
  if (boundBackend !== null) abortPendingNativeCalls("JUCE backend replaced");

  boundBackend = backend;
  backend.addEventListener("__juce__complete", (data) => {
    const payload = data as CompletePayload;
    settle(payload.promiseId)?.resolve(payload.result);
  });
}

/**
 * Rejects every in-flight call and forgets the backend we were listening to.
 * Used when the backend goes away, and by tests between cases.
 */
export function abortPendingNativeCalls(reason = "JUCE backend disconnected"): void {
  for (const promiseId of [...pending.keys()]) {
    settle(promiseId)?.reject(new Error(reason));
  }
  boundBackend = null;
}

/** Number of native calls awaiting a completion. Exposed for tests. */
export function pendingNativeCallCount(): number {
  return pending.size;
}

// `name` is typed against the generated route vocabulary (see
// gen-native-routes.mjs) so a call to a route the host does not register --
// including a direct getNativeFunction call outside bridge.ts -- is a compile
// error, not a runtime hang.
export function getNativeFunction(
  name: NativeRoute,
  timeoutMs: number = DEFAULT_NATIVE_TIMEOUT_MS,
): (...args: unknown[]) => Promise<unknown> {
  bindCompletionHandler();
  return (...args: unknown[]) => {
    // Re-check on every call (cheap, identity compare) so a replaced backend
    // gets its completion listener even for functions bound before the swap.
    bindCompletionHandler();
    const backend = window.__JUCE__?.backend;
    if (!backend) return Promise.reject(new Error("Not running inside JUCE"));

    const promiseId = lastPromiseId++;

    const result = new Promise<unknown>((resolve, reject) => {
      const timer = setTimeout(() => {
        settle(promiseId)?.reject(
          new Error(`Native call "${name}" timed out after ${timeoutMs}ms`),
        );
      }, timeoutMs);

      pending.set(promiseId, { resolve, reject, timer });
      watchBackendWhileCallsArePending();
    });

    try {
      backend.emitEvent("__juce__invoke", {
        name,
        params: args,
        resultId: promiseId,
      });
    } catch (error) {
      // The call never reached the backend, so no completion is coming.
      settle(promiseId)?.reject(error instanceof Error ? error : new Error(String(error)));
    }

    return result;
  };
}

export function addBackendEventListener(
  eventId: string,
  callback: (data: unknown) => void,
): () => void {
  let active = true;
  let detach: (() => void) | null = null;

  // The backend object can appear after the first render (the WebView injects
  // it). Attaching lazily instead of giving up once means the subscription is
  // not silently dead for the rest of the session.
  const attach = () => {
    const backend = window.__JUCE__?.backend;
    if (!active || !backend || detach) return;

    backend.addEventListener(eventId, (data) => {
      if (active) callback(data);
    });
    detach = () => {
      // addEventListener has no removal API on the bridge; clear state instead.
      detach = null;
    };
  };

  attach();

  const timer = detach ? null : window.setInterval(attach, 100);

  return () => {
    active = false;
    if (timer !== null) window.clearInterval(timer);
    detach?.();
    detach = null;
  };
}

export function registeredFunctions(): string[] {
  const fns = window.__JUCE__?.initialisationData?.__juce__functions;
  return Array.isArray(fns) ? (fns as string[]) : [];
}
