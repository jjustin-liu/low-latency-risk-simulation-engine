import { create } from "zustand";
import type { DecodedSnapshot } from "@/lib/types";
import { setSnapshot } from "./snapshotRef";

const ES_HISTORY_LEN = 90;

interface SnapshotState {
  /** Bumps on every decoded frame; the only signal deck.gl subscribes to. */
  frameId: number;
  hasData: boolean;
  tickSeq: number;
  var: number;
  es: number;
  totalLoss: number;
  numDefaults: number;
  nNodes: number;
  nEdges: number;
  /** Mean of node health in [0,1]; 1 = whole system healthy. Drives the status. */
  meanHealth: number;
  esHistory: number[];
  varHistory: number[];
  ingest: (snap: DecodedSnapshot) => void;
  reset: () => void;
}

const initial = {
  frameId: 0,
  hasData: false,
  tickSeq: 0,
  var: 0,
  es: 0,
  totalLoss: 0,
  numDefaults: 0,
  nNodes: 0,
  nEdges: 0,
  meanHealth: 1,
  esHistory: [] as number[],
  varHistory: [] as number[],
};

export const useSnapshotStore = create<SnapshotState>((set) => ({
  ...initial,
  ingest: (snap) => {
    setSnapshot(snap); // stash hot arrays outside React state
    set((state) => {
      const esHistory = [...state.esHistory, snap.es];
      if (esHistory.length > ES_HISTORY_LEN) esHistory.shift();
      const varHistory = [...state.varHistory, snap.var];
      if (varHistory.length > ES_HISTORY_LEN) varHistory.shift();
      let sum = 0;
      for (let i = 0; i < snap.nNodes; i++) sum += snap.nodeHealth[i];
      const meanHealth = snap.nNodes > 0 ? sum / snap.nNodes : 1;
      return {
        frameId: state.frameId + 1,
        hasData: true,
        tickSeq: snap.tickSeq,
        var: snap.var,
        es: snap.es,
        totalLoss: snap.totalLoss,
        numDefaults: snap.numDefaults,
        nNodes: snap.nNodes,
        nEdges: snap.nEdges,
        meanHealth,
        esHistory,
        varHistory,
      };
    });
  },
  reset: () => set({ ...initial }),
}));
