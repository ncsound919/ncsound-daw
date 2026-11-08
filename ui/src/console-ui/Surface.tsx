// GENERATED from console-ui — edit there, then run `npm run sync`.
import type { CSSProperties, ReactNode } from "react";

/** Root wrapper: scopes fonts, focus ring and reduced-motion rules. */
export function ConsoleRoot({
  children,
  style,
  className = "",
}: {
  children: ReactNode;
  style?: CSSProperties;
  className?: string;
}) {
  return (
    <div className={`cs-root ${className}`} style={style}>
      {children}
    </div>
  );
}

/** One continuous faceplate — the "singular" instrument look (Delay, Reverb, FX). */
export function Face({ children, style }: { children: ReactNode; style?: CSSProperties }) {
  return (
    <div className="cs-face" style={style}>
      <div className="cs-grain" />
      {children}
    </div>
  );
}

/** Engraved control group inside a Face (no separate card). */
export function Group({
  title,
  chip,
  extra,
  bypassed = false,
  children,
  style,
}: {
  title: string;
  chip?: string;
  extra?: ReactNode;
  bypassed?: boolean;
  children: ReactNode;
  style?: CSSProperties;
}) {
  return (
    <section
      className={`cs-grp${bypassed ? " cs-grp-bypassed" : ""}`}
      style={style}
      aria-label={bypassed ? `${title}, bypassed` : title}
    >
      <span className="cs-gl">
        {chip && <span className="cs-chip" style={{ background: chip }} />}
        {title}
        {extra && (
          <span style={{ marginLeft: "auto", display: "flex", alignItems: "center", gap: 6 }}>
            {extra}
          </span>
        )}
      </span>
      {children}
    </section>
  );
}

/** Separate raised card, for multi-stage strips (Channel Strip, Synth, Rompler). */
export function Module({
  title,
  num,
  chip,
  bypassed = false,
  led,
  headerExtra,
  children,
  style,
}: {
  title: string;
  num?: string;
  chip?: string;
  bypassed?: boolean;
  led?: "on" | "mod" | "off";
  headerExtra?: ReactNode;
  children: ReactNode;
  style?: CSSProperties;
}) {
  return (
    <section
      className={`cs-module${bypassed ? " cs-bypassed" : ""}`}
      style={style}
      aria-label={bypassed ? `${title}, bypassed` : title}
    >
      <header className="cs-mh">
        {num && (
          <span
            className="cs-readout"
            style={{ fontSize: 11, color: "#6f6c66", textShadow: "none" }}
          >
            {num}
          </span>
        )}
        {chip && (
          <span className="cs-chip" style={{ background: chip, opacity: bypassed ? 0.35 : 1 }} />
        )}
        <span className="cs-mt" style={{ color: bypassed ? "#8f8c85" : undefined }}>
          {title}
        </span>
        <span style={{ flexGrow: 1 }} />
        {headerExtra}
        {led && <Led state={bypassed ? "off" : led} />}
      </header>
      {children}
    </section>
  );
}

export function Led({
  state = "on",
  pulse = false,
}: {
  state?: "on" | "mod" | "off";
  pulse?: boolean;
}) {
  const cls = state === "on" ? " cs-led-on" : state === "mod" ? " cs-led-mod" : "";
  return <span className={`cs-led${cls}${pulse ? " cs-led-pulse" : ""}`} aria-hidden="true" />;
}

/** Large hardware key. Toggle when `pressed` is supplied. */
export function Key({
  children,
  pressed,
  onClick,
  style,
  disabled,
  label,
  className = "",
}: {
  children: ReactNode;
  pressed?: boolean;
  onClick?: () => void;
  style?: CSSProperties;
  disabled?: boolean;
  label?: string;
  className?: string;
}) {
  return (
    <button
      type="button"
      className={`cs-key ${className}`.trim()}
      aria-pressed={pressed}
      aria-label={label}
      onClick={onClick}
      style={style}
      disabled={disabled}
    >
      {children}
    </button>
  );
}

/** Segmented selector. */
export function Seg<T extends string>({
  options,
  value,
  onChange,
  label,
  style,
}: {
  options: readonly { id: T; label: ReactNode }[];
  value: T;
  onChange?: (id: T) => void;
  label: string;
  style?: CSSProperties;
}) {
  return (
    <div className="cs-seg" role="group" aria-label={label} style={style}>
      {options.map((o) => (
        <button
          key={o.id}
          type="button"
          aria-pressed={o.id === value}
          onClick={() => onChange?.(o.id)}
        >
          {o.label}
        </button>
      ))}
    </div>
  );
}

export function Lcd({ children, style }: { children: ReactNode; style?: CSSProperties }) {
  return (
    <div
      className="cs-lcd"
      style={{ display: "flex", alignItems: "center", padding: "0 12px", ...style }}
    >
      {children}
    </div>
  );
}

/** Dashed cyan badge naming an external key/sidechain source. */
export function SidechainBadge({ source }: { source: string }) {
  return <span className="cs-sc">SC · {source}</span>;
}

/** Animated left-to-right signal bus drawn behind a row of modules. */
export function Bus({
  top = 15,
  mod = false,
  style,
}: {
  top?: number;
  mod?: boolean;
  style?: CSSProperties;
}) {
  return (
    <div
      className={`cs-bus${mod ? " cs-bus-mod" : ""}`}
      style={{ top, ...style }}
      aria-hidden="true"
    />
  );
}
