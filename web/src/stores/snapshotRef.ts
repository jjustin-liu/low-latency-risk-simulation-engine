import type { DecodedSnapshot } from "@/lib/types";

/**
 * Hot typed arrays live HERE, outside React/Zustand state, so 10 Hz frame
 * updates never trigger a structural re-render or a store deep-compare.
 * deck.gl layer accessors read `snapshotRef.current` directly; the
 * snapshot store's `frameId` counter is the only thing React subscribes to.
 */
export const snapshotRef: {
  current: DecodedSnapshot | null;
  /** Cached [0..nNodes-1] index array, rebuilt only when node count changes. */
  indices: Uint32Array;
  /** Cached [0..nEdges-1] index array, rebuilt only when edge count changes. */
  edgeIndices: Uint32Array;
} = {
  current: null,
  indices: new Uint32Array(0),
  edgeIndices: new Uint32Array(0),
};

export function setSnapshot(snap: DecodedSnapshot): void {
  snapshotRef.current = snap;
  if (snapshotRef.indices.length !== snap.nNodes) {
    snapshotRef.indices = buildRange(snap.nNodes);
  }
  if (snapshotRef.edgeIndices.length !== snap.nEdges) {
    snapshotRef.edgeIndices = buildRange(snap.nEdges);
  }
}

function buildRange(n: number): Uint32Array {
  const arr = new Uint32Array(n);
  for (let i = 0; i < n; i++) arr[i] = i;
  return arr;
}
