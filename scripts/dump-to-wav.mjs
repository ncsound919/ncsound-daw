// Wraps the engine's raw interleaved float32 stereo capture in a WAV header so
// it can be measured or described by an audio tool.
import { readFileSync, writeFileSync } from "node:fs";

const [, , input, output = "build/capture.wav", rateArg = "48000"] = process.argv;
if (!input) {
  console.error("usage: node scripts/dump-to-wav.mjs <dump.f32> [out.wav] [rate]");
  process.exit(2);
}

const rate = Number(rateArg);
const pcm = readFileSync(input);
const frames = Math.floor(pcm.byteLength / 8);

const header = Buffer.alloc(44);
header.write("RIFF", 0);
header.writeUInt32LE(36 + pcm.byteLength, 4);
header.write("WAVE", 8);
header.write("fmt ", 12);
header.writeUInt32LE(16, 16);
header.writeUInt16LE(3, 20); // IEEE float
header.writeUInt16LE(2, 22); // stereo
header.writeUInt32LE(rate, 24);
header.writeUInt32LE(rate * 8, 28);
header.writeUInt16LE(8, 32);
header.writeUInt16LE(32, 34);
header.write("data", 36);
header.writeUInt32LE(pcm.byteLength, 40);

writeFileSync(output, Buffer.concat([header, pcm]));
console.log(`${output}: ${frames} frames @ ${rate} Hz (${(frames / rate).toFixed(2)} s)`);
