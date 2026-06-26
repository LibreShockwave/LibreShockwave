/* global createLibreShockwaveCppWasm */
"use strict";

let modulePromise = null;
let Module = null;
let api = null;
let handle = 0;
let playing = false;
let timer = 0;
let nextTickAt = 0;
let movieUrl = "";
let paramsText = "";
let tempoOverride = 0;
let debugPlaybackEnabled = false;
let slowHandlerWarningMs = 1000;
let websocketPath = "";
let websocketSsl = null;
let fetchCache = new Map();
const sockets = new Map();

function post(type, payload = {}, transfer) {
  self.postMessage({ type, ...payload }, transfer || []);
}

function errorMessage(error) {
  if (!error) return "unknown error";
  if (error.message) return error.message;
  return String(error);
}

function errorDetails(error) {
  if (!error) return "unknown error";
  if (error.stack) return error.stack;
  return errorMessage(error);
}

function postDebug(level, message) {
  if (message) post("debug", { level, message });
}

function bridgeCall(name, callback, fallback) {
  const started = performance.now();
  try {
    const result = callback();
    const elapsed = performance.now() - started;
    if (elapsed >= 1000) {
      postDebug("warning", `${name} took ${elapsed.toFixed(1)}ms`);
    }
    return result;
  } catch (error) {
    const message = `${name}: ${errorMessage(error)}`;
    post("warning", { message });
    postDebug("warning", `${name}: ${errorDetails(error)}`);
    return fallback;
  }
}

self.addEventListener("error", (event) => {
  post("error", { message: errorMessage(event.error || event.message), detail: errorDetails(event.error || event.message) });
  event.preventDefault();
});

self.addEventListener("unhandledrejection", (event) => {
  post("error", { message: errorMessage(event.reason), detail: errorDetails(event.reason) });
  event.preventDefault();
});

function ensureModule() {
  if (modulePromise) return modulePromise;
  importScripts("libreshockwave-cpp-wasm.js");
  modulePromise = createLibreShockwaveCppWasm({
    locateFile(path) {
      return path;
    }
  }).then((mod) => {
    Module = mod;
    api = {
      create: Module.cwrap("lsw_create", "number", []),
      destroy: Module.cwrap("lsw_destroy", null, ["number"]),
      loadMovie: Module.cwrap("lsw_load_movie", "number", ["number", "string", "number", "number", "string"]),
      setParams: Module.cwrap("lsw_set_external_params", null, ["number", "string"]),
      setPreloadCasts: Module.cwrap("lsw_set_preload_casts", null, ["number", "number"]),
      setDebugPlayback: Module.cwrap("lsw_set_debug_playback_enabled", null, ["number", "number"]),
      setSlowHandlerWarningMs: Module.cwrap("lsw_set_slow_handler_warning_ms", null, ["number", "number"]),
      setTempoOverride: Module.cwrap("lsw_set_tempo_override", null, ["number", "number"]),
      tempo: Module.cwrap("lsw_tempo", "number", ["number"]),
      baseTempo: Module.cwrap("lsw_base_tempo", "number", ["number"]),
      play: Module.cwrap("lsw_play", null, ["number"]),
      pause: Module.cwrap("lsw_pause", null, ["number"]),
      stop: Module.cwrap("lsw_stop", null, ["number"]),
      tick: Module.cwrap("lsw_tick", "number", ["number"]),
      render: Module.cwrap("lsw_render_frame", "number", ["number"]),
      framePixels: Module.cwrap("lsw_frame_pixels", "number", ["number"]),
      frameByteLength: Module.cwrap("lsw_frame_byte_length", "number", ["number"]),
      frameInfoJson: Module.cwrap("lsw_frame_info_json", "string", ["number"]),
      frameSpritesJson: Module.cwrap("lsw_frame_sprites_json", "string", ["number"]),
      imageOperationTraceJson: Module.cwrap("lsw_image_operation_trace_json", "string", ["number"]),
      clearImageOperationTrace: Module.cwrap("lsw_clear_image_operation_trace", null, ["number"]),
      fireTestError: Module.cwrap("lsw_fire_test_error", "number", ["number", "string"]),
      lastError: Module.cwrap("lsw_last_error", "string", ["number"]),
      pollDebugMessages: Module.cwrap("lsw_poll_debug_messages", "string", ["number"]),
      drainDebugMessages: Module.cwrap("lsw_drain_debug_messages", null, ["number"]),
      pollFetches: Module.cwrap("lsw_poll_fetch_requests", "string", ["number"]),
      drainFetches: Module.cwrap("lsw_drain_fetch_requests", null, ["number"]),
      pollMovieNavigations: Module.cwrap("lsw_poll_movie_navigation_requests", "string", ["number"]),
      movieNavigationComplete: Module.cwrap("lsw_movie_navigation_complete", null, ["number", "number"]),
      fetchComplete: Module.cwrap("lsw_fetch_complete", null, ["number", "number", "number", "number"]),
      fetchError: Module.cwrap("lsw_fetch_error", null, ["number", "number", "number"]),
      pollSockets: Module.cwrap("lsw_poll_multiuser_requests", "string", ["number"]),
      drainSockets: Module.cwrap("lsw_drain_multiuser_requests", null, ["number"]),
      pollAudio: Module.cwrap("lsw_poll_audio_commands", "string", ["number"]),
      drainAudio: Module.cwrap("lsw_drain_audio_commands", null, ["number"]),
      audioStopped: Module.cwrap("lsw_audio_stopped", null, ["number", "number"]),
      socketConnected: Module.cwrap("lsw_multiuser_connected", null, ["number", "number"]),
      socketDisconnected: Module.cwrap("lsw_multiuser_disconnected", null, ["number", "number"]),
      socketError: Module.cwrap("lsw_multiuser_error", null, ["number", "number", "number"]),
      socketMessageBytes: Module.cwrap("lsw_multiuser_message_bytes", null, ["number", "number", "number", "number"]),
      mouseMove: Module.cwrap("lsw_mouse_move", "number", ["number", "number", "number"]),
      mouseDown: Module.cwrap("lsw_mouse_down", null, ["number", "number", "number", "number"]),
      mouseUp: Module.cwrap("lsw_mouse_up", null, ["number", "number", "number", "number"]),
      blur: Module.cwrap("lsw_blur", null, ["number"]),
      keyDown: Module.cwrap("lsw_key_down", null, ["number", "number", "string", "number", "number", "number"]),
      keyUp: Module.cwrap("lsw_key_up", null, ["number", "number", "string", "number", "number", "number"]),
      pasteText: Module.cwrap("lsw_paste_text", null, ["number", "string"]),
      selectedText: Module.cwrap("lsw_selected_text", "string", ["number"]),
      selectAll: Module.cwrap("lsw_select_all", null, ["number"]),
      cutSelectedText: Module.cwrap("lsw_cut_selected_text", "string", ["number"])
    };
    handle = api.create();
    post("ready");
  });
  return modulePromise;
}

function withBytes(bytes, callback) {
  const length = bytes.byteLength || bytes.length || 0;
  const ptr = Module._malloc(Math.max(1, length));
  try {
    Module.HEAPU8.set(bytes, ptr);
    return callback(ptr, length);
  } finally {
    Module._free(ptr);
  }
}

function bytePreview(bytes, limit = 16) {
  const length = bytes.byteLength || bytes.length || 0;
  const count = Math.min(length, limit);
  const hex = [];
  let ascii = "";
  for (let index = 0; index < count; index += 1) {
    const value = bytes[index] & 0xff;
    hex.push(value.toString(16).padStart(2, "0"));
    ascii += value >= 32 && value <= 126 ? String.fromCharCode(value) : ".";
  }
  return { hex: hex.join(" "), ascii };
}

function parseJsonArray(text) {
  if (!text) return [];
  try {
    const parsed = JSON.parse(text);
    return Array.isArray(parsed) ? parsed : [];
  } catch (error) {
    post("warning", { message: `Bad bridge JSON: ${error.message}` });
    return [];
  }
}

function emitDebugMessages() {
  if (!handle || !api || !api.pollDebugMessages) return;
  const messages = parseJsonArray(bridgeCall("poll debug messages", () => api.pollDebugMessages(handle), "[]"));
  if (messages.length > 0) {
    bridgeCall("drain debug messages", () => api.drainDebugMessages(handle));
    for (const entry of messages) {
      if (entry && typeof entry === "object") {
        postDebug(entry.level || "debug", entry.message || "");
      } else if (entry) {
        postDebug("debug", String(entry));
      }
    }
  }
}

function resolveUrl(candidate, baseUrl = movieUrl) {
  if (!candidate) return "";
  try {
    return new URL(candidate, baseUrl || self.location.href).href;
  } catch {
    return candidate;
  }
}

function isDirectorMovieUrl(url) {
  try {
    const pathname = new URL(url, movieUrl || self.location.href).pathname.toLowerCase();
    return pathname.endsWith(".dcr") || pathname.endsWith(".dir") || pathname.endsWith(".dxr");
  } catch {
    const cleanUrl = String(url || "").split(/[?#]/, 1)[0].toLowerCase();
    return cleanUrl.endsWith(".dcr") || cleanUrl.endsWith(".dir") || cleanUrl.endsWith(".dxr");
  }
}

function fetchCandidates(candidate) {
  const resolved = resolveUrl(candidate);
  return resolved ? [resolved] : [];
}

function isHttpStatusError(error) {
  return /^(4|5)\d\d\b/.test(errorMessage(error));
}

function isRemoteCandidate(url) {
  try {
    return new URL(url).origin !== self.location.origin;
  } catch {
    return false;
  }
}

function delay(ms) {
  return new Promise((resolve) => setTimeout(resolve, ms));
}

async function fetchBytes(url, request = {}) {
  const method = request.method || "GET";
  const cacheKey = method === "GET" ? url : "";
  if (cacheKey && fetchCache.has(cacheKey)) {
    return fetchCache.get(cacheKey).slice(0);
  }

  const response = await fetch(url, {
    method,
    body: method === "POST" ? (request.postData || "") : undefined,
    credentials: "omit",
    cache: "default"
  });
  if (!response.ok) {
    throw new Error(`${response.status} ${response.statusText}`.trim());
  }
  const bytes = new Uint8Array(await response.arrayBuffer());
  if (cacheKey) {
    fetchCache.set(cacheKey, bytes.slice(0));
  }
  return bytes;
}

async function fetchCandidateBytes(candidate, request) {
  const retryDelays = isRemoteCandidate(candidate) ? [350, 1200, 2400] : [];
  let lastError = null;
  for (let attempt = 0; attempt <= retryDelays.length; attempt += 1) {
    try {
      return await fetchBytes(candidate, request);
    } catch (error) {
      lastError = error;
      if (isHttpStatusError(error) || attempt >= retryDelays.length) {
        break;
      }
      await delay(retryDelays[attempt]);
    }
  }
  throw lastError;
}

async function satisfyFetchRequest(request) {
  const candidates = [request.url, ...(request.fallbacks || [])]
    .filter(Boolean)
    .flatMap((candidate) => fetchCandidates(candidate));
  const uniqueCandidates = [...new Set(candidates)];
  let lastError = null;
  for (const candidate of uniqueCandidates) {
    try {
      const bytes = await fetchCandidateBytes(candidate, request);
      withBytes(bytes, (ptr, length) => bridgeCall("fetch complete", () => api.fetchComplete(handle, request.taskId, ptr, length)));
      return;
    } catch (error) {
      lastError = error;
    }
  }
  bridgeCall("fetch error", () => {
    api.fetchError(handle, request.taskId, lastError && /^(4|5)\d\d/.test(errorMessage(lastError))
      ? Number(errorMessage(lastError).slice(0, 3))
      : 404);
  });
  post("warning", { message: `Fetch failed for ${request.url}: ${lastError ? lastError.message : "no candidates"}` });
}

function decodeBase64(value) {
  if (!value) return new Uint8Array();
  const binary = atob(value);
  const bytes = new Uint8Array(binary.length);
  for (let index = 0; index < binary.length; index += 1) {
    bytes[index] = binary.charCodeAt(index) & 0xff;
  }
  return bytes;
}

function bytesFromWebSocketText(value) {
  const text = String(value || "");
  const bytes = new Uint8Array(text.length);
  for (let index = 0; index < text.length; index += 1) {
    bytes[index] = text.charCodeAt(index) & 0xff;
  }
  return bytes;
}

function socketUrlFor(host, port) {
  const secure = websocketSsl === null ? self.location.protocol === "https:" : Boolean(websocketSsl);
  const scheme = secure ? "wss" : "ws";
  return `${scheme}://${host}:${port}${websocketPath || ""}`;
}

function flushSocket(instanceId) {
  const entry = sockets.get(instanceId);
  if (!entry || entry.ws.readyState !== WebSocket.OPEN) return;
  while (entry.pending.length > 0) {
    entry.ws.send(entry.pending.shift());
  }
}

function connectSocket(request) {
  const previous = sockets.get(request.instanceId);
  if (previous) previous.ws.close();

  const url = socketUrlFor(request.host, request.port);
  post("socket", {
    phase: "connect-request",
    instanceId: request.instanceId,
    host: request.host,
    port: request.port,
    url
  });
  const entry = { ws: null, pending: [] };
  const ws = new WebSocket(url);
  entry.ws = ws;
  ws.binaryType = "arraybuffer";
  sockets.set(request.instanceId, entry);

  ws.addEventListener("open", () => {
    post("socket", { phase: "open", instanceId: request.instanceId, url });
    bridgeCall("socket connected", () => api.socketConnected(handle, request.instanceId));
    emitDebugMessages();
    flushSocket(request.instanceId);
    void pumpHostQueues().then(sendFrame).catch((error) => post("error", { message: errorMessage(error) }));
  });
  ws.addEventListener("message", (event) => {
    const deliver = (bytes) => {
      post("socket", { phase: "message", instanceId: request.instanceId, byteLength: bytes.byteLength || bytes.length || 0, preview: bytePreview(bytes), url });
      withBytes(bytes, (ptr, length) => bridgeCall("socket message", () => api.socketMessageBytes(handle, request.instanceId, ptr, length)));
      emitDebugMessages();
      void pumpHostQueues().then(sendFrame).catch((error) => post("error", { message: errorMessage(error) }));
    };
    if (event.data instanceof ArrayBuffer) {
      deliver(new Uint8Array(event.data));
    } else if (event.data instanceof Blob) {
      event.data.arrayBuffer()
        .then((buffer) => deliver(new Uint8Array(buffer)))
        .catch((error) => post("warning", { message: `WebSocket blob read failed: ${errorMessage(error)}` }));
    } else {
      deliver(bytesFromWebSocketText(event.data));
    }
  });
  ws.addEventListener("error", () => {
    post("socket", { phase: "error", instanceId: request.instanceId, url });
    bridgeCall("socket error", () => api.socketError(handle, request.instanceId, -2));
    emitDebugMessages();
    post("warning", { message: `WebSocket error for ${request.host}:${request.port}` });
  });
  ws.addEventListener("close", (event) => {
    post("socket", { phase: "close", instanceId: request.instanceId, code: event.code || 0, url });
    bridgeCall("socket disconnected", () => api.socketDisconnected(handle, request.instanceId));
    if (event.code && event.code !== 1000) {
      bridgeCall("socket close error", () => api.socketError(handle, request.instanceId, event.code));
    }
    emitDebugMessages();
    sockets.delete(request.instanceId);
  });
}

function sendSocketBytes(request) {
  const bytes = decodeBase64(request.bytes);
  const entry = sockets.get(request.instanceId);
  if (!entry) {
    bridgeCall("socket missing", () => api.socketError(handle, request.instanceId, -5));
    return;
  }
  if (entry.ws.readyState === WebSocket.OPEN) {
    post("socket", { phase: "send", instanceId: request.instanceId, byteLength: bytes.byteLength || bytes.length || 0, preview: bytePreview(bytes) });
    entry.ws.send(bytes);
  } else if (entry.ws.readyState === WebSocket.CONNECTING) {
    post("socket", { phase: "queue-send", instanceId: request.instanceId, byteLength: bytes.byteLength || bytes.length || 0, preview: bytePreview(bytes) });
    entry.pending.push(bytes);
  } else {
    bridgeCall("socket closed", () => api.socketError(handle, request.instanceId, -5));
  }
}

function handleSocketRequest(request) {
  if (request.type === 0) {
    connectSocket(request);
  } else if (request.type === 1) {
    sendSocketBytes(request);
  } else if (request.type === 2) {
    const entry = sockets.get(request.instanceId);
    if (entry) {
      entry.ws.close();
      sockets.delete(request.instanceId);
    }
    bridgeCall("socket disconnected", () => api.socketDisconnected(handle, request.instanceId));
  }
}

function emitAudioCommands() {
  if (!handle || !api || !api.pollAudio) return 0;
  const commands = parseJsonArray(bridgeCall("poll audio commands", () => api.pollAudio(handle), "[]"));
  if (commands.length === 0) return 0;
  bridgeCall("drain audio commands", () => api.drainAudio(handle));
  for (const command of commands) {
    if (!command || typeof command !== "object") continue;
    const { data, ...payload } = command;
    const bytes = data ? decodeBase64(data) : new Uint8Array();
    post("audio", { command: payload, bytes }, bytes.byteLength > 0 ? [bytes.buffer] : undefined);
  }
  return commands.length;
}

async function pumpHostQueues() {
  if (!handle) return;
  for (let round = 0; round < 64; round += 1) {
    const fetches = parseJsonArray(bridgeCall("poll fetches", () => api.pollFetches(handle), "[]"));
    if (fetches.length > 0) {
      bridgeCall("drain fetches", () => api.drainFetches(handle));
      await Promise.all(fetches.map(satisfyFetchRequest));
    }

    const navigations = parseJsonArray(bridgeCall("poll movie navigations", () => api.pollMovieNavigations(handle), "[]"));
    if (navigations.length > 0) {
      for (const nav of navigations) {
        const url = resolveUrl(nav.url);
        bridgeCall("movie navigation complete", () => api.movieNavigationComplete(handle, nav.taskId));
        if (!isDirectorMovieUrl(url)) {
          playing = false;
          clearTimer();
          post("navigation", { url });
          return;
        }
        await loadMovie(url, true);
      }
      return;
    }

    const socketRequests = parseJsonArray(bridgeCall("poll sockets", () => api.pollSockets(handle), "[]"));
    if (socketRequests.length > 0) {
      bridgeCall("drain sockets", () => api.drainSockets(handle));
      socketRequests.forEach(handleSocketRequest);
    }

    const audioCommands = emitAudioCommands();

    if (fetches.length === 0 && navigations.length === 0 && socketRequests.length === 0 && audioCommands === 0) {
      break;
    }
  }
}

function currentDelayMs() {
  const tempo = clampTempo(tempoOverride || bridgeCall("tempo", () => api.tempo(handle), 15));
  return Math.max(1, Math.round(1000 / tempo));
}

function clampTempo(value) {
  return Math.max(1, Math.min(240, Number(value || 0) || 15));
}

function applyDetectedTempoOverride() {
  const detectedTempo = clampTempo(bridgeCall("base tempo", () => api.baseTempo(handle), 15));
  tempoOverride = detectedTempo;
  bridgeCall("set detected tempo override", () => api.setTempoOverride(handle, tempoOverride));
  post("tempo", { tempo: tempoOverride, baseTempo: detectedTempo, source: "movie" });
  return detectedTempo;
}

function clearTimer() {
  if (timer) {
    clearTimeout(timer);
    timer = 0;
  }
}

function scheduleTick() {
  clearTimer();
  if (!playing || !handle) return;
  const now = performance.now();
  if (!nextTickAt || nextTickAt < now - 1000) {
    nextTickAt = now + currentDelayMs();
  }
  timer = setTimeout(async () => {
    timer = 0;
    if (!playing || !handle) return;
    try {
      const delay = currentDelayMs();
      nextTickAt += delay;
      if (nextTickAt < performance.now() - delay) {
        nextTickAt = performance.now();
      }
      bridgeCall("play", () => api.play(handle));
      const tickResult = bridgeCall("tick", () => api.tick(handle), 0);
      emitDebugMessages();
      if (!tickResult) {
        const message = bridgeCall("last error", () => api.lastError(handle), "");
        playing = false;
        if (message) {
          post("error", { message, detail: message });
        }
        return;
      }
      await pumpHostQueues();
      emitDebugMessages();
      sendFrame();
    } catch (error) {
      playing = false;
      post("error", { message: errorMessage(error), detail: errorDetails(error) });
    }
    scheduleTick();
  }, Math.max(0, Math.round(nextTickAt - now)));
}

function sendFrame() {
  if (!handle) return;
  const byteLength = bridgeCall("frame byte length", () => api.frameByteLength(handle), 0);
  const ptr = bridgeCall("frame pixels", () => api.framePixels(handle), 0);
  const infoText = bridgeCall("frame info", () => api.frameInfoJson(handle), "{}");
  let info = {};
  try {
    info = JSON.parse(infoText || "{}");
  } catch {
    info = {};
  }
  if (!ptr || byteLength <= 0 || !info.width || !info.height) {
    post("frameInfo", { info });
    return;
  }
  const pixels = Module.HEAPU8.slice(ptr, ptr + byteLength);
  post("frame", { info, pixels }, [pixels.buffer]);
}

function sendFrameInfo() {
  if (!handle) return;
  const infoText = bridgeCall("frame info", () => api.frameInfoJson(handle), "{}");
  let info = {};
  try {
    info = JSON.parse(infoText || "{}");
  } catch {
    info = {};
  }
  post("frameInfo", { info });
}

function paramsObjectToText(params) {
  if (!params) return "";
  const text = typeof params === "string"
    ? params
    : Object.entries(params)
    .filter(([key]) => key)
    .map(([key, value]) => `${key}=${value == null ? "" : String(value)}`)
    .join("\n");
  return text
    .replace(/\r\n?/g, "\n")
    .replace(/\\r\\n/g, "\n")
    .replace(/\\r/g, "\n")
    .replace(/\\n/g, "\n");
}

async function loadMovie(url, keepPlaying = false, requestId = 0) {
  movieUrl = resolveUrl(url || movieUrl);
  clearTimer();
  post("audio", { command: { action: "stopAll" } });
  const wasPlaying = keepPlaying || playing;
  playing = false;
  nextTickAt = 0;
  try {
    post("load-start", { url: movieUrl, requestId });
    const bytes = await fetchBytes(movieUrl);
    tempoOverride = 0;
    bridgeCall("clear tempo override before load", () => api.setTempoOverride(handle, 0));
    bridgeCall("set debug playback before load", () => api.setDebugPlayback(handle, debugPlaybackEnabled ? 1 : 0));
    const ok = withBytes(bytes, (ptr, length) => bridgeCall("load movie", () => api.loadMovie(handle, movieUrl, ptr, length, paramsText), 0));
    emitDebugMessages();
    if (!ok) {
      throw new Error(bridgeCall("last error", () => api.lastError(handle), "") || "load failed");
    }
    await pumpHostQueues();
    emitDebugMessages();
    applyDetectedTempoOverride();
    bridgeCall("render", () => api.render(handle), 0);
    emitDebugMessages();
    sendFrame();
    playing = wasPlaying;
    if (playing) bridgeCall("play", () => api.play(handle));
    post("load", {
      url: movieUrl,
      info: JSON.parse(bridgeCall("frame info", () => api.frameInfoJson(handle), "{}") || "{}"),
      requestId
    });
    scheduleTick();
  } catch (error) {
    post("error", { message: error.message || String(error), detail: errorDetails(error), requestId });
  } finally {
    emitDebugMessages();
  }
}

async function init(message) {
  await ensureModule();
  websocketPath = message.websocketPath || "";
  websocketSsl = typeof message.websocketSsl === "boolean" ? message.websocketSsl : null;
  paramsText = paramsObjectToText(message.params);
  tempoOverride = Number(message.tempoOverride || 0);
  debugPlaybackEnabled = Boolean(message.debugPlaybackEnabled);
  slowHandlerWarningMs = Math.max(0, Number(message.slowHandlerWarningMs || 1000));
  bridgeCall("set params", () => api.setParams(handle, paramsText));
  bridgeCall("set tempo override", () => api.setTempoOverride(handle, tempoOverride));
  bridgeCall("set preload casts", () => api.setPreloadCasts(handle, message.preloadCasts === false ? 0 : 1));
  bridgeCall("set debug playback", () => api.setDebugPlayback(handle, debugPlaybackEnabled ? 1 : 0));
  bridgeCall("set slow handler warning threshold", () => api.setSlowHandlerWarningMs(handle, slowHandlerWarningMs));
  emitDebugMessages();
  if (message.autoload && message.url) {
    playing = message.autoplay !== false;
    await loadMovie(message.url, playing);
  }
}

self.addEventListener("message", (event) => {
  const message = event.data || {};
  void (async () => {
    await ensureModule();
    switch (message.type) {
      case "init":
        await init(message);
        break;
      case "load":
        if (typeof message.debugPlaybackEnabled === "boolean") {
          debugPlaybackEnabled = message.debugPlaybackEnabled;
        }
        if (Number.isFinite(Number(message.slowHandlerWarningMs))) {
          slowHandlerWarningMs = Math.max(0, Number(message.slowHandlerWarningMs));
        }
        paramsText = paramsObjectToText(message.params ?? paramsText);
        bridgeCall("set params", () => api.setParams(handle, paramsText));
        bridgeCall("set debug playback", () => api.setDebugPlayback(handle, debugPlaybackEnabled ? 1 : 0));
        bridgeCall("set slow handler warning threshold", () => api.setSlowHandlerWarningMs(handle, slowHandlerWarningMs));
        emitDebugMessages();
        playing = message.autoplay !== false;
        await loadMovie(message.url, playing, message.requestId || 0);
        break;
      case "play":
        playing = true;
        bridgeCall("play", () => api.play(handle));
        await pumpHostQueues();
        nextTickAt = performance.now() + currentDelayMs();
        scheduleTick();
        break;
      case "pause":
        playing = false;
        bridgeCall("pause", () => api.pause(handle));
        clearTimer();
        await pumpHostQueues();
        sendFrame();
        break;
      case "stop":
        playing = false;
        bridgeCall("stop", () => api.stop(handle));
        clearTimer();
        await pumpHostQueues();
        sendFrame();
        break;
      case "tempo":
        tempoOverride = Number(message.tempo || 0);
        bridgeCall("set tempo override", () => api.setTempoOverride(handle, tempoOverride));
        nextTickAt = performance.now() + currentDelayMs();
        scheduleTick();
        sendFrame();
        break;
      case "debugPlayback":
        debugPlaybackEnabled = Boolean(message.enabled);
        bridgeCall("set debug playback", () => api.setDebugPlayback(handle, debugPlaybackEnabled ? 1 : 0));
        emitDebugMessages();
        break;
      case "slowHandlerWarningMs":
        slowHandlerWarningMs = Math.max(0, Number(message.milliseconds || 0));
        bridgeCall("set slow handler warning threshold", () => api.setSlowHandlerWarningMs(handle, slowHandlerWarningMs));
        emitDebugMessages();
        break;
      case "params":
        paramsText = paramsObjectToText(message.params);
        bridgeCall("set params", () => api.setParams(handle, paramsText));
        break;
      case "mouseMove":
        const mouseMoveFlags = bridgeCall("mouse move", () => api.mouseMove(handle, message.x | 0, message.y | 0), 0);
        await pumpHostQueues();
        if (mouseMoveFlags & 1) {
          sendFrame();
        } else if (mouseMoveFlags & 2) {
          sendFrameInfo();
        }
        break;
      case "mouseDown":
        bridgeCall("mouse down", () => api.mouseDown(handle, message.x | 0, message.y | 0, message.right ? 1 : 0));
        await pumpHostQueues();
        sendFrame();
        break;
      case "mouseUp":
        bridgeCall("mouse up", () => api.mouseUp(handle, message.x | 0, message.y | 0, message.right ? 1 : 0));
        await pumpHostQueues();
        sendFrame();
        break;
      case "blur":
        bridgeCall("blur", () => api.blur(handle));
        await pumpHostQueues();
        sendFrame();
        break;
      case "keyDown":
        bridgeCall("key down", () => api.keyDown(handle, message.keyCode | 0, message.text || "", message.shift ? 1 : 0, message.ctrl ? 1 : 0, message.alt ? 1 : 0));
        await pumpHostQueues();
        sendFrame();
        break;
      case "keyUp":
        bridgeCall("key up", () => api.keyUp(handle, message.keyCode | 0, message.text || "", message.shift ? 1 : 0, message.ctrl ? 1 : 0, message.alt ? 1 : 0));
        await pumpHostQueues();
        sendFrame();
        break;
      case "paste":
        bridgeCall("paste", () => api.pasteText(handle, message.text || ""));
        await pumpHostQueues();
        sendFrame();
        break;
      case "selectAll":
        bridgeCall("select all", () => api.selectAll(handle));
        sendFrame();
        break;
      case "selectedText":
        post("selectedText", { requestId: message.requestId, text: bridgeCall("selected text", () => api.selectedText(handle), "") || "" });
        break;
      case "debugSprites": {
        let sprites = { sprites: [] };
        try {
          sprites = JSON.parse(bridgeCall("frame sprites", () => api.frameSpritesJson(handle), "{\"sprites\":[]}"));
        } catch {
          sprites = { sprites: [] };
        }
        post("debugSprites", { requestId: message.requestId, ...sprites });
        break;
      }
      case "debugImageTrace": {
        let trace = { events: [] };
        try {
          trace = JSON.parse(bridgeCall("image operation trace", () => api.imageOperationTraceJson(handle), "{\"events\":[]}"));
        } catch {
          trace = { events: [] };
        }
        if (message.clear) {
          bridgeCall("clear image operation trace", () => api.clearImageOperationTrace(handle));
        }
        post("debugImageTrace", { requestId: message.requestId, ...trace });
        break;
      }
      case "fireTestError":
        post("fireTestError", {
          requestId: message.requestId,
          handled: !!bridgeCall("fire test error", () => api.fireTestError(handle, message.message || "LibreShockwave test error"), 0)
        });
        emitDebugMessages();
        await pumpHostQueues();
        sendFrame();
        break;
      case "cut":
        post("cut", { requestId: message.requestId, text: bridgeCall("cut", () => api.cutSelectedText(handle), "") || "" });
        await pumpHostQueues();
        sendFrame();
        break;
      case "audioStopped":
        bridgeCall("audio stopped", () => api.audioStopped(handle, message.channel | 0));
        break;
      case "destroy":
        clearTimer();
        if (handle) bridgeCall("destroy", () => api.destroy(handle));
        handle = 0;
        break;
      default:
        break;
    }
  })().catch((error) => post("error", { message: errorMessage(error), detail: errorDetails(error) }));
});
