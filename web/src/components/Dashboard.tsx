"use client";

import dynamic from "next/dynamic";
import { useBridge } from "@/lib/useBridge";
import { useControlsStore } from "@/stores/controlsStore";
import StatusHud from "./StatusHud";
import VarPanel from "./VarPanel";
import LossHistogram from "./LossHistogram";
import DefaultRiskPanel from "./DefaultRiskPanel";
import FireSalePanel from "./FireSalePanel";
import ShockControls from "./ShockControls";
import ShockConsole from "./ShockConsole";
import Onboarding from "./Onboarding";
import ShockFlash from "./ShockFlash";

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
            RISK<span style={{ color: "var(--cyan)" }}>SIM</span>
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

          {/* Shock feedback + onboarding overlays */}
          <ShockFlash />
          <Onboarding />

          {/* Overlay HUD (top-left) */}
          <div className="pointer-events-none absolute left-3 top-3 w-[300px] max-w-[calc(100%-1.5rem)]">
            <div className="pointer-events-auto">
              <StatusHud />
            </div>
          </div>

          {/* Map key (bottom-left) */}
          <div className="pointer-events-none absolute bottom-3 left-3 panel px-3 py-2.5">
            <div className="tt-label mb-2">Map key</div>
            <div className="flex flex-col gap-1.5 text-[10px] text-ink-dim">
              <div className="flex items-center gap-2">
                <div
                  className="h-2 w-16 rounded"
                  style={{
                    background:
                      "linear-gradient(90deg, var(--red), var(--amber), var(--green))",
                  }}
                />
                <span>
                  bank health — <span style={{ color: "var(--red)" }}>failed</span> →{" "}
                  <span style={{ color: "var(--green)" }}>healthy</span>
                </span>
              </div>
              <div className="flex items-center gap-2">
                <span
                  className="inline-block h-3 w-3 shrink-0 rounded-full"
                  style={{ border: "1.5px solid var(--cyan)", background: "var(--surface-3)" }}
                />
                <span>
                  <span style={{ color: "var(--cyan)" }}>cyan ring</span> = core bank
                  (systemically important)
                </span>
              </div>
              <div className="flex items-center gap-2">
                <span className="inline-flex w-3 shrink-0 items-center justify-center gap-0.5">
                  <span className="h-1.5 w-1.5 rounded-full bg-ink-muted" />
                  <span className="h-2.5 w-2.5 rounded-full bg-ink-dim" />
                </span>
                <span>dot size = bank&apos;s total assets</span>
              </div>
              <div className="flex items-center gap-2">
                <span
                  className="inline-block h-px w-3 shrink-0"
                  style={{ background: "var(--cyan)" }}
                />
                <span>line = a loan / exposure between two banks</span>
              </div>
            </div>
          </div>
        </main>

        {/* Side rail: panels */}
        <aside className="flex w-full shrink-0 flex-col gap-3 overflow-y-auto p-3 lg:w-[360px]">
          <VarPanel />
          <LossHistogram />
          <DefaultRiskPanel />
          <FireSalePanel />
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
