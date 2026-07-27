import type { NextConfig } from "next";

// deck.gl / WebSocket are client-only; guarded via dynamic import + effects.
// Type safety is enforced separately via `tsc --noEmit`; no eslint config here
// (there is no eslint setup, so `next build` does not lint).
const nextConfig: NextConfig = {};

export default nextConfig;
