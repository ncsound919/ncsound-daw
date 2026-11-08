// GENERATED from console-ui — edit there, then run `npm run sync`.
import { Component, type ErrorInfo, type ReactNode } from "react";

export interface ErrorBoundaryProps {
  /** Names the region in the fallback ("Sampler screen"), so the message says what broke. */
  label: string;
  children: ReactNode;
  /** Called once per caught error; defaults to console.error. */
  onError?: (error: unknown, info: ErrorInfo) => void;
  /** The boundary clears itself when any of these change (e.g. the selected track). */
  resetKeys?: readonly unknown[];
}

interface State {
  hasError: boolean;
  error: unknown;
}

const sameKeys = (a: readonly unknown[] = [], b: readonly unknown[] = []) =>
  a.length === b.length && a.every((v, i) => Object.is(v, b[i]));

/**
 * Contains a render error to one region. A throwing visualizer or panel shows a
 * small, recoverable message instead of unmounting the whole plugin UI -- in an
 * embedded WebView there is no console, so a blank window is undiagnosable.
 */
export class ErrorBoundary extends Component<ErrorBoundaryProps, State> {
  state: State = { hasError: false, error: null };

  static getDerivedStateFromError(error: unknown): State {
    return { hasError: true, error };
  }

  componentDidCatch(error: unknown, info: ErrorInfo) {
    if (this.props.onError) this.props.onError(error, info);
    else console.error(`[${this.props.label}] render failed:`, error, info.componentStack);
  }

  componentDidUpdate(prev: ErrorBoundaryProps) {
    if (this.state.hasError && !sameKeys(prev.resetKeys, this.props.resetKeys)) this.reset();
  }

  reset = () => this.setState({ hasError: false, error: null });

  render() {
    const { hasError, error } = this.state;
    if (!hasError) return this.props.children;
    let message: string;
    try {
      message = error instanceof Error ? error.message || String(error) : String(error);
    } catch {
      message = "Unknown error";
    }
    return (
      <div
        role="alert"
        className="cs-sub"
        style={{
          display: "flex",
          flexDirection: "column",
          alignItems: "flex-start",
          justifyContent: "center",
          gap: 10,
          flex: 1,
          width: "100%",
          height: "100%",
          boxSizing: "border-box",
          padding: 16,
          minHeight: 0,
        }}
      >
        <span>{this.props.label} hit an error and was stopped.</span>
        <span
          className="cs-readout"
          style={{ fontSize: 12, textTransform: "none", letterSpacing: 0 }}
        >
          {message}
        </span>
        <button type="button" className="cs-key" onClick={this.reset}>
          Try again
        </button>
      </div>
    );
  }
}
