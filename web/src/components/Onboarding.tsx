"use client";

import { useEffect, useState } from "react";

const KEY = "risksim_onboarded_v1";

function Row({ swatch, children }: { swatch: React.ReactNode; children: React.ReactNode }) {
  return (
    <div className="flex items-start gap-3">
      <div className="mt-0.5 flex h-4 w-4 shrink-0 items-center justify-center">{swatch}</div>
      <p className="text-[12px] leading-snug text-ink-dim">{children}</p>
    </div>
  );
}

export default function Onboarding() {
  const [open, setOpen] = useState(false);

  useEffect(() => {
    try {
      if (!localStorage.getItem(KEY)) setOpen(true);
    } catch {
      setOpen(true);
    }
  }, []);

  function dismiss() {
    setOpen(false);
    try {
      localStorage.setItem(KEY, "1");
    } catch {
      /* ignore */
    }
  }

  return (
    <>
      {/* Persistent help toggle */}
      <button
        aria-label="What is this?"
        onClick={() => setOpen(true)}
        className="term-btn absolute right-3 top-3 z-20 !px-2.5 !py-1"
        title="What am I looking at?"
      >
        ?
      </button>

      {open && (
        <div className="absolute inset-0 z-30 flex items-center justify-center p-4">
          <div
            className="absolute inset-0"
            style={{ background: "rgba(6,8,12,0.72)", backdropFilter: "blur(2px)" }}
            onClick={dismiss}
          />
          <div className="panel fadeup relative z-10 w-full max-w-[440px] p-5">
            <div className="tt-label mb-1">Interbank contagion simulator</div>
            <h2 className="mb-3 text-base font-semibold text-ink">
              How one bank&apos;s failure ripples through the system
            </h2>

            <div className="flex flex-col gap-3">
              <Row
                swatch={
                  <span
                    className="h-3 w-3 rounded-full"
                    style={{ border: "1.5px solid var(--cyan)", background: "var(--green)" }}
                  />
                }
              >
                Each <b className="text-ink">dot is a bank</b>; faint lines are loans between
                them. The <b style={{ color: "var(--cyan)" }}>cyan-ringed</b> banks in the
                centre are the large, systemically important <b className="text-ink">core</b>.
              </Row>
              <Row
                swatch={
                  <span
                    className="h-3 w-8 rounded-sm"
                    style={{
                      background:
                        "linear-gradient(90deg, var(--red), var(--amber), var(--green))",
                    }}
                  />
                }
              >
                Colour is <b className="text-ink">health</b>: green = solvent, red = failed.
                When a bank is shocked, watch the red spread to everyone exposed to it.
              </Row>
              <Row swatch={<span className="text-sm">📉</span>}>
                The right panel is the <b className="text-ink">system-wide risk</b>: the status
                (Calm → Critical), how many banks have failed, and how much the whole network
                could lose (<b className="text-ink">Expected Shortfall</b>).
              </Row>
              <Row swatch={<span className="text-sm">⚡</span>}>
                <b className="text-ink">Try it:</b> hit <b style={{ color: "var(--red)" }}>Apply
                core shock</b> in the panel and watch the cascade — then see the risk numbers
                jump.
              </Row>
            </div>

            <button className="term-btn danger mt-5 w-full" onClick={dismiss}>
              Got it — start exploring →
            </button>
          </div>
        </div>
      )}
    </>
  );
}
