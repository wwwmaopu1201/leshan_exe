import { Buffer } from "node:buffer";

export const HEAD1 = 0x44;
export const HEAD2 = 0x54;
export const HEADER_SIZE = 23;
const MAX_FRAME_SIZE = 1024 * 1024;
const UPLOAD_PATTERN_COMMAND_TYPE = 0x0b2a;
const UPLOAD_PATTERN_COMMAND_CODE = 0x0007;

function crc16Modbus(buffer) {
  let crc = 0xffff;
  for (const byte of buffer) {
    crc ^= byte;
    for (let i = 0; i < 8; i += 1) {
      if ((crc & 0x0001) !== 0) {
        crc = (crc >> 1) ^ 0xa001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc & 0xffff;
}

function crc16Arc(buffer) {
  let crc = 0x0000;
  for (const byte of buffer) {
    crc ^= byte;
    for (let i = 0; i < 8; i += 1) {
      if ((crc & 0x0001) !== 0) {
        crc = (crc >> 1) ^ 0xa001;
      } else {
        crc >>= 1;
      }
    }
  }
  return crc & 0xffff;
}

function crc16CcittFalse(buffer) {
  let crc = 0xffff;
  for (const byte of buffer) {
    crc ^= byte << 8;
    for (let i = 0; i < 8; i += 1) {
      if ((crc & 0x8000) !== 0) {
        crc = ((crc << 1) ^ 0x1021) & 0xffff;
      } else {
        crc = (crc << 1) & 0xffff;
      }
    }
  }
  return crc & 0xffff;
}

export const PROTOCOL_PRESETS = [
  {
    id: "be-modbus-lencrc",
    name: "大端 / CRC16 Modbus / 长度含CRC",
    byteOrder: "be",
    crcMode: "modbus",
    lengthMode: "includeCrc",
  },
];

export const DEFAULT_PROTOCOL_PRESET = PROTOCOL_PRESETS[0];

const PROTOCOL_PRESET_MAP = new Map(
  PROTOCOL_PRESETS.map((preset) => [preset.id, Object.freeze({ ...preset })])
);

function writeUInt16(buffer, value, offset, byteOrder) {
  if (byteOrder === "be") {
    buffer.writeUInt16BE(value, offset);
  } else {
    buffer.writeUInt16LE(value, offset);
  }
}

function writeUInt32(buffer, value, offset, byteOrder) {
  if (byteOrder === "be") {
    buffer.writeUInt32BE(value >>> 0, offset);
  } else {
    buffer.writeUInt32LE(value >>> 0, offset);
  }
}

function writeInt32(buffer, value, offset, byteOrder) {
  if (byteOrder === "be") {
    buffer.writeInt32BE(value, offset);
  } else {
    buffer.writeInt32LE(value, offset);
  }
}

function readUInt16(buffer, offset, byteOrder) {
  return byteOrder === "be" ? buffer.readUInt16BE(offset) : buffer.readUInt16LE(offset);
}

function readUInt32(buffer, offset, byteOrder) {
  return byteOrder === "be" ? buffer.readUInt32BE(offset) : buffer.readUInt32LE(offset);
}

function readInt32(buffer, offset, byteOrder) {
  return byteOrder === "be" ? buffer.readInt32BE(offset) : buffer.readInt32LE(offset);
}

function calculateCrc(buffer, crcMode) {
  if (crcMode === "arc") {
    return crc16Arc(buffer);
  }

  if (crcMode === "ccitt-false") {
    return crc16CcittFalse(buffer);
  }

  return crc16Modbus(buffer);
}

function computeLengthField(payloadLength, lengthMode) {
  return lengthMode === "payloadOnly" ? payloadLength : payloadLength + 2;
}

function computePacketLength(lengthField, lengthMode) {
  return HEADER_SIZE + lengthField + (lengthMode === "payloadOnly" ? 2 : 0);
}

function isLikelyUtf16LeText(buffer) {
  if (!buffer || buffer.length < 2) {
    return false;
  }

  let validUnits = 0;
  let invalidUnits = 0;
  const evenLength = buffer.length - (buffer.length % 2);
  for (let offset = 0; offset < evenLength; offset += 2) {
    const word = buffer.readUInt16LE(offset);
    if (word === 0x0000 || word === 0xfdfd || word === 0xffff) {
      break;
    }

    const isPrintableAscii = word >= 0x0020 && word <= 0x007e;
    const isCjk =
      (word >= 0x3400 && word <= 0x4dbf) ||
      (word >= 0x4e00 && word <= 0x9fff) ||
      (word >= 0xf900 && word <= 0xfaff);
    const isPunctuation =
      (word >= 0x3000 && word <= 0x303f) || (word >= 0xff00 && word <= 0xffef);

    if (isPrintableAscii || isCjk || isPunctuation) {
      validUnits += 1;
      continue;
    }

    invalidUnits += 1;
  }

  return validUnits > 0 && invalidUnits === 0;
}

function buildParsedFrame(buffer, protocolPreset, payload, crc = null, extra = {}) {
  return {
    head1: buffer.readUInt8(0),
    head2: buffer.readUInt8(1),
    addr1: readUInt32(buffer, 2, protocolPreset.byteOrder),
    addr2: readUInt32(buffer, 6, protocolPreset.byteOrder),
    reserve1: buffer.readUInt8(10),
    reserve2: buffer.readUInt8(11),
    reserve3: buffer.readUInt8(12),
    type: readUInt16(buffer, 13, protocolPreset.byteOrder),
    code: readUInt16(buffer, 15, protocolPreset.byteOrder),
    totalFrames: buffer.readUInt8(17),
    frameNo: buffer.readUInt8(18),
    length: readUInt32(buffer, 19, protocolPreset.byteOrder),
    payload,
    crc,
    protocol: protocolPreset,
    raw: buffer,
    ...extra,
  };
}

function tryRecoverUploadPatternCommandFrame(
  buffer,
  protocolPreset,
  lengthField,
  expectedCrc,
  actualCrc
) {
  if (protocolPreset.id !== "be-modbus-lencrc") {
    return null;
  }

  const type = readUInt16(buffer, 13, protocolPreset.byteOrder);
  const code = readUInt16(buffer, 15, protocolPreset.byteOrder);
  if (type !== UPLOAD_PATTERN_COMMAND_TYPE || code !== UPLOAD_PATTERN_COMMAND_CODE) {
    return null;
  }

  const totalFrames = buffer.readUInt8(17);
  const frameNo = buffer.readUInt8(18);
  if (totalFrames !== 1 || (frameNo !== 0 && frameNo !== 1)) {
    return null;
  }

  if (lengthField < 2 || lengthField > 256) {
    return null;
  }

  const recoveredPayload = buffer.subarray(HEADER_SIZE);
  if (recoveredPayload.length !== lengthField) {
    return null;
  }

  if (!isLikelyUtf16LeText(recoveredPayload.subarray(2))) {
    return null;
  }

  return buildParsedFrame(buffer, protocolPreset, recoveredPayload, null, {
    recovered: {
      type: "upload-pattern-no-crc",
      message: `上传花型指令 CRC 异常，已按无CRC/截断Unicode兼容处理（expected ${expectedCrc}, got ${actualCrc}）`,
      expectedCrc,
      actualCrc,
    },
  });
}

export function getProtocolPreset(protocolPresetId) {
  return PROTOCOL_PRESET_MAP.get(protocolPresetId) ?? null;
}

export function resolveProtocolPreset(protocolPreset) {
  if (!protocolPreset) {
    return DEFAULT_PROTOCOL_PRESET;
  }

  if (typeof protocolPreset === "string") {
    return getProtocolPreset(protocolPreset) ?? DEFAULT_PROTOCOL_PRESET;
  }

  if (protocolPreset.id && PROTOCOL_PRESET_MAP.has(protocolPreset.id)) {
    return getProtocolPreset(protocolPreset.id);
  }

  return {
    ...DEFAULT_PROTOCOL_PRESET,
    ...protocolPreset,
  };
}

export function encodeFrame({
  addr1 = 0,
  addr2 = 0,
  reserve1 = 0,
  reserve2 = 0,
  reserve3 = 0,
  type,
  code,
  totalFrames = 1,
  frameNo = 1,
  payload = Buffer.alloc(0),
  protocol,
}) {
  const protocolPreset = resolveProtocolPreset(protocol);
  const body = Buffer.alloc(HEADER_SIZE + payload.length);
  body.writeUInt8(HEAD1, 0);
  body.writeUInt8(HEAD2, 1);
  writeUInt32(body, addr1 >>> 0, 2, protocolPreset.byteOrder);
  writeUInt32(body, addr2 >>> 0, 6, protocolPreset.byteOrder);
  body.writeUInt8(reserve1, 10);
  body.writeUInt8(reserve2, 11);
  body.writeUInt8(reserve3, 12);
  writeUInt16(body, type, 13, protocolPreset.byteOrder);
  writeUInt16(body, code, 15, protocolPreset.byteOrder);
  body.writeUInt8(totalFrames, 17);
  body.writeUInt8(frameNo, 18);
  writeUInt32(
    body,
    computeLengthField(payload.length, protocolPreset.lengthMode),
    19,
    protocolPreset.byteOrder
  );
  payload.copy(body, HEADER_SIZE);

  const crc = calculateCrc(body, protocolPreset.crcMode);
  const packet = Buffer.alloc(body.length + 2);
  body.copy(packet, 0);
  writeUInt16(packet, crc, body.length, protocolPreset.byteOrder);
  return packet;
}

export function parseFrame(buffer, protocol) {
  const protocolPreset = resolveProtocolPreset(protocol);
  if (buffer.length < HEADER_SIZE + 2) {
    throw new Error("frame too short");
  }

  const head1 = buffer.readUInt8(0);
  const head2 = buffer.readUInt8(1);
  if (head1 !== HEAD1 || head2 !== HEAD2) {
    throw new Error("invalid frame head");
  }

  const lengthField = readUInt32(buffer, 19, protocolPreset.byteOrder);
  const expectedLength = computePacketLength(lengthField, protocolPreset.lengthMode);
  if (buffer.length !== expectedLength) {
    throw new Error("frame length mismatch");
  }

  const expectedCrc = readUInt16(buffer, buffer.length - 2, protocolPreset.byteOrder);
  const actualCrc = calculateCrc(buffer.subarray(0, buffer.length - 2), protocolPreset.crcMode);
  if (expectedCrc !== actualCrc) {
    const recoveredFrame = tryRecoverUploadPatternCommandFrame(
      buffer,
      protocolPreset,
      lengthField,
      expectedCrc,
      actualCrc
    );
    if (recoveredFrame) {
      return recoveredFrame;
    }

    throw new Error(`crc mismatch: expected ${expectedCrc}, got ${actualCrc}`);
  }

  return buildParsedFrame(
    buffer,
    protocolPreset,
    buffer.subarray(HEADER_SIZE, buffer.length - 2),
    expectedCrc
  );
}

export class FrameParser {
  constructor({ protocols } = {}) {
    this.buffer = Buffer.alloc(0);
    this.protocols = (protocols?.length ? protocols : [DEFAULT_PROTOCOL_PRESET]).map(
      resolveProtocolPreset
    );
  }

  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    const packets = [];

    while (this.buffer.length >= HEADER_SIZE + 2) {
      const headIndex = this.buffer.indexOf(Buffer.from([HEAD1, HEAD2]));
      if (headIndex === -1) {
        this.buffer = Buffer.alloc(0);
        break;
      }

      if (headIndex > 0) {
        this.buffer = this.buffer.subarray(headIndex);
      }

      if (this.buffer.length < HEADER_SIZE + 2) {
        break;
      }

      let parsedFrame = null;
      let parsedPacketLength = 0;
      let hasIncompleteCandidate = false;
      const errors = [];

      for (const protocolPreset of this.protocols) {
        const lengthField = readUInt32(this.buffer, 19, protocolPreset.byteOrder);
        const packetLength = computePacketLength(lengthField, protocolPreset.lengthMode);
        if (packetLength < HEADER_SIZE + 2 || packetLength > MAX_FRAME_SIZE) {
          continue;
        }

        if (this.buffer.length < packetLength) {
          hasIncompleteCandidate = true;
          continue;
        }

        const packet = this.buffer.subarray(0, packetLength);

        try {
          parsedFrame = parseFrame(packet, protocolPreset);
          parsedPacketLength = packetLength;
          break;
        } catch (error) {
          errors.push(`${protocolPreset.id}: ${error.message}`);
        }
      }

      if (parsedFrame) {
        this.buffer = this.buffer.subarray(parsedPacketLength);
        packets.push(parsedFrame);
        continue;
      }

      if (hasIncompleteCandidate) {
        break;
      }

      packets.push({
        error: errors.length > 0 ? errors.join(" | ") : "unrecognized frame",
        raw: this.buffer.subarray(0, Math.min(this.buffer.length, HEADER_SIZE + 32)),
      });
      this.buffer = this.buffer.subarray(2);
    }

    return packets;
  }
}

export function encodeUShort(value, byteOrder = "le") {
  const buffer = Buffer.alloc(2);
  writeUInt16(buffer, value, 0, byteOrder);
  return buffer;
}

export function encodeUInt(value, byteOrder = "le") {
  const buffer = Buffer.alloc(4);
  writeUInt32(buffer, value >>> 0, 0, byteOrder);
  return buffer;
}

export function encodeInt(value, byteOrder = "le") {
  const buffer = Buffer.alloc(4);
  writeInt32(buffer, value, 0, byteOrder);
  return buffer;
}

export function decodeUShort(buffer, offset = 0, byteOrder = "le") {
  return readUInt16(buffer, offset, byteOrder);
}

export function decodeUInt(buffer, offset = 0, byteOrder = "le") {
  return readUInt32(buffer, offset, byteOrder);
}

export function decodeInt(buffer, offset = 0, byteOrder = "le") {
  return readInt32(buffer, offset, byteOrder);
}

export function encodeString(value, encoding = "utf8") {
  return Buffer.from(value ?? "", encoding);
}

export function decodeString(buffer, encoding = "utf8") {
  return buffer.toString(encoding).replace(/\0+$/g, "");
}

export function toHex(buffer) {
  return buffer.toString("hex").replace(/(..)/g, "$1 ").trim();
}
