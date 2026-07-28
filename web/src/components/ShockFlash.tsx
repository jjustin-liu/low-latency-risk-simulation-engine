"use client";

import { useEffect, useRef, useState } from "react";
import { useControlsStore } from "@/stores/controlsStore";

/**
 * Transient visual feedback when the operator dispatches a shock: a red edge
 * flash over the canvas + a banner, so cause→effect is obvious. Keyed off the
 * controls store's lastAction (only successful dispatches, marked with "→").
 */
export default function ShockFlash() {
  const lastAction = useControlsStore((s) => s.lastAction);
  const [flash, setFlash] = useState<{ id: number; text: string } | null>(null);
  const prev = useRef<string | null>(null);
  const idRef = useRef(0);

  useEffect(() => {
    if (!lastAction || lastAction === prev.current) return;
    prev.current = lastAction;
    // Only flash on a dispatched action (not the "⚠ dropped" case).
    if (!lastAction.startsWith("→")) return;
    idRef.current += 1;
    setFlash({ id: idRef.current, text: lastAction.replace(/^→\s*/, "") });
    const myId = idRef.current;
    const t = setTimeout(() => {
      setFlash((f) => (f && f.id === myId ? null : f));
    }, 2600);
    return () => clearTimeout(t);
  }, [lastAction]);

  if (!flash) return null;

  return (
    <div key={flash.id} className="pointer-events-none absolute inset-0 z-20">
      <div className="shock-vignette absolute inset-0" />
      <div className="absolute left-1/2 top-4 -translate-x-1/2">
        <div
          className="shock-banner flex items-center gap-2 rounded border px-3 py-1.5"
          style={{
            background: "color-mix(in srgb, var(--red) 16%, var(--surface))",
            borderColor: "var(--red)",
          }}
        >
          <span>⚡</span>
          <span
            className="text-[11px] font-semibold uppercase tracking-wider"
            style={{ color: "var(--red)" }}
          >
            Shock applied
          </span>
          <span className="tt-mono-num text-[11px] text-ink">· {flash.text}</span>
        </div>
      </div>
    </div>
  );
}
