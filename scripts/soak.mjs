#!/usr/bin/env node

import { spawn } from "node:child_process";
import fs from "node:fs";
import path from "node:path";

function parseArgs(args) {
  let minutes = 1;
  let out = null;

  for (let i = 0; i < args.length; ++i) {
    if (args[i] === "--minutes" && i + 1 < args.length) {
      minutes = parseFloat(args[++i]);
    } else if (args[i] === "--out" && i + 1 < args.length) {
      out = args[++i];
    }
  }

  return { minutes, out };
}

const { minutes, out } = parseArgs(process.argv.slice(2));

const repoRoot = path.resolve(import.meta.dirname, "..");
const testBinary = path.resolve(
  repoRoot,
  "build/app/NCMixRackTests_artefacts/Release/NCMixRackTests.exe"
);

if (!fs.existsSync(testBinary)) {
  console.error(`Soak test binary not found at: ${testBinary}`);
  process.exit(1);
}

const env = {
  ...process.env,
  NC_SOAK_MINUTES: String(minutes),
};

console.log(`Starting soak run for ${minutes} minute(s)...`);

const child = spawn(testBinary, ["[soak]", "-s"], {
  cwd: repoRoot,
  env,
  stdio: ["inherit", "pipe", "pipe"],
});

let stdout = "";
let stderr = "";

child.stdout.on("data", (chunk) => {
  const str = chunk.toString();
  stdout += str;
  process.stdout.write(str);
});

child.stderr.on("data", (chunk) => {
  const str = chunk.toString();
  stderr += str;
  process.stderr.write(str);
});

child.on("close", (code) => {
  const fullLog = stdout + "\n" + stderr;
  const blocks = Math.round((minutes * 60.0 * 48000.0) / 512.0);

  let report = `=== SOAK TEST REPORT ===\n`;
  report += `Date: ${new Date().toISOString()}\n`;
  report += `Duration: ${minutes} minute(s) (${blocks} blocks rendered @ 48kHz / 512 samples)\n`;
  report += `Exit code: ${code}\n`;
  report += `no hosted plugins exercised (synthetic inserts only)\n\n`;
  report += `Raw output:\n${fullLog}\n`;

  if (out) {
    const outPath = path.resolve(repoRoot, out);
    fs.mkdirSync(path.dirname(outPath), { recursive: true });
    fs.writeFileSync(outPath, report, "utf8");
    console.log(`Soak report written to: ${outPath}`);
  }

  if (code !== 0) {
    console.error(`Soak test failed with exit code ${code}`);
    process.exit(code || 1);
  }

  console.log(`Soak run completed successfully.`);
  process.exit(0);
});
