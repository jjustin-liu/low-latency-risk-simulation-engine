import type { DecodeResponse, Shock } from "@/lib/types";
import { useConnectionStore } from "@/stores/connectionStore";
import { useSnapshotStore } from "@/stores/snapshotStore";

const MAGIC = new Uint8Array([0x52, 0x53, 0x49, 0x4d]); // "RSIM"

/**
 * Owns the WebSocket to the bridge and the decode Web Worker. Binary frames
 * are handed to the worker (which decodes + posts back transferables); the
 * worker's reply drives the Zustand stores. Everything here is client-only.
 */
class BridgeClient {
  private ws: WebSocket | null = null;
  private worker: Worker | null = null;
  private manualClose = false;
  private reconnectTimer: ReturnType<typeof setTimeout> | null = null;
  private url = "";

  connect(url: string): void {
    if (typeof window === "undefined") return;
    this.url = url;
    this.manualClose = false;
    useConnectionStore.getState().setUrl(url);

    if (!this.worker) {
      this.worker = new Worker(
        new URL("../workers/decode.worker.ts", import.meta.url),
        { type: "module" },
      );
      this.worker.onmessage = (e: MessageEvent<DecodeResponse>) => {
        const msg = e.data;
        if (!msg.ok) return;
        const latency = Math.max(0, performance.now() - msg.recvTime);
        useConnectionStore.getState().recordFrame(msg.decodeMs, latency);
        useSnapshotStore.getState().ingest(msg.snapshot);
      };
    }

    this.openSocket();
  }

  private openSocket(): void {
    const conn = useConnectionStore.getState();
    conn.setStatus("connecting");
    let ws: WebSocket;
    try {
      ws = new WebSocket(this.url);
    } catch (err) {
      conn.setStatus("error", err instanceof Error ? err.message : String(err));
      this.scheduleReconnect();
      return;
    }
    ws.binaryType = "arraybuffer";
    this.ws = ws;

    ws.onopen = () => useConnectionStore.getState().setStatus("open");

    ws.onmessage = (event) => {
      const data = event.data;
      if (!(data instanceof ArrayBuffer)) return; // ignore any text frames
      if (!looksLikeFrame(data)) return;
      const recvTime = performance.now();
      // Zero-copy handoff to the worker.
      this.worker?.postMessage({ buffer: data, recvTime }, [data]);
    };

    ws.onerror = () => {
      useConnectionStore.getState().setStatus("error", "socket error");
    };

    ws.onclose = () => {
      useConnectionStore.getState().setStatus("closed");
      if (!this.manualClose) this.scheduleReconnect();
    };
  }

  private scheduleReconnect(): void {
    if (this.manualClose || this.reconnectTimer) return;
    this.reconnectTimer = setTimeout(() => {
      this.reconnectTimer = null;
      if (!this.manualClose) this.openSocket();
    }, 2000);
  }

  send(shock: Shock | { type: "clear" }): boolean {
    const payload =
      "type" in shock && shock.type === "clear"
        ? { type: "clear" }
        : { type: "shock", ...(shock as Shock) };
    if (this.ws && this.ws.readyState === WebSocket.OPEN) {
      this.ws.send(JSON.stringify(payload));
      return true;
    }
    return false;
  }

  disconnect(): void {
    this.manualClose = true;
    if (this.reconnectTimer) {
      clearTimeout(this.reconnectTimer);
      this.reconnectTimer = null;
    }
    this.ws?.close();
    this.ws = null;
  }
}

function looksLikeFrame(buf: ArrayBuffer): boolean {
  if (buf.byteLength < 4) return false;
  const head = new Uint8Array(buf, 0, 4);
  return (
    head[0] === MAGIC[0] &&
    head[1] === MAGIC[1] &&
    head[2] === MAGIC[2] &&
    head[3] === MAGIC[3]
  );
}

export const bridge = new BridgeClient();
