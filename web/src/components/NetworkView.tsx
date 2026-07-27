"use client";

import { useMemo } from "react";
import DeckGL from "@deck.gl/react";
import { OrthographicView } from "@deck.gl/core";
import { ScatterplotLayer, LineLayer } from "@deck.gl/layers";
import { useSnapshotStore } from "@/stores/snapshotStore";
import { snapshotRef } from "@/stores/snapshotRef";
import { healthColor, COLORS } from "@/lib/viz";

const INITIAL_VIEW_STATE = {
  target: [0, 0, 0] as [number, number, number],
  zoom: 8,
  minZoom: 5,
  maxZoom: 14,
};

export default function NetworkView() {
  // frameId is the ONLY reactive dependency; hot arrays are read from the ref.
  const frameId = useSnapshotStore((s) => s.frameId);
  const hasData = useSnapshotStore((s) => s.hasData);

  const layers = useMemo(() => {
    const snap = snapshotRef.current;
    if (!snap) return [];

    // Per-frame normalization constants for radius / width scaling.
    let maxValue = 1e-6;
    for (let i = 0; i < snap.nNodes; i++) {
      if (snap.nodeValue[i] > maxValue) maxValue = snap.nodeValue[i];
    }
    let maxFlow = 1e-6;
    for (let i = 0; i < snap.nEdges; i++) {
      if (snap.edgeFlow[i] > maxFlow) maxFlow = snap.edgeFlow[i];
    }

    const trigger = frameId;

    const edgeLayer = new LineLayer<number>({
      id: "exposures",
      data: snapshotRef.edgeIndices,
      getSourcePosition: (i: number) => [
        snap.nodeX[snap.edgeSrc[i]],
        snap.nodeY[snap.edgeSrc[i]],
      ],
      getTargetPosition: (i: number) => [
        snap.nodeX[snap.edgeDst[i]],
        snap.nodeY[snap.edgeDst[i]],
      ],
      getColor: (i: number) => {
        const t = snap.edgeFlow[i] / maxFlow;
        const a = 22 + Math.round(Math.min(1, t) * 70);
        return [COLORS.edge[0], COLORS.edge[1], COLORS.edge[2], a];
      },
      getWidth: (i: number) => 0.4 + (snap.edgeFlow[i] / maxFlow) * 2.6,
      widthUnits: "pixels",
      widthMinPixels: 0.4,
      updateTriggers: {
        getSourcePosition: trigger,
        getTargetPosition: trigger,
        getColor: trigger,
        getWidth: trigger,
      },
    });

    const nodeLayer = new ScatterplotLayer<number>({
      id: "banks",
      data: snapshotRef.indices,
      getPosition: (i: number) => [snap.nodeX[i], snap.nodeY[i]],
      getRadius: (i: number) => {
        const norm = Math.sqrt(Math.max(0, snap.nodeValue[i]) / maxValue);
        return 3 + norm * 22;
      },
      getFillColor: (i: number) => healthColor(snap.nodeHealth[i]),
      getLineColor: (i: number) => {
        const c = healthColor(snap.nodeHealth[i]);
        return [
          Math.min(255, c[0] + 40),
          Math.min(255, c[1] + 40),
          Math.min(255, c[2] + 40),
          255,
        ];
      },
      radiusUnits: "pixels",
      radiusMinPixels: 2.5,
      radiusMaxPixels: 34,
      stroked: true,
      lineWidthUnits: "pixels",
      getLineWidth: 1,
      pickable: true,
      updateTriggers: {
        getPosition: trigger,
        getRadius: trigger,
        getFillColor: trigger,
        getLineColor: trigger,
      },
    });

    return [edgeLayer, nodeLayer];
  }, [frameId]);

  return (
    <div className="relative h-full w-full">
      <DeckGL
        views={new OrthographicView({ id: "topology" })}
        initialViewState={INITIAL_VIEW_STATE}
        controller={{ scrollZoom: true, doubleClickZoom: false }}
        layers={layers}
        getTooltip={({ index }) => {
          const snap = snapshotRef.current;
          if (index < 0 || !snap) return null;
          const health = (snap.nodeHealth[index] * 100).toFixed(1);
          const value = snap.nodeValue[index].toFixed(2);
          return {
            html: `<div style="font-family:var(--mono);font-size:11px;padding:2px 4px">
              <div style="color:#93a1b3;letter-spacing:.1em">BANK #${index}</div>
              <div>health <b>${health}%</b></div>
              <div>value <b>${value}</b></div>
            </div>`,
            style: {
              backgroundColor: "#10151f",
              border: "1px solid #273244",
              borderRadius: "4px",
              color: "#e6edf3",
            },
          };
        }}
        style={{ position: "absolute", inset: "0" }}
      />

      {!hasData && (
        <div className="pointer-events-none absolute inset-0 flex items-center justify-center">
          <div className="text-center">
            <div className="tt-label mb-2">Awaiting frames from bridge</div>
            <div className="text-ink-muted text-xs">
              scanning <span className="blink">▮</span>
            </div>
          </div>
        </div>
      )}
    </div>
  );
}
