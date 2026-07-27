"use client";

import { useState } from "react";
import { bridge } from "@/lib/bridge";
import { useControlsStore } from "@/stores/controlsStore";
import type { Shock } from "@/lib/types";

interface ApiResponse {
  shock?: Shock;
  confirmation?: string;
  source?: "model" | "fallback";
  error?: string;
}

const EXAMPLES = [
  "crash the three biggest banks by 40%",
  "shock bank 7 and 12 by 60%",
  "hit the core at 90%",
];

export default function ShockConsole() {
  const [text, setText] = useState("");
  const [busy, setBusy] = useState(false);
  const [result, setResult] = useState<ApiResponse | null>(null);
  const setLastAction = useControlsStore((s) => s.setLastAction);

  async function submit(prompt: string) {
    const q = prompt.trim();
    if (!q || busy) return;
    setBusy(true);
    setResult(null);
    try {
      const res = await fetch("/api/shock", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ prompt: q }),
      });
      const data: ApiResponse = await res.json();
      if (!res.ok || !data.shock) {
        setResult({ error: data.error ?? `request failed (${res.status})` });
        return;
      }
      const sent = bridge.send(data.shock);
      const confirmation =
        data.confirmation ?? describeShock(data.shock);
      setResult({ ...data, confirmation });
      setLastAction(
        sent
          ? `→ ${confirmation}`
          : `⚠ bridge offline — parsed: ${confirmation}`,
      );
    } catch (err) {
      setResult({ error: err instanceof Error ? err.message : String(err) });
    } finally {
      setBusy(false);
    }
  }

  return (
    <div className="panel">
      <div className="panel-header">
        <span className="tt-label">Shock Console · natural language</span>
        <span className="tt-label" style={{ color: "var(--violet)" }}>
          claude
        </span>
      </div>

      <div className="p-3">
        <div className="flex items-start gap-2">
          <span
            className="pt-1.5 text-sm"
            style={{ color: "var(--violet)" }}
          >
            &gt;
          </span>
          <textarea
            value={text}
            onChange={(e) => setText(e.target.value)}
            onKeyDown={(e) => {
              if (e.key === "Enter" && !e.shiftKey) {
                e.preventDefault();
                void submit(text);
              }
            }}
            rows={2}
            placeholder="describe a stress scenario…"
            className="min-h-[44px] w-full resize-none rounded border border-border px-2 py-1.5 text-[13px] text-ink outline-none focus:border-cyan"
            style={{ background: "var(--surface-2)" }}
          />
        </div>

        <div className="mt-2 flex flex-wrap gap-1.5">
          {EXAMPLES.map((ex) => (
            <button
              key={ex}
              type="button"
              onClick={() => {
                setText(ex);
                void submit(ex);
              }}
              disabled={busy}
              className="rounded border border-border px-2 py-1 text-[10px] text-ink-dim hover:border-cyan disabled:opacity-40"
              style={{ background: "var(--surface-2)" }}
            >
              {ex}
            </button>
          ))}
        </div>

        <button
          className="term-btn mt-2 w-full"
          disabled={busy || !text.trim()}
          onClick={() => void submit(text)}
        >
          {busy ? "Parsing…" : "Interpret & dispatch"}
        </button>

        {result && (
          <div className="fadeup mt-3 rounded border border-border p-2 text-[11px]" style={{ background: "var(--surface-2)" }}>
            {result.error ? (
              <span style={{ color: "var(--red)" }}>error: {result.error}</span>
            ) : (
              <>
                <div className="flex items-center justify-between">
                  <span className="tt-label">Interpreted</span>
                  <span
                    className="tt-label"
                    style={{
                      color:
                        result.source === "model"
                          ? "var(--violet)"
                          : "var(--amber)",
                    }}
                  >
                    {result.source === "model" ? "model" : "offline parser"}
                  </span>
                </div>
                <div className="mt-1 text-ink">{result.confirmation}</div>
                <pre className="mt-1 whitespace-pre-wrap text-ink-muted">
                  {JSON.stringify(result.shock)}
                </pre>
              </>
            )}
          </div>
        )}
      </div>
    </div>
  );
}

function describeShock(s: Shock): string {
  const pct = `${Math.round(s.magnitude * 100)}%`;
  if (s.target === "core") return `shock core @ ${pct}`;
  if (s.banks?.length)
    return `shock bank${s.banks.length > 1 ? "s" : ""} ${s.banks.join(", ")} @ ${pct}`;
  return `shock @ ${pct}`;
}
