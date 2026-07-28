// Palette + formatting shared by the network view and the panels.
// Status ramp (health): healthy green -> amber -> critical red.
// Chosen for a dark surface (#07090d) and CVD legibility.

export type RGBA = [number, number, number, number];

export const COLORS = {
  healthy: [63, 185, 80] as [number, number, number], // #3fb950
  warning: [210, 153, 34] as [number, number, number], // #d29922
  critical: [248, 81, 73] as [number, number, number], // #f85149
  edge: [88, 166, 255] as [number, number, number], // #58a6ff (cool, low-op)
};

/**
 * Map node health -> RGBA. Assumes health in [0,1] where 1 = healthy.
 * Ramp: green (1.0) -> amber (0.5) -> red (0.0), degrading toward default.
 */
export function healthColor(health: number, alpha = 235): RGBA {
  const h = clamp01(health);
  let rgb: [number, number, number];
  if (h >= 0.5) {
    rgb = lerp3(COLORS.warning, COLORS.healthy, (h - 0.5) / 0.5);
  } else {
    rgb = lerp3(COLORS.critical, COLORS.warning, h / 0.5);
  }
  return [rgb[0], rgb[1], rgb[2], alpha];
}

function lerp3(
  a: [number, number, number],
  b: [number, number, number],
  t: number,
): [number, number, number] {
  const u = clamp01(t);
  return [
    Math.round(a[0] + (b[0] - a[0]) * u),
    Math.round(a[1] + (b[1] - a[1]) * u),
    Math.round(a[2] + (b[2] - a[2]) * u),
  ];
}

function clamp01(n: number): number {
  if (!Number.isFinite(n)) return 0;
  return Math.min(1, Math.max(0, n));
}

export function rgbCss(rgb: [number, number, number]): string {
  return `rgb(${rgb[0]}, ${rgb[1]}, ${rgb[2]})`;
}

// ---- formatting -----------------------------------------------------------

export function fmtCurrency(v: number): string {
  const abs = Math.abs(v);
  if (abs >= 1e9) return `${sign(v)}$${(abs / 1e9).toFixed(2)}B`;
  if (abs >= 1e6) return `${sign(v)}$${(abs / 1e6).toFixed(2)}M`;
  if (abs >= 1e3) return `${sign(v)}$${(abs / 1e3).toFixed(1)}K`;
  return `${sign(v)}$${abs.toFixed(0)}`;
}

export function fmtNum(v: number, digits = 2): string {
  if (!Number.isFinite(v)) return "—";
  return v.toLocaleString(undefined, {
    minimumFractionDigits: digits,
    maximumFractionDigits: digits,
  });
}

export function fmtInt(v: number): string {
  return Math.round(v).toLocaleString();
}

function sign(v: number): string {
  return v < 0 ? "-" : "";
}

// ---- plain-language helpers ------------------------------------------------

/** A bank is "core" (systemically important) if it sits near the layout centre.
 *  The generator places the dealer core at radius ~0.28 and the periphery at ~1. */
export function isCore(x: number, y: number): boolean {
  return x * x + y * y < 0.36;
}

/** Plain word for a health value in [0,1]. */
export function healthLabel(h: number): string {
  if (h >= 0.95) return "healthy";
  if (h >= 0.7) return "under stress";
  if (h >= 0.3) return "distressed";
  if (h > 0.02) return "near failure";
  return "failed";
}

export interface StressLevel {
  label: string;
  color: string;
  hint: string;
  /** 0..3, for optional intensity effects. */
  rank: number;
}

/** Headline system status derived from average network health + defaults. */
export function stressLevel(meanHealth: number, numDefaults: number): StressLevel {
  if (numDefaults === 0 && meanHealth >= 0.985)
    return { label: "CALM", color: "var(--green)", hint: "no distress in the system", rank: 0 };
  if (meanHealth >= 0.9)
    return { label: "ELEVATED", color: "var(--amber)", hint: "localized stress building", rank: 1 };
  if (meanHealth >= 0.75)
    return { label: "STRESSED", color: "#f0883e", hint: "contagion spreading", rank: 2 };
  return { label: "CRITICAL", color: "var(--red)", hint: "systemic failure underway", rank: 3 };
}
