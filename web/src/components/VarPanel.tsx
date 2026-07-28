"use client";

import { useSnapshotStore } from "@/stores/snapshotStore";
import { fmtCurrency, stressLevel } from "@/lib/viz";
import Sparkline from "./Sparkline";

function StatTile({
  label,
  value,
  sub,
  accent,
  emphasis = false,
  delta,
}: {
  label: string;
  value: string;
  sub: string;
  accent: string;
  emphasis?: boolean;
  delta?: { up: boolean; text: string } | null;
}) {
  return (
    <div
      className="rounded border border-border px-3 py-2"
      style={{ background: "var(--surface-2)" }}
    >
      <div className="mb-1 flex items-center justify-between">
        <div className="tt-label flex items-center gap-1.5">
          <span
            className="inline-block h-1.5 w-1.5 rounded-full"
            style={{ background: accent }}
          />
          {label}
        </div>
        {delta && (
          <span
            className="tt-mono-num text-[10px]"
            style={{ color: delta.up ? "var(--red)" : "var(--green)" }}
          >
            {delta.up ? "▲" : "▼"} {delta.text}
          </span>
        )}
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
      <div className="mt-1 text-[10px] leading-tight text-ink-muted">{sub}</div>
    </div>
  );
}

export default function VarPanel() {
  const varVal = useSnapshotStore((s) => s.var);
  const es = useSnapshotStore((s) => s.es);
  const numDefaults = useSnapshotStore((s) => s.numDefaults);
  const totalLoss = useSnapshotStore((s) => s.totalLoss);
  const nNodes = useSnapshotStore((s) => s.nNodes);
  const meanHealth = useSnapshotStore((s) => s.meanHealth);
  const esHistory = useSnapshotStore((s) => s.esHistory);

  const status = stressLevel(meanHealth, numDefaults);

  // ES change vs ~12 ticks ago, so you can see a shock bite.
  let esDelta: { up: boolean; text: string } | null = null;
  if (esHistory.length >= 13) {
    const prev = esHistory[esHistory.length - 13];
    const d = es - prev;
    if (Math.abs(d) > Math.max(1, Math.abs(prev) * 0.01)) {
      esDelta = { up: d > 0, text: fmtCurrency(Math.abs(d)) };
    }
  }

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="tt-label">Systemic Risk</span>
        <span className="tt-label" title="Losses beyond the worst 2.5% of scenarios">
          97.5% confidence
        </span>
      </div>

      <div className="p-3">
        {/* Headline status */}
        <div
          className="mb-3 flex items-center justify-between rounded border px-3 py-2"
          style={{
            borderColor: status.color,
            background: `color-mix(in srgb, ${status.color} 10%, var(--surface-2))`,
          }}
        >
          <div>
            <div className="tt-label">System status</div>
            <div
              className={`text-lg font-semibold leading-none tracking-wide ${status.rank >= 3 ? "blink" : ""}`}
              style={{ color: status.color }}
            >
              {status.label}
            </div>
            <div className="mt-0.5 text-[10px] text-ink-muted">{status.hint}</div>
          </div>
          <div className="text-right">
            <div
              className="tt-mono-num text-2xl leading-none"
              style={{ color: numDefaults > 0 ? "var(--red)" : "var(--green)" }}
            >
              {numDefaults}
              <span className="text-sm text-ink-muted">/{nNodes || "—"}</span>
            </div>
            <div className="tt-label mt-0.5">banks failed</div>
          </div>
        </div>

        <div className="grid grid-cols-2 gap-2">
          <StatTile
            label="Expected Shortfall"
            value={fmtCurrency(es)}
            sub="average loss across the worst 2.5% of outcomes — the headline risk number"
            accent="var(--amber)"
            emphasis
            delta={esDelta}
          />
          <StatTile
            label="Value at Risk"
            value={fmtCurrency(varVal)}
            sub="the loss you'd expect to exceed only 1 day in 40"
            accent="var(--cyan)"
          />
          <StatTile
            label="Loss this tick"
            value={fmtCurrency(totalLoss)}
            sub="value destroyed across the network right now"
            accent="var(--red)"
          />
          <StatTile
            label="Network health"
            value={`${Math.round(meanHealth * 100)}%`}
            sub="average bank health (100% = everyone solvent)"
            accent="var(--green)"
          />
        </div>

        <div className="mt-3">
          <div className="mb-1 flex items-center justify-between">
            <span className="tt-label">Expected Shortfall · recent ticks</span>
            <span className="tt-label">{esHistory.length}t</span>
          </div>
          <Sparkline data={esHistory} color="var(--amber)" />
        </div>

        <div className="mt-2 text-[9px] leading-tight text-ink-muted">
          Figures are in the model's currency units (a synthetic calibrated network),
          not real dollars.
        </div>
      </div>
    </div>
  );
}
