import { defineConfig } from "vitest/config";
import react from "@vitejs/plugin-react";

// Separate from vite.config.ts so the production bundle config stays untouched.
export default defineConfig({
  plugins: [react()],
  test: {
    environment: "jsdom",
    setupFiles: ["./src/test/setup.ts"],
    globals: false,
    restoreMocks: true,
    include: ["src/**/*.test.{ts,tsx}"],
    pool: "threads",
    poolOptions: { threads: { singleThread: true } },
  },
});
