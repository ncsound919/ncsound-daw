// GENERATED from console-ui — edit there, then run `npm run sync`.
import type { ReactNode } from "react";
import { ConsoleRoot, Face, Key, Lcd } from "../Surface";
import { ErrorBoundary } from "../ErrorBoundary";

export const PANEL_W = 1320;
export const PANEL_H = 780;

/**
 * Single-faceplate plugin layout: header, full-width display on the top half,
 * engraved control deck below. Base size 1320×780; the host scales it.
 */
export function PluginFrame({ name, tag, subtitle, presetName, onPresetStep, headerRight, screen, screenHeight = 344, children, width = PANEL_W, height = PANEL_H, resetKeys }: {
  name: string; tag: string; subtitle: string; presetName?: string; onPresetStep?: (dir: -1 | 1) => void;
  headerRight?: ReactNode; screen: ReactNode; screenHeight?: number; children: ReactNode; width?: number; height?: number;
  resetKeys?: readonly unknown[];
}) {
  return (
    <ConsoleRoot style={{ width, height, boxSizing: "border-box", padding: 0 }}>
      <Face style={{ width, height, boxSizing: "border-box", padding: 16, display: "flex", flexDirection: "column", gap: 14 }}>
        <header style={{ position: "relative", height: 52, display: "flex", alignItems: "center", gap: 16, flexShrink: 0 }}>
          <div style={{ display: "flex", flexDirection: "column", gap: 1 }}>
            <div style={{ display: "flex", alignItems: "baseline", gap: 10 }}>
              <h1 className="cs-wordmark" style={{ margin: 0 }}>{name}</h1>
              <span className="cs-readout" style={{ fontSize: 12 }}>{tag}</span>
            </div>
            <span className="cs-sub">{subtitle}</span>
          </div>
          {presetName !== undefined && (
            <div style={{ display: "flex", alignItems: "center", gap: 6, marginLeft: 12 }}>
              {/* Arrows only when stepping is wired: no placebo buttons. */}
              {onPresetStep && <Key label="Previous preset" onClick={() => onPresetStep(-1)} className="cs-key-lg" style={{ width: 44, padding: 0 }}>◂</Key>}
              <Lcd style={{ width: 300, height: 40, fontSize: 17 }}>{presetName}</Lcd>
              {onPresetStep && <Key label="Next preset" onClick={() => onPresetStep(1)} className="cs-key-lg" style={{ width: 44, padding: 0 }}>▸</Key>}
            </div>
          )}
          <div style={{ flexGrow: 1 }} />
          {headerRight}
        </header>
        <div style={{ position: "relative", height: screenHeight, flexShrink: 0 }}><ErrorBoundary label={`${name} display`} resetKeys={resetKeys}>{screen}</ErrorBoundary></div>
        <div style={{ position: "relative", flexGrow: 1, minHeight: 0, display: "flex", paddingTop: 4 }}><ErrorBoundary label={`${name} controls`} resetKeys={resetKeys}>{children}</ErrorBoundary></div>
      </Face>
    </ConsoleRoot>
  );
}

/** Absolutely positioned overlay on top of a display. */
export function Overlay({ children, style, interactive = false }: { children: ReactNode; style: React.CSSProperties; interactive?: boolean }) {
  return <div className={`cs-ov${interactive ? " cs-ov-interactive" : ""}`} style={style}>{children}</div>;
}
