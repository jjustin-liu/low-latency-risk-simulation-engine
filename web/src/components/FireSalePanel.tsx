"use client";

import { useMemo } from "react";
import { useSnapshotStore } from "@/stores/snapshotStore";
import { snapshotRef } from "@/stores/snapshotRef";

// The second contagion channel: price-mediated fire sales. Distressed banks
// deleverage by market-selling shared assets into a finite-depth limit-order
// book; the resulting slippage marks the asset down for everyone. This panel
// shows the emergent price impact per asset and how far each sell-off has eaten
// into the resting bid ladder (the real matching engine's depth).
const LADDER_LEVELS = 18;

function impactColor(x: number): string {
  if (x < 0.03) return "var(--green)";
  if (x < 0.12) return "var(--amber)";
  return "var(--red)";
}

export default function FireSalePanel() {
  const frameId = useSnapshotStore((s) => s.frameId);
  const hasData = useSnapshotStore((s) => s.hasData);

  const view = useMemo(() => {
    const snap = snapshotRef.current;
    if (!snap || !snap.assetImpact || snap.assetImpact.length === 0) return null;
    const n = snap.assetImpact.length;
    const assets = new Array(n);
    let worst = 0;
    for (let i = 0; i < n; i++) {
      assets[i] = { id: i, impact: snap.assetImpact[i], depth: snap.assetDepth[i] };
      if (snap.assetImpact[i] > snap.assetImpact[worst]) worst = i;
    }
    return { assets, worst };
  }, [frameId]);

  const worst = view?.assets[view.worst];
  const consumedLevels = worst
    ? Math.round((1 - worst.depth) * LADDER_LEVELS)
    : 0;

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="tt-label">fire-sale · order book</span>
        <span className="tt-label" style={{ color: "var(--ink-muted)" }}>
          price-mediated
        </span>
      </div>
      <div className="px-3 pb-2.5 pt-2">
        {view && hasData ? (
          <>
            {/* Per-asset price impact + remaining depth */}
            <div className="flex flex-col gap-1.5">
              {view.assets.map((a) => (
                <div key={a.id} className="flex items-center gap-2">
                  <span className="tt-label w-12 shrink-0" style={{ letterSpacing: ".08em" }}>
                    asset {a.id}
                  </span>
                  <div className="relative h-2.5 flex-1 overflow-hidden rounded-sm bg-[var(--surface)]">
                    {/* remaining resting depth (cool) */}
                    <div
                      className="absolute inset-y-0 left-0 rounded-sm"
                      style={{
                        width: `${Math.max(2, a.depth * 100)}%`,
                        background: "color-mix(in srgb, var(--cyan) 35%, transparent)",
                      }}
                    />
                    {/* price impact (warm), grows from left */}
                    <div
                      className="absolute inset-y-0 left-0 rounded-sm"
                      style={{
                        width: `${Math.min(100, a.impact * 100)}%`,
                        background: impactColor(a.impact),
                        opacity: 0.9,
                      }}
                    />
                  </div>
                  <span
                    className="tt-mono-num w-12 shrink-0 text-right text-[11px]"
                    style={{ color: impactColor(a.impact) }}
                  >
                    −{(a.impact * 100).toFixed(0)}%
                  </span>
                </div>
              ))}
            </div>

            {/* Representative order book: the worst-hit asset's bid ladder */}
            <div className="mt-2.5 border-t border-border pt-2">
              <div className="mb-1 flex items-center justify-between">
                <span className="tt-label">book depth · asset {view.worst}</span>
                <span className="tt-mono-num text-[10px] text-ink-muted">
                  {worst ? (worst.depth * 100).toFixed(0) : "100"}% resting
                </span>
              </div>
              <div className="flex items-end gap-[2px]" style={{ height: 28 }}>
                {Array.from({ length: LADDER_LEVELS }).map((_, i) => {
                  // Top of the book (i=0) is consumed first as price falls.
                  const eaten = i < consumedLevels;
                  // Resting ladder tapers slightly for a book-like silhouette.
                  const h = 40 + (1 - i / LADDER_LEVELS) * 60;
                  return (
                    <div
                      key={i}
                      className="flex-1 rounded-[1px]"
                      style={{
                        height: `${h}%`,
                        background: eaten ? "var(--red)" : "var(--cyan)",
                        opacity: eaten ? 0.28 : 0.55,
                      }}
                    />
                  );
                })}
              </div>
              <div className="mt-1 flex items-center justify-between text-[9px] text-ink-muted">
                <span>best bid → </span>
                <span>← deep liquidity</span>
              </div>
            </div>

            <div className="mt-2 text-[10px] leading-tight text-ink-muted">
              distressed banks sell shared holdings into the book; consumed levels
              (<span style={{ color: "var(--red)" }}>faded</span>) push the price
              down for every holder — a feedback loop independent of the loan
              network.
            </div>
          </>
        ) : (
          <div className="py-4 text-center text-[11px] text-ink-muted">
            books calm — no forced selling
          </div>
        )}
      </div>
    </div>
  );
}
