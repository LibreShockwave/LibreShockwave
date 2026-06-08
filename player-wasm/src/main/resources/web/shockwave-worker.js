'use strict';

// Suppress noisy worker console output by default, but still relay the logs that
// matter for startup/network failures to the main thread console.
function _relayWorkerLog(type, args) {
    var msg = Array.prototype.slice.call(args).join(' ');
    if (!msg) return;
    if (type === 'error') {
        self.postMessage({ type: 'error', msg: msg });
        return;
    }
    if (!_debugLogsEnabled) {
        return;
    }
    if (msg.indexOf('[WORKER]') >= 0
            || msg.indexOf('[TRACE]') >= 0
            || msg.indexOf('Lingo call stack:') >= 0
            || msg.indexOf('[Player]') >= 0
            || msg.indexOf('[CastLib]') >= 0
            || msg.indexOf('[NetManager]') >= 0
            || msg.indexOf('[MUS]') >= 0
            || msg.indexOf('[EH]') >= 0
            || msg.indexOf('[EventDispatcher]') >= 0
            || msg.indexOf('[HitTest]') >= 0
            || msg.indexOf('[DEBUG') >= 0) {
        self.postMessage({ type: 'debugLog', msg: msg });
    }
}
console.log = function() { _relayWorkerLog('log', arguments); };
console.warn = function() { _relayWorkerLog('warn', arguments); };
console.error = function() { _relayWorkerLog('error', arguments); };

function _musDebug(msg) {
    if (_debugLogsEnabled) {
        self.postMessage({ type: 'debugLog', msg: '[MUS] ' + msg });
    }
}

function _musPreview(data) {
    if (!_debugLogsEnabled || data == null) return '';
    var bytes = data instanceof Uint8Array ? data : _binaryStringToBytes(String(data));
    var hex = [];
    var text = '';
    var limit = Math.min(bytes.length, 64);
    for (var i = 0; i < limit; i++) {
        var b = bytes[i];
        hex.push((b < 16 ? '0' : '') + b.toString(16));
        text += (b >= 32 && b < 127) ? String.fromCharCode(b) : '.';
    }
    if (bytes.length > limit) {
        hex.push('...');
        text += '...';
    }
    return ' hex=' + hex.join(' ') + ' ascii=' + JSON.stringify(text);
}

/**
 * LibreShockwave Web Worker — runs the WASM engine off the main thread.
 *
 * Message protocol (main → worker):
 *   {type:'init',    basePath}
 *   {type:'loadMovie', data:ArrayBuffer, basePath}
 *   {type:'setParam',  key, value}
 *   {type:'clearParams'}
 *   {type:'setMovieProperty', key, value}
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
 *   {type:'frame', playing, enginePlaying, tempo, lastFrame, frameCount,
 *                  rgba:Uint8ClampedArray, width, height}
 *   {type:'error', msg}
 */

var _e = null;          // WasmEngine instance
var _isTicking = false; // guard against overlapping ticks
var _pageProtocol = ''; // page protocol from main thread (e.g. 'https:')
var _debugLogsEnabled = false;
// --- Multiuser Xtra WebSocket connections ---
var _musSockets = {};      // instanceId -> WebSocket
var _musInbound = {};      // instanceId -> [Uint8Array] (raw MUS TCP chunks)

var _musConnected = {};    // instanceId -> true (pending connect notifications)
var _musDisconnected = {}; // instanceId -> true (pending disconnect notifications)
var _musErrors = {};       // instanceId -> errorCode
var _musDrainScheduled = false;
var _isMusDraining = false;

// --- Non-blocking fetch delivery queue ---
var _fetchQueue = [];   // [{taskId, data: ArrayBuffer}] or [{taskId, error: number}]
var _jpegDecodeQueue = []; // [{id, width, height, data: Uint8Array}]
var _jpegDecodeInFlight = {}; // id -> true
var _jpegDecodeSeq = 0; // increments per movie load to ignore stale async decodes
var _loadStartTime = 0; // timestamp when loading began (for perf logging)
var _networkSeq = 0;    // increments per movie load to ignore stale async fetches
var _sharedFrameBytes = null;
var _sharedFrameControl = null;
var _sharedFrameCapacity = 0;
var _sharedFrameSeq = 0;
var _fetchResultCache = {};
var _fetchInflight = {};
var _lastTickTiming = {};

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

function _refreshTempoCache() {
    if (!_e || !_e.exports) return;
    try {
        _e._lastTempo = _e.exports.getTempo();
        _e._clearEx();
    } catch (tempoErr) {}
}

function _flushWasmDiagnostics() {
    if (!_e || !_e.exports) return;

    if (_debugLogsEnabled) {
        try {
            var logLen = _e.exports.getDebugLog(); _e._clearEx();
            if (logLen > 0) {
                var logAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
                var logMsg = _e._readString(logAddr, logLen);
                if (logMsg) {
                    self.postMessage({ type: 'debugLog', msg: logMsg });
                }
            }
        } catch (logErr) {}
    }

    _relayLastError();

    _drainGotoNetPages();
}

function _relayLastError() {
    if (!_e || !_e.exports) return;
    try {
        var errLen = _e.exports.getLastError(); _e._clearEx();
        if (errLen <= 0) {
            return;
        }
        var errAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
        var errMsg = _e._readString(errAddr, errLen);
        if (errMsg) {
            self.postMessage({ type: 'error', msg: errMsg });
        }
    } catch (errRead) {}
}
function _drainGotoNetPages() {
    if (!_e || !_e.exports) return;
    try {
        while (true) {
            var packed = _e.exports.readNextGotoNetPage(); _e._clearEx();
            if (!packed) break;
            var urlLen = (packed >>> 16) & 0xFFFF;
            var targetLen = packed & 0xFFFF;
            var strAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
            var url = urlLen > 0
                ? new TextDecoder().decode(new Uint8Array(_e._mem(), strAddr, urlLen))
                : '';
            var target = targetLen > 0
                ? new TextDecoder().decode(new Uint8Array(_e._mem(), strAddr + urlLen, targetLen))
                : '';
            self.postMessage({ type: 'gotoNetPage', url: url, target: target });
        }
    } catch (navErr) {}
    _drainGotoNetMovies();
}

function _drainGotoNetMovies() {
    if (!_e || !_e.exports) return;
    try {
        while (true) {
            var urlLen = _e.exports.readNextGotoNetMovie(); _e._clearEx();
            if (!urlLen) break;
            var strAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
            var url = new TextDecoder().decode(new Uint8Array(_e._mem(), strAddr, urlLen));
            self.postMessage({ type: 'gotoNetMovie', url: url });
        }
    } catch (navErr) {}
}

// --- External params stored locally for runtime checks ---
var _params = {};

// --- Diagnostic tick counter ---
var _tickNum = 0;

function _musRuntimeDiagnostics() {
    var socketStates = {};
    for (var instId in _musSockets) {
        if (Object.prototype.hasOwnProperty.call(_musSockets, instId)) {
            var ws = _musSockets[instId];
            socketStates[instId] = ws ? ws.readyState : null;
        }
    }
    var inboundCounts = {};
    for (var inboundId in _musInbound) {
        if (Object.prototype.hasOwnProperty.call(_musInbound, inboundId)) {
            inboundCounts[inboundId] = _musInbound[inboundId] ? _musInbound[inboundId].length : 0;
        }
    }
    var pendingCount = 0;
    if (_e && _e.exports && !_e._wasmDead) {
        try {
            pendingCount = _e.exports.getMusPendingCount(); _e._clearEx();
        } catch (e) {}
    }
    return {
        pendingCount: pendingCount,
        sockets: socketStates,
        connected: Object.assign({}, _musConnected),
        disconnected: Object.assign({}, _musDisconnected),
        errors: Object.assign({}, _musErrors),
        inbound: inboundCounts
    };
}

function _hasMusQueuedJsEvents() {
    return Object.keys(_musConnected).length > 0
        || Object.keys(_musDisconnected).length > 0
        || Object.keys(_musErrors).length > 0
        || Object.keys(_musInbound).some(function(id) {
            return _musInbound[id] && _musInbound[id].length > 0;
        });
}

function _getMusPendingCountSafe() {
    if (!_e || !_e.exports || _e._wasmDead) return 0;
    try {
        var count = _e.exports.getMusPendingCount(); _e._clearEx();
        return count || 0;
    } catch (e) {
        return 0;
    }
}

function _scheduleMusDrain() {
    if (_musDrainScheduled) return;
    _musDrainScheduled = true;
    setTimeout(function() {
        _musDrainScheduled = false;
        _drainMusNow();
    }, 0);
}

function _drainMusNow() {
    if (!_e || _e._wasmDead || _isTicking || _isMusDraining) {
        if (_hasMusQueuedJsEvents() || _getMusPendingCountSafe() > 0) {
            _scheduleMusDrain();
        }
        return;
    }
    _isMusDraining = true;
    try {
        var deadline = performance.now() + 12;
        for (var i = 0; i < 12; i++) {
            var hadWork = _hasMusQueuedJsEvents() || _getMusPendingCountSafe() > 0;
            if (!hadWork) break;
            try { _e.deliverMusEvents(); }
            catch (deliverErr) { console.error('[WORKER] MUS fast deliver error: ' + deliverErr); }
            try { _e.pumpMusRequests(); }
            catch (pumpErr) { console.error('[WORKER] MUS fast pump error: ' + pumpErr); }
            if (performance.now() >= deadline) break;
        }
    } finally {
        _isMusDraining = false;
    }
    if (_hasMusQueuedJsEvents() || _getMusPendingCountSafe() > 0) {
        _scheduleMusDrain();
    }
}

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
WasmEngine.prototype._readBytes = function(addr, len) {
    var copy = new Uint8Array(len);
    copy.set(new Uint8Array(this._mem(), addr, len));
    return copy;
};

function _binaryStringToBytes(value) {
    var bytes = new Uint8Array(value.length);
    for (var i = 0; i < value.length; i++) {
        bytes[i] = value.charCodeAt(i) & 0xff;
    }
    return bytes;
}

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

WasmEngine.prototype.setInitialBuiltinSymbol = function(key, value) {
    var kb = new TextEncoder().encode(key), vb = new TextEncoder().encode(value);
    var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 4096);
    sbuf.set(kb); sbuf.set(vb, kb.length);
    this.exports.setInitialBuiltinSymbol(kb.length, vb.length);
    this._clearEx();
};

WasmEngine.prototype.setMovieProperty = function(key, value) {
    var kb = new TextEncoder().encode(key), vb = new TextEncoder().encode(String(value));
    var sbuf = new Uint8Array(this._mem(), this.exports.getStringBufferAddress(), 4096);
    sbuf.set(kb); sbuf.set(vb, kb.length);
    this.exports.setMovieProperty(kb.length, vb.length);
    this._clearEx();
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

/**
 * Legacy helper kept for callers that still ask to "collect" requests.
 * It now only starts the fetches and returns immediately; completed responses
 * are delivered later by deliverQueuedResults().
 */
WasmEngine.prototype.pumpNetworkCollect = function() {
    this.pumpNetworkFire();
    return [];
};

// ============================================================
// Non-blocking fetch pipeline (used during tick for async I/O)
// ============================================================

/**
 * Ask the main thread to perform a fetch that the worker should not perform
 * directly. Resolves asynchronously; never blocks the worker tick loop.
 */
WasmEngine.prototype._relayFetch = function(url, method, postData) {
    return new Promise(function(resolve, reject) {
        var relayId = ++_fetchRelayCounter;
        _fetchRelayMap[relayId] = {
            url: url,
            resolve: resolve,
            reject: reject
        };
        self.postMessage({
            type: 'fetchRelay',
            relayId: relayId,
            url: url,
            method: method || 'GET',
            postData: postData || null
        });
    });
};

WasmEngine.prototype._fetchOnce = function(url, method, postData) {
    if (_isCrossOrigin(url)) {
        return this._relayFetch(url, method, postData);
    }
    var opts = {};
    if (method === 'POST') {
        opts.method = 'POST';
        opts.body = postData || null;
        opts.headers = { 'Content-Type': 'application/x-www-form-urlencoded' };
    }
    return _fetchWithTimeout(url, opts, 30000)
        .then(function(r) {
            if (!r.ok) throw { status: r.status };
            return r.arrayBuffer();
        });
};

WasmEngine.prototype._fetchWithFallbacks = async function(taskId, url, method, postData, fallbacks) {
    var urls = [url];
    if (fallbacks) {
        for (var i = 0; i < fallbacks.length; i++) urls.push(fallbacks[i]);
    }
    for (var j = 0; j < urls.length; j++) {
        var tryUrl = urls[j];
        console.log('[WORKER] fetch: ' + tryUrl + (j === 0 && urls.length > 1 ? ' (+' + (urls.length - 1) + ' fallbacks)' : ''));
        try {
            var buf = await this._fetchOnce(tryUrl, method, postData);
            if (_isImportableImageUrl(tryUrl)) {
                buf = await _decodeImageForImport(buf, tryUrl);
            }
            console.log('[WORKER] fetch OK: ' + tryUrl + ' (' + buf.byteLength + ' bytes)');
            return { data: buf, url: tryUrl, status: 200 };
        } catch (e) {
            console.warn('[WORKER] fetch ERR: ' + tryUrl + ' status=' + ((e && e.status) || 'network'));
        }
    }
    return { error: true, status: 0 };
};

function _fetchCacheKey(req) {
    return [
        req.method || 'GET',
        req.url || '',
        req.postData || '',
        (req.fallbacks || []).join('\u0001')
    ].join('\u0000');
}

function _cloneFetchResult(result) {
    if (!result) return result;
    var copy = {};
    for (var k in result) copy[k] = result[k];
    if (result.data) {
        copy.data = result.data.slice(0);
    }
    return copy;
}

function _isImportableImageUrl(url) {
    if (!url) return false;
    var lower = String(url).toLowerCase();
    var qi = lower.indexOf('?');
    if (qi >= 0) lower = lower.substring(0, qi);
    return lower.endsWith('.gif') || lower.endsWith('.png')
        || lower.endsWith('.jpg') || lower.endsWith('.jpeg');
}

async function _decodeImageForImport(arrayBuffer, url) {
    if (typeof createImageBitmap !== 'function' || typeof OffscreenCanvas === 'undefined') {
        return arrayBuffer;
    }
    try {
        var blob = new Blob([arrayBuffer]);
        var bitmap = await createImageBitmap(blob);
        var canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
        var ctx = canvas.getContext('2d');
        ctx.drawImage(bitmap, 0, 0);
        if (bitmap.close) bitmap.close();
        var rgba = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
        var out = new Uint8Array(12 + rgba.length);
        out[0] = 0x4c; out[1] = 0x53; out[2] = 0x57; out[3] = 0x49; // LSWI
        _writeU32BE(out, 4, canvas.width);
        _writeU32BE(out, 8, canvas.height);
        out.set(rgba, 12);
        return out.buffer;
    } catch (e) {
        console.warn('[WORKER] image decode skipped for ' + url + ': ' + e);
        return arrayBuffer;
    }
}

async function _decodeImageToRgba(arrayBuffer) {
    if (typeof createImageBitmap !== 'function' || typeof OffscreenCanvas === 'undefined') {
        return null;
    }
    var blob = new Blob([arrayBuffer], { type: 'image/jpeg' });
    var bitmap = await createImageBitmap(blob);
    var canvas = new OffscreenCanvas(bitmap.width, bitmap.height);
    var ctx = canvas.getContext('2d');
    ctx.drawImage(bitmap, 0, 0);
    if (bitmap.close) bitmap.close();
    var rgba = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
    var copy = new Uint8Array(rgba.length);
    copy.set(rgba);
    return { width: canvas.width, height: canvas.height, data: copy };
}

function _writeU32BE(bytes, offset, value) {
    bytes[offset] = (value >>> 24) & 0xFF;
    bytes[offset + 1] = (value >>> 16) & 0xFF;
    bytes[offset + 2] = (value >>> 8) & 0xFF;
    bytes[offset + 3] = value & 0xFF;
}

/**
 * Deliver queued fetch results into WASM.
 * Completed browser fetches are already asynchronous; holding finished results
 * to one per tick makes startup scripts wait on artificial frame latency.
 * @return number of results delivered
 */
WasmEngine.prototype.deliverQueuedResults = function() {
    if (_fetchQueue.length === 0) return 0;
    var delivered = 0;
    while (_fetchQueue.length > 0) {
        var item = _fetchQueue.shift();
        if (item.data !== undefined) {
            this._deliverResult(item.taskId, item.data, item.url);
        } else {
            this._deliverError(item.taskId, item.error);
        }
        delivered++;
    }
    _flushWasmDiagnostics();
    return delivered;
};

WasmEngine.prototype.deliverJpegDecodeResults = function() {
    if (_jpegDecodeQueue.length === 0) return 0;
    var delivered = 0;
    while (_jpegDecodeQueue.length > 0) {
        var item = _jpegDecodeQueue.shift();
        try {
            if (item.data.length > 0) {
                var addr = this.exports.allocateNetBuffer(item.data.length); this._clearEx();
                new Uint8Array(this._mem(), addr, item.data.length).set(item.data);
            }
            this.exports.deliverJpegDecodeResult(item.id, item.width, item.height, item.data.length);
            this._clearEx();
            delivered++;
        } catch (e) {
            console.error('[WORKER] deliverJpegDecodeResult error id=' + item.id + ': ' + e);
            this._clearEx();
        }
    }
    return delivered;
};

WasmEngine.prototype.pumpJpegDecodeRequests = function() {
    if (this._wasmDead) return;
    var count;
    try {
        count = this.exports.getPendingJpegDecodeCount(); this._clearEx();
    } catch (e) { return; }
    if (count === 0) return;

    var engine = this;
    var seq = _jpegDecodeSeq;
    for (var i = 0; i < count; i++) {
        var id = this.exports.getPendingJpegDecodeId(i); this._clearEx();
        if (!id || _jpegDecodeInFlight[id]) continue;
        var len = this.exports.getPendingJpegDecodeData(id); this._clearEx();
        var addr = this.exports.getPendingJpegDecodeDataAddress(); this._clearEx();
        if (!len || !addr) continue;
        var bytes = new Uint8Array(len);
        bytes.set(new Uint8Array(this._mem(), addr, len));
        _jpegDecodeInFlight[id] = true;
        (function(decodeId, data) {
            _decodeImageToRgba(data.buffer).then(function(decoded) {
                if (seq !== _jpegDecodeSeq) return;
                if (decoded) {
                    _jpegDecodeQueue.push({
                        id: decodeId,
                        width: decoded.width,
                        height: decoded.height,
                        data: decoded.data
                    });
                } else {
                    _jpegDecodeQueue.push({
                        id: decodeId,
                        width: 0,
                        height: 0,
                        data: new Uint8Array(0)
                    });
                }
            }).catch(function(e) {
                if (seq !== _jpegDecodeSeq) return;
                console.warn('[WORKER] embedded JPEG decode failed id=' + decodeId + ': ' + e);
                _jpegDecodeQueue.push({
                    id: decodeId,
                    width: 0,
                    height: 0,
                    data: new Uint8Array(0)
                });
            }).finally(function() {
                if (seq !== _jpegDecodeSeq) return;
                delete _jpegDecodeInFlight[decodeId];
            });
        })(id, bytes);
    }
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
    var seq = _networkSeq;
    var engine = this;
    for (var i = 0; i < reqs.length; i++) {
        let req = reqs[i];
        let key = _fetchCacheKey(req);
        if (req.method === 'GET' && _fetchResultCache[key]) {
            var cachedResult = _cloneFetchResult(_fetchResultCache[key]);
            if (cachedResult && cachedResult.data) {
                _fetchQueue.push({ taskId: req.taskId, data: cachedResult.data, url: cachedResult.url || req.url });
            } else {
                _fetchQueue.push({ taskId: req.taskId, error: cachedResult && cachedResult.status ? cachedResult.status : 0 });
            }
            continue;
        }
        if (req.method === 'GET' && _fetchInflight[key]) {
            _fetchInflight[key].then(function(result) {
                if (seq !== _networkSeq) return;
                result = _cloneFetchResult(result);
                if (result && result.data) {
                    _fetchQueue.push({ taskId: req.taskId, data: result.data, url: result.url || req.url });
                } else {
                    _fetchQueue.push({ taskId: req.taskId, error: result && result.status ? result.status : 0 });
                }
            }).catch(function(e) {
                if (seq !== _networkSeq) return;
                _fetchQueue.push({ taskId: req.taskId, error: e && e.status ? e.status : 0 });
            });
            continue;
        }
        var promise = engine._fetchWithFallbacks(req.taskId, req.url, req.method, req.postData, req.fallbacks || []);
        if (req.method === 'GET') {
            _fetchInflight[key] = promise;
        }
        promise
            .then(function(result) {
                if (seq !== _networkSeq) return;
                if (req.method === 'GET' && result && result.data) {
                    _fetchResultCache[key] = _cloneFetchResult(result);
                }
                if (result && result.data) {
                    _fetchQueue.push({ taskId: req.taskId, data: result.data, url: result.url || req.url });
                } else {
                    _fetchQueue.push({ taskId: req.taskId, error: result && result.status ? result.status : 0 });
                }
            })
            .catch(function(e) {
                if (seq !== _networkSeq) return;
                _fetchQueue.push({ taskId: req.taskId, error: e && e.status ? e.status : 0 });
            })
            .finally(function() {
                if (req.method === 'GET') {
                    delete _fetchInflight[key];
                }
            });
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
 * By default the WebSocket URL is built from the Lingo host/port, but
 * websocket.mode can force ws:// or wss:// from the player options.
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

            var wsUrl = _buildMusWebSocketUrl(host, port);
            _musDebug('connect request instance=' + instId + ' target=' + host + ':' + port + ' url=' + wsUrl);
            this._musConnect(instId, wsUrl);

        } else if (type === 1) {
            // SEND — raw content bytes (must be binary frame for websockify)
            var dataLen = this.exports.getMusPendingSendData(i); this._clearEx();
            var data = this._readBytes(strAddr, dataLen);
            var ws = _musSockets[instId];
            if (ws && ws.readyState === WebSocket.OPEN) {
                _musDebug('send instance=' + instId + ' bytes=' + dataLen + _musPreview(data));
                ws.send(data.buffer);
            } else {
                _musDebug('send dropped instance=' + instId + ' readyState=' + (ws ? ws.readyState : 'missing') + ' bytes=' + dataLen + _musPreview(data));
            }

        } else if (type === 2) {
            // DISCONNECT
            var ws2 = _musSockets[instId];
            if (ws2) {
                _musDebug('disconnect request instance=' + instId);
                ws2.onclose = null; // prevent double-notification
                ws2.close();
                delete _musSockets[instId];
            }
        }
    }

    this.exports.drainMusPending(); this._clearEx();
};

function _buildMusWebSocketUrl(host, port) {
    var protocol = _shouldUseSecureMusWebSocket(host, port) ? 'wss' : 'ws';
    return protocol + '://' + host + ':' + port;
}

function _shouldUseSecureMusWebSocket(host, port) {
    var forcedMode = String((_params && _params['websocket.mode']) || '').toLowerCase();
    if (forcedMode === 'wss') {
        return true;
    }
    if (forcedMode === 'ws') {
        return false;
    }
    var normalizedPort = String(port || '');
    if (_pageProtocol === 'https:' || normalizedPort === '443') {
        return true;
    }
    return false;
}

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
        _musDebug('WebSocket constructor failed instance=' + instId + ' url=' + wsUrl + ' error=' + e.message);
        _musErrors[instId] = -3; // connection refused
        return;
    }
    ws.binaryType = 'arraybuffer';
    _musSockets[instId] = ws;

    ws.onopen = function() {
        _musDebug('open instance=' + instId + ' url=' + wsUrl);
        _musConnected[instId] = true;
        _scheduleMusDrain();
    };

    ws.onmessage = function(evt) {
        // Receive raw message content
        if (!_musInbound[instId]) _musInbound[instId] = [];
        var data;
        if (typeof evt.data === 'string') {
            data = _binaryStringToBytes(evt.data);
        } else {
            data = new Uint8Array(evt.data);
        }
        _musDebug('message instance=' + instId + ' bytes=' + data.length + _musPreview(data));
        _musInbound[instId].push(data);
        _scheduleMusDrain();
    };

    ws.onclose = function(evt) {
        _musDebug('close instance=' + instId + ' code=' + (evt && evt.code) + ' reason=' + (evt && evt.reason ? evt.reason : '') + ' wasClean=' + (evt && evt.wasClean));
        _musDisconnected[instId] = true;
        delete _musSockets[instId];
        _scheduleMusDrain();
    };

    ws.onerror = function() {
        _musDebug('error instance=' + instId + ' url=' + wsUrl);
        _musErrors[instId] = -2; // network error
        _scheduleMusDrain();
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
        } catch(e) {
            console.error('[MUS] musDeliverConnected error instance=' + id + ': ' + e);
        }
    }
    _musConnected = {};

    // Deliver error notifications
    for (var eid in _musErrors) {
        try {
            this.exports.musDeliverError(parseInt(eid), _musErrors[eid]); this._clearEx();
        } catch(e) {
            console.error('[MUS] musDeliverError error instance=' + eid + ': ' + e);
        }
    }
    _musErrors = {};

    // Deliver messages
    var stringBufferCapacity = 4096;
    try {
        if (this.exports.getStringBufferCapacity) {
            stringBufferCapacity = this.exports.getStringBufferCapacity(); this._clearEx();
        }
    } catch(e) {
        stringBufferCapacity = 4096;
    }
    for (var mid in _musInbound) {
        var msgs = _musInbound[mid];
        var iid = parseInt(mid);
        for (var i = 0; i < msgs.length; i++) {
            var msgBytes = msgs[i];
            var len = Math.min(msgBytes.length, stringBufferCapacity);
            _musDebug('deliver instance=' + iid + ' bytes=' + msgBytes.length + ' copied=' + len + _musPreview(msgBytes));
            new Uint8Array(this._mem(), strAddr, len).set(msgBytes.subarray(0, len));
            try {
                this.exports.musDeliverMessage(iid, len); this._clearEx();
            } catch(e) {
                console.error('[MUS] musDeliverMessage error instance=' + iid + ': ' + e);
            }
        }
    }
    _musInbound = {};

    // Deliver disconnect notifications
    for (var did in _musDisconnected) {
        try {
            this.exports.musDeliverDisconnected(parseInt(did)); this._clearEx();
        } catch(e) {
            console.error('[MUS] musDeliverDisconnected error instance=' + did + ': ' + e);
        }
    }
    _musDisconnected = {};
};

// ============================================================
// URL helpers
// ============================================================

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

// ============================================================
// Message handler
// ============================================================

self.onmessage = async function(e) {
    var msg = e.data;
    try {
        switch (msg.type) {

            case 'init': {
                _pageProtocol = msg.pageProtocol || '';
                if (msg.sharedFrameBuffer && msg.sharedFrameControl && typeof Atomics === 'object') {
                    _sharedFrameBytes = new Uint8ClampedArray(msg.sharedFrameBuffer);
                    _sharedFrameControl = new Int32Array(msg.sharedFrameControl);
                    _sharedFrameCapacity = msg.sharedFrameCapacity || _sharedFrameBytes.length;
                }
                // Fallback: detect protocol from worker's own location
                // (e.g. blob:https://... when loaded as a blob URL worker)
                if (!_pageProtocol && self.location && self.location.href) {
                    _pageProtocol = (self.location.href.indexOf('https:') !== -1) ? 'https:' : '';
                }
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
                _networkSeq++;
                _fetchQueue = [];
                _fetchResultCache = {};
                _fetchInflight = {};
                _jpegDecodeSeq++;
                _jpegDecodeQueue = [];
                _jpegDecodeInFlight = {};
                var info = _e.loadMovie(new Uint8Array(msg.data), msg.basePath);
                _e.playing  = false;
                _flushWasmDiagnostics();
                self.postMessage({ type: 'movieLoaded', info: info });
                break;
            }

            case 'setParam':
                _e.setExternalParam(msg.key, msg.value);
                _params[msg.key] = msg.value;
                break;

            case 'setDebugPlayback':
                _debugLogsEnabled = !!msg.enabled;
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

            case 'setInitialBuiltinSymbol':
                _e.setInitialBuiltinSymbol(msg.key, msg.value);
                break;

            case 'setMovieProperty':
                _e.setMovieProperty(msg.key, msg.value);
                break;

            case 'play':
                console.log('[WORKER] play() — starting animation');
                _e.exports.play(); _e._clearEx(); _e.playing = true; _refreshTempoCache();
                _drainGotoNetPages();
                break;
            case 'pause':
                _e.exports.pause(); _e._clearEx(); _e.playing = false;
                break;
            case 'stop':
                _e.exports.stop(); _e._clearEx(); _e.playing = false;
                break;

            case 'goToFrame':
                _e.exports.goToFrame(msg.frame); _e._clearEx(); _refreshTempoCache(); _drainGotoNetPages();
                break;
            case 'stepForward':
                _e.exports.stepForward(); _e._clearEx(); _refreshTempoCache(); _drainGotoNetPages();
                break;
            case 'stepBackward':
                _e.exports.stepBackward(); _e._clearEx(); _refreshTempoCache(); _drainGotoNetPages();
                break;
            case 'setTempo':
                _e.exports.setPuppetTempo(msg.tempo | 0); _e._clearEx(); _refreshTempoCache();
                break;

            // --- Input events ---
            case 'mouseMove':
                if (_e && !_e._wasmDead) try { _e.mouseMove(msg.x, msg.y); } catch(ie) {}
                break;
            case 'mouseDown':
                if (_e && !_e._wasmDead) try {
                    _e.mouseDown(msg.x, msg.y, msg.button); _drainGotoNetPages();
                } catch(ie) {
                    console.error('[WORKER] mouseDown error:', ie);
                }
                break;
            case 'mouseUp':
                if (_e && !_e._wasmDead) try { _e.mouseUp(msg.x, msg.y, msg.button); _drainGotoNetPages(); } catch(ie) {
                    console.error('[WORKER] mouseUp error:', ie);
                }
                break;
            case 'keyDown':
                if (_e && !_e._wasmDead) try {
                    _e.keyDown(msg.keyCode, msg.key || '', msg.modifiers); _drainGotoNetPages();
                } catch(ie) { console.error('[WORKER] keyDown error:', ie); }
                break;
            case 'keyUp':
                if (_e && !_e._wasmDead) try {
                    _e.keyUp(msg.keyCode, msg.key || '', msg.modifiers); _drainGotoNetPages();
                } catch(ie) { console.error('[WORKER] keyUp error:', ie); }
                break;

            case 'paste':
                if (_e && !_e._wasmDead) try { _e.pasteText(msg.text); _drainGotoNetPages(); } catch(e) {}
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
                    if (relay.reject) {
                        relay.reject({ status: msg.status || 0 });
                    } else {
                        _fetchQueue.push({ taskId: relay.taskId, error: msg.status || 0 });
                    }
                } else {
                    console.log('[WORKER] relay OK: ' + relay.url + ' (' + msg.data.byteLength + ' bytes)');
                    if (relay.resolve) {
                        relay.resolve(msg.data);
                    } else {
                        _fetchQueue.push({ taskId: relay.taskId, data: msg.data, url: relay.url });
                    }
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
                    var timing = { startedAt: performance.now() };

                    // Phase 1: deliver completed network results from previous ticks.
                    // This runs BEFORE tick() so netDone() returns true for finished fetches.
                    // Cast files are automatically detected and cached in CastLibManager
                    // when delivered, so they're available when Lingo sets castLib.fileName.
                    var nDelivered = 0;
                    if (!_e._wasmDead) {
                        try {
                            nDelivered = _e.deliverQueuedResults();
                            _e.deliverJpegDecodeResults();
                        } catch (deliverErr) {
                            console.error('[WORKER] deliver error: ' + deliverErr);
                        }
                    }
                    timing.afterFetchDeliver = performance.now();

                    // Phase 1.8: deliver pending Multiuser Xtra events (WebSocket)
                    if (!_e._wasmDead) {
                        try { _e.deliverMusEvents(); }
                        catch (musErr) { console.error('[WORKER] MUS deliver error: ' + musErr); }
                    }
                    timing.afterMusDeliver = performance.now();

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
                    timing.afterTick = performance.now();

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
                    if (_hasMusQueuedJsEvents() || _getMusPendingCountSafe() > 0) {
                        _scheduleMusDrain();
                    }
                    timing.afterNetworkPump = performance.now();

                    // Phase 3.6: pump audio commands and send to main thread
                    try { _e.pumpAudioCommands(); }
                    catch (audioErr) { /* silent */ }

                    // Phase 3.7: decode embedded JPEG/ediM bitmaps in the browser.
                    try { _e.pumpJpegDecodeRequests(); }
                    catch (jpegErr) { console.error('[WORKER] JPEG pump error: ' + jpegErr); }

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
                            _e.pumpJpegDecodeRequests();
                        } catch (renderErr) {
                            console.error('[WORKER] render() error: ' + renderErr);
                        }
                    }
                    timing.afterRender = performance.now();

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

                    var debugLog = null;
                    if (_debugLogsEnabled) {
                        try {
                            var logLen = _e.exports.getDebugLog(); _e._clearEx();
                            if (logLen > 0) {
                                var strAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
                                debugLog = _e._readString(strAddr, logLen);
                            }
                        } catch (logErr) {}
                    }

                    _relayLastError();
                    _drainGotoNetPages();

                    var sharedFrame = false;
                    var sharedSeq = 0;
                    if (frame && _sharedFrameBytes && frame.rgba.length <= _sharedFrameCapacity) {
                        _sharedFrameBytes.set(frame.rgba);
                        sharedSeq = ++_sharedFrameSeq;
                        Atomics.store(_sharedFrameControl, 0, sharedSeq);
                        Atomics.store(_sharedFrameControl, 1, frame.rgba.length);
                        Atomics.store(_sharedFrameControl, 2, frame.w);
                        Atomics.store(_sharedFrameControl, 3, frame.h);
                        Atomics.notify(_sharedFrameControl, 0, 1);
                        sharedFrame = true;
                    }

                    // Always send a frame response to unblock main thread
                    var transferList = [];
                    if (frame && !sharedFrame) transferList.push(frame.rgba.buffer);
                    if (cursorBitmap) transferList.push(cursorBitmap.rgba.buffer);
                    self.postMessage({
                        type:          'frame',
                        playing:       stillPlaying,
                        enginePlaying: _e.playing,
                        tempo:         _e._lastTempo,
                        lastFrame:     _e._lastFrame,
                        frameCount:    _e._lastFrameCount,
                        rgba:          frame && !sharedFrame ? frame.rgba : null,
                        sharedFrame:   sharedFrame,
                        sharedSeq:     sharedSeq,
                        width:         frame ? frame.w : 0,
                        height:        frame ? frame.h : 0,
                        spriteCount:   spriteCount,
                        cursorType:    cursorType,
                        cursorBitmap:  cursorBitmap,
                        caretInfo:     caretInfo,
                        selectionRects: selectionRects,
                        debugLog:      debugLog
                    }, transferList);
                    timing.afterPostMessage = performance.now();
                    _lastTickTiming = {
                        tick: _tickNum,
                        totalMs: Math.round((timing.afterPostMessage - timing.startedAt) * 10) / 10,
                        fetchDeliverMs: Math.round((timing.afterFetchDeliver - timing.startedAt) * 10) / 10,
                        musDeliverMs: Math.round((timing.afterMusDeliver - timing.afterFetchDeliver) * 10) / 10,
                        tickMs: Math.round((timing.afterTick - timing.afterMusDeliver) * 10) / 10,
                        networkPumpMs: Math.round((timing.afterNetworkPump - timing.afterTick) * 10) / 10,
                        renderMs: Math.round((timing.afterRender - timing.afterNetworkPump) * 10) / 10,
                        postMessageMs: Math.round((timing.afterPostMessage - timing.afterRender) * 10) / 10,
                        skippedRender: !!msg.skipRender
                    };

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

            case 'getWindowSpriteDiagnostics': {
                var diagStr = '';
                try {
                    var dlen = _e.exports.getWindowSpriteDiagnostics(); _e._clearEx();
                    if (dlen > 0) {
                        var daddr = _e.exports.getStringBufferAddress(); _e._clearEx();
                        diagStr = _e._readString(daddr, dlen);
                    }
                } catch (diagErr) {}
                self.postMessage({ type: 'windowSpriteDiagnostics', diagnostics: diagStr });
                break;
            }

            case 'getVisibleTextDiagnostics': {
                var textDiagStr = '';
                try {
                    var textDiagLen = _e.exports.getVisibleTextDiagnostics(); _e._clearEx();
                    if (textDiagLen > 0) {
                        var textDiagAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
                        textDiagStr = _e._readString(textDiagAddr, textDiagLen);
                    }
                } catch (textDiagErr) {}
                self.postMessage({ type: 'visibleTextDiagnostics', diagnostics: textDiagStr });
                break;
            }

            case 'getBootstrapDiagnostics': {
                var bootstrapDiagStr = '';
                try {
                    var bootstrapDiagLen = _e.exports.getBootstrapDiagnostics(); _e._clearEx();
                    if (bootstrapDiagLen > 0) {
                        var bootstrapDiagAddr = _e.exports.getStringBufferAddress(); _e._clearEx();
                        bootstrapDiagStr = _e._readString(bootstrapDiagAddr, bootstrapDiagLen);
                    }
                } catch (bootstrapDiagErr) {}
                self.postMessage({ type: 'bootstrapDiagnostics', diagnostics: bootstrapDiagStr });
                break;
            }

            case 'getRuntimeDiagnostics': {
                self.postMessage({
                    type: 'runtimeDiagnostics',
                    diagnostics: {
                        tick: _tickNum,
                        params: Object.assign({}, _params),
                        network: {
                            queuedResults: _fetchQueue.length,
                            cachedResults: Object.keys(_fetchResultCache).length,
                            inflight: Object.keys(_fetchInflight).length
                        },
                        mus: _musRuntimeDiagnostics(),
                        lastTickTiming: Object.assign({}, _lastTickTiming),
                        isTicking: _isTicking
                    }
                });
                break;
            }

            default:
                break;
        }
    } catch (err) {
                self.postMessage({ type: 'error', msg: String(err) });
    }
};
