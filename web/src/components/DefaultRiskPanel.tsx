"use client";

import { useMemo } from "react";
import { useSnapshotStore } from "@/stores/snapshotStore";
import { snapshotRef } from "@/stores/snapshotRef";
import { isCore } from "@/lib/viz";

// Per-bank Merton probability of default: PD = Φ(−distance-to-default), where the
// distance shrinks as the tick's contagion erodes a bank's equity. This ranks the
// banks closest to failure right now — the leading edge of the cascade.
const TOP_N = 6;

function pdColor(pd: number): string {
  if (pd < 0.05) return "var(--green)";
  if (pd < 0.2) return "var(--amber)";
  return "var(--red)";
}

export default function DefaultRiskPanel() {
  const frameId = useSnapshotStore((s) => s.frameId);
  const hasData = useSnapshotStore((s) => s.hasData);

  const view = useMemo(() => {
    const snap = snapshotRef.current;
    if (!snap || !snap.nodePd || snap.nodePd.length === 0) return null;
    const n = snap.nodePd.length;
    const idx = Array.from({ length: n }, (_, i) => i);
    idx.sort((a, b) => snap.nodePd[b] - snap.nodePd[a]);
    const top = idx.slice(0, TOP_N).map((i) => ({
      id: i,
      pd: snap.nodePd[i],
      core: isCore(snap.nodeX[i], snap.nodeY[i]),
    }));
    let sum = 0;
    for (let i = 0; i < n; i++) sum += snap.nodePd[i];
    return { top, avg: sum / n };
  }, [frameId]);

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="tt-label">default risk · merton</span>
        <span className="tt-mono-num text-[10px] text-ink-muted">
          avg&nbsp;
          <span style={{ color: view ? pdColor(view.avg) : "var(--ink-muted)" }}>
            {view ? (view.avg * 100).toFixed(1) : "—"}%
          </span>
        </span>
      </div>
      <div className="px-3 pb-2.5 pt-2">
        {view && hasData ? (
          <>
            <div className="flex flex-col gap-1">
              {view.top.map((b) => (
                <div key={b.id} className="flex items-center gap-2">
                  <span className="tt-mono-num w-14 shrink-0 text-[11px]">
                    bank&nbsp;#{b.id}
                  </span>
                  <span
                    className="w-[52px] shrink-0 text-[9px]"
                    style={{ color: b.core ? "var(--cyan)" : "var(--ink-muted)" }}
                  >
                    {b.core ? "CORE" : "PERIPH"}
                  </span>
                  <div className="relative h-2 flex-1 overflow-hidden rounded-sm bg-[var(--surface)]">
                    <div
                      className="absolute inset-y-0 left-0 rounded-sm"
                      style={{ width: `${Math.min(100, b.pd * 100)}%`, background: pdColor(b.pd) }}
                    />
                  </div>
                  <span
                    className="tt-mono-num w-10 shrink-0 text-right text-[11px]"
                    style={{ color: pdColor(b.pd) }}
                  >
                    {(b.pd * 100).toFixed(0)}%
                  </span>
                </div>
              ))}
            </div>
            <div className="mt-2 text-[10px] leading-tight text-ink-muted">
              1-year probability of default from each bank&apos;s distance-to-default.
              Rises as equity is impaired — a bank hits ~50% when its assets reach
              the default point.
            </div>
          </>
        ) : (
          <div className="py-4 text-center text-[11px] text-ink-muted">
            awaiting balance sheets…
          </div>
        )}
      </div>
    </div>
  );
}
