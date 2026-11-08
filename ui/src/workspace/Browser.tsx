import { useEffect, useMemo, useState } from "react";
import type { PluginInfo } from "../types";

const FILTERS: { id: "all" | "instrument" | "effect"; label: string }[] = [
  { id: "all", label: "All" },
  { id: "instrument", label: "Instruments" },
  { id: "effect", label: "Effects" },
];

export interface BrowserProps {
  plugins: PluginInfo[];
  targetName?: string;
  /** Absent for targets that are not a track insert, such as the master. */
  targetSlot?: number;
  scanning?: boolean;
  progress?: number;
  status?: string;
  onLoad?: (plugin: PluginInfo) => void;
  onScan?: () => void;
}

/**
 * Plug-in browser, laid out like the native BrowserOverlay: header with the
 * target slot, a search field, category filters, a list and a detail pane.
 * It renders whatever catalogue the host pushes in AppState; with no catalogue
 * it says so rather than inventing plug-ins.
 */
export function Browser({
  plugins,
  targetName,
  targetSlot,
  scanning = false,
  progress = 0,
  status,
  onLoad,
  onScan,
}: BrowserProps) {
  const [query, setQuery] = useState("");
  const [filter, setFilter] = useState<(typeof FILTERS)[number]["id"]>("all");
  const [selected, setSelected] = useState(0);

  // A different target must not inherit the previous selection: the row under
  // the pointer would no longer be the row the user chose.
  useEffect(() => {
    setSelected(0);
  }, [targetName, targetSlot]);

  const filtered = useMemo(() => {
    const needle = query.trim().toLowerCase();
    return plugins.filter((plugin) => {
      if (filter !== "all" && (plugin.category ?? "effect") !== filter) return false;
      if (!needle) return true;
      return `${plugin.name} ${plugin.vendor ?? ""}`.toLowerCase().includes(needle);
    });
  }, [plugins, query, filter]);

  const current = filtered[Math.min(selected, Math.max(0, filtered.length - 1))];

  return (
    <div className="ws-browser">
      <header className="ws-browser-head">
        <span className="ws-browser-target">
          TARGET <b>{targetName ?? "NO TRACK"}</b>
          {targetSlot !== undefined && (
            <>
              {" "}· SLOT <b>{targetSlot + 1}</b>
            </>
          )}
        </span>
        <span className="ws-spacer" />
        {scanning ? (
          <span className="ws-browser-scanning">SCANNING {Math.round(progress * 100)}%</span>
        ) : null}
        {onScan ? (
          <button className="ws-btn" onClick={onScan} disabled={scanning} title="Rescan the plug-in folders">
            {scanning ? "Scanning" : "Scan"}
          </button>
        ) : null}
      </header>

      <div className="ws-browser-tools">
        <input
          className="ws-browser-search"
          type="search"
          value={query}
          placeholder="Search plug-ins"
          aria-label="Search plug-ins"
          onChange={(event) => {
            setQuery(event.target.value);
            setSelected(0);
          }}
        />
        <span className="ws-browser-filters" role="group" aria-label="Filter by category">
          {FILTERS.map((entry) => (
            <button
              key={entry.id}
              className="ws-btn"
              aria-pressed={filter === entry.id}
              onClick={() => {
                setFilter(entry.id);
                setSelected(0);
              }}
            >
              {entry.label}
            </button>
          ))}
        </span>
      </div>

      <div className={`ws-browser-body${plugins.length === 0 ? " is-empty" : ""}`}>
        <div className="ws-browser-list" role="group" aria-label="Plug-ins">
          {filtered.length === 0 ? (
            <p className="ws-browser-empty">
              {plugins.length === 0 ? (
                <>
                  <strong>No plug-ins indexed yet.</strong>
                  <span>Scan the plug-in folders to build the catalogue.</span>
                </>
              ) : (
                "No plug-in matches that search."
              )}
            </p>
          ) : (
            filtered.map((plugin, index) => (
              <button
                key={`${plugin.name}-${plugin.path ?? index}`}
                className={current === plugin ? "ws-browser-row is-selected" : "ws-browser-row"}
                aria-pressed={current === plugin}
                onClick={() => setSelected(index)}
                onDoubleClick={() => onLoad?.(plugin)}
                onKeyDown={(event) => {
                  if (event.key === "ArrowDown" || event.key === "ArrowUp") {
                    event.preventDefault();
                    const delta = event.key === "ArrowDown" ? 1 : -1;
                    const next = Math.max(0, Math.min(filtered.length - 1, index + delta));
                    setSelected(next);
                    event.currentTarget.parentElement
                      ?.querySelectorAll<HTMLButtonElement>(".ws-browser-row")
                      [next]?.focus();
                  }
                }}
                title={plugin.path ?? plugin.name}
              >
                <span className="ws-browser-row-name">{plugin.name}</span>
                <span className="ws-browser-row-vendor">{plugin.vendor ?? ""}</span>
                <span className="ws-browser-row-tag">{plugin.category === "instrument" ? "INST" : "FX"}</span>
              </button>
            ))
          )}
        </div>

        <div className="ws-browser-detail" aria-live="polite">
          {current ? (
            <>
              <h3>{current.name}</h3>
              <dl>
                <div>
                  <dt>Vendor</dt>
                  <dd>{current.vendor || "—"}</dd>
                </div>
                <div>
                  <dt>Format</dt>
                  <dd>{current.format || "VST3"}</dd>
                </div>
                <div>
                  <dt>Category</dt>
                  <dd>{current.category === "instrument" ? "Instrument" : "Effect"}</dd>
                </div>
                <div>
                  <dt>Path</dt>
                  <dd className="ws-browser-path">{current.path || "—"}</dd>
                </div>
              </dl>
              <button
                className="ws-btn"
                disabled={!onLoad || current.missing}
                title={onLoad ? "Load into the target slot" : "No load target selected"}
                onClick={() => onLoad?.(current)}
              >
                {current.missing ? "Missing" : "Load"}
              </button>
            </>
          ) : (
            <p className="ws-browser-empty">Select a plug-in to see its details.</p>
          )}
        </div>
      </div>

      <footer className="ws-browser-foot" role="status">
        {status || `${filtered.length} of ${plugins.length} plug-ins`}
      </footer>
    </div>
  );
}
