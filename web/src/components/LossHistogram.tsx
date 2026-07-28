"use client";

import { useMemo } from "react";
import { useSnapshotStore } from "@/stores/snapshotStore";
import { snapshotRef } from "@/stores/snapshotRef";
import { fmtCurrency } from "@/lib/viz";

// The Monte-Carlo systemic-loss distribution — the sampled histogram VaR and ES
// are literally read off of. Bars use a sqrt height scale so the deep tail stays
// visible next to the near-zero mode; everything past VaR is the "worst 2.5%"
// tail whose average is ES.
export default function LossHistogram() {
  // Re-render on each decoded frame; hot arrays are read from snapshotRef.
  const frameId = useSnapshotStore((s) => s.frameId);
  const hasData = useSnapshotStore((s) => s.hasData);

  const view = useMemo(() => {
    const snap = snapshotRef.current;
    if (!snap || !snap.lossHist || snap.lossHist.length === 0) return null;
    const bins = snap.lossHist;
    const n = bins.length;
    const histMax = snap.histMax > 0 ? snap.histMax : 1;

    let peak = 0;
    for (let i = 0; i < n; i++) if (bins[i] > peak) peak = bins[i];
    const norm = peak > 0 ? peak : 1;

    const W = 100;
    const H = 34;
    const bw = W / n;
    const bars = new Array<string>(n);
    for (let i = 0; i < n; i++) {
      const h = Math.sqrt(bins[i] / norm) * H; // sqrt reveals the tail
      const x = i * bw;
      const inTail = (i / n) * histMax >= snap.var && snap.var > 0;
      bars[i] = `${x.toFixed(3)},${(H - h).toFixed(3)},${bw.toFixed(3)},${h.toFixed(3)},${inTail ? 1 : 0}`;
    }
    const varX = (Math.min(snap.var, histMax) / histMax) * W;
    const esX = (Math.min(snap.es, histMax) / histMax) * W;
    return { bars, W, H, varX, esX, histMax, var: snap.var, es: snap.es };
  }, [frameId]);

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="tt-label">loss distribution</span>
        <span className="tt-label" style={{ color: "var(--ink-muted)" }}>
          monte&nbsp;carlo · {frameId > 0 ? "6k paths" : "—"}
        </span>
      </div>
      <div className="px-3 pb-2.5 pt-2">
        {view && hasData ? (
          <>
            <svg
              viewBox={`0 0 ${view.W} ${view.H}`}
              preserveAspectRatio="none"
              className="h-[64px] w-full"
              style={{ display: "block" }}
            >
              {/* baseline */}
              <line
                x1={0}
                y1={view.H}
                x2={view.W}
                y2={view.H}
                stroke="var(--border-bright)"
                strokeWidth={0.3}
              />
              {view.bars.map((b, i) => {
                const [x, y, w, h, tail] = b.split(",");
                return (
                  <rect
                    key={i}
                    x={x}
                    y={y}
                    width={Math.max(0, parseFloat(w) - 0.12)}
                    height={h}
                    fill={tail === "1" ? "var(--red)" : "var(--cyan)"}
                    opacity={tail === "1" ? 0.85 : 0.5}
                  />
                );
              })}
              {/* VaR line */}
              {view.var > 0 && (
                <line
                  x1={view.varX}
                  y1={0}
                  x2={view.varX}
                  y2={view.H}
                  stroke="var(--amber)"
                  strokeWidth={0.5}
                  strokeDasharray="1.2 1"
                />
              )}
              {/* ES line */}
              {view.es > 0 && (
                <line
                  x1={view.esX}
                  y1={0}
                  x2={view.esX}
                  y2={view.H}
                  stroke="var(--red)"
                  strokeWidth={0.5}
                  strokeDasharray="1.2 1"
                />
              )}
            </svg>
            <div className="mt-1.5 flex items-center justify-between text-[10px]">
              <span className="text-ink-muted">$0</span>
              <span className="flex items-center gap-3">
                <span style={{ color: "var(--amber)" }}>
                  ▏VaR {fmtCurrency(view.var)}
                </span>
                <span style={{ color: "var(--red)" }}>
                  ▏ES {fmtCurrency(view.es)}
                </span>
              </span>
              <span className="text-ink-muted">{fmtCurrency(view.histMax)}</span>
            </div>
            <div className="mt-1 text-[10px] leading-tight text-ink-muted">
              simulated system losses over {frameId > 0 ? "6,000" : "—"} correlated
              scenarios. <span style={{ color: "var(--red)" }}>Red</span> bars are
              the worst-2.5% tail; ES is their average.
            </div>
          </>
        ) : (
          <div className="py-4 text-center text-[11px] text-ink-muted">
            awaiting distribution…
          </div>
        )}
      </div>
    </div>
  );
}
