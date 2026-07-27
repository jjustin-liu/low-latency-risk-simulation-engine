"use client";

import { useSnapshotStore } from "@/stores/snapshotStore";
import { fmtCurrency, fmtInt } from "@/lib/viz";
import Sparkline from "./Sparkline";

function StatTile({
  label,
  value,
  accent,
  emphasis = false,
}: {
  label: string;
  value: string;
  accent: string;
  emphasis?: boolean;
}) {
  return (
    <div
      className="rounded border border-border px-3 py-2"
      style={{ background: "var(--surface-2)" }}
    >
      <div className="tt-label mb-1 flex items-center gap-1.5">
        <span
          className="inline-block h-1.5 w-1.5 rounded-full"
          style={{ background: accent }}
        />
        {label}
      </div>
      <div
        className="tt-mono-num leading-none"
        style={{
          fontSize: emphasis ? 24 : 18,
          color: emphasis ? accent : "var(--ink)",
        }}
      >
        {value}
      </div>
    </div>
  );
}

export default function VarPanel() {
  const varVal = useSnapshotStore((s) => s.var);
  const es = useSnapshotStore((s) => s.es);
  const numDefaults = useSnapshotStore((s) => s.numDefaults);
  const totalLoss = useSnapshotStore((s) => s.totalLoss);
  const esHistory = useSnapshotStore((s) => s.esHistory);

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="tt-label">Systemic Risk · VaR / ES</span>
        <span className="tt-label" style={{ color: "var(--amber)" }}>
          97.5%
        </span>
      </div>

      <div className="p-3">
        <div className="grid grid-cols-2 gap-2">
          <StatTile
            label="Value at Risk"
            value={fmtCurrency(varVal)}
            accent="var(--cyan)"
          />
          <StatTile
            label="Expected Shortfall"
            value={fmtCurrency(es)}
            accent="var(--amber)"
            emphasis
          />
          <StatTile
            label="Defaults"
            value={fmtInt(numDefaults)}
            accent={numDefaults > 0 ? "var(--red)" : "var(--green)"}
          />
          <StatTile
            label="Total Loss"
            value={fmtCurrency(totalLoss)}
            accent="var(--red)"
          />
        </div>

        <div className="mt-3">
          <div className="mb-1 flex items-center justify-between">
            <span className="tt-label">Expected Shortfall · recent ticks</span>
            <span className="tt-label">{esHistory.length}t</span>
          </div>
          <Sparkline data={esHistory} color="var(--amber)" />
        </div>
      </div>
    </div>
  );
}
