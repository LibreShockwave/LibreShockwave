#!/usr/bin/env node
"use strict";

const crypto = require("crypto");
const http = require("http");
const net = require("net");

const defaultMappings = [
  { name: "Habbo game", wsPort: 30002, tcpHost: "au.h4bbo.net", tcpPort: 30001 },
  { name: "Habbo MUS", wsPort: 38102, tcpHost: "au.h4bbo.net", tcpPort: 38101 }
];

const GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
const OPCODE_CONTINUATION = 0x0;
const OPCODE_TEXT = 0x1;
const OPCODE_BINARY = 0x2;
const OPCODE_CLOSE = 0x8;
const OPCODE_PING = 0x9;
const OPCODE_PONG = 0xa;

function usage() {
  console.log(`Usage: node ws-tcp-proxy.cjs [--map wsPort:tcpHost:tcpPort[:name]]...

Default mappings:
  ws://127.0.0.1:30002  -> tcp://au.h4bbo.net:30001
  ws://127.0.0.1:38102  -> tcp://au.h4bbo.net:38101

The player socket proxy field uses:
  au.h4bbo.net:30001=ws://127.0.0.1:30002
  au.h4bbo.net:38101=ws://127.0.0.1:38102`);
}

function parseMap(value) {
  const parts = value.split(":");
  if (parts.length < 3) {
    throw new Error(`Invalid --map "${value}". Expected wsPort:tcpHost:tcpPort[:name].`);
  }
  const wsPort = Number(parts[0]);
  const tcpHost = parts[1];
  const tcpPort = Number(parts[2]);
  const name = parts.slice(3).join(":") || `${tcpHost}:${tcpPort}`;
  if (!Number.isInteger(wsPort) || wsPort <= 0 ||
      !Number.isInteger(tcpPort) || tcpPort <= 0 ||
      !tcpHost) {
    throw new Error(`Invalid --map "${value}".`);
  }
  return { name, wsPort, tcpHost, tcpPort };
}

function parseArgs(argv) {
  const mappings = [];
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "-h" || arg === "--help") {
      usage();
      process.exit(0);
    }
    if (arg === "--map") {
      index += 1;
      if (index >= argv.length) throw new Error("--map requires a value.");
      mappings.push(parseMap(argv[index]));
    } else if (arg.startsWith("--map=")) {
      mappings.push(parseMap(arg.slice("--map=".length)));
    } else {
      throw new Error(`Unknown argument: ${arg}`);
    }
  }
  return mappings.length > 0 ? mappings : defaultMappings;
}

function acceptKey(key) {
  return crypto.createHash("sha1").update(key + GUID).digest("base64");
}

function encodeFrame(opcode, payload = Buffer.alloc(0)) {
  const bytes = Buffer.isBuffer(payload) ? payload : Buffer.from(payload);
  let headerLength = 2;
  if (bytes.length >= 126 && bytes.length <= 0xffff) {
    headerLength += 2;
  } else if (bytes.length > 0xffff) {
    headerLength += 8;
  }

  const frame = Buffer.alloc(headerLength + bytes.length);
  frame[0] = 0x80 | opcode;
  if (bytes.length < 126) {
    frame[1] = bytes.length;
    bytes.copy(frame, 2);
  } else if (bytes.length <= 0xffff) {
    frame[1] = 126;
    frame.writeUInt16BE(bytes.length, 2);
    bytes.copy(frame, 4);
  } else {
    frame[1] = 127;
    frame.writeBigUInt64BE(BigInt(bytes.length), 2);
    bytes.copy(frame, 10);
  }
  return frame;
}

class FrameParser {
  constructor(onFrame) {
    this.buffer = Buffer.alloc(0);
    this.onFrame = onFrame;
  }

  push(chunk) {
    this.buffer = Buffer.concat([this.buffer, chunk]);
    while (this.parseOne()) {
      // Keep parsing complete frames from the current buffer.
    }
  }

  parseOne() {
    if (this.buffer.length < 2) return false;

    const first = this.buffer[0];
    const second = this.buffer[1];
    const finalFrame = (first & 0x80) !== 0;
    const opcode = first & 0x0f;
    const masked = (second & 0x80) !== 0;
    let length = second & 0x7f;
    let offset = 2;

    if (length === 126) {
      if (this.buffer.length < offset + 2) return false;
      length = this.buffer.readUInt16BE(offset);
      offset += 2;
    } else if (length === 127) {
      if (this.buffer.length < offset + 8) return false;
      const bigLength = this.buffer.readBigUInt64BE(offset);
      if (bigLength > BigInt(Number.MAX_SAFE_INTEGER)) {
        throw new Error("WebSocket frame is too large.");
      }
      length = Number(bigLength);
      offset += 8;
    }

    let mask = null;
    if (masked) {
      if (this.buffer.length < offset + 4) return false;
      mask = this.buffer.subarray(offset, offset + 4);
      offset += 4;
    }

    if (this.buffer.length < offset + length) return false;
    const payload = Buffer.from(this.buffer.subarray(offset, offset + length));
    this.buffer = this.buffer.subarray(offset + length);

    if (mask) {
      for (let index = 0; index < payload.length; index += 1) {
        payload[index] ^= mask[index % 4];
      }
    }
    this.onFrame({ finalFrame, opcode, payload });
    return this.buffer.length > 0;
  }
}

function createProxy(mapping) {
  const server = http.createServer();

  server.on("upgrade", (req, socket) => {
    const key = req.headers["sec-websocket-key"];
    if (!key) {
      socket.destroy();
      return;
    }

    socket.write([
      "HTTP/1.1 101 Switching Protocols",
      "Upgrade: websocket",
      "Connection: Upgrade",
      `Sec-WebSocket-Accept: ${acceptKey(key)}`,
      "",
      ""
    ].join("\r\n"));

    const tcp = net.createConnection({ host: mapping.tcpHost, port: mapping.tcpPort });
    let closing = false;
    let fragmented = [];
    let fragmentOpcode = 0;

    const closeBoth = () => {
      if (closing) return;
      closing = true;
      try { socket.end(encodeFrame(OPCODE_CLOSE)); } catch {}
      tcp.destroy();
    };

    tcp.on("connect", () => {
      console.log(`[${mapping.name}] connected tcp://${mapping.tcpHost}:${mapping.tcpPort}`);
    });
    tcp.on("data", (data) => {
      if (!socket.destroyed) {
        socket.write(encodeFrame(OPCODE_BINARY, data));
      }
    });
    tcp.on("close", closeBoth);
    tcp.on("error", (error) => {
      console.error(`[${mapping.name}] TCP error: ${error.message}`);
      closeBoth();
    });

    const parser = new FrameParser(({ finalFrame, opcode, payload }) => {
      if (opcode === OPCODE_CLOSE) {
        closeBoth();
        return;
      }
      if (opcode === OPCODE_PING) {
        socket.write(encodeFrame(OPCODE_PONG, payload));
        return;
      }
      if (opcode === OPCODE_PONG) {
        return;
      }
      if (opcode === OPCODE_TEXT || opcode === OPCODE_BINARY) {
        if (finalFrame) {
          if (tcp.writable) tcp.write(payload);
          return;
        }
        fragmentOpcode = opcode;
        fragmented = [payload];
        return;
      }
      if (opcode === OPCODE_CONTINUATION && fragmentOpcode) {
        fragmented.push(payload);
        if (finalFrame) {
          if (tcp.writable) tcp.write(Buffer.concat(fragmented));
          fragmentOpcode = 0;
          fragmented = [];
        }
      }
    });

    socket.on("data", (data) => {
      try {
        parser.push(data);
      } catch (error) {
        console.error(`[${mapping.name}] WebSocket parse error: ${error.message}`);
        closeBoth();
      }
    });
    socket.on("close", closeBoth);
    socket.on("error", (error) => {
      console.error(`[${mapping.name}] WebSocket error: ${error.message}`);
      closeBoth();
    });
  });

  server.on("error", (error) => {
    console.error(`[${mapping.name}] server error: ${error.message}`);
  });

  server.listen(mapping.wsPort, "127.0.0.1", () => {
    console.log(`[${mapping.name}] ws://127.0.0.1:${mapping.wsPort} -> tcp://${mapping.tcpHost}:${mapping.tcpPort}`);
  });
  return server;
}

try {
  const mappings = parseArgs(process.argv.slice(2));
  const servers = mappings.map(createProxy);
  process.on("SIGINT", () => {
    for (const server of servers) server.close();
    process.exit(0);
  });
} catch (error) {
  console.error(error.message);
  usage();
  process.exit(1);
}
