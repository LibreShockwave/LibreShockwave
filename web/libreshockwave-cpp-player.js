"use strict";

(function attach(global) {
  const defaultHabboParams = {
    sw1: "site.url=http://127.0.0.1;url.prefix=http://127.0.0.1",
    sw2: "connection.info.host=au.h4bbo.net;connection.info.port=30001",
    sw3: "client.reload.url=http://127.0.0.1/",
    sw4: "connection.mus.host=au.h4bbo.net;connection.mus.port=38101",
    sw5: "external.variables.txt=http://127.0.0.1/gamedata/external_variables.txt?;external.texts.txt=http://127.0.0.1/gamedata/external_texts.txt?"
  };
  const defaultHabboSocketProxy = [
    { host: "au.h4bbo.net", port: 30001, proxyUrl: "ws://127.0.0.1:30002" },
    { host: "au.h4bbo.net", port: 38101, proxyUrl: "ws://127.0.0.1:38102" }
  ];

  function $(canvasOrId) {
    if (typeof canvasOrId === "string") return document.getElementById(canvasOrId);
    return canvasOrId;
  }

  function browserKeyCodeFromEvent(event) {
    if (event.key === "Enter" || event.code === "NumpadEnter") return 13;
    if (event.key === "Backspace") return 8;
    if (event.key === "Tab") return 9;
    if (event.key === "Escape") return 27;
    if (event.key === " ") return 32;
    if (event.key === "PageUp") return 33;
    if (event.key === "PageDown") return 34;
    if (event.key === "End") return 35;
    if (event.key === "Home") return 36;
    if (event.key === "ArrowLeft") return 37;
    if (event.key === "ArrowUp") return 38;
    if (event.key === "ArrowRight") return 39;
    if (event.key === "ArrowDown") return 40;
    if (event.key === "Delete") return 46;
    if (event.key && event.key.length === 1) return event.key.toUpperCase().charCodeAt(0);
    return event.keyCode || event.which || 0;
  }

  function keyTextFromEvent(event) {
    if (event.ctrlKey || event.metaKey || event.altKey) return "";
    if (event.key === "Tab") return "\t";
    if (event.key === "Enter" || event.code === "NumpadEnter") return "\r";
    if (event.key && event.key.length === 1) return event.key;
    return "";
  }

  function shouldPreventKey(event) {
    if (event.ctrlKey || event.metaKey) return false;
    return event.key === "Tab" ||
      event.key === "Enter" ||
      event.key === "Backspace" ||
      event.key === "Delete" ||
      event.key === " " ||
      event.key.startsWith("Arrow");
  }

  function decodeBase64(value) {
    const binary = atob(value || "");
    const bytes = new Uint8Array(binary.length);
    for (let index = 0; index < binary.length; index += 1) {
      bytes[index] = binary.charCodeAt(index) & 0xff;
    }
    return bytes;
  }

  function customCursorDataUrl(cursor) {
    if (!cursor || !cursor.pixels || !cursor.width || !cursor.height) return "";
    const canvas = document.createElement("canvas");
    canvas.width = cursor.width;
    canvas.height = cursor.height;
    const ctx = canvas.getContext("2d");
    const image = new ImageData(new Uint8ClampedArray(decodeBase64(cursor.pixels)), cursor.width, cursor.height);
    ctx.putImageData(image, 0, 0);
    return canvas.toDataURL("image/png");
  }

  function create(canvasOrId, options = {}) {
    const canvas = $(canvasOrId);
    if (!canvas) throw new Error("LibreShockwave canvas not found");
    const ctx = canvas.getContext("2d", { alpha: false });
    const scriptUrl = document.currentScript && document.currentScript.src
      ? document.currentScript.src
      : location.href;
    const basePath = options.basePath || new URL(".", scriptUrl).href;
    const worker = new Worker(new URL("libreshockwave-cpp-worker.js", basePath), { name: "LibreShockwaveCppWorker" });
    const callbacks = new Map();
    let nextRequestId = 1;
    let lastInfo = null;
    let destroyed = false;
    let frameCssWidth = 1;
    let frameCssHeight = 1;

    canvas.tabIndex = canvas.tabIndex >= 0 ? canvas.tabIndex : 0;

    function send(type, payload = {}) {
      if (!destroyed) worker.postMessage({ type, ...payload });
    }

    function request(type, payload = {}) {
      const requestId = nextRequestId++;
      send(type, { ...payload, requestId });
      return new Promise((resolve) => callbacks.set(requestId, resolve));
    }

    function stagePoint(event) {
      const rect = canvas.getBoundingClientRect();
      const scaleX = lastInfo && lastInfo.width ? lastInfo.width / rect.width : frameCssWidth / rect.width;
      const scaleY = lastInfo && lastInfo.height ? lastInfo.height / rect.height : frameCssHeight / rect.height;
      return {
        x: Math.max(0, Math.floor((event.clientX - rect.left) * scaleX)),
        y: Math.max(0, Math.floor((event.clientY - rect.top) * scaleY))
      };
    }

    function applyCursor(cursor) {
      if (!cursor) {
        canvas.style.cursor = "default";
        return;
      }
      if (cursor.pixels) {
        const url = customCursorDataUrl(cursor);
        if (url) {
          canvas.style.cursor = `url("${url}") ${cursor.hotX || 0} ${cursor.hotY || 0}, ${cursor.css || "default"}`;
          return;
        }
      }
      canvas.style.cursor = cursor.css || "default";
    }

    function drawFrame(info, pixels) {
      lastInfo = info || lastInfo;
      if (!info || !pixels || !info.width || !info.height) return;
      if (canvas.width !== info.width) canvas.width = info.width;
      if (canvas.height !== info.height) canvas.height = info.height;
      frameCssWidth = info.width;
      frameCssHeight = info.height;
      ctx.putImageData(new ImageData(new Uint8ClampedArray(pixels), info.width, info.height), 0, 0);
      applyCursor(info.cursor);
      if (typeof options.onFrame === "function") options.onFrame(info);
    }

    worker.addEventListener("message", (event) => {
      const message = event.data || {};
      if (message.type === "frame") {
        drawFrame(message.info, message.pixels);
      } else if (message.type === "frameInfo") {
        lastInfo = message.info || lastInfo;
        applyCursor(lastInfo && lastInfo.cursor);
      } else if (message.type === "load") {
        if (typeof options.onLoad === "function") options.onLoad(message.info || {});
        const resolve = callbacks.get(message.requestId);
        if (resolve) {
          callbacks.delete(message.requestId);
          resolve(message.info || {});
        }
      } else if (message.type === "error") {
        if (typeof options.onError === "function") options.onError(message.message);
        if (typeof options.onDebug === "function") options.onDebug({ level: "error", message: message.detail || message.message || "unknown error" });
        const resolve = callbacks.get(message.requestId);
        if (resolve) {
          callbacks.delete(message.requestId);
          resolve(Promise.reject(new Error(message.message)));
        }
      } else if (message.type === "selectedText" || message.type === "cut") {
        const resolve = callbacks.get(message.requestId);
        if (resolve) {
          callbacks.delete(message.requestId);
          resolve(message.text || "");
        }
      } else if (message.type === "socket" && typeof options.onSocket === "function") {
        options.onSocket(message);
      } else if (message.type === "tempo" && typeof options.onTempo === "function") {
        options.onTempo(message);
      } else if (message.type === "navigation") {
        if (typeof options.onNavigation === "function") {
          options.onNavigation({ url: message.url || "" });
        } else if (message.url) {
          global.location.href = message.url;
        }
      } else if (message.type === "debug" && typeof options.onDebug === "function") {
        options.onDebug({ level: message.level || "debug", message: message.message || "" });
      } else if (message.type === "warning") {
        if (typeof options.onWarning === "function") options.onWarning(message.message);
        if (typeof options.onDebug === "function") options.onDebug({ level: "warning", message: message.message || "" });
      } else if (message.type === "ready" && typeof options.onReady === "function") {
        options.onReady();
      }
    });

    canvas.addEventListener("mousemove", (event) => {
      const point = stagePoint(event);
      send("mouseMove", point);
    });
    canvas.addEventListener("mousedown", (event) => {
      canvas.focus();
      event.preventDefault();
      const point = stagePoint(event);
      send("mouseDown", { ...point, right: event.button === 2 });
    });
    canvas.addEventListener("mouseup", (event) => {
      event.preventDefault();
      const point = stagePoint(event);
      send("mouseUp", { ...point, right: event.button === 2 });
    });
    canvas.addEventListener("contextmenu", (event) => event.preventDefault());
    canvas.addEventListener("blur", () => send("blur"));
    canvas.addEventListener("keydown", async (event) => {
      const key = event.key.toLowerCase();
      if ((event.ctrlKey || event.metaKey) && key === "a") {
        event.preventDefault();
        send("selectAll");
        return;
      }
      if ((event.ctrlKey || event.metaKey) && key === "c") {
        event.preventDefault();
        const text = await request("selectedText");
        if (text && navigator.clipboard) await navigator.clipboard.writeText(text);
        return;
      }
      if ((event.ctrlKey || event.metaKey) && key === "x") {
        event.preventDefault();
        const text = await request("cut");
        if (text && navigator.clipboard) await navigator.clipboard.writeText(text);
        return;
      }
      if (shouldPreventKey(event)) event.preventDefault();
      if (!event.repeat) {
        send("keyDown", {
          keyCode: browserKeyCodeFromEvent(event),
          text: keyTextFromEvent(event),
          shift: event.shiftKey,
          ctrl: event.ctrlKey || event.metaKey,
          alt: event.altKey
        });
      }
    });
    canvas.addEventListener("keyup", (event) => {
      send("keyUp", {
        keyCode: browserKeyCodeFromEvent(event),
        text: keyTextFromEvent(event),
        shift: event.shiftKey,
        ctrl: event.ctrlKey || event.metaKey,
        alt: event.altKey
      });
    });
    canvas.addEventListener("paste", (event) => {
      const text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
      if (text) {
        event.preventDefault();
        send("paste", { text });
      }
    });

    send("init", {
      url: options.url || "",
      autoload: Boolean(options.autoload && options.url),
      autoplay: options.autoplay !== false,
      params: options.params || {},
      tempoOverride: options.tempoOverride || 0,
      preloadCasts: options.preloadCasts !== false,
      debugPlaybackEnabled: Boolean(options.debugPlaybackEnabled),
      slowHandlerWarningMs: Number.isFinite(Number(options.slowHandlerWarningMs))
        ? Math.max(0, Number(options.slowHandlerWarningMs))
        : 1000,
      socketProxy: options.socketProxy || [],
      websocketPath: options.websocketPath || "",
      websocketSsl: typeof options.websocketSsl === "boolean" ? options.websocketSsl : null
    });

    return {
      load(url, loadOptions = {}) {
        return request("load", {
          url,
          params: loadOptions.params || options.params || {},
          autoplay: loadOptions.autoplay !== false,
          debugPlaybackEnabled: typeof loadOptions.debugPlaybackEnabled === "boolean"
            ? loadOptions.debugPlaybackEnabled
            : Boolean(options.debugPlaybackEnabled),
          slowHandlerWarningMs: Number.isFinite(Number(loadOptions.slowHandlerWarningMs))
            ? Math.max(0, Number(loadOptions.slowHandlerWarningMs))
            : (Number.isFinite(Number(options.slowHandlerWarningMs)) ? Math.max(0, Number(options.slowHandlerWarningMs)) : 1000),
          socketProxy: loadOptions.socketProxy || options.socketProxy || []
        });
      },
      play() { send("play"); },
      pause() { send("pause"); },
      stop() { send("stop"); },
      setTempoOverride(tempo) { send("tempo", { tempo: Number(tempo || 0) }); },
      setDebugPlaybackEnabled(enabled) { send("debugPlayback", { enabled: Boolean(enabled) }); },
      setSlowHandlerWarningMs(milliseconds) { send("slowHandlerWarningMs", { milliseconds: Math.max(0, Number(milliseconds || 0)) }); },
      setParams(params) { send("params", { params }); },
      setSocketProxy(socketProxy, socketOptions = {}) {
        send("socketProxy", {
          socketProxy: Array.isArray(socketProxy) ? socketProxy : [],
          websocketPath: socketOptions.websocketPath || options.websocketPath || "",
          websocketSsl: typeof socketOptions.websocketSsl === "boolean"
            ? socketOptions.websocketSsl
            : (typeof options.websocketSsl === "boolean" ? options.websocketSsl : null)
        });
      },
      pasteText(text) { send("paste", { text: text || "" }); },
      selectedText() { return request("selectedText"); },
      destroy() {
        destroyed = true;
        worker.postMessage({ type: "destroy" });
        worker.terminate();
      },
      get lastInfo() { return lastInfo; },
      worker
    };
  }

  global.LibreShockwavePlayer = {
    create,
    defaultHabboParams,
    defaultHabboSocketProxy
  };
})(typeof window !== "undefined" ? window : globalThis);
