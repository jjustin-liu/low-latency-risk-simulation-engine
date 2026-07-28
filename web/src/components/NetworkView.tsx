"use client";

import { useMemo, useState } from "react";
import DeckGL from "@deck.gl/react";
import { OrthographicView } from "@deck.gl/core";
import { ScatterplotLayer, LineLayer } from "@deck.gl/layers";
import { useSnapshotStore } from "@/stores/snapshotStore";
import { snapshotRef } from "@/stores/snapshotRef";
import { healthColor, healthLabel, isCore, COLORS } from "@/lib/viz";

const INITIAL_VIEW_STATE = {
  target: [0, 0, 0] as [number, number, number],
  zoom: 8,
  minZoom: 5,
  maxZoom: 14,
};

const DIM = (c: [number, number, number, number]): [number, number, number, number] => [
  Math.round(c[0] * 0.32 + 26),
  Math.round(c[1] * 0.32 + 31),
  Math.round(c[2] * 0.32 + 40),
  70,
];

export default function NetworkView() {
  const frameId = useSnapshotStore((s) => s.frameId);
  const hasData = useSnapshotStore((s) => s.hasData);
  const [hovered, setHovered] = useState<number | null>(null);
  const [pinned, setPinned] = useState<number | null>(null);
  const focus = pinned ?? hovered;

  const layers = useMemo(() => {
    const snap = snapshotRef.current;
    if (!snap) return [];

    let maxValue = 1e-6;
    for (let i = 0; i < snap.nNodes; i++) {
      if (snap.nodeValue[i] > maxValue) maxValue = snap.nodeValue[i];
    }
    let maxFlow = 1e-6;
    for (let i = 0; i < snap.nEdges; i++) {
      if (snap.edgeFlow[i] > maxFlow) maxFlow = snap.edgeFlow[i];
    }

    // When a bank is focused, precompute its neighbourhood + incident edges so
    // everything else can be dimmed.
    let neighbors: Set<number> | null = null;
    let focusEdges: Set<number> | null = null;
    if (focus != null) {
      neighbors = new Set<number>([focus]);
      focusEdges = new Set<number>();
      for (let e = 0; e < snap.nEdges; e++) {
        const s = snap.edgeSrc[e];
        const d = snap.edgeDst[e];
        if (s === focus || d === focus) {
          focusEdges.add(e);
          neighbors.add(s);
          neighbors.add(d);
        }
      }
    }

    const trigger = frameId;
    const focusTrigger = `${frameId}:${focus ?? -1}`;

    const coreRing = new ScatterplotLayer<number>({
      id: "core-region",
      data: [0],
      getPosition: () => [0, 0],
      getRadius: 0.62,
      radiusUnits: "common",
      stroked: true,
      filled: false,
      getLineColor: [88, 166, 255, focus == null ? 38 : 16],
      lineWidthUnits: "pixels",
      getLineWidth: 1,
      updateTriggers: { getLineColor: focusTrigger },
    });

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
        if (focusEdges) {
          if (focusEdges.has(i)) return [120, 190, 255, 220]; // incident -> bright
          return [88, 166, 255, 7]; // unrelated -> nearly invisible
        }
        const t = snap.edgeFlow[i] / maxFlow;
        const a = 22 + Math.round(Math.min(1, t) * 70);
        return [COLORS.edge[0], COLORS.edge[1], COLORS.edge[2], a];
      },
      getWidth: (i: number) => {
        if (focusEdges && focusEdges.has(i)) return 1.8;
        return 0.4 + (snap.edgeFlow[i] / maxFlow) * 2.6;
      },
      widthUnits: "pixels",
      widthMinPixels: 0.4,
      updateTriggers: {
        getSourcePosition: trigger,
        getTargetPosition: trigger,
        getColor: focusTrigger,
        getWidth: focusTrigger,
      },
    });

    const dimmed = (i: number) => neighbors != null && !neighbors.has(i);

    const nodeLayer = new ScatterplotLayer<number>({
      id: "banks",
      data: snapshotRef.indices,
      getPosition: (i: number) => [snap.nodeX[i], snap.nodeY[i]],
      getRadius: (i: number) => {
        const norm = Math.sqrt(Math.max(0, snap.nodeValue[i]) / maxValue);
        const base = 3 + norm * 22;
        return i === focus ? base * 1.25 + 2 : base;
      },
      getFillColor: (i: number) => {
        const c = healthColor(snap.nodeHealth[i]);
        return dimmed(i) ? DIM(c) : c;
      },
      getLineColor: (i: number) => {
        if (i === focus) return [235, 240, 246, 255]; // focused -> white ring
        if (dimmed(i)) return [46, 54, 68, 90];
        if (isCore(snap.nodeX[i], snap.nodeY[i])) return [88, 166, 255, 255];
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
      radiusMaxPixels: 40,
      stroked: true,
      lineWidthUnits: "pixels",
      getLineWidth: (i: number) =>
        i === focus ? 2 : isCore(snap.nodeX[i], snap.nodeY[i]) ? 1.75 : 1,
      pickable: true,
      autoHighlight: false,
      updateTriggers: {
        getPosition: trigger,
        getRadius: focusTrigger,
        getFillColor: focusTrigger,
        getLineColor: focusTrigger,
        getLineWidth: focusTrigger,
      },
    });

    return [coreRing, edgeLayer, nodeLayer];
  }, [frameId, focus]);

  return (
    <div className="relative h-full w-full">
      <DeckGL
        views={new OrthographicView({ id: "topology" })}
        initialViewState={INITIAL_VIEW_STATE}
        controller={{ scrollZoom: true, doubleClickZoom: false }}
        layers={layers}
        onHover={({ index }) => setHovered(index >= 0 ? index : null)}
        onClick={({ index }) =>
          setPinned((prev) => (index >= 0 ? (prev === index ? null : index) : null))
        }
        getCursor={({ isHovering }) => (isHovering ? "pointer" : "grab")}
        getTooltip={({ index }) => {
          const snap = snapshotRef.current;
          if (index < 0 || !snap) return null;
          const h = snap.nodeHealth[index];
          const healthPct = (h * 100).toFixed(0);
          const pd = snap.nodePd ? snap.nodePd[index] : 0;
          const pdPct = (pd * 100).toFixed(0);
          const pdColor =
            pd < 0.05 ? "#3fb950" : pd < 0.2 ? "#e3b341" : "#f85149";
          const core = isCore(snap.nodeX[index], snap.nodeY[index]);
          let degree = 0;
          for (let e = 0; e < snap.nEdges; e++) {
            if (snap.edgeSrc[e] === index || snap.edgeDst[e] === index) degree++;
          }
          const hc = healthColor(h);
          const hColor = `rgb(${hc[0]},${hc[1]},${hc[2]})`;
          return {
            html: `<div style="font-family:var(--mono);font-size:11px;padding:3px 5px;min-width:150px">
              <div style="display:flex;justify-content:space-between;align-items:center;margin-bottom:3px">
                <span style="color:#e6edf3;font-weight:600">Bank #${index}</span>
                <span style="color:${core ? "#58a6ff" : "#56637a"};letter-spacing:.12em;font-size:9px">${core ? "CORE" : "PERIPHERY"}</span>
              </div>
              <div style="display:flex;justify-content:space-between"><span style="color:#93a1b3">health</span><span style="color:${hColor};font-weight:600">${healthPct}% · ${healthLabel(h)}</span></div>
              <div style="display:flex;justify-content:space-between"><span style="color:#93a1b3">default prob</span><span style="color:${pdColor};font-weight:600">${pdPct}%</span></div>
              <div style="display:flex;justify-content:space-between"><span style="color:#93a1b3">size</span><span>${snap.nodeValue[index] >= 1000 ? (snap.nodeValue[index] / 1000).toFixed(1) + "k" : snap.nodeValue[index].toFixed(0)}</span></div>
              <div style="display:flex;justify-content:space-between"><span style="color:#93a1b3">exposures</span><span>${degree} links</span></div>
              <div style="margin-top:3px;color:#56637a;font-size:9px">${pinned === index ? "click to unpin" : "click to pin focus"}</div>
            </div>`,
            style: {
              backgroundColor: "#0b0f16",
              border: "1px solid #273244",
              borderRadius: "5px",
              color: "#e6edf3",
              boxShadow: "0 6px 20px rgba(0,0,0,.5)",
            },
          };
        }}
        style={{ position: "absolute", inset: "0" }}
      />

      {/* Focus indicator */}
      {focus != null && (
        <div className="pointer-events-none absolute bottom-3 right-3 panel px-3 py-1.5">
          <span className="tt-label">
            focus{" "}
            <span className="tt-mono-num" style={{ color: "var(--cyan)" }}>
              bank #{focus}
            </span>
            {pinned != null && (
              <span style={{ color: "var(--amber)" }}> · pinned</span>
            )}
          </span>
        </div>
      )}

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
