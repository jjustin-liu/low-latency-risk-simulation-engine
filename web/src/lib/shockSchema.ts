import { z } from "zod";

/**
 * The structured shock the model must produce and the client must send.
 * At least one of `target` or `banks` must be present.
 */
export const shockSchema = z
  .object({
    target: z.literal("core").optional(),
    banks: z.array(z.number().int().nonnegative()).max(512).optional(),
    magnitude: z.number().min(0).max(1),
  })
  .refine((s) => s.target !== undefined || (s.banks && s.banks.length > 0), {
    message: "Shock must specify a target or at least one bank id.",
  });

export type ShockInput = z.infer<typeof shockSchema>;

/** JSON Schema handed to Claude as a strict tool definition. */
export const shockToolSchema = {
  type: "object" as const,
  additionalProperties: false,
  properties: {
    target: {
      type: "string" as const,
      enum: ["core"],
      description:
        "Set to 'core' when the user targets the systemically important / largest / central banks as a group.",
    },
    banks: {
      type: "array" as const,
      items: { type: "integer" as const, minimum: 0 },
      description:
        "Explicit bank ids to shock, when the user names specific numeric ids.",
    },
    magnitude: {
      type: "number" as const,
      minimum: 0,
      maximum: 1,
      description:
        "Shock severity as a fraction in [0,1]. '40%' -> 0.4. Default to 0.5 when unspecified.",
    },
  },
  required: ["magnitude"],
};

/**
 * Deterministic offline fallback parser. Runs when ANTHROPIC_API_KEY is unset
 * or the API call fails, so the demo works without network access.
 */
export function fallbackParseShock(text: string): ShockInput {
  const lower = text.toLowerCase();

  // Magnitude: prefer an explicit percentage, then a bare decimal in [0,1].
  let magnitude = 0.5;
  const pct = lower.match(/(\d+(?:\.\d+)?)\s*%/);
  if (pct) {
    magnitude = clamp01(parseFloat(pct[1]) / 100);
  } else {
    const dec = lower.match(/\b0?\.\d+\b/);
    if (dec) magnitude = clamp01(parseFloat(dec[0]));
  }

  // Explicit bank ids: "bank 3", "banks 3 and 7", "bank id 12".
  const banks: number[] = [];
  const bankMatches = lower.matchAll(/bank(?:\s*id)?s?\s*#?\s*(\d+)/g);
  for (const m of bankMatches) banks.push(parseInt(m[1], 10));
  // Bare "shock 3, 7, 11" style lists following the word bank(s).
  if (banks.length) {
    const tail = lower.split(/banks?/).pop() ?? "";
    for (const m of tail.matchAll(/\b(\d+)\b/g)) {
      const id = parseInt(m[1], 10);
      // avoid re-consuming the percentage digits
      if (!lower.includes(`${id}%`) && !banks.includes(id)) banks.push(id);
    }
  }

  const coreWords =
    /\b(core|central|systemic|biggest|largest|major|top|central-bank)\b/;
  if (banks.length > 0) {
    return { banks: dedupe(banks), magnitude };
  }
  if (coreWords.test(lower)) {
    return { target: "core", magnitude };
  }
  // Default: treat as a systemic (core) shock.
  return { target: "core", magnitude };
}

function clamp01(n: number): number {
  if (Number.isNaN(n)) return 0.5;
  return Math.min(1, Math.max(0, n));
}

function dedupe(xs: number[]): number[] {
  return Array.from(new Set(xs));
}
