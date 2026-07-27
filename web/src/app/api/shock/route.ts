import { NextResponse } from "next/server";
import Anthropic from "@anthropic-ai/sdk";
import { shockSchema, fallbackParseShock } from "@/lib/shockSchema";
import type { Shock } from "@/lib/types";

export const runtime = "nodejs";

const MODEL = "claude-haiku-4-5";

const SYSTEM_PROMPT = `You translate a natural-language stress-test request for an interbank \
default-contagion simulation into a structured shock. Rules:
- If the user targets the systemically important / central / largest / "biggest" banks as a group, use scope "core".
- If the user names specific numeric bank ids, use scope "banks" and list them.
- magnitude is a fraction in [0,1]; "40%" -> 0.4. If unspecified, use 0.5.
Always call the emit_shock tool exactly once.`;

// Strict-tool schema: every property required, additionalProperties:false,
// no numeric range keywords (unsupported under strict — enforced server-side).
const EMIT_SHOCK_TOOL = {
  name: "emit_shock",
  description:
    "Emit the structured interbank shock parsed from the user's request.",
  strict: true,
  input_schema: {
    type: "object" as const,
    additionalProperties: false,
    properties: {
      scope: {
        type: "string" as const,
        enum: ["core", "banks"],
        description:
          "'core' for the central/largest/systemic banks as a group; 'banks' for explicit ids.",
      },
      banks: {
        type: "array" as const,
        items: { type: "integer" as const },
        description: "Explicit bank ids (empty when scope is 'core').",
      },
      magnitude: {
        type: "number" as const,
        description: "Shock severity in [0,1]; '40%' -> 0.4.",
      },
    },
    required: ["scope", "banks", "magnitude"],
  },
};

export async function POST(request: Request): Promise<Response> {
  let prompt: string;
  try {
    const body = (await request.json()) as { prompt?: unknown };
    if (typeof body.prompt !== "string" || !body.prompt.trim()) {
      return NextResponse.json(
        { error: "Body must include a non-empty 'prompt' string." },
        { status: 400 },
      );
    }
    prompt = body.prompt.trim();
  } catch {
    return NextResponse.json({ error: "Invalid JSON body." }, { status: 400 });
  }

  // No key configured -> deterministic offline parser so the demo still works.
  if (!process.env.ANTHROPIC_API_KEY) {
    return respondWith(fallbackShock(prompt), "fallback");
  }

  try {
    const shock = await extractWithModel(prompt);
    return respondWith(shock, "model");
  } catch (err) {
    // Any model/validation failure degrades gracefully to the offline parser.
    console.error("[/api/shock] model extraction failed:", err);
    return respondWith(fallbackShock(prompt), "fallback");
  }
}

async function extractWithModel(prompt: string): Promise<Shock> {
  const client = new Anthropic();
  const message = await client.messages.create({
    model: MODEL,
    max_tokens: 512,
    system: SYSTEM_PROMPT,
    tools: [EMIT_SHOCK_TOOL],
    tool_choice: { type: "tool", name: "emit_shock" },
    messages: [{ role: "user", content: prompt }],
  });

  const toolUse = message.content.find((b) => b.type === "tool_use");
  if (!toolUse || toolUse.type !== "tool_use") {
    throw new Error("Model did not return a tool_use block.");
  }

  const raw = toolUse.input as {
    scope?: string;
    banks?: unknown;
    magnitude?: unknown;
  };
  const magnitude = clamp01(Number(raw.magnitude));
  let candidate: unknown;
  if (raw.scope === "banks" && Array.isArray(raw.banks) && raw.banks.length) {
    candidate = {
      banks: raw.banks
        .map((n) => Math.trunc(Number(n)))
        .filter((n) => Number.isFinite(n) && n >= 0),
      magnitude,
    };
  } else {
    candidate = { target: "core", magnitude };
  }

  // Final gate: validate against the shared zod contract before returning.
  return shockSchema.parse(candidate) as Shock;
}

function fallbackShock(prompt: string): Shock {
  return shockSchema.parse(fallbackParseShock(prompt)) as Shock;
}

function respondWith(shock: Shock, source: "model" | "fallback"): Response {
  return NextResponse.json({
    shock,
    source,
    confirmation: describeShock(shock),
  });
}

function describeShock(s: Shock): string {
  const pct = `${Math.round(s.magnitude * 100)}%`;
  if (s.target === "core") return `Shock core banks at ${pct}`;
  if (s.banks?.length)
    return `Shock bank${s.banks.length > 1 ? "s" : ""} ${s.banks.join(", ")} at ${pct}`;
  return `Shock at ${pct}`;
}

function clamp01(n: number): number {
  if (!Number.isFinite(n)) return 0.5;
  return Math.min(1, Math.max(0, n));
}
