// Shared shapes for the decoded wire frame and worker messages.

/** A decoded Snapshot with hot typed arrays kept out of React state. */
export interface DecodedSnapshot {
  tickSeq: number;
  simTimeNs: number;
  wallEmitNs: number;
  nNodes: number;
  nEdges: number;
  var: number;
  es: number;
  totalLoss: number;
  numDefaults: number;
  nodeHealth: Float32Array;
  nodeValue: Float32Array;
  nodeX: Float32Array;
  nodeY: Float32Array;
  edgeSrc: Uint32Array;
  edgeDst: Uint32Array;
  edgeFlow: Float32Array;
}

/** Message posted TO the decode worker. */
export interface DecodeRequest {
  buffer: ArrayBuffer;
  recvTime: number;
}

/** Message posted FROM the decode worker. */
export type DecodeResponse =
  | { ok: true; snapshot: DecodedSnapshot; decodeMs: number; recvTime: number }
  | { ok: false; recvTime: number };

/** Validated stress-scenario shock (mirrors the /api/shock contract). */
export interface Shock {
  target?: "core";
  banks?: number[];
  magnitude: number;
}

export type ConnectionStatus =
  | "idle"
  | "connecting"
  | "open"
  | "closed"
  | "error";
