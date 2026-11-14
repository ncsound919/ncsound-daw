import assert from "node:assert/strict";
import { test } from "node:test";
import { validateMasterPanel } from "./check-master-uplot.mjs";

const currentPanel = `
import { TimePlot } from "../TimePlot";
import { maximizerPlotValues } from "../masterPlotValues";
// SYNC NOTE: the Maximizer uses app-local uPlot, not Scope.
function MaxView() {
  const tgt = targetLufs(MASTER_TARGETS[useOption("target")] ?? MASTER_TARGETS[0]);
  const plots = maximizerPlotValues(getFrame);
  const loud = plots?.shortTermLufs;
  const gr = plots?.gainReductionDb;
  return <>
    <TimePlot read={loud} seconds={6} refLine={tgt} refText={\`TARGET \${tgt} LUFS\`} y={[-30, -4]} />
    <TimePlot read={gr} seconds={6} y={[-12, 0]} />
  </>;
}
// ---------------------------------------------------------------- loudness column
`;

test("accepts the app-local uPlot Maximizer plot contract", () => {
  assert.deepEqual(validateMasterPanel(currentPanel), []);
});

test("rejects generated output that restores Scope in the Maximizer", () => {
  const syncedPanel = currentPanel.replaceAll("<TimePlot", "<Scope");
  assert.ok(validateMasterPanel(syncedPanel).some((issue) => issue.includes("two uPlot TimePlot")));
});

test("rejects generated output that loses the sync warning or adapter import", () => {
  const noNote = currentPanel.replace("// SYNC NOTE:", "// NOTE:");
  const noAdapter = currentPanel.replace(
    'import { maximizerPlotValues } from "../masterPlotValues";',
    "",
  );
  assert.ok(validateMasterPanel(noNote).some((issue) => issue.includes("SYNC NOTE")));
  assert.ok(validateMasterPanel(noAdapter).some((issue) => issue.includes("maximizerPlotValues")));
});

test("rejects positive-normalized GR mapping and a changed GR scale", () => {
  const wrongMapping = currentPanel.replace(
    "const gr = plots?.gainReductionDb",
    "const gr = () => 0.5",
  );
  const wrongRange = currentPanel.replace("y={[-12, 0]}", "y={[-1, 0]}");
  assert.ok(validateMasterPanel(wrongMapping).some((issue) => issue.includes("negative dB")));
  assert.ok(validateMasterPanel(wrongRange).some((issue) => issue.includes("-12 to 0 dB")));
});

test("rejects losing the LUFS live value, six-second window, or target line", () => {
  const noValue = currentPanel.replace("plots?.shortTermLufs", "undefined");
  const wrongWindow = currentPanel.replace("seconds={6}", "seconds={3}");
  const noTarget = currentPanel.replace("refLine={tgt}", "");
  assert.ok(validateMasterPanel(noValue).some((issue) => issue.includes("short-term LUFS")));
  assert.ok(validateMasterPanel(wrongWindow).some((issue) => issue.includes("six-second")));
  assert.ok(validateMasterPanel(noTarget).some((issue) => issue.includes("LUFS target line")));
});
