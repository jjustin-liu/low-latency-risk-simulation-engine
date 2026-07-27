/// <reference lib="webworker" />
import { decodeSnapshot } from "../generated/wire";
import type { DecodeRequest, DecodeResponse, DecodedSnapshot } from "../lib/types";

const ctx = self as unknown as DedicatedWorkerGlobalScope;

ctx.onmessage = (event: MessageEvent<DecodeRequest>) => {
  const { buffer, recvTime } = event.data;
  const t0 = performance.now();
  const snap = decodeSnapshot(buffer);
  const decodeMs = performance.now() - t0;

  if (!snap) {
    const fail: DecodeResponse = { ok: false, recvTime };
    ctx.postMessage(fail);
    return;
  }

  const out: DecodedSnapshot = {
    tickSeq: Number(snap.header.tick_seq),
    simTimeNs: Number(snap.header.sim_time_ns),
    wallEmitNs: Number(snap.header.wall_emit_ns),
    nNodes: snap.n_nodes,
    nEdges: snap.n_edges,
    var: snap.var,
    es: snap.es,
    totalLoss: snap.total_loss,
    numDefaults: snap.num_defaults,
    nodeHealth: snap.node_health,
    nodeValue: snap.node_value,
    nodeX: snap.node_x,
    nodeY: snap.node_y,
    edgeSrc: snap.edge_src,
    edgeDst: snap.edge_dst,
    edgeFlow: snap.edge_flow,
  };

  const response: DecodeResponse = { ok: true, snapshot: out, decodeMs, recvTime };
  // All typed arrays are views over the single incoming frame buffer;
  // transfer it once (zero-copy) rather than structured-cloning the payload.
  ctx.postMessage(response, [buffer]);
};
