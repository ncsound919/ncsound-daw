#!/usr/bin/env node

import fs from "node:fs";
import path from "node:path";

const repoRoot = path.resolve(import.meta.dirname, "..");
const pinFile = path.resolve(repoRoot, "third_party/JUCE_PIN.txt");
const noticesFile = path.resolve(repoRoot, "THIRD-PARTY-NOTICES.md");

if (!fs.existsSync(pinFile)) {
  console.error(`JUCE_PIN.txt not found at ${pinFile}`);
  process.exit(1);
}

if (!fs.existsSync(noticesFile)) {
  console.error(`THIRD-PARTY-NOTICES.md not found at ${noticesFile}`);
  process.exit(1);
}

const pinContent = fs.readFileSync(pinFile, "utf8");
const noticesContent = fs.readFileSync(noticesFile, "utf8");

// 1. Extract tag from JUCE_PIN.txt
const tagMatch = pinContent.match(/^tag:\s*([^\r\n]+)/m);
if (!tagMatch) {
  console.error("Could not parse 'tag:' line from third_party/JUCE_PIN.txt");
  process.exit(1);
}
const pinnedTag = tagMatch[1].trim();

// 2. Verify tag appears in notices
if (!noticesContent.includes(pinnedTag)) {
  console.error(
    `JUCE version drift: tag '${pinnedTag}' in JUCE_PIN.txt is not mentioned in THIRD-PARTY-NOTICES.md`
  );
  process.exit(1);
}

// 3. Verify all directory entries under third_party/ are mentioned
const thirdPartyDir = path.resolve(repoRoot, "third_party");
const entries = fs.readdirSync(thirdPartyDir, { withFileTypes: true });
for (const entry of entries) {
  if (entry.isDirectory()) {
    if (!noticesContent.toLowerCase().includes(entry.name.toLowerCase())) {
      console.error(
        `Missing third_party notice: '${entry.name}' is not mentioned in THIRD-PARTY-NOTICES.md`
      );
      process.exit(1);
    }
  }
}

console.log("Legal notices check passed: JUCE tag and third_party components verified.");
process.exit(0);
