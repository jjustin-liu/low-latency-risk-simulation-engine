"use client";

import { useEffect } from "react";
import { bridge } from "@/lib/bridge";
import { useConnectionStore } from "@/stores/connectionStore";

const DEFAULT_URL = "ws://localhost:8080";

/**
 * Mounts the bridge connection + a 1 Hz client-side frames/sec meter.
 * Call once, from the top-level dashboard client component.
 */
export function useBridge(): void {
  useEffect(() => {
    const url = process.env.NEXT_PUBLIC_BRIDGE_URL || DEFAULT_URL;
    bridge.connect(url);

    let prevCount = useConnectionStore.getState().framesReceived;
    const fpsTimer = setInterval(() => {
      const now = useConnectionStore.getState().framesReceived;
      useConnectionStore.getState().setFps(now - prevCount);
      prevCount = now;
    }, 1000);

    return () => {
      clearInterval(fpsTimer);
      bridge.disconnect();
    };
  }, []);
}
