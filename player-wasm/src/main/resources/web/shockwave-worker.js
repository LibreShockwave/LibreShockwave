'use strict';

// Suppress console.log/warn in the worker. When DevTools is open, Chrome renders
// every message (including TeaVM's System.out via putwchar) which stalls the
// message loop and prevents 'frame' responses from being processed in time.
// Errors are kept since they only fire in exceptional cases.
// TEMP: relay console.log to main thread for debugging
var _origLog = console.log;
console.log = function() {
    var msg = Array.prototype.slice.call(arguments).join(' ');
    if (msg.indexOf('[EventDispatcher]') >= 0 || msg.indexOf('[HitTest]') >= 0 || msg.indexOf('[DEBUG') >= 0) {
        self.postMessage({ type: 'debugLog', msg: msg });
    }
};
console.warn = function() {};

/**
 * LibreShockwave Web Worker — runs the WASM engine off the main thread.
 *
 * Message protocol (main → worker):
 *   {type:'init',    basePath}
 *   {type:'loadMovie', data:ArrayBuffer, basePath}
 *   {type:'setParam',  key, value}
 *   {type:'clearParams'}
 *   {type:'preloadCasts'}
 *   {type:'play'|'pause'|'stop'}
 *   {type:'tick'}
 *   {type:'goToFrame', frame}
 *   {type:'stepForward'}
 *   {type:'stepBackward'}
 *   {type:'mouseMove',  x, y}
 *   {type:'mouseDown',  x, y, button}  (button: 0=left, 2=right)
 *   {type:'mouseUp',    x, y, button}
 *   {type:'keyDown',    keyCode, key, modifiers}  (modifiers: 1=shift,2=ctrl,4=alt)
 *   {type:'keyUp',      keyCode, key, modifiers}
 *
 * Message protocol (worker → main):
 *   {type:'ready'}
 *   {type:'movieLoaded',  info:{width,height,frameCount,tempo} | null}
 *   {type:'castsDone'}
 *   {type:'frame', playing, enginePlaying, tempo, lastFrame, frameCount,
 *                  rgba:Uint8ClampedArray, width, height}
 *   {type:'error', msg}
 */

var _e = null;          // WasmEngine instance
var _isTicking = false; // guard against overlapping ticks
var _pageProtocol = ''; // page protocol from main thread (e.g. 'https:')

// --- Multiuser Xtra WebSocket connections ---
var _musSockets = {};      // instanceId -> WebSocket
var _musInbound = {};      // instanceId -> [string] (MUS message bodies, tab-separated)

var _musConnected = {};    // instanceId -> true (pending connect notifications)
var _musDisconnected = {}; // instanceId -> true (pending disconnect notifications)
var _musErrors = {};       // instanceId -> errorCode

// --- Non-blocking fetch delivery queue ---
var _fetchQueue = [];   // [{taskId, data: ArrayBuffer}] or [{taskId, error: number}]
var _loadStartTime = 0; // timestamp when loading began (for perf logging)

// --- Cross-origin fetch relay (main thread fetches on behalf of worker) ---
var _fetchRelayMap = {};  // relayId -> { engine, taskId, url, fallbacks }
var _fetchRelayCounter = 0;

// --- Fetch with timeout (prevents hanging requests on mobile) ---
function _fetchWithTimeout(url, opts, timeoutMs) {
    timeoutMs = timeoutMs || 30000;
    var controller = new AbortController();
    var timer = setTimeout(function() { controller.abort(); }, timeoutMs);
    opts = opts || {};
    opts.signal = controller.signal;
    return fetch(url, opts).finally(function() { clearTimeout(timer); });
}

// --- External params stored locally for pre-fetch access ---
var _params = {};

// --- Diagnostic tick counter ---
var _tickNum = 0;

// --- Movie base path (set during loadMovie) ---
var _movieBasePath = '';

// ============================================================
// WasmEngine — mirrors the main-thread version but without Canvas
// ============================================================

function WasmEngine() {
    this.teavm    = null;
    this.exports  = null;
    this.playing  = false;
    this._lastTempo      = 15;
    this._lastFrame      = 0;
    this._lastFrameCount = 0;
}

WasmEngine.prototype._mem = function() { return this.teavm.memory.buffer; };

WasmEngine.prototype._readString = function(addr, len) {
    return new TextDecoder().decode(new Uint8Array(this._mem(), addr, len));
};

WasmEngine.prototype._writeBytes = function(addr, bytes, maxLen) {
    new Uint8Array(this._mem(), addr, maxLen).set(bytes.subarray(0, Math.min(bytes.length, maxLen)));
};

WasmEngine.prototype._clearEx = function() {
    var ex = this.teavm.instance && this.teavm.instance.exports;
    if (ex && ex.teavm_catchException) ex.teavm_catchException();
};

WasmEngine.prototype.loadMovie = function(bytes, basePath) {
    var bp = new TextEncoder().encode(basePath || '');
    this._writeBytes(this.exports.getStringBufferAddress(), bp, 4096);
    var bufAddr = this.exports.allocateBuffer(bytes.length);
    new Uint8Array(this._mem(), bufAddr, bytes.length).set(bytes);
    var result = this.exports.loadMovie(bytes.length, bp.length);
    this._clearEx();
    if (result === 0) return null;
    var w = (result >> 16) & 0xFFFF, h = result & 0xFFFF;
    this._lastFrameCount = this.exports.getFrameCount();
    this._lastTempo      = this.exports.getTempo();
    return { width: w, height: h, frameCount: this._lastFrameCount, tempo: this._lastTempo };
};

WasmEngine.prototype.setExternalParam = function(key, value) {
    var kb = new TextEncoder().encode(key), vb = new TextEncoder().encode(value);
    var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 4096);
    sbuf.set(kb); sbuf.set(vb, kb.length);
    this.exports.setExternalParam(kb.length, vb.length);
    this._clearEx();
};

WasmEngine.prototype.clearExternalParams = function() {
    this.exports.clearExternalParams(); this._clearEx();
};

WasmEngine.prototype.addTraceHandler = function(name) {
    var nb = new TextEncoder().encode(name);
    var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 4096);
    sbuf.set(nb);
    this.exports.addTraceHandler(nb.length);
    this._clearEx();
};

WasmEngine.prototype.removeTraceHandler = function(name) {
    var nb = new TextEncoder().encode(name);
    var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 4096);
    sbuf.set(nb);
    this.exports.removeTraceHandler(nb.length);
    this._clearEx();
};

WasmEngine.prototype.clearTraceHandlers = function() {
    this.exports.clearTraceHandlers();
    this._clearEx();
};

WasmEngine.prototype.preloadCasts = function() {
    var n = this.exports.preloadCasts(); this._clearEx(); return n;
};

WasmEngine.prototype.tick = function() {
    var r = this.exports.tick(); this._clearEx(); return r !== 0;
};

/**
 * Render the current frame into an RGBA buffer via SoftwareRenderer (WASM-side).
 * Returns { w, h, rgba: Uint8ClampedArray } or null on failure.
 */
WasmEngine.prototype.renderFrame = function() {
    var len = this.exports.render(); this._clearEx();
    if (len <= 0) return null;

    var w = this.exports.getStageWidth();
    var h = this.exports.getStageHeight();
    if (w <= 0 || h <= 0) return null;

    var ptr = this.exports.getRenderBufferAddress(); this._clearEx();
    if (ptr === 0) return null;

    // Update cached frame metadata
    this._lastFrame      = this.exports.getCurrentFrame();
    this._lastFrameCount = this.exports.getFrameCount();

    // Copy RGBA out of WASM heap (buffer may move after GC)
    var rgba = new Uint8ClampedArray(w * h * 4);
    rgba.set(new Uint8ClampedArray(this._mem(), ptr, rgba.length));
    return { w: w, h: h, rgba: rgba };
};

// --- Input forwarding ---

WasmEngine.prototype.mouseMove = function(x, y) {
    this.exports.mouseMove(x, y); this._clearEx();
};

WasmEngine.prototype.mouseDown = function(x, y, button) {
    this.exports.mouseDown(x, y, button); this._clearEx();
};

WasmEngine.prototype.debugHitTest = function(x, y) {
    var result = this.exports.debugHitTest(x, y); this._clearEx();
    return result;
};

WasmEngine.prototype.mouseUp = function(x, y, button) {
    this.exports.mouseUp(x, y, button); this._clearEx();
};

WasmEngine.prototype.keyDown = function(browserKeyCode, keyChar, modifiers) {
    var kb = new TextEncoder().encode(keyChar || '');
    if (kb.length > 0) {
        var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 4096);
        sbuf.set(kb);
    }
    this.exports.keyDown(browserKeyCode, kb.length, modifiers); this._clearEx();
};

WasmEngine.prototype.keyUp = function(browserKeyCode, keyChar, modifiers) {
    var kb = new TextEncoder().encode(keyChar || '');
    if (kb.length > 0) {
        var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 4096);
        sbuf.set(kb);
    }
    this.exports.keyUp(browserKeyCode, kb.length, modifiers); this._clearEx();
};

WasmEngine.prototype.pasteText = function(text) {
    var kb = new TextEncoder().encode(text || '');
    if (kb.length > 0) {
        var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 65536);
        sbuf.set(kb);
    }
    this.exports.pasteText(kb.length); this._clearEx();
};

WasmEngine.prototype._drainRequests = function() {
    var count = this.exports.getPendingFetchCount(); this._clearEx();
    if (count === 0) return null;
    var reqs = [];
    var strAddr = this.exports.getStringBufferAddress();
    for (var i = 0; i < count; i++) {
        var taskId = this.exports.getPendingFetchTaskId(i); this._clearEx();
        var urlLen = this.exports.getPendingFetchUrl(i); this._clearEx();
        var url = this._readString(strAddr, urlLen);
        var method = this.exports.getPendingFetchMethod(i); this._clearEx();
        var postData = null;
        if (method === 1) {
            var pdLen = this.exports.getPendingFetchPostData(i); this._clearEx();
            if (pdLen > 0) postData = this._readString(strAddr, pdLen);
        }
        var fbCount = this.exports.getPendingFetchFallbackCount(i); this._clearEx();
        var fallbacks = [];
        for (var j = 0; j < fbCount; j++) {
            var fbLen = this.exports.getPendingFetchFallbackUrl(i, j); this._clearEx();
            fallbacks.push(this._readString(strAddr, fbLen));
        }
        reqs.push({taskId: taskId, url: url, method: method === 1 ? 'POST' : 'GET',
                   postData: postData, fallbacks: fallbacks});
    }
    this.exports.drainPendingFetches(); this._clearEx();
    return reqs;
};

/**
 * Detect if a URL points to a cast file (.cct or .cst).
 */

WasmEngine.prototype._deliverResult = function(taskId, arrayBuffer, url) {
    // Deliver all fetch results uniformly — cast file detection and caching
    // is handled on the Java side in deliverFetchResult.
    try {
        var bytes = new Uint8Array(arrayBuffer);
        var addr  = this.exports.allocateNetBuffer(bytes.length); this._clearEx();
        new Uint8Array(this._mem(), addr, bytes.length).set(bytes);
        this.exports.deliverFetchResult(taskId, bytes.length);
        this._clearEx();
    } catch (e) {
        console.error('[WORKER] deliverFetchResult error for taskId=' + taskId + ': ' + e);
        this._clearEx();
    }
};

WasmEngine.prototype._wasmDead = false;



WasmEngine.prototype._deliverError = function(taskId, status) {
    this.exports.deliverFetchError(taskId, status || 0); this._clearEx();
};

/** Drain pending network requests; fetch synchronously and deliver. */
WasmEngine.prototype.pumpNetworkCollect = function() {
    var reqs = this._drainRequests();
    if (!reqs) return [];
    for (var i = 0; i < reqs.length; i++) {
        var req = reqs[i];
        var data = _fetchSyncData(req.url, req.fallbacks);
        if (data) {
            this._deliverResult(req.taskId, data, req.url);
        } else {
            this._deliverError(req.taskId, 0);
        }
    }
    return [];
};

// ============================================================
// Non-blocking fetch pipeline (used during tick for async I/O)
// ============================================================

/**
 * Fetch a URL synchronously using XMLHttpRequest (allowed in Workers).
 * Blocks until the response arrives, giving natural network latency.
 * Result is pushed to _fetchQueue for delivery on the next tick.
 */
WasmEngine.prototype._doFetchSync = function(taskId, url, method, postData, fallbacks) {
    console.log('[WORKER] fetch: ' + url + (fallbacks.length ? ' (+' + fallbacks.length + ' fallbacks)' : ''));

    try {
        var xhr = new XMLHttpRequest();
        xhr.open(method || 'GET', url, false); // synchronous
        xhr.responseType = 'arraybuffer';
        if (method === 'POST') {
            xhr.setRequestHeader('Content-Type', 'application/x-www-form-urlencoded');
        }
        xhr.send(postData || null);

        if (xhr.status >= 200 && xhr.status < 300) {
            var buf = xhr.response;
            console.log('[WORKER] fetch OK: ' + url + ' (' + buf.byteLength + ' bytes)');
            _fetchQueue.push({ taskId: taskId, data: buf, url: url });
        } else {
            throw { status: xhr.status };
        }
    } catch (e) {
        console.log('[WORKER] fetch ERR: ' + url + ' status=' + ((e && e.status) || 'network'));
        if (fallbacks && fallbacks.length > 0) {
            return this._doFetchSync(taskId, fallbacks[0], method, postData, fallbacks.slice(1));
        } else {
            _fetchQueue.push({ taskId: taskId, error: (e && e.status) || 0 });
        }
    }
};

/**
 * Deliver queued fetch results into WASM.
 * Non-cast results are delivered one at a time to simulate real network latency.
 * Cast file results (.cct/.cst) are delivered immediately so cast members are
 * available when Lingo sets castLib.fileName on the same or next tick.
 * @return number of results delivered
 */
WasmEngine.prototype.deliverQueuedResults = function() {
    if (_fetchQueue.length === 0) return 0;
    var delivered = 0;
    // First pass: deliver all cast file results immediately
    for (var i = _fetchQueue.length - 1; i >= 0; i--) {
        var item = _fetchQueue[i];
        if (item.data !== undefined && item.url && _isCastFileUrl(item.url)) {
            _fetchQueue.splice(i, 1);
            this._deliverResult(item.taskId, item.data, item.url);
            delivered++;
        }
    }
    // Second pass: deliver one non-cast result (rate-limited)
    if (_fetchQueue.length > 0) {
        var item = _fetchQueue.shift();
        if (item.data !== undefined) {
            this._deliverResult(item.taskId, item.data, item.url);
        } else {
            this._deliverError(item.taskId, item.error);
        }
        delivered++;
    }
    return delivered;
};

function _isCastFileUrl(url) {
    if (!url) return false;
    var lower = url.toLowerCase();
    var qi = lower.indexOf('?');
    if (qi > 0) lower = lower.substring(0, qi);
    return lower.endsWith('.cct') || lower.endsWith('.cst');
}

/**
 * Drain pending requests from WASM and fire them all non-blocking.
 * Checks JS-side cache first to avoid duplicate fetches.
 * @return number of requests fired
 */
WasmEngine.prototype.pumpNetworkFire = function() {
    var reqs = this._drainRequests();
    if (!reqs) return 0;
    for (var i = 0; i < reqs.length; i++) {
        var req = reqs[i];
        this._doFetchSync(req.taskId, req.url, req.method, req.postData, req.fallbacks || []);
    }
    return reqs.length;
};

// ============================================================
// Multiuser Xtra — WebSocket bridge
// ============================================================

/**
 * Drain pending MUS requests from WASM and act on them:
 *   type 0 = connect → open WebSocket
 *   type 1 = send    → send on existing WebSocket
 *   type 2 = disconnect → close WebSocket
 *
 * WebSocket URL is built as ws://host:port (or wss:// if page is HTTPS).
 * A WebSocket proxy must be running at that address to bridge to the
 * real Multiuser Server TCP socket.
 */
WasmEngine.prototype.pumpMusRequests = function() {
    if (this._wasmDead) return;
    var count;
    try {
        count = this.exports.getMusPendingCount(); this._clearEx();
    } catch(e) { return; }
    if (count === 0) return;

    var strAddr = this.exports.getStringBufferAddress(); this._clearEx();

    for (var i = 0; i < count; i++) {
        var type = this.exports.getMusPendingType(i); this._clearEx();
        var instId = this.exports.getMusPendingInstanceId(i); this._clearEx();

        if (type === 0) {
            // CONNECT
            var hostLen = this.exports.getMusPendingHost(i); this._clearEx();
            var host = this._readString(strAddr, hostLen);
            var port = this.exports.getMusPendingPort(i); this._clearEx();

            var protocol = (_pageProtocol === 'https:') ? 'wss' : 'ws';
            var wsUrl = protocol + '://' + host + ':' + port;
            this._musConnect(instId, wsUrl);

        } else if (type === 1) {
            // SEND — raw content bytes (must be binary frame for websockify)
            var dataLen = this.exports.getMusPendingSendData(i); this._clearEx();
            var data = this._readString(strAddr, dataLen);
            var ws = _musSockets[instId];
            if (ws && ws.readyState === WebSocket.OPEN) {
                var bytes = new TextEncoder().encode(data);
                ws.send(bytes.buffer);
            }

        } else if (type === 2) {
            // DISCONNECT
            var ws2 = _musSockets[instId];
            if (ws2) {
                ws2.onclose = null; // prevent double-notification
                ws2.close();
                delete _musSockets[instId];
            }
        }
    }

    this.exports.drainMusPending(); this._clearEx();
};

/**
 * Open a WebSocket and wire up event handlers.
 * Messages are queued in JS and delivered to WASM during the next tick.
 */
WasmEngine.prototype._musConnect = function(instId, wsUrl) {
    // Close any existing connection for this instance
    if (_musSockets[instId]) {
        _musSockets[instId].onclose = null;
        _musSockets[instId].close();
    }

    var ws;
    try {
        ws = new WebSocket(wsUrl);
    } catch(e) {
        self.postMessage({type:'error', msg:'[MUS] WebSocket constructor failed: ' + e.message});
        _musErrors[instId] = -3; // connection refused
        return;
    }
    ws.binaryType = 'arraybuffer';
    _musSockets[instId] = ws;

    ws.onopen = function() {
        _musConnected[instId] = true;
    };

    ws.onmessage = function(evt) {
        // Receive raw message content
        if (!_musInbound[instId]) _musInbound[instId] = [];
        var data;
        if (typeof evt.data === 'string') {
            data = evt.data;
        } else {
            data = new TextDecoder().decode(new Uint8Array(evt.data));
        }
        _musInbound[instId].push(data);
    };

    ws.onclose = function() {
        _musDisconnected[instId] = true;
        delete _musSockets[instId];
    };

    ws.onerror = function() {
        _musErrors[instId] = -2; // network error
    };
};

/**
 * Pump audio commands from WASM and send to main thread for Web Audio playback.
 * Audio must play on the main thread because Web Workers don't have AudioContext.
 */
WasmEngine.prototype.pumpAudioCommands = function() {
    if (this._wasmDead) return;
    var count;
    try {
        count = this.exports.getAudioPendingCount(); this._clearEx();
    } catch(e) { return; }
    if (count === 0) return;

    var strAddr = this.exports.getStringBufferAddress(); this._clearEx();

    for (var i = 0; i < count; i++) {
        var actionLen = this.exports.getAudioPendingAction(i); this._clearEx();
        var action = this._readString(strAddr, actionLen);
        var channel = this.exports.getAudioPendingChannel(i); this._clearEx();

        if (action === 'play') {
            var fmtLen = this.exports.getAudioPendingFormat(i); this._clearEx();
            var format = this._readString(strAddr, fmtLen);
            var loopCount = this.exports.getAudioPendingLoopCount(i); this._clearEx();
            var volume = this.exports.getAudioPendingVolume(i); this._clearEx();
            var dataLen = this.exports.getAudioPendingData(i); this._clearEx();

            if (dataLen > 0) {
                var dataAddr = this.exports.getAudioBufferAddress(); this._clearEx();
                var audioData = new Uint8Array(dataLen);
                audioData.set(new Uint8Array(this._mem(), dataAddr, dataLen));

                self.postMessage({
                    type: 'audio',
                    action: 'play',
                    channel: channel,
                    format: format,
                    loopCount: loopCount,
                    volume: volume,
                    data: audioData.buffer
                }, [audioData.buffer]);
            }
        } else if (action === 'stop') {
            self.postMessage({ type: 'audio', action: 'stop', channel: channel });
        } else if (action === 'volume') {
            var vol = this.exports.getAudioPendingVolume(i); this._clearEx();
            self.postMessage({ type: 'audio', action: 'volume', channel: channel, volume: vol });
        }
    }

    this.exports.drainAudioPending(); this._clearEx();
};

/**
 * Deliver queued MUS events (connected/messages/disconnected/errors) to WASM.
 * Called at the start of each tick, before WASM tick().
 */
WasmEngine.prototype.deliverMusEvents = function() {
    if (this._wasmDead) return;
    var strAddr;
    try {
        strAddr = this.exports.getStringBufferAddress(); this._clearEx();
    } catch(e) { return; }

    // Deliver connect notifications
    for (var id in _musConnected) {
        try {
            this.exports.musDeliverConnected(parseInt(id)); this._clearEx();
        } catch(e) {}
    }
    _musConnected = {};

    // Deliver error notifications
    for (var eid in _musErrors) {
        try {
            this.exports.musDeliverError(parseInt(eid), _musErrors[eid]); this._clearEx();
        } catch(e) {}
    }
    _musErrors = {};

    // Deliver messages
    for (var mid in _musInbound) {
        var msgs = _musInbound[mid];
        var iid = parseInt(mid);
        for (var i = 0; i < msgs.length; i++) {
            var msgBytes = new TextEncoder().encode(msgs[i]);
            var len = Math.min(msgBytes.length, 4096);
            new Uint8Array(this._mem(), strAddr, len).set(msgBytes.subarray(0, len));
            try {
                this.exports.musDeliverMessage(iid, len); this._clearEx();
            } catch(e) {
                self.postMessage({type:'error', msg:'[MUS] musDeliverMessage error: ' + e});
            }
        }
    }
    _musInbound = {};

    // Deliver disconnect notifications
    for (var did in _musDisconnected) {
        try {
            this.exports.musDeliverDisconnected(parseInt(did)); this._clearEx();
        } catch(e) {}
    }
    _musDisconnected = {};
};

// ============================================================
// JS-side response cache helpers
// ============================================================

/**
 * Fetch a URL synchronously, trying fallbacks on failure.
 * @return ArrayBuffer or null
 */
function _fetchSyncData(url, fallbacks) {
    var urls = [url];
    if (fallbacks) {
        for (var i = 0; i < fallbacks.length; i++) urls.push(fallbacks[i]);
    }
    for (var j = 0; j < urls.length; j++) {
        try {
            var xhr = new XMLHttpRequest();
            xhr.open('GET', urls[j], false);
            xhr.responseType = 'arraybuffer';
            xhr.send(null);
            if (xhr.status >= 200 && xhr.status < 300 && xhr.response) {
                return xhr.response;
            }
        } catch (e) { /* try next */ }
    }
    return null;
}

/**
 * Returns true if the URL is cross-origin relative to this worker.
 * Cross-origin fetches from a Web Worker can hang in Chrome when
 * the host:port differs from the page origin. We relay those through
 * the main thread, which has no such limitation.
 */
function _isCrossOrigin(url) {
    try {
        return new URL(url, self.location.href).origin !== self.location.origin;
    } catch(e) { return false; }
}

/**
 * Compute the base directory from a movie URL.
 * "http://host/path/movie.dcr" -> "http://host/path/"
 */
function _getBaseDir(url) {
    var i = url.lastIndexOf('/');
    return i >= 0 ? url.substring(0, i + 1) : '';
}

/**
 * Pre-fetch sw1 param URLs (external_variables, external_texts)
 * and any dynamic cast entries found in external_variables.txt.
 * This runs during preloadCasts, BEFORE play(), so the data is
 * cached and available instantly when the Lingo state machine needs it.
 */
async function _prefetchSw1Assets(basePath) {
    var baseDir = _getBaseDir(basePath);

    // Collect all HTTP URLs from ALL sw params (sw1-sw9).
    // Params use "key=value;key=value" format; values containing "://" are URLs.
    var urls = [];
    for (var i = 1; i <= 9; i++) {
        var sw = _params['sw' + i] || _params['SW' + i];
        if (!sw) continue;
        sw.split(';').forEach(function(pair) {
            pair = pair.trim();
            var eq = pair.indexOf('=');
            if (eq < 0) return;
            var val = pair.substring(eq + 1).trim();
            if (val.indexOf('://') !== -1) urls.push(val);
        });
    }
    if (urls.length === 0) return;

    console.log('[WORKER] Pre-fetching ' + urls.length + ' sw1 URLs');

    // No pre-fetching — files are fetched synchronously on demand.
    // The browser HTTP cache serves repeated requests efficiently.
}

// ============================================================
// Message handler
// ============================================================

self.onmessage = async function(e) {
    var msg = e.data;
    try {
        switch (msg.type) {

            case 'init': {
                _pageProtocol = msg.pageProtocol || '';
                // importScripts is synchronous; TeaVM.wasm.load is async
                importScripts(msg.basePath + 'player-wasm.wasm-runtime.js');
                var instance = await TeaVM.wasm.load(msg.basePath + 'player-wasm.wasm');
                await instance.main([]);
                _e = new WasmEngine();
                _e.teavm   = instance;
                _e.exports = instance.instance.exports;
                self.postMessage({ type: 'ready' });
                break;
            }

            case 'loadMovie': {
                _tickNum = 0;
                _movieBasePath = msg.basePath || '';
                var info = _e.loadMovie(new Uint8Array(msg.data), msg.basePath);
                _e.playing  = false;
                self.postMessage({ type: 'movieLoaded', info: info });
                break;
            }

            case 'setParam':
                _e.setExternalParam(msg.key, msg.value);
                _params[msg.key] = msg.value; // Store locally for pre-fetch
                break;

            case 'setDebugPlayback':
                _e.exports.setDebugPlaybackEnabled(msg.enabled ? 1 : 0);
                _e._clearEx();
                break;

            case 'addTraceHandler':
                _e.addTraceHandler(msg.name);
                break;

            case 'removeTraceHandler':
                _e.removeTraceHandler(msg.name);
                break;

            case 'clearTraceHandlers':
                _e.clearTraceHandlers();
                break;

            case 'clearParams':
                _e.clearExternalParams();
                _params = {};
                break;

            case 'preloadCasts': {
                var castT0 = performance.now();
                _loadStartTime = castT0;
                var n = _e.preloadCasts();
                console.log('[WORKER] preloadCasts: ' + n + ' casts queued');
                if (n > 0) {
                    var p1 = _e.pumpNetworkCollect();
                    if (p1.length > 0) await Promise.all(p1);
                    // pump any secondary requests queued during prepareMovie
                    var p2 = _e.pumpNetworkCollect();
                    if (p2.length > 0) await Promise.all(p2);
                }
                // Continue pumping until no more requests
                while (true) {
                    var pn = _e.pumpNetworkCollect();
                    if (pn.length === 0) break;
                    await Promise.all(pn);
                }
                console.log('[WORKER] preloadCasts done in ' + Math.round(performance.now() - castT0) + 'ms');

                // Pre-fetch sw1 assets (external_variables, external_texts, dynamic casts)
                // so they're cached before the Lingo state machine needs them
                try {
                    await _prefetchSw1Assets(_movieBasePath);
                } catch (prefetchErr) {
                    console.error('[WORKER] prefetch error: ' + prefetchErr);
                }

                self.postMessage({ type: 'castsDone' });
                break;
            }

            case 'play':
                console.log('[WORKER] play() — starting animation');
                _e.exports.play(); _e._clearEx(); _e.playing = true;
                break;
            case 'pause':
                _e.exports.pause(); _e._clearEx(); _e.playing = false;
                break;
            case 'stop':
                _e.exports.stop(); _e._clearEx(); _e.playing = false;
                break;

            case 'goToFrame':
                _e.exports.goToFrame(msg.frame); _e._clearEx();
                break;
            case 'stepForward':
                _e.exports.stepForward(); _e._clearEx();
                break;
            case 'stepBackward':
                _e.exports.stepBackward(); _e._clearEx();
                break;

            // --- Input events ---
            case 'mouseMove':
                if (_e && !_e._wasmDead) try { _e.mouseMove(msg.x, msg.y); } catch(ie) {}
                break;
            case 'mouseDown':
                if (_e && !_e._wasmDead) try {
                    _e.mouseDown(msg.x, msg.y, msg.button);
                } catch(ie) {
                    console.error('[WORKER] mouseDown error:', ie);
                }
                break;
            case 'getDispatchInfo':
                if (_e && !_e._wasmDead) try {
                    var diLen = _e.exports.getLastDispatchInfo();
                    var diStr = '';
                    if (diLen > 0) {
                        var sbuf2 = _e.exports.getStringBufferAddress();
                        var mem2 = new Uint8Array(_e._mem(), sbuf2, diLen);
                        diStr = new TextDecoder().decode(mem2);
                    }
                    self.postMessage({ type: 'dispatchInfoResult', info: diStr });
                } catch(ie) {
                    console.error('[WORKER] getDispatchInfo error:', ie);
                }
                break;
            case 'debugHitTest':
                if (_e && !_e._wasmDead) try {
                    var hitResult = _e.debugHitTest(msg.x, msg.y);
                    // Read the debug info string from WASM
                    var infoLen = _e.exports.getDebugHitInfo();
                    var infoStr = '';
                    if (infoLen > 0) {
                        var sbuf = _e.exports.getStringBufferAddress();
                        var mem = new Uint8Array(_e._mem(), sbuf, infoLen);
                        infoStr = new TextDecoder().decode(mem);
                    }
                    self.postMessage({ type: 'debugHitTestResult', x: msg.x, y: msg.y, hit: hitResult, info: infoStr });
                } catch(ie) {
                    console.error('[WORKER] debugHitTest error:', ie);
                }
                break;
            case 'debugSpriteInfo':
                if (_e && !_e._wasmDead) try {
                    var infoLen2 = _e.exports.debugSpriteInfo(msg.channel);
                    var infoStr2 = '';
                    if (infoLen2 > 0) {
                        var sbuf3 = _e.exports.getStringBufferAddress();
                        var mem3 = new Uint8Array(_e._mem(), sbuf3, infoLen2);
                        infoStr2 = new TextDecoder().decode(mem3);
                    }
                    self.postMessage({ type: 'debugSpriteInfoResult', channel: msg.channel, info: infoStr2 });
                } catch(ie) {
                    console.error('[WORKER] debugSpriteInfo error:', ie);
                }
                break;
            case 'mouseUp':
                if (_e && !_e._wasmDead) try { _e.mouseUp(msg.x, msg.y, msg.button); } catch(ie) {
                    console.error('[WORKER] mouseUp error:', ie);
                }
                break;
            case 'keyDown':
                if (_e && !_e._wasmDead) try {
                    _e.keyDown(msg.keyCode, msg.key || '', msg.modifiers);
                } catch(ie) { console.error('[WORKER] keyDown error:', ie); }
                break;
            case 'keyUp':
                if (_e && !_e._wasmDead) try {
                    _e.keyUp(msg.keyCode, msg.key || '', msg.modifiers);
                } catch(ie) { console.error('[WORKER] keyUp error:', ie); }
                break;

            case 'paste':
                if (_e && !_e._wasmDead) try { _e.pasteText(msg.text); } catch(e) {}
                break;

            case 'getSelectedText':
                var selText = '';
                if (_e && !_e._wasmDead) try {
                    var len = _e.exports.getSelectedTextLength(); _e._clearEx();
                    if (len > 0) {
                        var addr = _e.exports.getStringBufferAddress(); _e._clearEx();
                        selText = new TextDecoder().decode(new Uint8Array(_e._mem(), addr, len));
                    }
                } catch(e) {}
                self.postMessage({ type: 'selectedText', text: selText });
                break;

            case 'cutSelectedText':
                var cutText = '';
                if (_e && !_e._wasmDead) try {
                    var clen = _e.exports.cutSelectedText(); _e._clearEx();
                    if (clen > 0) {
                        var caddr = _e.exports.getStringBufferAddress(); _e._clearEx();
                        cutText = new TextDecoder().decode(new Uint8Array(_e._mem(), caddr, clen));
                    }
                } catch(e) {}
                self.postMessage({ type: 'cutText', text: cutText });
                break;

            case 'selectAll':
                if (_e && !_e._wasmDead) try {
                    _e.exports.selectAll(); _e._clearEx();
                } catch(e) {}
                break;

            case 'fetchRelayResult': {
                // Main thread completed a cross-origin fetch on our behalf
                var relay = _fetchRelayMap[msg.relayId];
                if (!relay) break;
                delete _fetchRelayMap[msg.relayId];
                if (msg.error) {
                    console.log('[WORKER] relay ERR: ' + relay.url + ' status=' + (msg.status || 0));
                    if (relay.fallbacks.length > 0) {
                        relay.engine._doFetchSync(relay.taskId, relay.fallbacks[0],
                                                   relay.method, relay.postData,
                                                   relay.fallbacks.slice(1));
                    } else {
                        _fetchQueue.push({ taskId: relay.taskId, error: msg.status || 0 });
                    }
                } else {
                    console.log('[WORKER] relay OK: ' + relay.url + ' (' + msg.data.byteLength + ' bytes)');
                    _fetchQueue.push({ taskId: relay.taskId, data: msg.data, url: relay.url });
                }
                break;
            }

            case 'tick': {
                if (_isTicking) {
                    // ALWAYS respond so main thread's _waitFor('frame') doesn't hang
                    self.postMessage({
                        type: 'frame', playing: true,
                        enginePlaying: _e ? _e.playing : false,
                        tempo: _e ? _e._lastTempo : 15,
                        lastFrame: _e ? _e._lastFrame : 0,
                        frameCount: _e ? _e._lastFrameCount : 0,
                        rgba: null, width: 0, height: 0, spriteCount: 0
                    });
                    return;
                }
                _isTicking = true;
                try {
                    var stillPlaying = true;
                    var frame = null;
                    _tickNum++;

                    // Phase 1: deliver completed network results from previous ticks.
                    // This runs BEFORE tick() so netDone() returns true for finished fetches.
                    // Cast files are automatically detected and cached in CastLibManager
                    // when delivered, so they're available when Lingo sets castLib.fileName.
                    var nDelivered = 0;
                    if (!_e._wasmDead) {
                        try {
                            nDelivered = _e.deliverQueuedResults();
                        } catch (deliverErr) {
                            console.error('[WORKER] deliver error: ' + deliverErr);
                        }
                    }

                    // Phase 1.8: deliver pending Multiuser Xtra events (WebSocket)
                    if (!_e._wasmDead) {
                        try { _e.deliverMusEvents(); }
                        catch (musErr) { console.error('[WORKER] MUS deliver error: ' + musErr); }
                    }

                    // Phase 2: advance WASM by one Lingo frame
                    if (_e._wasmDead) {
                        console.error('[WORKER] WASM instance is dead, skipping tick');
                        stillPlaying = false;
                    } else {
                        var tickT0 = performance.now();
                        try {
                            stillPlaying = _e.tick();
                        } catch (tickErr) {
                            console.error('[WORKER] tick() error: ' + tickErr);
                            // Check if WASM is still alive
                            try { _e.exports.getStringBufferAddress(); _e._clearEx(); } catch(e) {
                                console.error('[WORKER] WASM instance dead after tick error');
                                _e._wasmDead = true;
                            }
                        }
                    }

                    // Phase 3: fire new network requests (non-blocking).
                    // Results are queued asynchronously and delivered at the start of
                    // the next tick. This decouples network I/O from frame execution,
                    // preventing deadlocks when Lingo polls netDone() in update loops.
                    var nFired = 0;
                    try {
                        nFired = _e.pumpNetworkFire();
                    } catch (netErr) {
                        console.error('[WORKER] pump error: ' + netErr);
                    }

                    // Phase 3.5: pump Multiuser Xtra WebSocket requests
                    try { _e.pumpMusRequests(); }
                    catch (musErr2) { console.error('[WORKER] MUS pump error: ' + musErr2); }

                    // Phase 3.6: pump audio commands and send to main thread
                    try { _e.pumpAudioCommands(); }
                    catch (audioErr) { /* silent */ }

                    // Always update frame metadata from WASM (needed for fast-loop detection)
                    try {
                        _e._lastFrame      = _e.exports.getCurrentFrame();
                        _e._lastFrameCount = _e.exports.getFrameCount();
                        _e._lastTempo      = _e.exports.getTempo();
                    } catch (ignore) {}

                    // Phase 4: render (skip during fast-loading for performance)
                    if (!msg.skipRender) {
                        try {
                            frame = _e.renderFrame();
                        } catch (renderErr) {
                            console.error('[WORKER] render() error: ' + renderErr);
                        }
                    }

                    // Always read sprite count (needed for fast-loop detection on main thread)
                    var spriteCount = 0;
                    try { spriteCount = _e.exports.getSpriteCount(); _e._clearEx(); } catch(e4) {}

                    // Read cursor type for current mouse position
                    var cursorType = 0;
                    try { cursorType = _e.exports.getCursorType(); _e._clearEx(); } catch(e5) {}

                    // Read cursor bitmap data (composited on main thread at 60fps)
                    var cursorBitmap = null;
                    try {
                        var hasCursor = _e.exports.updateCursorBitmap(); _e._clearEx();
                        if (hasCursor) {
                            var cw = _e.exports.getCursorBitmapWidth();
                            var ch = _e.exports.getCursorBitmapHeight();
                            var cLen = _e.exports.getCursorBitmapLength();
                            var cAddr = _e.exports.getCursorBitmapAddress(); _e._clearEx();
                            if (cAddr && cLen > 0) {
                                var cRgba = new Uint8ClampedArray(cLen);
                                cRgba.set(new Uint8ClampedArray(_e._mem(), cAddr, cLen));
                                cursorBitmap = {
                                    rgba: cRgba,
                                    w: cw,
                                    h: ch,
                                    regX: _e.exports.getCursorRegPointX(),
                                    regY: _e.exports.getCursorRegPointY()
                                };
                            }
                        }
                    } catch(e6) {}

                    // Read text caret info for blinking cursor overlay
                    var caretInfo = null;
                    var selectionRects = null;
                    try {
                        if (_e.exports.isCaretVisible()) {
                            caretInfo = { x: _e.exports.getCaretX(), y: _e.exports.getCaretY(),
                                          h: _e.exports.getCaretHeight() };
                        }
                        _e._clearEx();
                        var selCount = _e.exports.getSelectionRectCount(); _e._clearEx();
                        if (selCount > 0) {
                            selectionRects = [];
                            for (var si = 0; si < selCount; si++) {
                                selectionRects.push({
                                    x: _e.exports.getSelectionRectX(si),
                                    y: _e.exports.getSelectionRectY(si),
                                    w: _e.exports.getSelectionRectW(si),
                                    h: _e.exports.getSelectionRectH(si)
                                });
                            }
                            _e._clearEx();
                        }
                    } catch(e7) {}

                    // Drain debug log from WASM
                    var debugLog = null;
                    try {
                        var logLen = _e.exports.getDebugLog(); _e._clearEx();
                        if (logLen > 0) {
                            var strAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
                            debugLog = _e._readString(strAddr, logLen);
                        }
                    } catch (logErr) {}

                    // Always send a frame response to unblock main thread
                    var transferList = [];
                    if (frame) transferList.push(frame.rgba.buffer);
                    if (cursorBitmap) transferList.push(cursorBitmap.rgba.buffer);
                    self.postMessage({
                        type:          'frame',
                        playing:       stillPlaying,
                        enginePlaying: _e.playing,
                        tempo:         _e._lastTempo,
                        lastFrame:     _e._lastFrame,
                        frameCount:    _e._lastFrameCount,
                        rgba:          frame ? frame.rgba : null,
                        width:         frame ? frame.w : 0,
                        height:        frame ? frame.h : 0,
                        spriteCount:   spriteCount,
                        cursorType:    cursorType,
                        cursorBitmap:  cursorBitmap,
                        caretInfo:     caretInfo,
                        selectionRects: selectionRects,
                        debugLog:      debugLog
                    }, transferList);

                } finally {
                    _isTicking = false;
                }
                break;
            }

            case 'audioStopped': {
                // Main thread notifies that a sound channel finished playing
                try {
                    _e.exports.audioNotifyStopped(msg.channel); _e._clearEx();
                } catch(e) {}
                break;
            }

            case 'triggerTestError': {
                var result = 0;
                try {
                    result = _e.exports.triggerTestError(); _e._clearEx();
                } catch (err) {}
                // Re-render so the error dialog appears on screen
                var frame = null;
                try { frame = _e.renderFrame(); } catch (re) {}
                self.postMessage({
                    type: 'frame',
                    playing: true,
                    enginePlaying: _e.playing,
                    tempo: _e._lastTempo,
                    lastFrame: _e._lastFrame,
                    frameCount: _e._lastFrameCount,
                    rgba: frame ? frame.rgba : null,
                    width: frame ? frame.w : 0,
                    height: frame ? frame.h : 0,
                    spriteCount: 0
                }, frame ? [frame.rgba.buffer] : []);
                break;
            }

            case 'getCallStack': {
                var stackStr = '';
                try {
                    var len = _e.exports.getCallStack(); _e._clearEx();
                    if (len > 0) {
                        var addr = _e.exports.getStringBufferAddress(); _e._clearEx();
                        stackStr = _e._readString(addr, len);
                    }
                } catch (csErr) {}
                self.postMessage({ type: 'callStack', callStack: stackStr });
                break;
            }

            default:
                break;
        }
    } catch (err) {
        self.postMessage({ type: 'error', msg: String(err) });
    }
};
