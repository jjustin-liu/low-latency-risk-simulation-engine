"use client";

import dynamic from "next/dynamic";
import { useBridge } from "@/lib/useBridge";
import { useControlsStore } from "@/stores/controlsStore";
import StatusHud from "./StatusHud";
import VarPanel from "./VarPanel";
import ShockControls from "./ShockControls";
import ShockConsole from "./ShockConsole";

// deck.gl + WebGL are client-only — never render on the server.
const NetworkView = dynamic(() => import("./NetworkView"), {
  ssr: false,
  loading: () => (
    <div className="flex h-full w-full items-center justify-center">
      <span className="tt-label">initializing renderer…</span>
    </div>
  ),
});

export default function Dashboard() {
  useBridge();
  const lastAction = useControlsStore((s) => s.lastAction);

  return (
    <div className="flex h-screen w-screen flex-col overflow-hidden">
      {/* Masthead */}
      <header className="flex items-center justify-between border-b border-border px-4 py-2">
        <div className="flex items-baseline gap-3">
          <span
            className="text-sm font-semibold tracking-[0.2em]"
            style={{ color: "var(--ink)" }}
          >
            OPTI<span style={{ color: "var(--cyan)" }}>RISK</span>
          </span>
          <span className="tt-label hidden sm:inline">
            interbank default-contagion · real-time
          </span>
        </div>
        <span className="tt-label">systemic-risk console</span>
      </header>

      {/* Main: canvas + side rail */}
      <div className="relative flex min-h-0 flex-1 flex-col lg:flex-row">
        {/* Network canvas fills the main area */}
        <main className="relative min-h-[45vh] flex-1 border-b border-border lg:border-b-0 lg:border-r">
          <NetworkView />

          {/* Overlay HUD (top-left) */}
          <div className="pointer-events-none absolute left-3 top-3 w-[300px] max-w-[calc(100%-1.5rem)]">
            <div className="pointer-events-auto">
              <StatusHud />
            </div>
          </div>

          {/* Legend (bottom-left) */}
          <div className="pointer-events-none absolute bottom-3 left-3 panel px-3 py-2">
            <div className="tt-label mb-1.5">Node health</div>
            <div className="flex items-center gap-2">
              <div
                className="h-2 w-24 rounded"
                style={{
                  background:
                    "linear-gradient(90deg, var(--red), var(--amber), var(--green))",
                }}
              />
              <div className="flex w-24 justify-between text-[9px] text-ink-muted">
                <span>default</span>
                <span>healthy</span>
              </div>
            </div>
          </div>
        </main>

        {/* Side rail: panels */}
        <aside className="flex w-full shrink-0 flex-col gap-3 overflow-y-auto p-3 lg:w-[360px]">
          <VarPanel />
          <ShockControls />
          <ShockConsole />
        </aside>
      </div>

      {/* Ticker */}
      <footer className="flex items-center gap-2 border-t border-border px-4 py-1.5">
        <span className="tt-label">last action</span>
        <span
          className="tt-mono-num truncate text-[11px]"
          style={{ color: lastAction ? "var(--green)" : "var(--ink-muted)" }}
        >
          {lastAction ?? "— no actions dispatched —"}
        </span>
      </footer>
    </div>
  );
}
