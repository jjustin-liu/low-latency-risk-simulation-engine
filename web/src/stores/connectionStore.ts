import { create } from "zustand";
import type { ConnectionStatus } from "@/lib/types";

interface ConnectionState {
  status: ConnectionStatus;
  url: string;
  framesReceived: number;
  fps: number;
  lastDecodeMs: number;
  latencyMs: number;
  error: string | null;
  setStatus: (status: ConnectionStatus, error?: string | null) => void;
  setUrl: (url: string) => void;
  recordFrame: (decodeMs: number, latencyMs: number) => void;
  setFps: (fps: number) => void;
}

export const useConnectionStore = create<ConnectionState>((set) => ({
  status: "idle",
  url: "",
  framesReceived: 0,
  fps: 0,
  lastDecodeMs: 0,
  latencyMs: 0,
  error: null,
  setStatus: (status, error = null) => set({ status, error }),
  setUrl: (url) => set({ url }),
  recordFrame: (decodeMs, latencyMs) =>
    set((s) => ({
      framesReceived: s.framesReceived + 1,
      lastDecodeMs: decodeMs,
      latencyMs,
    })),
  setFps: (fps) => set({ fps }),
}));
