"use client";

import { useConnectionStore } from "@/stores/connectionStore";
import { useSnapshotStore } from "@/stores/snapshotStore";
import { fmtInt, fmtNum } from "@/lib/viz";

const STATUS_META: Record<string, { label: string; color: string }> = {
  idle: { label: "IDLE", color: "var(--ink-muted)" },
  connecting: { label: "LINKING", color: "var(--amber)" },
  open: { label: "LIVE", color: "var(--green)" },
  closed: { label: "CLOSED", color: "var(--ink-muted)" },
  error: { label: "ERROR", color: "var(--red)" },
};

function Metric({
  label,
  value,
  unit,
}: {
  label: string;
  value: string;
  unit?: string;
}) {
  return (
    <div className="flex flex-col">
      <span className="tt-label">{label}</span>
      <span className="tt-mono-num text-sm text-ink">
        {value}
        {unit && <span className="ml-0.5 text-ink-muted text-[10px]">{unit}</span>}
      </span>
    </div>
  );
}

export default function StatusHud() {
  const status = useConnectionStore((s) => s.status);
  const url = useConnectionStore((s) => s.url);
  const fps = useConnectionStore((s) => s.fps);
  const decodeMs = useConnectionStore((s) => s.lastDecodeMs);
  const framesReceived = useConnectionStore((s) => s.framesReceived);
  const tickSeq = useSnapshotStore((s) => s.tickSeq);
  const nNodes = useSnapshotStore((s) => s.nNodes);
  const nEdges = useSnapshotStore((s) => s.nEdges);

  const meta = STATUS_META[status] ?? STATUS_META.idle;

  return (
    <div className="panel px-3 py-2">
      <div className="mb-2 flex items-center justify-between">
        <div className="flex items-center gap-2">
          <span
            className={`inline-block h-2 w-2 rounded-full ${
              status === "open" ? "blink" : ""
            }`}
            style={{ background: meta.color, boxShadow: `0 0 6px ${meta.color}` }}
          />
          <span
            className="text-xs font-medium tracking-wider"
            style={{ color: meta.color }}
          >
            {meta.label}
          </span>
        </div>
        <span className="tt-label truncate max-w-[160px]" title={url}>
          {url || "—"}
        </span>
      </div>

      <div className="grid grid-cols-3 gap-x-3 gap-y-2">
        <Metric label="Tick" value={fmtInt(tickSeq)} />
        <Metric label="Frames/s" value={fmtInt(fps)} unit="hz" />
        <Metric label="Decode" value={fmtNum(decodeMs, 2)} unit="ms" />
        <Metric label="Nodes" value={fmtInt(nNodes)} />
        <Metric label="Edges" value={fmtInt(nEdges)} />
        <Metric label="Frames" value={fmtInt(framesReceived)} />
      </div>
    </div>
  );
}
