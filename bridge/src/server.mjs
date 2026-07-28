// RiskSim bridge: engine binary frame stream <-> browser WebSocket clients.
//
// - Reads length-prefixed binary frames from the engine stdout (live mode)
//   or from a recorded replay file looped at ~10 Hz (replay mode).
// - Fans each wire frame out to all connected WebSocket clients as a single
//   binary message (length prefix stripped), with lossy drop-to-latest
//   backpressure handling per client.
// - Forwards operator control commands (JSON over WS text) to the engine
//   stdin as the line protocol (live mode) or logs them as no-ops (replay).
// - Serves GET /metrics (Prometheus text) and GET /healthz.

import { spawn } from "node:child_process";
import { createReadStream } from "node:fs";
import http from "node:http";
import { dirname, join } from "node:path";
import { fileURLToPath } from "node:url";
import { WebSocketServer } from "ws";

import { FrameReader } from "./framer.mjs";

// Repo root, resolved relative to this file (bridge/src/server.mjs).
const REPO_ROOT = join(dirname(fileURLToPath(import.meta.url)), "..", "..");

// ----------------------------------------------------------------------------
// Config (env)
// ----------------------------------------------------------------------------
const MODE = process.env.RISKSIM_MODE || "live";
const PORT = Number.parseInt(process.env.PORT || "8080", 10);

const DEFAULT_ENGINE = join(REPO_ROOT, "build/release/engine/runtime/risksim_engine");
const ENGINE_PATH = process.env.RISKSIM_ENGINE || DEFAULT_ENGINE;

const DEFAULT_REPLAY = join(REPO_ROOT, "data/replay.bin");
const REPLAY_PATH = process.env.RISKSIM_REPLAY || DEFAULT_REPLAY;

// Replay emit cadence (~10 Hz to mirror the default engine rate).
const REPLAY_HZ = Number.parseFloat(process.env.RISKSIM_REPLAY_HZ || "10");
const REPLAY_INTERVAL_MS = Math.max(1, Math.round(1000 / REPLAY_HZ));

// Per-client high-water mark: above this bufferedAmount we drop (don't queue).
const HIGH_WATER_MARK = Number.parseInt(
  process.env.RISKSIM_HWM_BYTES || `${1024 * 1024}`,
  10,
);

// ----------------------------------------------------------------------------
// Logging (structured JSON to stderr)
// ----------------------------------------------------------------------------
function log(level, msg, fields = {}) {
  process.stderr.write(
    JSON.stringify({
      ts: new Date().toISOString(),
      level,
      svc: "bridge",
      msg,
      ...fields,
    }) + "\n",
  );
}

// ----------------------------------------------------------------------------
// Metrics
// ----------------------------------------------------------------------------
const metrics = {
  connectedClients: 0,
  framesForwarded: 0,
  droppedFrames: 0,
  tickSeq: 0,
};

// tick_seq lives at header byte offset 4(magic)+2(ver)+2(type) = 8, u64 LE.
const TICK_SEQ_OFFSET = 8;
function readTickSeq(frame) {
  if (frame.length < TICK_SEQ_OFFSET + 8) return null;
  // tick_seq fits comfortably in a JS number for our purposes.
  return Number(frame.readBigUInt64LE(TICK_SEQ_OFFSET));
}

// ----------------------------------------------------------------------------
// HTTP + WebSocket server
// ----------------------------------------------------------------------------
const httpServer = http.createServer((req, res) => {
  if (req.method === "GET" && req.url === "/healthz") {
    res.writeHead(200, { "content-type": "text/plain" });
    res.end("ok");
    return;
  }
  if (req.method === "GET" && req.url === "/metrics") {
    const body =
      `# HELP risksim_connected_clients Currently connected WebSocket clients.\n` +
      `# TYPE risksim_connected_clients gauge\n` +
      `risksim_connected_clients ${metrics.connectedClients}\n` +
      `# HELP risksim_frames_forwarded Total frames broadcast to clients.\n` +
      `# TYPE risksim_frames_forwarded counter\n` +
      `risksim_frames_forwarded ${metrics.framesForwarded}\n` +
      `# HELP risksim_dropped_frames Total per-client frame sends skipped due to backpressure.\n` +
      `# TYPE risksim_dropped_frames counter\n` +
      `risksim_dropped_frames ${metrics.droppedFrames}\n` +
      `# HELP risksim_tick_seq Last observed engine tick sequence number.\n` +
      `# TYPE risksim_tick_seq gauge\n` +
      `risksim_tick_seq ${metrics.tickSeq}\n`;
    res.writeHead(200, { "content-type": "text/plain; version=0.0.4" });
    res.end(body);
    return;
  }
  res.writeHead(404, { "content-type": "text/plain" });
  res.end("not found");
});

const wss = new WebSocketServer({ server: httpServer });

wss.on("connection", (ws, req) => {
  metrics.connectedClients += 1;
  const peer = req.socket.remoteAddress;
  log("info", "client_connected", {
    peer,
    clients: metrics.connectedClients,
  });

  ws.on("message", (data, isBinary) => {
    if (isBinary) return; // clients only send text control commands
    handleControlMessage(ws, data.toString());
  });

  ws.on("close", () => {
    metrics.connectedClients -= 1;
    log("info", "client_disconnected", { clients: metrics.connectedClients });
  });

  ws.on("error", (err) => {
    // Never let a client socket error crash the process.
    log("warn", "client_error", { error: err.message });
  });
});

// Broadcast one wire frame (length prefix already stripped) to all clients.
// Lossy: skip clients whose send buffer is over the high-water mark.
function broadcast(frame) {
  const seq = readTickSeq(frame);
  if (seq !== null) metrics.tickSeq = seq;
  metrics.framesForwarded += 1;

  for (const client of wss.clients) {
    if (client.readyState !== client.OPEN) continue;
    if (client.bufferedAmount > HIGH_WATER_MARK) {
      metrics.droppedFrames += 1;
      continue; // drop-to-latest: don't queue behind a slow client
    }
    client.send(frame, { binary: true }, (err) => {
      if (err) log("warn", "send_error", { error: err.message });
    });
  }
}

// ----------------------------------------------------------------------------
// Control channel: JSON -> engine stdin line protocol
// ----------------------------------------------------------------------------
// Translate a validated control command object to an engine stdin line, or
// return { error } describing why it is invalid. Never throws.
function commandToLine(cmd) {
  if (cmd === null || typeof cmd !== "object") {
    return { error: "not an object" };
  }
  switch (cmd.type) {
    case "clear":
      return { line: "clear\n" };
    case "shock": {
      const mag = cmd.magnitude;
      if (typeof mag !== "number" || !Number.isFinite(mag)) {
        return { error: "magnitude must be a finite number" };
      }
      // Engine expects a decimal point in the magnitude; toFixed guarantees it.
      const magStr = mag.toFixed(6);

      if (cmd.target === "core") {
        return { line: `shock core ${magStr}\n` };
      }
      if (Array.isArray(cmd.banks)) {
        if (cmd.banks.length === 0) return { error: "banks is empty" };
        const ids = [];
        for (const b of cmd.banks) {
          if (!Number.isInteger(b) || b < 0) {
            return { error: `invalid bank id: ${JSON.stringify(b)}` };
          }
          ids.push(String(b));
        }
        return { line: `shock ${ids.join(" ")} ${magStr}\n` };
      }
      return { error: "shock needs target:'core' or banks:[...]" };
    }
    default:
      return { error: `unknown type: ${JSON.stringify(cmd.type)}` };
  }
}

function handleControlMessage(ws, text) {
  let cmd;
  try {
    cmd = JSON.parse(text);
  } catch {
    log("warn", "control_parse_error", { raw: text.slice(0, 200) });
    return;
  }

  const { line, error } = commandToLine(cmd);
  if (error) {
    log("warn", "control_invalid", { error, cmd });
    return;
  }

  if (MODE === "replay") {
    log("info", "control_noop_replay", { line: line.trimEnd() });
    return;
  }

  // live mode: forward to engine stdin
  if (engineChild && engineChild.stdin && engineChild.stdin.writable) {
    engineChild.stdin.write(line, (err) => {
      if (err) log("warn", "engine_stdin_error", { error: err.message });
    });
    log("info", "control_forwarded", { line: line.trimEnd() });
  } else {
    log("warn", "control_dropped_no_engine", { line: line.trimEnd() });
  }
}

// ----------------------------------------------------------------------------
// Source: live engine child process
// ----------------------------------------------------------------------------
let engineChild = null;

function startLive() {
  const args = parseEngineArgs(process.env.RISKSIM_ENGINE_ARGS);
  log("info", "spawning_engine", { path: ENGINE_PATH, args });

  engineChild = spawn(ENGINE_PATH, args, {
    stdio: ["pipe", "pipe", "inherit"], // engine stderr -> our stderr
  });

  engineChild.on("error", (err) => {
    log("error", "engine_spawn_error", { error: err.message });
    process.exit(1);
  });

  // Ignore EPIPE writing to engine stdin (e.g. engine gone).
  engineChild.stdin.on("error", (err) => {
    if (err.code === "EPIPE") return;
    log("warn", "engine_stdin_error", { error: err.message });
  });

  const reader = new FrameReader();
  engineChild.stdout.on("data", (chunk) => {
    try {
      for (const frame of reader.push(chunk)) broadcast(frame);
    } catch (err) {
      log("error", "frame_reader_error", { error: err.message });
      process.exit(1);
    }
  });

  engineChild.on("exit", (code, signal) => {
    log("error", "engine_exited", { code, signal });
    // Exit non-zero so a supervisor restarts us (and the engine).
    process.exit(code === 0 ? 1 : (code ?? 1));
  });
}

function parseEngineArgs(raw) {
  if (!raw) return [];
  // Simple whitespace split; engine args are flags + numbers, no quoting needed.
  return raw.trim().split(/\s+/).filter(Boolean);
}

// ----------------------------------------------------------------------------
// Source: replay file looped at ~10 Hz
// ----------------------------------------------------------------------------
function startReplay() {
  log("info", "replay_mode", {
    path: REPLAY_PATH,
    hz: REPLAY_HZ,
    intervalMs: REPLAY_INTERVAL_MS,
  });

  // Read the whole file once into memory, parse all frames, then loop.
  const reader = new FrameReader();
  const frames = [];
  const rs = createReadStream(REPLAY_PATH);

  rs.on("error", (err) => {
    log("error", "replay_read_error", { error: err.message });
    process.exit(1);
  });

  rs.on("data", (chunk) => {
    try {
      for (const frame of reader.push(chunk)) frames.push(frame);
    } catch (err) {
      log("error", "replay_frame_error", { error: err.message });
      process.exit(1);
    }
  });

  rs.on("end", () => {
    if (frames.length === 0) {
      log("error", "replay_empty", { path: REPLAY_PATH });
      process.exit(1);
    }
    log("info", "replay_loaded", {
      frames: frames.length,
      pendingBytes: reader.pending,
    });

    let i = 0;
    setInterval(() => {
      broadcast(frames[i]);
      i = (i + 1) % frames.length; // loop back at EOF
    }, REPLAY_INTERVAL_MS);
  });
}

// ----------------------------------------------------------------------------
// Boot
// ----------------------------------------------------------------------------
process.on("uncaughtException", (err) => {
  if (err && err.code === "EPIPE") return; // ignore broken pipes
  log("error", "uncaught_exception", { error: err.message, stack: err.stack });
  process.exit(1);
});

httpServer.listen(PORT, () => {
  log("info", "listening", { port: PORT, mode: MODE });
  if (MODE === "replay") {
    startReplay();
  } else {
    startLive();
  }
});

function shutdown(sig) {
  log("info", "shutting_down", { signal: sig });
  try {
    if (engineChild) engineChild.kill("SIGTERM");
  } catch {
    /* ignore */
  }
  httpServer.close(() => process.exit(0));
  // Force-exit if close hangs.
  setTimeout(() => process.exit(0), 1000).unref();
}
process.on("SIGINT", () => shutdown("SIGINT"));
process.on("SIGTERM", () => shutdown("SIGTERM"));
