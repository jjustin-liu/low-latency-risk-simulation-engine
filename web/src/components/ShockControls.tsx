"use client";

import { bridge } from "@/lib/bridge";
import { useControlsStore } from "@/stores/controlsStore";
import { useConnectionStore } from "@/stores/connectionStore";

export default function ShockControls() {
  const coreMagnitude = useControlsStore((s) => s.coreMagnitude);
  const bankId = useControlsStore((s) => s.bankId);
  const bankMagnitude = useControlsStore((s) => s.bankMagnitude);
  const setCoreMagnitude = useControlsStore((s) => s.setCoreMagnitude);
  const setBankId = useControlsStore((s) => s.setBankId);
  const setBankMagnitude = useControlsStore((s) => s.setBankMagnitude);
  const setLastAction = useControlsStore((s) => s.setLastAction);
  const status = useConnectionStore((s) => s.status);

  const connected = status === "open";

  function dispatch(
    payload: Parameters<typeof bridge.send>[0],
    confirmation: string,
  ) {
    const sent = bridge.send(payload);
    setLastAction(
      sent
        ? `→ ${confirmation}`
        : `⚠ bridge offline — dropped: ${confirmation}`,
    );
  }

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="tt-label">Stress Scenarios</span>
        <span className="tt-label">manual</span>
      </div>

      <div className="flex flex-col gap-4 p-3">
        {/* Shock core */}
        <div>
          <div className="mb-1.5 flex items-center justify-between">
            <span className="tt-label">Shock core</span>
            <span className="tt-mono-num text-sm" style={{ color: "var(--cyan)" }}>
              {(coreMagnitude * 100).toFixed(0)}%
            </span>
          </div>
          <input
            type="range"
            min={0}
            max={1}
            step={0.01}
            value={coreMagnitude}
            onChange={(e) => setCoreMagnitude(parseFloat(e.target.value))}
            className="w-full"
          />
          <button
            className="term-btn danger mt-2 w-full"
            disabled={!connected}
            onClick={() =>
              dispatch(
                { target: "core", magnitude: coreMagnitude },
                `shock core @ ${(coreMagnitude * 100).toFixed(0)}%`,
              )
            }
          >
            Apply core shock
          </button>
        </div>

        <div className="border-t border-border" />

        {/* Shock a specific bank */}
        <div>
          <div className="mb-1.5 flex items-center justify-between">
            <span className="tt-label">Shock bank</span>
            <span className="tt-mono-num text-sm" style={{ color: "var(--amber)" }}>
              {(bankMagnitude * 100).toFixed(0)}%
            </span>
          </div>
          <div className="mb-2 flex items-center gap-2">
            <label className="tt-label">id</label>
            <input
              type="number"
              min={0}
              value={bankId}
              onChange={(e) =>
                setBankId(Math.max(0, parseInt(e.target.value || "0", 10)))
              }
              className="tt-mono-num w-20 rounded border border-border bg-surface-2 px-2 py-1 text-sm text-ink outline-none focus:border-cyan"
              style={{ background: "var(--surface-2)" }}
            />
          </div>
          <input
            type="range"
            min={0}
            max={1}
            step={0.01}
            value={bankMagnitude}
            onChange={(e) => setBankMagnitude(parseFloat(e.target.value))}
            className="w-full"
          />
          <button
            className="term-btn mt-2 w-full"
            disabled={!connected}
            onClick={() =>
              dispatch(
                { banks: [bankId], magnitude: bankMagnitude },
                `shock bank #${bankId} @ ${(bankMagnitude * 100).toFixed(0)}%`,
              )
            }
          >
            Apply bank shock
          </button>
        </div>

        <div className="border-t border-border" />

        <button
          className="term-btn w-full"
          disabled={!connected}
          onClick={() => dispatch({ type: "clear" }, "clear all shocks")}
        >
          Clear
        </button>
      </div>
    </div>
  );
}
