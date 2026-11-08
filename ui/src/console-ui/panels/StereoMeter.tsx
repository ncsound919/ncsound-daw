// GENERATED from console-ui — edit there, then run `npm run sync`.
import { Meter } from "../Meter";

/** Two horizontal LED ladders reading [L, R] from one tap. Renders nothing without a tap. */
export function StereoMeter({ read, segments = 20, label = "Output level" }: { read?: () => [number, number]; segments?: number; label?: string }) {
  if (!read) return null;
  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 3, alignSelf: "stretch" }}>
      <Meter read={() => read()[0]} segments={segments} label={`${label} left`} />
      <Meter read={() => read()[1]} segments={segments} label={`${label} right`} />
    </div>
  );
}
