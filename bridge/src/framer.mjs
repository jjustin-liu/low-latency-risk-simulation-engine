// Reassembles length-prefixed frames from a byte stream.
//
// Wire stream layout (little-endian):
//   [frameLen: u32 LE][frame bytes] repeated forever.
// `frameLen` is the number of bytes in the wire frame that follows (the
// 36-byte header + body, WITHOUT the 4-byte length prefix). This class
// buffers partial reads and yields complete wire frames (prefix stripped).

const LEN_PREFIX_BYTES = 4;

// Sanity cap so a corrupt/desynced stream can't make us allocate wildly.
// Frames are ~9 KB in practice; 64 MB is far above any legitimate frame.
const MAX_FRAME_BYTES = 64 * 1024 * 1024;

export class FrameReader {
  constructor() {
    this._buf = Buffer.alloc(0);
  }

  // Feed a chunk of bytes. Returns an array of complete frame Buffers
  // (each is a full wire frame WITHOUT the 4-byte length prefix).
  // Throws if a frame length exceeds MAX_FRAME_BYTES (caller decides policy).
  push(chunk) {
    this._buf =
      this._buf.length === 0 ? chunk : Buffer.concat([this._buf, chunk]);

    const frames = [];
    let offset = 0;

    while (this._buf.length - offset >= LEN_PREFIX_BYTES) {
      const frameLen = this._buf.readUInt32LE(offset);

      if (frameLen > MAX_FRAME_BYTES) {
        throw new Error(
          `frame length ${frameLen} exceeds max ${MAX_FRAME_BYTES} (stream desync?)`,
        );
      }

      const totalNeeded = LEN_PREFIX_BYTES + frameLen;
      if (this._buf.length - offset < totalNeeded) {
        break; // wait for more bytes
      }

      const start = offset + LEN_PREFIX_BYTES;
      // Copy so retained frames don't pin the (possibly large) source buffer.
      frames.push(Buffer.from(this._buf.subarray(start, start + frameLen)));
      offset += totalNeeded;
    }

    // Retain only the unconsumed tail.
    this._buf = offset === 0 ? this._buf : this._buf.subarray(offset);

    return frames;
  }

  get pending() {
    return this._buf.length;
  }
}
