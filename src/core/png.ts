/**
 * Enough PNG to shrink a capture before it is handed to an agent: decode any
 * non-interlaced PNG to RGBA8, box-filter it down, and encode it again. Built
 * on node:zlib alone, so no image library ships with the server.
 */
import { deflateSync, inflateSync } from "node:zlib";

const SIGNATURE = Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]);

/** Pixels as RGBA, 8 bits a channel, row-major from the top-left. */
export interface RgbaImage {
  width: number;
  height: number;
  data: Uint8Array;
}

let crcTable: Uint32Array | undefined;

function crc32(bytes: Uint8Array): number {
  if (!crcTable) {
    crcTable = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
      crcTable[n] = c >>> 0;
    }
  }
  let crc = 0xffffffff;
  for (let i = 0; i < bytes.length; i++) crc = crcTable[(crc ^ bytes[i]) & 0xff] ^ (crc >>> 8);
  return (crc ^ 0xffffffff) >>> 0;
}

/** Whether these bytes start with the PNG signature. */
export function isPng(bytes: Uint8Array): boolean {
  return bytes.length >= 8 && SIGNATURE.equals(Buffer.from(bytes.subarray(0, 8)));
}

/** Width and height from the IHDR chunk, without decoding the pixels. */
export function pngSize(bytes: Uint8Array): { width: number; height: number } | undefined {
  if (!isPng(bytes) || bytes.length < 24) return undefined;
  const view = Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (view.toString("latin1", 12, 16) !== "IHDR") return undefined;
  return { width: view.readUInt32BE(16), height: view.readUInt32BE(20) };
}

/** Width and height from a JPEG's first start-of-frame marker. */
export function jpegSize(bytes: Uint8Array): { width: number; height: number } | undefined {
  if (bytes.length < 4 || bytes[0] !== 0xff || bytes[1] !== 0xd8) return undefined;
  let i = 2;
  while (i + 9 < bytes.length) {
    if (bytes[i] !== 0xff) return undefined;
    const marker = bytes[i + 1];
    const length = (bytes[i + 2] << 8) | bytes[i + 3];
    // SOF0-SOF15, except DHT (C4), JPG (C8) and DAC (CC).
    if (marker >= 0xc0 && marker <= 0xcf && marker !== 0xc4 && marker !== 0xc8 && marker !== 0xcc) {
      return { height: (bytes[i + 5] << 8) | bytes[i + 6], width: (bytes[i + 7] << 8) | bytes[i + 8] };
    }
    i += 2 + length;
  }
  return undefined;
}

const CHANNELS: Record<number, number> = { 0: 1, 2: 3, 3: 1, 4: 2, 6: 4 };

function paeth(a: number, b: number, c: number): number {
  const p = a + b - c;
  const pa = Math.abs(p - a);
  const pb = Math.abs(p - b);
  const pc = Math.abs(p - c);
  return pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
}

/** Decode a PNG to RGBA8. Throws with the reason on anything it cannot read. */
export function decodePng(bytes: Uint8Array): RgbaImage {
  if (!isPng(bytes)) throw new Error("not a PNG file");
  const buf = Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let width = 0, height = 0, depth = 0, colorType = -1, interlace = 0;
  let palette: Buffer | undefined;
  let transparency: Buffer | undefined;
  const idat: Buffer[] = [];
  let offset = 8;
  let ended = false;
  while (offset + 8 <= buf.length) {
    const length = buf.readUInt32BE(offset);
    const type = buf.toString("latin1", offset + 4, offset + 8);
    const start = offset + 8;
    if (start + length + 4 > buf.length) throw new Error("PNG is truncated");
    const body = buf.subarray(start, start + length);
    if (type === "IHDR") {
      width = body.readUInt32BE(0);
      height = body.readUInt32BE(4);
      depth = body[8];
      colorType = body[9];
      interlace = body[12];
    } else if (type === "PLTE") {
      palette = body;
    } else if (type === "tRNS") {
      transparency = body;
    } else if (type === "IDAT") {
      idat.push(body);
    } else if (type === "IEND") {
      ended = true;
      break;
    }
    offset = start + length + 4;
  }
  if (!ended) throw new Error("PNG is truncated (no IEND chunk)");
  const channels = CHANNELS[colorType];
  if (!width || !height || channels === undefined) throw new Error(`unsupported PNG (color type ${colorType})`);
  if (interlace !== 0) throw new Error("interlaced PNG is not supported");
  if (colorType === 3 && !palette) throw new Error("palette PNG without a PLTE chunk");

  const raw = inflateSync(Buffer.concat(idat));
  const bitsPerPixel = channels * depth;
  const stride = Math.ceil((width * bitsPerPixel) / 8);
  const filterBpp = Math.max(1, bitsPerPixel >> 3);
  if (raw.length < (stride + 1) * height) throw new Error("PNG pixel data is truncated");

  const out = new Uint8Array(width * height * 4);
  let prev = new Uint8Array(stride);
  let row = new Uint8Array(stride);
  const maxSample = (1 << Math.min(depth, 8)) - 1;
  for (let y = 0; y < height; y++) {
    const base = y * (stride + 1);
    const filter = raw[base];
    for (let x = 0; x < stride; x++) {
      const v = raw[base + 1 + x];
      const a = x >= filterBpp ? row[x - filterBpp] : 0;
      const b = prev[x];
      const c = x >= filterBpp ? prev[x - filterBpp] : 0;
      switch (filter) {
        case 0: row[x] = v; break;
        case 1: row[x] = (v + a) & 0xff; break;
        case 2: row[x] = (v + b) & 0xff; break;
        case 3: row[x] = (v + ((a + b) >> 1)) & 0xff; break;
        case 4: row[x] = (v + paeth(a, b, c)) & 0xff; break;
        default: throw new Error(`bad PNG filter type ${filter}`);
      }
    }
    // One sample at (pixel, channel), scaled to 8 bits. 16-bit keeps its high byte.
    const sample = (px: number, ch: number): number => {
      const index = px * channels + ch;
      if (depth === 8) return row[index];
      if (depth === 16) return row[index * 2];
      const bit = index * depth;
      const value = (row[bit >> 3] >> (8 - depth - (bit & 7))) & maxSample;
      return colorType === 3 ? value : Math.round((value * 255) / maxSample);
    };
    for (let x = 0; x < width; x++) {
      const o = (y * width + x) * 4;
      let r: number, g: number, bl: number, al = 255;
      if (colorType === 3) {
        const i = sample(x, 0);
        r = palette![i * 3] ?? 0; g = palette![i * 3 + 1] ?? 0; bl = palette![i * 3 + 2] ?? 0;
        if (transparency && i < transparency.length) al = transparency[i];
      } else if (colorType === 0 || colorType === 4) {
        r = g = bl = sample(x, 0);
        if (colorType === 4) al = sample(x, 1);
      } else {
        r = sample(x, 0); g = sample(x, 1); bl = sample(x, 2);
        if (colorType === 6) al = sample(x, 3);
      }
      out[o] = r; out[o + 1] = g; out[o + 2] = bl; out[o + 3] = al;
    }
    [prev, row] = [row, prev];
  }
  return { width, height, data: out };
}

/**
 * Shrink to fit a box of `maxEdge` on the longer side, averaging every source
 * pixel a destination pixel covers. Colour is averaged premultiplied, so a
 * transparent pixel's colour does not bleed into its neighbours.
 */
export function downscale(image: RgbaImage, maxEdge: number): RgbaImage {
  const scale = Math.min(1, maxEdge / Math.max(image.width, image.height));
  if (scale >= 1) return image;
  const width = Math.max(1, Math.round(image.width * scale));
  const height = Math.max(1, Math.round(image.height * scale));
  const out = new Uint8Array(width * height * 4);
  const sx = image.width / width;
  const sy = image.height / height;
  for (let y = 0; y < height; y++) {
    const y0 = Math.floor(y * sy);
    const y1 = Math.max(y0 + 1, Math.min(image.height, Math.floor((y + 1) * sy)));
    for (let x = 0; x < width; x++) {
      const x0 = Math.floor(x * sx);
      const x1 = Math.max(x0 + 1, Math.min(image.width, Math.floor((x + 1) * sx)));
      let r = 0, g = 0, b = 0, a = 0, n = 0;
      for (let yy = y0; yy < y1; yy++) {
        for (let xx = x0; xx < x1; xx++) {
          const i = (yy * image.width + xx) * 4;
          const al = image.data[i + 3];
          r += image.data[i] * al; g += image.data[i + 1] * al; b += image.data[i + 2] * al; a += al; n++;
        }
      }
      const o = (y * width + x) * 4;
      if (a > 0) {
        out[o] = Math.round(r / a); out[o + 1] = Math.round(g / a); out[o + 2] = Math.round(b / a);
      }
      out[o + 3] = Math.round(a / n);
    }
  }
  return { width, height, data: out };
}

function chunk(type: string, body: Uint8Array): Buffer {
  const head = Buffer.alloc(8);
  head.writeUInt32BE(body.length, 0);
  head.write(type, 4, "latin1");
  const crc = Buffer.alloc(4);
  crc.writeUInt32BE(crc32(Buffer.concat([head.subarray(4), body])), 0);
  return Buffer.concat([head, body, crc]);
}

/**
 * Encode RGBA8 as a PNG: RGB when every pixel is opaque, RGBA otherwise, each
 * row with whichever filter leaves the smallest residual.
 */
export function encodePng(image: RgbaImage): Buffer {
  const { width, height, data } = image;
  let opaque = true;
  for (let i = 3; i < data.length; i += 4) if (data[i] !== 255) { opaque = false; break; }
  const channels = opaque ? 3 : 4;
  const stride = width * channels;
  const rows = Buffer.alloc((stride + 1) * height);
  let prev = new Uint8Array(stride);
  let line = new Uint8Array(stride);
  const candidate = new Uint8Array(stride);
  const best = new Uint8Array(stride);
  for (let y = 0; y < height; y++) {
    for (let x = 0; x < width; x++) {
      const s = (y * width + x) * 4;
      const d = x * channels;
      line[d] = data[s]; line[d + 1] = data[s + 1]; line[d + 2] = data[s + 2];
      if (channels === 4) line[d + 3] = data[s + 3];
    }
    let bestFilter = 0;
    let bestScore = Infinity;
    for (let f = 0; f <= 4; f++) {
      let score = 0;
      for (let x = 0; x < stride; x++) {
        const a = x >= channels ? line[x - channels] : 0;
        const b = prev[x];
        const c = x >= channels ? prev[x - channels] : 0;
        const predicted = f === 0 ? 0 : f === 1 ? a : f === 2 ? b : f === 3 ? (a + b) >> 1 : paeth(a, b, c);
        const v = (line[x] - predicted) & 0xff;
        candidate[x] = v;
        score += v < 128 ? v : 256 - v;
      }
      if (score < bestScore) {
        bestScore = score;
        bestFilter = f;
        best.set(candidate);
      }
    }
    const base = y * (stride + 1);
    rows[base] = bestFilter;
    rows.set(best, base + 1);
    [prev, line] = [line, prev];
  }
  const ihdr = Buffer.alloc(13);
  ihdr.writeUInt32BE(width, 0);
  ihdr.writeUInt32BE(height, 4);
  ihdr[8] = 8;
  ihdr[9] = opaque ? 2 : 6;
  return Buffer.concat([
    SIGNATURE,
    chunk("IHDR", ihdr),
    chunk("IDAT", deflateSync(rows, { level: 9 })),
    chunk("IEND", new Uint8Array(0)),
  ]);
}
