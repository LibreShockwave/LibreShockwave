"use strict";

(function attach(global) {
  const defaultHabboParams = {
    sw1: "site.url=http://127.0.0.1;url.prefix=http://127.0.0.1",
    sw2: "connection.info.host=au.h4bbo.net;connection.info.port=30001",
    sw3: "client.reload.url=http://127.0.0.1/",
    sw4: "connection.mus.host=au.h4bbo.net;connection.mus.port=38101",
    sw5: "external.variables.txt=http://127.0.0.1/gamedata/external_variables.txt?;external.texts.txt=http://127.0.0.1/gamedata/external_texts.txt?"
  };
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

  function installCanvasMenuStyles() {
    if (!global.document || document.getElementById("libreshockwave-canvas-menu-style")) return;
    const style = document.createElement("style");
    style.id = "libreshockwave-canvas-menu-style";
    style.textContent = `
      .libreshockwave-canvas-menu {
        position: absolute;
        z-index: 10000;
        min-width: 172px;
        padding: 4px;
        border: 1px solid rgba(255,255,255,.18);
        background: #1d211c;
        color: #f1f4ee;
        box-shadow: 0 10px 28px rgba(0,0,0,.45);
        font: 13px/1.3 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      }
      .libreshockwave-canvas-menu[hidden],
      .libreshockwave-about-panel[hidden] {
        display: none;
      }
      .libreshockwave-canvas-menu button {
        display: block;
        width: 100%;
        border: 0;
        padding: 8px 10px;
        background: transparent;
        color: inherit;
        font: inherit;
        text-align: left;
        cursor: pointer;
      }
      .libreshockwave-canvas-menu button:hover,
      .libreshockwave-canvas-menu button:focus {
        background: rgba(255,255,255,.1);
        outline: none;
      }
      .libreshockwave-about-panel {
        position: absolute;
        z-index: 9999;
        width: min(320px, calc(100% - 24px));
        border: 1px solid rgba(255,255,255,.2);
        background: #20251f;
        color: #eef3ea;
        box-shadow: 0 14px 36px rgba(0,0,0,.5);
        font: 13px/1.45 system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
      }
      .libreshockwave-about-head {
        display: flex;
        align-items: center;
        justify-content: space-between;
        gap: 12px;
        padding: 10px 12px;
        border-bottom: 1px solid rgba(255,255,255,.12);
        font-weight: 700;
      }
      .libreshockwave-about-close {
        width: 28px;
        height: 28px;
        border: 0;
        background: transparent;
        color: inherit;
        font: 20px/1 system-ui, sans-serif;
        cursor: pointer;
      }
      .libreshockwave-about-body {
        padding: 12px;
      }
      .libreshockwave-about-body p {
        margin: 0 0 8px;
      }
      .libreshockwave-about-body a {
        color: #9dd4ff;
      }
    `;
    document.head.appendChild(style);
  }

  function formatScreenshotName() {
    const stamp = new Date().toISOString().replace(/[:.]/g, "-");
    return `libreshockwave-canvas-${stamp}.png`;
  }

  function escapeHtml(value) {
    return String(value).replace(/[&<>"']/g, (character) => ({
      "&": "&amp;",
      "<": "&lt;",
      ">": "&gt;",
      "\"": "&quot;",
      "'": "&#39;"
    }[character]));
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
    let inputTempo = clampTempo(options.tempoOverride || 15);
    let lastMouseClientX = 0;
    let lastMouseClientY = 0;
    let hasMousePosition = false;
    let lastSentMouseStageX = -1;
    let lastSentMouseStageY = -1;
    let mouseInputTimer = 0;
    let appliedCursorKey = "";
    let appliedCursorCss = "";
    const customCursorUrlCache = new Map();
    let audioContext = null;
    let audioWarningShown = false;
    const audioChannels = new Map();
    const canvasMenuEnabled = options.canvasMenu !== false;
    const projectAuthor = options.projectAuthor || "Quackster";
    const projectRepoUrl = options.projectRepoUrl || "https://github.com/Quackster/LibreShockwave";
    const overlayHost = canvas.parentElement || document.body;
    let contextMenu = null;
    let aboutPanel = null;

    canvas.tabIndex = canvas.tabIndex >= 0 ? canvas.tabIndex : 0;
    if (canvasMenuEnabled) {
      installCanvasMenuStyles();
      if (global.getComputedStyle && getComputedStyle(overlayHost).position === "static") {
        overlayHost.style.position = "relative";
      }
    }

    function send(type, payload = {}) {
      if (!destroyed) worker.postMessage({ type, ...payload });
    }

    function clampTempo(value) {
      return Math.max(1, Math.min(240, Number(value || 0) || 15));
    }

    function inputDelayMs() {
      return Math.max(1, Math.round(1000 / inputTempo));
    }

    function clearMouseInputTimer() {
      if (mouseInputTimer) {
        clearTimeout(mouseInputTimer);
        mouseInputTimer = 0;
      }
    }

    function sendSampledMousePosition() {
      if (!hasMousePosition || destroyed) return;
      const point = stagePointFromClient(lastMouseClientX, lastMouseClientY);
      if (point.x === lastSentMouseStageX && point.y === lastSentMouseStageY) return;
      lastSentMouseStageX = point.x;
      lastSentMouseStageY = point.y;
      send("mouseMove", point);
    }

    function scheduleMouseInputSample(reset = false) {
      if (reset) {
        clearMouseInputTimer();
      } else if (mouseInputTimer) {
        return;
      }
      if (destroyed || !hasMousePosition) return;
      mouseInputTimer = setTimeout(() => {
        mouseInputTimer = 0;
        sendSampledMousePosition();
        scheduleMouseInputSample();
      }, inputDelayMs());
    }

    function recordMousePosition(event) {
      lastMouseClientX = event.clientX;
      lastMouseClientY = event.clientY;
      hasMousePosition = true;
      scheduleMouseInputSample();
    }

    function recordMouseClientPosition(clientX, clientY) {
      lastMouseClientX = clientX;
      lastMouseClientY = clientY;
      if (hasMousePosition) return;
      hasMousePosition = true;
      scheduleMouseInputSample();
    }

    function request(type, payload = {}) {
      const requestId = nextRequestId++;
      send(type, { ...payload, requestId });
      return new Promise((resolve) => callbacks.set(requestId, resolve));
    }

    function reportAudioWarning(message) {
      if (!message) return;
      if (typeof options.onWarning === "function") options.onWarning(message);
      if (typeof options.onDebug === "function") options.onDebug({ level: "warning", message });
    }

    function getAudioContext() {
      if (audioContext) return audioContext;
      const AudioContextCtor = global.AudioContext || global.webkitAudioContext;
      if (!AudioContextCtor) {
        if (!audioWarningShown) {
          audioWarningShown = true;
          reportAudioWarning("Web Audio is not available in this browser");
        }
        return null;
      }
      audioContext = new AudioContextCtor();
      return audioContext;
    }

    async function unlockAudio() {
      const context = getAudioContext();
      if (!context || context.state !== "suspended") return;
      try {
        await context.resume();
      } catch (error) {
        if (!audioWarningShown) {
          audioWarningShown = true;
          reportAudioWarning(`Audio playback is blocked: ${error.message || String(error)}`);
        }
      }
    }

    async function suspendAudio() {
      if (!audioContext || audioContext.state !== "running") return;
      try {
        await audioContext.suspend();
      } catch {
        // Ignore suspend failures; playback commands remain channel-scoped.
      }
    }

    function decodeAudioBuffer(context, bytes) {
      const buffer = bytes.buffer.slice(bytes.byteOffset, bytes.byteOffset + bytes.byteLength);
      return new Promise((resolve, reject) => {
        const result = context.decodeAudioData(buffer, resolve, reject);
        if (result && typeof result.then === "function") result.then(resolve, reject);
      });
    }

    function channelVolume(command) {
      const volume = Number(command && command.volume);
      return Math.max(0, Math.min(255, Number.isFinite(volume) ? volume : 255)) / 255;
    }

    function stopAudioChannel(channel) {
      const entry = audioChannels.get(channel);
      if (!entry) return;
      entry.stopped = true;
      if (entry.source) {
        entry.source.onended = null;
        try {
          entry.source.stop();
        } catch {
          // Already stopped.
        }
      }
      if (entry.gain) {
        try {
          entry.gain.disconnect();
        } catch {
          // Already disconnected.
        }
      }
      audioChannels.delete(channel);
    }

    function stopAllAudio() {
      for (const channel of [...audioChannels.keys()]) stopAudioChannel(channel);
    }

    function setAudioVolume(command) {
      const channel = Number(command && command.channel) | 0;
      const entry = audioChannels.get(channel);
      if (entry && entry.gain) {
        entry.gain.gain.value = channelVolume(command);
      }
    }

    async function playAudioCommand(command, bytes) {
      const channel = Number(command && command.channel) | 0;
      if (!channel || !bytes || !bytes.byteLength) return;
      const context = getAudioContext();
      if (!context) return;
      await unlockAudio();
      const decoded = await decodeAudioBuffer(context, bytes);
      stopAudioChannel(channel);

      const gain = context.createGain();
      gain.gain.value = channelVolume(command);
      gain.connect(context.destination);

      const loopCount = Math.max(0, Number(command.loopCount || 0) | 0);
      const entry = {
        buffer: decoded,
        channel,
        gain,
        loopCount,
        remaining: loopCount > 0 ? loopCount : 1,
        source: null,
        stopped: false
      };
      audioChannels.set(channel, entry);

      const startSource = () => {
        if (destroyed || entry.stopped || audioChannels.get(channel) !== entry) return;
        const source = context.createBufferSource();
        source.buffer = entry.buffer;
        source.loop = entry.loopCount === 0;
        source.connect(entry.gain);
        entry.source = source;
        source.onended = () => {
          if (entry.stopped || audioChannels.get(channel) !== entry || entry.loopCount === 0) return;
          if (entry.remaining > 1) {
            entry.remaining -= 1;
            startSource();
            return;
          }
          audioChannels.delete(channel);
          send("audioStopped", { channel });
        };
        source.start(0);
      };
      startSource();
      if (typeof options.onAudio === "function") {
        options.onAudio({ action: "play", channel, format: command.format || "", byteLength: bytes.byteLength });
      }
    }

    async function handleAudioCommand(command, bytes) {
      const action = command && command.action;
      const channel = Number(command && command.channel) | 0;
      if (action === "play") {
        await playAudioCommand(command, bytes instanceof Uint8Array ? bytes : new Uint8Array(bytes || []));
      } else if (action === "stop") {
        stopAudioChannel(channel);
      } else if (action === "stopAll") {
        stopAllAudio();
      } else if (action === "volume") {
        setAudioVolume(command);
      }
    }

    function stagePointFromClient(clientX, clientY) {
      const rect = canvas.getBoundingClientRect();
      const scaleX = lastInfo && lastInfo.width ? lastInfo.width / rect.width : frameCssWidth / rect.width;
      const scaleY = lastInfo && lastInfo.height ? lastInfo.height / rect.height : frameCssHeight / rect.height;
      return {
        x: Math.max(0, Math.floor((clientX - rect.left) * scaleX)),
        y: Math.max(0, Math.floor((clientY - rect.top) * scaleY))
      };
    }

    function stagePoint(event) {
      return stagePointFromClient(event.clientX, event.clientY);
    }

    function applyCursor(cursor) {
      const cursorKey = cursor
        ? `${cursor.css || ""}|${cursor.hotX || 0}|${cursor.hotY || 0}|${cursor.width || 0}|${cursor.height || 0}|${cursor.pixels || ""}`
        : "default";
      if (cursorKey === appliedCursorKey) return;

      let css = "default";
      if (!cursor) {
        css = "default";
      } else if (cursor.pixels) {
        let url = customCursorUrlCache.get(cursorKey);
        if (!url) {
          url = customCursorDataUrl(cursor);
          if (url) customCursorUrlCache.set(cursorKey, url);
        }
        css = url
          ? `url("${url}") ${cursor.hotX || 0} ${cursor.hotY || 0}, ${cursor.css || "default"}`
          : (cursor.css || "default");
      } else {
        css = cursor.css || "default";
      }

      appliedCursorKey = cursorKey;
      if (css !== appliedCursorCss) {
        appliedCursorCss = css;
        canvas.style.cursor = css;
      }
    }

    function drawFrame(info, pixels) {
      lastInfo = info || lastInfo;
      if (lastInfo && lastInfo.tempo) inputTempo = clampTempo(lastInfo.tempo);
      if (!info || !pixels || !info.width || !info.height) return;
      if (canvas.width !== info.width) canvas.width = info.width;
      if (canvas.height !== info.height) canvas.height = info.height;
      frameCssWidth = info.width;
      frameCssHeight = info.height;
      ctx.putImageData(new ImageData(new Uint8ClampedArray(pixels), info.width, info.height), 0, 0);
      applyCursor(info.cursor);
      if (typeof options.onFrame === "function") options.onFrame(info);
    }

    function downloadCanvasScreenshot() {
      const saveBlob = (blob) => {
        const link = document.createElement("a");
        link.download = formatScreenshotName();
        if (blob) {
          const url = URL.createObjectURL(blob);
          link.href = url;
          link.click();
          URL.revokeObjectURL(url);
          return;
        }
        link.href = canvas.toDataURL("image/png");
        link.click();
      };

      if (canvas.toBlob) {
        canvas.toBlob(saveBlob, "image/png");
      } else {
        saveBlob(null);
      }
    }

    function hideContextMenu() {
      if (contextMenu) contextMenu.hidden = true;
    }

    function positionElementOverCanvas(element) {
      const canvasRect = canvas.getBoundingClientRect();
      const hostRect = overlayHost.getBoundingClientRect();
      const left = canvasRect.left - hostRect.left + overlayHost.scrollLeft + ((canvasRect.width - element.offsetWidth) / 2);
      const top = canvasRect.top - hostRect.top + overlayHost.scrollTop + ((canvasRect.height - element.offsetHeight) / 2);
      element.style.left = `${Math.max(8, Math.round(left))}px`;
      element.style.top = `${Math.max(8, Math.round(top))}px`;
    }

    function showAboutPanel() {
      hideContextMenu();
      if (!aboutPanel) {
        aboutPanel = document.createElement("div");
        aboutPanel.className = "libreshockwave-about-panel";
        aboutPanel.hidden = true;
        aboutPanel.setAttribute("role", "dialog");
        aboutPanel.setAttribute("aria-modal", "false");
        aboutPanel.innerHTML = `
          <div class="libreshockwave-about-head">
            <span>About LibreShockwave</span>
            <button class="libreshockwave-about-close" type="button" aria-label="Close about window">&times;</button>
          </div>
          <div class="libreshockwave-about-body">
            <p>LibreShockwave is a C++20 project for parsing and playing Macromedia and Adobe Director/Shockwave movies.</p>
            <p>Author: ${escapeHtml(projectAuthor)}</p>
            <p><a href="${escapeHtml(projectRepoUrl)}" target="_blank" rel="noopener noreferrer">GitHub repository</a></p>
          </div>
        `;
        aboutPanel.querySelector("button").addEventListener("click", () => {
          aboutPanel.hidden = true;
          canvas.focus();
        });
        overlayHost.appendChild(aboutPanel);
      }
      aboutPanel.hidden = false;
      positionElementOverCanvas(aboutPanel);
    }

    function showContextMenu(event) {
      if (!canvasMenuEnabled) return;
      if (!contextMenu) {
        contextMenu = document.createElement("div");
        contextMenu.className = "libreshockwave-canvas-menu";
        contextMenu.hidden = true;
        contextMenu.setAttribute("role", "menu");
        contextMenu.innerHTML = `
          <button type="button" role="menuitem" data-action="screenshot">Save canvas screenshot</button>
          <button type="button" role="menuitem" data-action="about">About LibreShockwave</button>
        `;
        contextMenu.addEventListener("click", (menuEvent) => {
          const button = menuEvent.target.closest("button");
          if (!button) return;
          if (button.dataset.action === "screenshot") {
            hideContextMenu();
            downloadCanvasScreenshot();
          } else if (button.dataset.action === "about") {
            showAboutPanel();
          }
        });
        overlayHost.appendChild(contextMenu);
      }

      const hostRect = overlayHost.getBoundingClientRect();
      const left = event.clientX - hostRect.left + overlayHost.scrollLeft;
      const top = event.clientY - hostRect.top + overlayHost.scrollTop;
      contextMenu.hidden = false;
      const maxLeft = overlayHost.scrollLeft + overlayHost.clientWidth - contextMenu.offsetWidth - 6;
      const maxTop = overlayHost.scrollTop + overlayHost.clientHeight - contextMenu.offsetHeight - 6;
      contextMenu.style.left = `${Math.max(6, Math.min(left, maxLeft))}px`;
      contextMenu.style.top = `${Math.max(6, Math.min(top, maxTop))}px`;
      contextMenu.querySelector("button").focus();
    }

    function handleDocumentPointerDown(event) {
      if (!contextMenu || contextMenu.hidden || contextMenu.contains(event.target)) return;
      hideContextMenu();
    }

    function handleDocumentKeyDown(event) {
      if (event.key === "Escape") {
        hideContextMenu();
        if (aboutPanel) aboutPanel.hidden = true;
      }
    }

    function handleNavigation(url) {
      if (!url) return;
      const navigation = {
        url,
        redirect() {
          global.location.href = url;
        }
      };
      if (typeof options.onNavigation === "function") {
        const result = options.onNavigation(navigation);
        if (result === true) navigation.redirect();
        return;
      }
      navigation.redirect();
    }

    worker.addEventListener("message", (event) => {
      const message = event.data || {};
      if (message.type === "frame") {
        drawFrame(message.info, message.pixels);
      } else if (message.type === "frameInfo") {
        lastInfo = message.info || lastInfo;
        if (lastInfo && lastInfo.tempo) inputTempo = clampTempo(lastInfo.tempo);
        applyCursor(lastInfo && lastInfo.cursor);
      } else if (message.type === "load") {
        if (message.info && message.info.tempo) inputTempo = clampTempo(message.info.tempo);
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
      } else if (message.type === "fireTestError") {
        const resolve = callbacks.get(message.requestId);
        if (resolve) {
          callbacks.delete(message.requestId);
          resolve(!!message.handled);
        }
      } else if (message.type === "debugSprites") {
        const resolve = callbacks.get(message.requestId);
        if (resolve) {
          callbacks.delete(message.requestId);
          resolve(message.sprites || []);
        }
      } else if (message.type === "debugImageTrace") {
        const resolve = callbacks.get(message.requestId);
        if (resolve) {
          callbacks.delete(message.requestId);
          resolve(message.events || []);
        }
      } else if (message.type === "socket" && typeof options.onSocket === "function") {
        options.onSocket(message);
      } else if (message.type === "audio") {
        void handleAudioCommand(message.command || {}, message.bytes).catch((error) => {
          reportAudioWarning(`Audio playback failed: ${error.message || String(error)}`);
        });
      } else if (message.type === "tempo") {
        inputTempo = clampTempo(message.tempo || message.baseTempo || inputTempo);
        scheduleMouseInputSample(true);
        if (typeof options.onTempo === "function") options.onTempo(message);
      } else if (message.type === "navigation") {
        handleNavigation(message.url || "");
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
      recordMouseClientPosition(event.clientX, event.clientY);
    }, { passive: true });
    canvas.addEventListener("mousedown", (event) => {
      canvas.focus();
      event.preventDefault();
      void unlockAudio();
      recordMousePosition(event);
      const point = stagePoint(event);
      lastSentMouseStageX = point.x;
      lastSentMouseStageY = point.y;
      send("mouseDown", { ...point, right: event.button === 2 });
    });
    canvas.addEventListener("mouseup", (event) => {
      event.preventDefault();
      recordMousePosition(event);
      const point = stagePoint(event);
      lastSentMouseStageX = point.x;
      lastSentMouseStageY = point.y;
      send("mouseUp", { ...point, right: event.button === 2 });
    });
    canvas.addEventListener("contextmenu", (event) => {
      event.preventDefault();
      event.stopPropagation();
      canvas.focus();
      showContextMenu(event);
    });
    document.addEventListener("pointerdown", handleDocumentPointerDown);
    document.addEventListener("keydown", handleDocumentKeyDown);
    global.addEventListener("resize", hideContextMenu);
    canvas.addEventListener("blur", () => send("blur"));
    canvas.addEventListener("keydown", async (event) => {
      void unlockAudio();
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
      websocketPath: options.websocketPath || "",
      websocketSsl: typeof options.websocketSsl === "boolean" ? options.websocketSsl : null
    });

    return {
      load(url, loadOptions = {}) {
        stopAllAudio();
        void unlockAudio();
        return request("load", {
          url,
          params: loadOptions.params || options.params || {},
          autoplay: loadOptions.autoplay !== false,
          debugPlaybackEnabled: typeof loadOptions.debugPlaybackEnabled === "boolean"
            ? loadOptions.debugPlaybackEnabled
            : Boolean(options.debugPlaybackEnabled),
          slowHandlerWarningMs: Number.isFinite(Number(loadOptions.slowHandlerWarningMs))
            ? Math.max(0, Number(loadOptions.slowHandlerWarningMs))
            : (Number.isFinite(Number(options.slowHandlerWarningMs)) ? Math.max(0, Number(options.slowHandlerWarningMs)) : 1000)
        });
      },
      play() {
        void unlockAudio();
        send("play");
      },
      pause() {
        void suspendAudio();
        send("pause");
      },
      stop() {
        stopAllAudio();
        send("stop");
      },
      setTempoOverride(tempo) {
        inputTempo = clampTempo(tempo || inputTempo);
        scheduleMouseInputSample(true);
        send("tempo", { tempo: Number(tempo || 0) });
      },
      setDebugPlaybackEnabled(enabled) { send("debugPlayback", { enabled: Boolean(enabled) }); },
      setSlowHandlerWarningMs(milliseconds) { send("slowHandlerWarningMs", { milliseconds: Math.max(0, Number(milliseconds || 0)) }); },
      setParams(params) { send("params", { params }); },
      pasteText(text) { send("paste", { text: text || "" }); },
      selectedText() { return request("selectedText"); },
      debugSprites() { return request("debugSprites"); },
      debugImageTrace(options = {}) { return request("debugImageTrace", { clear: !!options.clear }); },
      fireTestError(message) { return request("fireTestError", { message: message || "LibreShockwave test error" }); },
      destroy() {
        stopAllAudio();
        destroyed = true;
        clearMouseInputTimer();
        if (contextMenu) contextMenu.remove();
        if (aboutPanel) aboutPanel.remove();
        document.removeEventListener("pointerdown", handleDocumentPointerDown);
        document.removeEventListener("keydown", handleDocumentKeyDown);
        global.removeEventListener("resize", hideContextMenu);
        worker.postMessage({ type: "destroy" });
        worker.terminate();
        if (audioContext && audioContext.state !== "closed") {
          void audioContext.close().catch(() => {});
        }
      },
      get lastInfo() { return lastInfo; },
      worker
    };
  }

  global.LibreShockwavePlayer = {
    create,
    defaultHabboParams
  };
})(typeof window !== "undefined" ? window : globalThis);
