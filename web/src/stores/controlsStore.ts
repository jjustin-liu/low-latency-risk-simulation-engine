import { create } from "zustand";

interface ControlsState {
  coreMagnitude: number;
  bankId: number;
  bankMagnitude: number;
  /** Human-readable confirmation of the last dispatched action. */
  lastAction: string | null;
  setCoreMagnitude: (v: number) => void;
  setBankId: (v: number) => void;
  setBankMagnitude: (v: number) => void;
  setLastAction: (msg: string) => void;
}

export const useControlsStore = create<ControlsState>((set) => ({
  coreMagnitude: 0.85,
  bankId: 0,
  bankMagnitude: 0.6,
  lastAction: null,
  setCoreMagnitude: (v) => set({ coreMagnitude: v }),
  setBankId: (v) => set({ bankId: v }),
  setBankMagnitude: (v) => set({ bankMagnitude: v }),
  setLastAction: (msg) => set({ lastAction: msg }),
}));
