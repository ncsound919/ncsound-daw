#!/usr/bin/env node
// console-ui's source and sync script are not present in this checkout. Guard
// its generated MasterPanel output so a future refresh cannot silently replace
// the app-local uPlot Maximizer plots with the generic Scope renderers.
import { readFileSync } from "node:fs";
import { fileURLToPath, pathToFileURL } from "node:url";
import { dirname, resolve } from "node:path";

const here = dirname(fileURLToPath(import.meta.url));
const panelPath = resolve(here, "../src/console-ui/panels/MasterPanel.tsx");

export function validateMasterPanel(source) {
  const issues = [];
  if (!source.includes('import { TimePlot } from "../TimePlot";')) {
    issues.push('MasterPanel must import the app-local "../TimePlot".');
  }
  if (!source.includes('import { maximizerPlotValues } from "../masterPlotValues";')) {
    issues.push("MasterPanel must use the tested app-local maximizerPlotValues adapter.");
  }
  if (!source.includes("// SYNC NOTE:")) {
    issues.push("MasterPanel must retain its // SYNC NOTE for the generated uPlot override.");
  }

  const start = source.indexOf("function MaxView(");
  const end = source.indexOf(
    "// ---------------------------------------------------------------- loudness column",
    start,
  );
  if (start < 0 || end < 0) {
    issues.push("Could not locate the generated MasterPanel Maximizer view.");
    return issues;
  }

  const maxView = source.slice(start, end);
  const timePlots = maxView.match(/<TimePlot\b/g) ?? [];
  if (timePlots.length !== 2 || /<\/?Scope\b/.test(maxView)) {
    issues.push(
      "MasterPanel Maximizer must keep its two uPlot TimePlot views, not Scope renderers.",
    );
  }
  if (
    !/const plots = maximizerPlotValues\(getFrame\)/.test(maxView) ||
    !/read=\{loud\}/.test(maxView) ||
    !/const loud = plots\?\.shortTermLufs/.test(maxView)
  ) {
    issues.push("MasterPanel must feed live short-term LUFS values to the first plot.");
  }
  if (!/const gr = plots\?\.gainReductionDb/.test(maxView) || !/read=\{gr\}/.test(maxView)) {
    issues.push("MasterPanel must feed host GR as negative dB to the second plot.");
  }
  if ((maxView.match(/seconds=\{6\}/g) ?? []).length !== 2) {
    issues.push("Both MasterPanel Maximizer plots must retain their six-second window.");
  }
  if (!/refLine=\{tgt\}/.test(maxView) || !/refText=\{`TARGET \$\{tgt\} LUFS`\}/.test(maxView)) {
    issues.push("MasterPanel LUFS plot must retain its moving LUFS target line and label.");
  }
  if (!/y=\{\[-30,\s*-4\]\}/.test(maxView)) {
    issues.push("MasterPanel LUFS plot must retain its -30 to -4 LUFS scale.");
  }
  if (!/y=\{\[-12,\s*0\]\}/.test(maxView)) {
    issues.push("MasterPanel gain-reduction plot must retain its -12 to 0 dB scale.");
  }

  return issues;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  const source = readFileSync(panelPath, "utf8");
  const issues = validateMasterPanel(source);
  if (issues.length) {
    console.error(`${panelPath} is missing the local Maximizer uPlot override:`);
    for (const issue of issues) console.error(`- ${issue}`);
    console.error("Re-apply the uPlot MaxView wiring after console-ui sync.");
    process.exit(1);
  }

  console.log("MasterPanel retains both local uPlot Maximizer plots and its sync note.");
}
