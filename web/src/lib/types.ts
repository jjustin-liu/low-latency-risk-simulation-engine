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
  /** Merton probability of default per bank, in [0,1]. */
  nodePd: Float32Array;
  edgeSrc: Uint32Array;
  edgeDst: Uint32Array;
  edgeFlow: Float32Array;
  /** Monte-Carlo systemic-loss histogram: bin k is the fraction of paths with
   *  loss in [k, k+1) * histMax / nHist. Bars sum to ~1. */
  lossHist: Float32Array;
  /** Loss value at the right edge of the last histogram bin (left edge = 0). */
  histMax: number;
  /** Number of tradeable assets in the fire-sale channel. */
  nAssets: number;
  /** Fractional fire-sale price drop per asset, in [0,1]. */
  assetImpact: Float32Array;
  /** Fraction of order-book bid liquidity still resting per asset (1=full). */
  assetDepth: Float32Array;
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
