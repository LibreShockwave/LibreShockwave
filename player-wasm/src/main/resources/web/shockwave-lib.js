/**
 * LibreShockwave - Embeddable Shockwave/Director player for the web.
 *
 * Usage:
 *   <canvas id="stage" width="640" height="480"></canvas>
 *   <script src="shockwave-lib.js"></script>
 *   <script>
 *     var player = LibreShockwave.create("stage");
 *     player.load("http://example.com/movie.dcr");
 *   </script>
 *
 * Architecture: WASM runs entirely in a Web Worker (shockwave-worker.js).
 * The main thread owns only the canvas and the animation loop. All rendering,
 * WASM calls, and network I/O happen in the worker. Each tick, the worker
 * sends a pre-composited RGBA frame buffer which the main thread blits via
 * putImageData — no per-sprite drawing on the main thread.
 */
var LibreShockwave = (function() {

    // Fetch with timeout (prevents hanging requests on mobile)
    function _fetchWithTimeout(url, opts, timeoutMs) {
        timeoutMs = timeoutMs || 30000;
        var controller = new AbortController();
        var timer = setTimeout(function() { controller.abort(); }, timeoutMs);
        opts = opts || {};
        opts.signal = controller.signal;
        return fetch(url, opts).finally(function() { clearTimeout(timer); });
    }

    // Auto-detect base path from <script src="...shockwave-lib.js">
    var _autoBasePath = '';
    (function() {
        var scripts = document.getElementsByTagName('script');
        for (var i = scripts.length - 1; i >= 0; i--) {
            var src = scripts[i].src || '';
            if (src.indexOf('shockwave-lib.js') !== -1) {
                _autoBasePath = src.substring(0, src.lastIndexOf('/') + 1);
                break;
            }
        }
    })();

    // ========================================================================
    // Public API: ShockwavePlayer
    // ========================================================================

    /**
     * Create a player attached to a canvas element.
     *
     * @param {string|HTMLCanvasElement} canvas - Canvas element or its ID.
     * @param {Object} [options]
     * @param {string}  [options.basePath]  - Directory containing WASM files.
     * @param {Object}  [options.params]    - External parameters (e.g. { sw1: "..." }).
     * @param {boolean} [options.autoplay]  - Start playing after load (default: true).
     * @param {boolean} [options.remember]  - Persist params/URL in localStorage (default: false).
     * @param {Function} [options.onLoad]   - Called with { width, height, frameCount, tempo }.
     * @param {Function} [options.onError]  - Called with error message string.
     * @param {Function} [options.onFrame]  - Called with (frame, total) on each frame.
     * @returns {ShockwavePlayer}
     */
    function create(canvas, options) {
        return new ShockwavePlayer(canvas, options || {});
    }

    function ShockwavePlayer(canvas, opts) {
        var el = typeof canvas === 'string' ? document.getElementById(canvas) : canvas;
        if (!el) throw new Error('LibreShockwave: canvas "' + canvas + '" not found');

        this._opts        = opts;
        this._basePath    = opts.basePath || _autoBasePath;
        this._params      = opts.params ? _clone(opts.params) : {};
        this._autoplay    = opts.autoplay !== false;
        this._remember    = !!opts.remember;
        this._canvas      = el;
        this._ctx         = el.getContext('2d');
        this._animFrameId = null;
        this._lastFrameTime = 0;

        // Worker state
        this._worker      = null;
        this._workerReady = false;
        this._pendingUrl  = null;
        this._pendingFile = null;

        // Playback state (mirrors worker)
        this._playing     = false;
        this._lastTempo   = 15;
        this._tempoOverride = 0;
        this._lastFrame   = 0;
        this._lastFrameCount = 0;
        this._stageWidth  = 640;
        this._stageHeight = 480;
        this._blockedGotoNetPages = Object.create(null);
        this._loadedMovieUrl = null;

        // Cursor compositing state (decoupled from game tick for smooth movement)
        this._baseFrame      = null;  // last base frame ImageData (no cursor)
        this._cursorBitmap   = null;  // {rgba, w, h, regX, regY} or null
        this._mouseX         = 0;
        this._mouseY         = 0;
        this._cursorDirty    = true;  // needs re-composite
        this._cursorRafId    = null;  // rAF/timer handle for cursor loop
        this._cursorFps      = opts.cursorFps || 0; // 0 = use requestAnimationFrame (screen refresh)

        // Load deduplication: each load() increments this; stale loads bail out
        this._loadSeq     = 0;

        // Restore remembered params
        if (this._remember) {
            try {
                var saved = JSON.parse(localStorage.getItem('ls_extParams'));
                if (saved && typeof saved === 'object') {
                    for (var k in saved) {
                        if (!(k in this._params)) this._params[k] = saved[k];
                    }
                }
            } catch(e) {}
        }

        this._initAbout(el);
        this._initInput(el);
        this._initWorker();
    }

    // --- About dialog (built into lib so embedders cannot remove it) ---

    ShockwavePlayer.prototype._initAbout = function(canvas) {
        // --- Context menu ---
        var menu = document.createElement('div');
        menu.style.cssText = 'position:fixed;display:none;background:#fff;border:1px solid #999;padding:2px 0;z-index:10000;font-family:Verdana,Arial,Helvetica,sans-serif;font-size:11px;color:#333;min-width:160px;box-shadow:2px 2px 6px rgba(0,0,0,0.2);';

        function addMenuItem(label) {
            var item = document.createElement('div');
            item.textContent = label;
            item.style.cssText = 'padding:4px 12px;cursor:pointer;';
            item.addEventListener('mouseenter', function() { item.style.background = '#336699'; item.style.color = '#fff'; });
            item.addEventListener('mouseleave', function() { item.style.background = ''; item.style.color = '#333'; });
            menu.appendChild(item);
            return item;
        }

        var saveItem = addMenuItem('Save image as...');
        var sep = document.createElement('div');
        sep.style.cssText = 'border-top:1px solid #ccc;margin:2px 0;';
        menu.appendChild(sep);
        var aboutItem = addMenuItem('About LibreShockwave');
        document.body.appendChild(menu);

        function hideMenu() { menu.style.display = 'none'; }

        document.addEventListener('click', hideMenu);
        document.addEventListener('contextmenu', function(e) {
            if (e.target !== canvas) hideMenu();
        });

        canvas.addEventListener('contextmenu', function(e) {
            e.preventDefault();
            menu.style.left = e.clientX + 'px';
            menu.style.top = e.clientY + 'px';
            menu.style.display = 'block';
        });

        // --- About dialog ---
        var overlay = document.createElement('div');
        overlay.style.cssText = 'position:fixed;top:0;left:0;right:0;bottom:0;background:rgba(0,0,0,0.4);display:none;align-items:center;justify-content:center;z-index:9999;';

        var win = document.createElement('div');
        win.style.cssText = 'background:#fff;border:1px solid #999;width:360px;max-width:90%;font-family:Verdana,Arial,Helvetica,sans-serif;font-size:11px;color:#333;';

        var titlebar = document.createElement('div');
        titlebar.style.cssText = 'display:flex;justify-content:space-between;align-items:center;padding:4px 8px;background:#336699;color:#fff;font-weight:bold;font-size:11px;';
        titlebar.innerHTML = '<span>About LibreShockwave</span>';

        var closeBtn = document.createElement('button');
        closeBtn.textContent = 'X';
        closeBtn.style.cssText = 'background:#eee;border:1px solid #999;color:#333;font-size:10px;font-weight:bold;padding:0 5px;cursor:pointer;font-family:Verdana,Arial,Helvetica,sans-serif;line-height:16px;';
        closeBtn.addEventListener('click', function() { overlay.style.display = 'none'; });
        titlebar.appendChild(closeBtn);

        var body = document.createElement('div');
        body.style.cssText = 'text-align:center;padding:16px 20px;line-height:1.6;';
        body.innerHTML =
            '<div style="font-size:16px;font-weight:bold;color:#336699;margin-bottom:8px;">LibreShockwave</div>' +
            '<p style="margin-bottom:6px;">An open-source Macromedia Shockwave player,<br>bringing Director movies back to life in the browser.</p>' +
            '<p style="margin-bottom:10px;color:#666;">Made by <strong>Alexandra Miller-Blake</strong></p>' +
            '<p><a href="https://github.com/Quackster/LibreShockwave" target="_blank" rel="noopener" style="color:#003399;">View on GitHub</a></p>';

        win.appendChild(titlebar);
        win.appendChild(body);
        overlay.appendChild(win);
        document.body.appendChild(overlay);

        overlay.addEventListener('click', function(e) {
            if (e.target === overlay) overlay.style.display = 'none';
        });

        saveItem.addEventListener('click', function() {
            hideMenu();
            try {
                var link = document.createElement('a');
                link.download = 'screenshot.png';
                link.href = canvas.toDataURL('image/png');
                link.click();
            } catch(e) {
                console.error('[LS] Save image failed:', e);
            }
        });

        aboutItem.addEventListener('click', function() {
            hideMenu();
            overlay.style.display = 'flex';
        });
    };

    // --- Input event forwarding ---

    ShockwavePlayer.prototype._initInput = function(canvas) {
        var self = this;

        function getCanvasPoint(clientX, clientY) {
            var r = canvas.getBoundingClientRect();
            var scaleX = r.width > 0 ? (canvas.width / r.width) : 1;
            var scaleY = r.height > 0 ? (canvas.height / r.height) : 1;
            return {
                x: Math.round((clientX - r.left) * scaleX),
                y: Math.round((clientY - r.top) * scaleY)
            };
        }

        // Make canvas focusable for keyboard events
        if (!canvas.hasAttribute('tabindex')) {
            canvas.setAttribute('tabindex', '0');
        }
        canvas.style.outline = 'none'; // Hide focus outline

        var ua = navigator.userAgent || '';
        var isLikelyMobile = !!((navigator.userAgentData && navigator.userAgentData.mobile) ||
            /Android|iPhone|iPad|iPod|Mobile|IEMobile|Opera Mini/i.test(ua));
        var mobileKeyboardInput = null;
        var lastMobileKeyDownTime = 0;
        if (isLikelyMobile && document.body) {
            // A tiny hidden textarea keeps mobile virtual keyboard behavior reliable.
            mobileKeyboardInput = document.createElement('textarea');
            mobileKeyboardInput.setAttribute('autocapitalize', 'off');
            mobileKeyboardInput.setAttribute('autocomplete', 'off');
            mobileKeyboardInput.setAttribute('autocorrect', 'off');
            mobileKeyboardInput.setAttribute('spellcheck', 'false');
            mobileKeyboardInput.setAttribute('inputmode', 'text');
            mobileKeyboardInput.style.cssText = 'position:fixed;left:0;top:0;width:1px;height:1px;opacity:0;pointer-events:none;z-index:-1;padding:0;border:0;resize:none;';
            document.body.appendChild(mobileKeyboardInput);
        }
        self._mobileKeyboardInput = mobileKeyboardInput;

        function hasPlayerFocus() {
            return document.activeElement === canvas ||
                (!!mobileKeyboardInput && document.activeElement === mobileKeyboardInput);
        }

        function focusKeyboardTarget() {
            self._canvasFocused = true;
            if (mobileKeyboardInput) {
                try {
                    mobileKeyboardInput.focus({ preventScroll: true });
                } catch (e) {
                    mobileKeyboardInput.focus();
                }
                mobileKeyboardInput.value = '';
                try { mobileKeyboardInput.setSelectionRange(0, 0); } catch (e2) {}
                return;
            }
            canvas.focus();
        }

        function toKeyChar(rawKey) {
            var keyChar = rawKey;
            if (keyChar === 'Enter') keyChar = '\r';
            else if (keyChar === 'Tab') keyChar = '\t';
            else if (!keyChar || keyChar.length !== 1) keyChar = '';
            return keyChar;
        }

        function toModifiers(e) {
            return (e.shiftKey ? 1 : 0) | (e.ctrlKey ? 2 : 0) | (e.altKey ? 4 : 0);
        }

        function handleKeyDown(e) {
            if (!self._canvasFocused) return;
            if (!self._worker || !self._workerReady) return;
            if (mobileKeyboardInput && e.target === mobileKeyboardInput) {
                lastMobileKeyDownTime = Date.now();
            }
            // Handle Ctrl/Cmd shortcuts selectively
            if (e.ctrlKey || e.metaKey) {
                if (e.key === 'v' || e.key === 'V') return; // Let browser fire paste event
                if (e.key === 'c' || e.key === 'C') {
                    e.preventDefault();
                    self._worker.postMessage({ type: 'getSelectedText' });
                    return;
                }
                if (e.key === 'x' || e.key === 'X') {
                    e.preventDefault();
                    self._worker.postMessage({ type: 'cutSelectedText' });
                    return;
                }
                if (e.key === 'a' || e.key === 'A') {
                    e.preventDefault();
                    self._worker.postMessage({ type: 'selectAll' });
                    return;
                }
                return; // Block other Ctrl shortcuts from reaching player
            }
            e.preventDefault();
            self._worker.postMessage({
                type: 'keyDown',
                keyCode: e.keyCode,
                key: toKeyChar(e.key),
                modifiers: toModifiers(e)
            });
        }

        function handleKeyUp(e) {
            if (!self._canvasFocused) return;
            if (!self._worker || !self._workerReady) return;
            if (e.ctrlKey || e.metaKey) return;
            e.preventDefault();
            self._worker.postMessage({
                type: 'keyUp',
                keyCode: e.keyCode,
                key: toKeyChar(e.key),
                modifiers: toModifiers(e)
            });
        }

        // Track whether the canvas/input bridge has focus - suppress input when it doesn't
        self._canvasFocused = hasPlayerFocus();
        function notifyBlurRelease(forceRelease) {
            if (!forceRelease && hasPlayerFocus()) {
                self._canvasFocused = true;
                return;
            }
            self._canvasFocused = false;
            if (!self._worker || !self._workerReady) return;
            self._worker.postMessage({ type: 'blur' });
        }
        canvas.addEventListener('focus', function() { self._canvasFocused = true; });
        canvas.addEventListener('blur', function() { notifyBlurRelease(false); });
        if (mobileKeyboardInput) {
            mobileKeyboardInput.addEventListener('focus', function() { self._canvasFocused = true; });
            mobileKeyboardInput.addEventListener('blur', function() { notifyBlurRelease(false); });
            mobileKeyboardInput.addEventListener('keydown', handleKeyDown);
            mobileKeyboardInput.addEventListener('keyup', handleKeyUp);
            mobileKeyboardInput.addEventListener('input', function(e) {
                if (!self._canvasFocused) {
                    mobileKeyboardInput.value = '';
                    return;
                }
                if (!self._worker || !self._workerReady) {
                    mobileKeyboardInput.value = '';
                    return;
                }
                // Some mobile keyboards emit input without key events.
                if (Date.now() - lastMobileKeyDownTime > 30) {
                    var inputType = e.inputType || '';
                    var inserted = e.data || '';
                    if (inputType === 'deleteContentBackward') {
                        self._worker.postMessage({ type: 'keyDown', keyCode: 8, key: '', modifiers: 0 });
                        self._worker.postMessage({ type: 'keyUp', keyCode: 8, key: '', modifiers: 0 });
                    } else if (inserted) {
                        for (var i = 0; i < inserted.length; i++) {
                            var ch = inserted.charAt(i);
                            var code = ch.charCodeAt(0);
                            self._worker.postMessage({ type: 'keyDown', keyCode: code, key: ch, modifiers: 0 });
                            self._worker.postMessage({ type: 'keyUp', keyCode: code, key: ch, modifiers: 0 });
                        }
                    }
                }
                mobileKeyboardInput.value = '';
            });
        }
        window.addEventListener('blur', function() { notifyBlurRelease(true); });
        document.addEventListener('visibilitychange', function() {
            if (document.hidden) notifyBlurRelease(true);
        });

        canvas.addEventListener('mousemove', function(e) {
            var pt = getCanvasPoint(e.clientX, e.clientY);
            var x = pt.x;
            var y = pt.y;
            self._mouseX = x;
            self._mouseY = y;
            self._cursorDirty = true;
            if (!self._canvasFocused) return;
            if (!self._worker || !self._workerReady) return;
            self._worker.postMessage({ type: 'mouseMove', x: x, y: y });
        });

        canvas.addEventListener('mousedown', function(e) {
            if (!self._worker || !self._workerReady) return;
            // Left-click only (right-click handled by context menu)
            if (e.button !== 0 && e.button !== 2) return;
            focusKeyboardTarget();
            var pt = getCanvasPoint(e.clientX, e.clientY);
            var x = pt.x;
            var y = pt.y;
            self._worker.postMessage({ type: 'mouseDown', x: x, y: y, button: e.button });
        });

        canvas.addEventListener('mouseup', function(e) {
            if (!self._canvasFocused) return;
            if (!self._worker || !self._workerReady) return;
            if (e.button !== 0 && e.button !== 2) return;
            var pt = getCanvasPoint(e.clientX, e.clientY);
            var x = pt.x;
            var y = pt.y;
            self._worker.postMessage({ type: 'mouseUp', x: x, y: y, button: e.button });
        });

        canvas.addEventListener('dblclick', function(e) {
            if (e.button !== 0) return;
            focusKeyboardTarget();
            if (!self._worker || !self._workerReady) return;
            self._worker.postMessage({ type: 'selectAll' });
        });

        // --- Touch event support (mobile browsers) ---
        // Convert touch events to mouse events so the Director player works on
        // mobile Chrome/Safari without any changes to the engine.
        canvas.addEventListener('pointerdown', function(e) {
            if (e.pointerType === 'touch') {
                focusKeyboardTarget();
            }
        });

        canvas.addEventListener('touchstart', function(e) {
            focusKeyboardTarget();
            e.preventDefault();
            var touch = e.changedTouches[0];
            var pt = getCanvasPoint(touch.clientX, touch.clientY);
            var x = pt.x;
            var y = pt.y;
            self._mouseX = x;
            self._mouseY = y;
            self._cursorDirty = true;
            if (!self._worker || !self._workerReady) return;
            self._worker.postMessage({ type: 'mouseMove', x: x, y: y });
            self._worker.postMessage({ type: 'mouseDown', x: x, y: y, button: 0 });
        }, { passive: false });

        canvas.addEventListener('touchmove', function(e) {
            if (!self._canvasFocused) return;
            e.preventDefault();
            var touch = e.changedTouches[0];
            var pt = getCanvasPoint(touch.clientX, touch.clientY);
            var x = pt.x;
            var y = pt.y;
            self._mouseX = x;
            self._mouseY = y;
            self._cursorDirty = true;
            if (!self._worker || !self._workerReady) return;
            self._worker.postMessage({ type: 'mouseMove', x: x, y: y });
        }, { passive: false });

        canvas.addEventListener('touchend', function(e) {
            if (!self._canvasFocused) return;
            e.preventDefault();
            var touch = e.changedTouches[0];
            var pt = getCanvasPoint(touch.clientX, touch.clientY);
            var x = pt.x;
            var y = pt.y;
            if (!self._worker || !self._workerReady) return;
            self._worker.postMessage({ type: 'mouseUp', x: x, y: y, button: 0 });
        }, { passive: false });

        canvas.addEventListener('touchcancel', function(e) {
            if (!self._canvasFocused) return;
            var touch = e.changedTouches[0];
            if (!touch) return;
            var pt = getCanvasPoint(touch.clientX, touch.clientY);
            var x = pt.x;
            var y = pt.y;
            if (!self._worker || !self._workerReady) return;
            self._worker.postMessage({ type: 'mouseUp', x: x, y: y, button: 0 });
        });

        canvas.addEventListener('keydown', handleKeyDown);
        canvas.addEventListener('keyup', handleKeyUp);

        // Clipboard paste support
        document.addEventListener('paste', function(e) {
            if (!self._canvasFocused) return;
            var text = (e.clipboardData || window.clipboardData).getData('text');
            if (text && self._worker && self._workerReady) {
                self._worker.postMessage({ type: 'paste', text: text });
                e.preventDefault();
            }
        });
    };

    // --- Worker lifecycle ---

    ShockwavePlayer.prototype._initWorker = function() {
        var self = this;
        // Make the base path absolute so importScripts() in the worker resolves it correctly
        var absBase = new URL(this._basePath, document.baseURI).href;

        function setupWorker(worker) {
            self._worker = worker;
            worker.onmessage = function(e) { self._onWorkerMessage(e.data); };
            worker.onerror   = function(e) {
                console.error('[LS] Worker error:', e.message);
                if (self._opts.onError) self._opts.onError(e.message);
            };
            // Send init with absolute base path so importScripts/fetch work from the worker
            worker.postMessage({ type: 'init', basePath: absBase, pageProtocol: location.protocol });
        }

        // Create worker from file URL (most reliable, works on all mobile browsers).
        // Bundled deployments override this to try blob URL first with file URL fallback.
        setupWorker(new Worker(absBase + 'shockwave-worker.js'));
    };

    ShockwavePlayer.prototype._onWorkerMessage = function(msg) {
        switch (msg.type) {

            case 'ready':
                console.log('[LS] Worker ready');
                this._workerReady = true;
                if (this._resetResolve) {
                    var resolve = this._resetResolve;
                    this._resetResolve = null;
                    resolve();
                }
                if (this._pendingUrl)  { this.load(this._pendingUrl);  this._pendingUrl  = null; }
                if (this._pendingFile) { this.loadFile(this._pendingFile); this._pendingFile = null; }
                break;

            case 'movieLoaded':
                this._resolveOnce('movieLoaded', msg.info);
                break;

            case 'castsDone':
                this._resolveOnce('castsDone', null);
                break;

            case 'frame':
                this._resolveOnce('frame', msg);
                break;

            case 'audio':
                this._handleAudio(msg);
                break;

            case 'debugLog':
                console.log(msg.msg);
                break;

            case 'selectedText':
            case 'cutText':
                if (msg.text) navigator.clipboard.writeText(msg.text).catch(function(){});
                break;

            case 'gotoNetPage':
                this._handleGotoNetPage(msg.url, msg.target);
                break;

            case 'fetchRelay': {
                // Worker needs a cross-origin fetch; do it from main thread and relay back
                var worker = this._worker;
                var relayId = msg.relayId;
                var relayUrl = msg.url;

                // Check relay cache first (with and without query string for cache-busted URLs)
                var baseUrl = relayUrl.indexOf('?') >= 0 ? relayUrl.substring(0, relayUrl.indexOf('?')) : relayUrl;
                var relayBuf = this._relayCache && (this._relayCache[relayUrl] || this._relayCache[baseUrl]);
                if (relayBuf) {
                    var copy = relayBuf.slice(0); // copy so original stays reusable
                    worker.postMessage({ type: 'fetchRelayResult', relayId: relayId, data: copy }, [copy]);
                    break;
                }

                var opts = {};
                if (msg.method === 'POST') {
                    opts.method  = 'POST';
                    opts.body    = msg.postData;
                    opts.headers = { 'Content-Type': 'application/x-www-form-urlencoded' };
                }
                _fetchWithTimeout(relayUrl, opts)
                    .then(function(r) { if (!r.ok) throw { status: r.status }; return r.arrayBuffer(); })
                    .then(function(buf) {
                        worker.postMessage({ type: 'fetchRelayResult', relayId: relayId, data: buf }, [buf]);
                    })
                    .catch(function(e) {
                        worker.postMessage({ type: 'fetchRelayResult', relayId: relayId,
                                             error: true, status: (e && e.status) || 0 });
                    });
                break;
            }

            case 'error':
                console.error('[LS] Worker reported:', msg.msg);
                if (this._opts.onError) this._opts.onError(msg.msg);
                break;

            default:
                break;
        }
    };

    ShockwavePlayer.prototype._handleGotoNetPage = function(url, target) {
        if (!url) return;

        var rawTarget = (target == null ? '' : String(target));
        var normalizedTarget = this._normalizeGotoNetPageTarget(rawTarget);
        var destinationLabel = normalizedTarget === '_blank' ? 'a new page' : 'this page';
        var requestKey = normalizedTarget + '\n' + String(url);

        if (this._blockedGotoNetPages[requestKey]) {
            console.warn('[LS] gotoNetPage suppressed after previous cancel:', {
                url: String(url),
                rawTarget: rawTarget,
                normalizedTarget: normalizedTarget
            });
            return;
        }

        console.error('[LS] gotoNetPage request:', {
            url: String(url),
            rawTarget: rawTarget,
            normalizedTarget: normalizedTarget,
            frame: this._lastFrame,
            frameCount: this._lastFrameCount
        });

        if (this._maybeHandleMovieResetNavigation(url, normalizedTarget)) {
            return;
        }

        if (typeof this._opts.onGotoNetPage === 'function') {
            this._opts.onGotoNetPage(url, normalizedTarget);
            return;
        }

        try {
            var approved = window.confirm(
                'Director wants to open:\n' + url + '\n\nDestination: ' + destinationLabel + '\n\nPress OK to continue.'
            );
            if (!approved) {
                this._blockedGotoNetPages[requestKey] = true;
                console.warn('[LS] gotoNetPage cancelled:', {
                    url: String(url),
                    normalizedTarget: normalizedTarget
                });
                return;
            }

            if (normalizedTarget !== '_self') {
                var opened = window.open(url, normalizedTarget, 'noopener');
                if (!opened) {
                    window.location.assign(url);
                }
                return;
            }

            window.location.assign(url);
        } catch (e) {
            console.error('[LS] gotoNetPage failed:', e);
        }
    };

    ShockwavePlayer.prototype._normalizeGotoNetPageTarget = function(target) {
        var raw = (target == null ? '' : String(target)).trim();
        if (!raw) return '_self';

        var lowered = raw.toLowerCase();
        if (lowered === 'self' || lowered === '_self') return '_self';
        if (lowered === 'new' || lowered === '_new' || lowered === 'blank' || lowered === '_blank') {
            return '_blank';
        }
        if (lowered === 'parent' || lowered === '_parent') return '_parent';
        if (lowered === 'top' || lowered === '_top') return '_top';
        return raw;
    };

    ShockwavePlayer.prototype._maybeHandleMovieResetNavigation = function(url, normalizedTarget) {
        if (normalizedTarget !== '_self' || !this._loadedMovieUrl) {
            return false;
        }

        try {
            var requestedUrl = new URL(String(url), this._loadedMovieUrl);
            var loadedMovieUrl = new URL(this._loadedMovieUrl, window.location.href);
            var currentPageUrl = new URL(window.location.href);

            if (currentPageUrl.origin === loadedMovieUrl.origin) {
                return false;
            }
            if (requestedUrl.origin !== loadedMovieUrl.origin) {
                return false;
            }

            console.warn('[LS] gotoNetPage treated as movie reset:', {
                url: requestedUrl.href,
                movieUrl: loadedMovieUrl.href
            });
            this.load(this._loadedMovieUrl);
            return true;
        } catch (e) {
            return false;
        }
    };

    // Simple one-shot resolver map: type → resolve function
    ShockwavePlayer.prototype._resolveOnce = function(type, value) {
        if (this._pending && this._pending.type === type) {
            var resolve = this._pending.resolve;
            this._pending = null;
            resolve(value);
        }
    };

    /**
     * Handle audio commands from the worker (Web Audio API playback).
     * Lazy-initializes AudioContext on first use (requires user gesture on most browsers).
     */
    ShockwavePlayer.prototype._handleAudio = function(msg) {
        if (!this._audioCtx) {
            try {
                this._audioCtx = new (window.AudioContext || window.webkitAudioContext)();
                this._audioChannels = {}; // channelNum -> { source, gain }
            } catch(e) {
                return;
            }
        }
        var ctx = this._audioCtx;
        var ch = msg.channel;

        if (msg.action === 'stop') {
            if (this._audioChannels[ch]) {
                try { this._audioChannels[ch].source.stop(); } catch(e) {}
                this._audioChannels[ch] = null;
            }
            return;
        }

        if (msg.action === 'volume') {
            if (this._audioChannels[ch] && this._audioChannels[ch].gain) {
                this._audioChannels[ch].gain.gain.value = (msg.volume || 0) / 255.0;
            }
            return;
        }

        if (msg.action === 'play' && msg.data) {
            // Stop existing sound on this channel
            if (this._audioChannels[ch]) {
                try { this._audioChannels[ch].source.stop(); } catch(e) {}
            }

            var self = this;
            var worker = this._worker;
            var audioData = msg.data; // ArrayBuffer
            var loopCount = msg.loopCount || 1;
            var volume = (msg.volume || 255) / 255.0;

            ctx.decodeAudioData(audioData).then(function(buffer) {
                var source = ctx.createBufferSource();
                source.buffer = buffer;
                source.loop = (loopCount === 0);
                if (loopCount > 1) {
                    // Web Audio doesn't support finite loop counts directly
                    // We'll just set loop=true and stop after duration * loopCount
                    source.loop = true;
                    var dur = buffer.duration * loopCount * 1000;
                    setTimeout(function() {
                        try { source.stop(); } catch(e) {}
                        self._audioChannels[ch] = null;
                        if (worker) worker.postMessage({ type: 'audioStopped', channel: ch });
                    }, dur);
                }

                var gainNode = ctx.createGain();
                gainNode.gain.value = volume;
                source.connect(gainNode);
                gainNode.connect(ctx.destination);

                source.onended = function() {
                    self._audioChannels[ch] = null;
                    if (worker) worker.postMessage({ type: 'audioStopped', channel: ch });
                };

                source.start();
                self._audioChannels[ch] = { source: source, gain: gainNode };
            }).catch(function(err) {
                // Decoding failed — silently ignore
            });
        }
    };

    ShockwavePlayer.prototype._waitFor = function(type, timeoutMs) {
        var self = this;
        timeoutMs = timeoutMs || 60000; // 60s safety default (mobile networks need more time)
        return new Promise(function(resolve) {
            var resolved = false;
            var pendingId = {}; // unique reference for this pending
            var timerId = setTimeout(function() {
                if (!resolved && self._pending && self._pending._id === pendingId) {
                    console.warn('[LS] _waitFor timeout for: ' + type);
                    self._pending = null;
                    resolved = true;
                    resolve(null);
                }
            }, timeoutMs);
            self._pending = {
                type: type,
                _id: pendingId,
                resolve: function(val) {
                    if (resolved) return;
                    resolved = true;
                    clearTimeout(timerId);
                    resolve(val);
                }
            };
        });
    };

    // --- Movie loading ---

    ShockwavePlayer.prototype.load = function(url) {
        console.log('[LS] load(' + url + '), ready=' + this._workerReady);
        if (!this._workerReady) { this._pendingUrl = url; return; }
        var self = this;
        var seq = ++this._loadSeq; // Deduplicate: only the latest load proceeds
        if (this._remember) {
            try { localStorage.setItem('ls_urlInput', url); } catch(e) {}
        }
        _fetchWithTimeout(url)
            .then(function(r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.arrayBuffer(); })
            .then(function(buf) {
                if (self._loadSeq !== seq) {
                    console.log('[LS] Ignoring superseded load #' + seq + ' (current=' + self._loadSeq + ')');
                    return;
                }
                console.log('[LS] Movie fetched, ' + buf.byteLength + ' bytes');
                self._loadMovieBuffer(buf, url);
            })
            .catch(function(e) {
                console.error('[LS] load error:', e);
                if (self._opts.onError) self._opts.onError(e.message);
            });
    };

    ShockwavePlayer.prototype.loadFile = function(file) {
        if (!this._workerReady) { this._pendingFile = file; return; }
        var self = this;
        var seq = ++this._loadSeq;
        var reader = new FileReader();
        reader.onload = function() {
            if (self._loadSeq !== seq) return;
            self._loadMovieBuffer(reader.result, file.name);
        };
        reader.readAsArrayBuffer(file);
    };

    ShockwavePlayer.prototype._loadMovieBuffer = async function(buf, basePath) {
        // Cancel any active loops and pending resolvers from a previous/concurrent load
        this._stopLoop();
        this._playing = false;
        this._pending = null;
        this._lastSpriteCount = 0; // Reset so fast loop doesn't skip immediately
        this._loadStartTime = performance.now();
        this._loadedMovieUrl = (typeof basePath === 'string' && basePath.indexOf('://') !== -1)
            ? basePath
            : null;
        var mySeq = this._loadSeq;
        // Send movie bytes to worker (transfer ownership — zero copy)
        this._worker.postMessage({ type: 'loadMovie', data: buf, basePath: basePath },
                                 [buf]);
        var info = await this._waitFor('movieLoaded');
        if (this._loadSeq !== mySeq) return; // Superseded by newer load
        if (!info) {
            console.error('[LS] loadMovie returned null');
            if (this._opts.onError) this._opts.onError('Failed to load movie');
            return;
        }
        console.log('[LS] Movie loaded:', info.width + 'x' + info.height +
                    ', ' + info.frameCount + ' frames');
        this._stageWidth  = info.width;
        this._stageHeight = info.height;
        this._lastTempo   = info.tempo;
        this._canvas.width  = info.width;
        this._canvas.height = info.height;
        await this._onMovieLoaded(info);
    };

    ShockwavePlayer.prototype._onMovieLoaded = async function(info) {
        var mySeq = this._loadSeq;
        // Push external params into the worker
        this._worker.postMessage({ type: 'clearParams' });
        for (var k in this._params) {
            this._worker.postMessage({ type: 'setParam', key: k, value: this._params[k] });
        }

        // Send debug playback toggle to worker
        var dbg = this._opts.debugPlayback !== undefined
            ? !!this._opts.debugPlayback
            : !!this._opts.onDebugLog;
        this._worker.postMessage({ type: 'setDebugPlayback', enabled: dbg });

        // Restore trace handlers after movie load
        if (this._traceHandlers && this._traceHandlers.length > 0) {
            for (var i = 0; i < this._traceHandlers.length; i++) {
                this._worker.postMessage({ type: 'addTraceHandler', name: this._traceHandlers[i] });
            }
        }

        if (this._opts.onLoad) this._opts.onLoad(info);
        if (this._tempoOverride > 0) {
            this.setTempo(this._tempoOverride);
        }

        // Preload external casts before starting; worker handles the network pump
        this._worker.postMessage({ type: 'preloadCasts' });
        await this._waitFor('castsDone');
        if (this._loadSeq !== mySeq) return; // Superseded by newer load

        // Pre-fetch sw1 URLs from main thread into relay cache.
        // The worker cannot reliably fetch cross-origin URLs (Chrome hang bug),
        // so we do it here and serve from cache when the relay arrives.
        this._relayCache = {};
        await this._prefetchRelayCache();
        if (this._loadSeq !== mySeq) return; // Superseded by newer load

        console.log('[LS] Ready to play after ' +
                    Math.round(performance.now() - this._loadStartTime) + 'ms');
        if (this._autoplay) this.play();
    };

    // Parse all sw1-sw9 params for URLs (key=value;key=value format)
    function _parseSwUrls(params) {
        var urls = [];
        for (var i = 1; i <= 9; i++) {
            var sw = params['sw' + i] || params['SW' + i] || '';
            if (!sw) continue;
            sw.split(';').forEach(function(pair) {
                pair = pair.trim();
                var eq = pair.indexOf('=');
                if (eq < 0) return;
                var val = pair.substring(eq + 1).trim();
                if (val.indexOf('://') !== -1) urls.push(val);
            });
        }
        return urls;
    }

    ShockwavePlayer.prototype._prefetchRelayCache = async function() {
        var urls = _parseSwUrls(this._params);
        if (urls.length === 0) return;
        console.log('[LS] Pre-fetching ' + urls.length + ' sw URLs for relay cache');
        var self = this;
        await Promise.all(urls.map(function(url) {
            return _fetchWithTimeout(url)
                .then(function(r) { if (!r.ok) throw new Error('HTTP ' + r.status); return r.arrayBuffer(); })
                .then(function(buf) {
                    self._relayCache[url] = buf;
                    console.log('[LS] Pre-fetched: ' + url + ' (' + buf.byteLength + ' bytes)');
                })
                .catch(function(e) {
                    console.warn('[LS] Pre-fetch failed: ' + url + ' — ' + e.message);
                });
        }));
    };

    // --- Playback control ---

    ShockwavePlayer.prototype.play = function() {
        this._worker.postMessage({ type: 'play' });
        this._playing = true;
        this._lastFrameTime = 0;
        this._startLoop();
    };

    ShockwavePlayer.prototype.pause = function() {
        this._worker.postMessage({ type: 'pause' });
        this._playing = false;
        this._stopLoop();
        this._stopCursorLoop();
    };

    ShockwavePlayer.prototype.stop = function() {
        this._worker.postMessage({ type: 'stop' });
        this._playing = false;
        this._stopLoop();
        this._stopCursorLoop();
    };

    ShockwavePlayer.prototype.goToFrame = function(f) {
        this._worker.postMessage({ type: 'goToFrame', frame: f });
    };

    ShockwavePlayer.prototype.stepForward = function() {
        this._worker.postMessage({ type: 'stepForward' });
    };

    ShockwavePlayer.prototype.stepBackward = function() {
        this._worker.postMessage({ type: 'stepBackward' });
    };

    ShockwavePlayer.prototype.getCurrentFrame = function() {
        return this._lastFrame;
    };

    ShockwavePlayer.prototype.getFrameCount = function() {
        return this._lastFrameCount;
    };

    ShockwavePlayer.prototype.setParam = function(key, value) {
        this._params[key] = value;
        if (this._worker && this._workerReady) {
            this._worker.postMessage({ type: 'setParam', key: key, value: value });
        }
        if (this._remember) {
            try { localStorage.setItem('ls_extParams', JSON.stringify(this._params)); } catch(e) {}
        }
    };

    ShockwavePlayer.prototype.setParams = function(obj) {
        for (var k in obj) this.setParam(k, obj[k]);
    };

    ShockwavePlayer.prototype.setDebugPlayback = function(enabled) {
        if (this._worker && this._workerReady) {
            this._worker.postMessage({ type: 'setDebugPlayback', enabled: enabled });
        }
    };

    ShockwavePlayer.prototype.addTraceHandler = function(name) {
        if (!this._traceHandlers) this._traceHandlers = [];
        name = name.toLowerCase();
        if (this._traceHandlers.indexOf(name) === -1) this._traceHandlers.push(name);
        if (this._worker && this._workerReady) {
            this._worker.postMessage({ type: 'addTraceHandler', name: name });
        }
    };

    ShockwavePlayer.prototype.removeTraceHandler = function(name) {
        if (!this._traceHandlers) this._traceHandlers = [];
        name = name.toLowerCase();
        var idx = this._traceHandlers.indexOf(name);
        if (idx !== -1) this._traceHandlers.splice(idx, 1);
        if (this._worker && this._workerReady) {
            this._worker.postMessage({ type: 'removeTraceHandler', name: name });
        }
    };

    ShockwavePlayer.prototype.clearTraceHandlers = function() {
        this._traceHandlers = [];
        if (this._worker && this._workerReady) {
            this._worker.postMessage({ type: 'clearTraceHandlers' });
        }
    };

    /**
     * Fully reset the player: terminate the worker, clear all state,
     * and spin up a fresh WASM instance. Returns a Promise that resolves
     * when the new worker is ready to accept loadMovie.
     */
    ShockwavePlayer.prototype.reset = function() {
        var self = this;
        this._stopLoop();
        this._stopCursorLoop();
        this._playing = false;
        this._pending = null;
        this._pendingUrl = null;
        this._pendingFile = null;
        this._relayCache = {};
        this._lastSpriteCount = 0;
        this._lastFrame = 0;
        this._lastFrameCount = 0;
        ++this._loadSeq; // Invalidate any in-flight loads
        if (this._worker) { this._worker.terminate(); this._worker = null; }
        this._workerReady = false;

        // Clear the canvas
        this._ctx.clearRect(0, 0, this._canvas.width, this._canvas.height);

        return new Promise(function(resolve) {
            self._resetResolve = resolve;
            self._initWorker();
        });
    };

    /**
     * Clear all persistent player cache data from localStorage and
     * reset the internal engine state.
     */
    ShockwavePlayer.prototype.clearCache = function() {
        try {
            localStorage.removeItem('ls_extParams');
            localStorage.removeItem('ls_debugPlayback');
            localStorage.removeItem('ls_traceHandlers');
            localStorage.removeItem('ls_paramPresets');
        } catch (e) {}
        this._params = {};
        return this.reset();
    };

    /**
     * Get the current Lingo call stack. Returns a Promise that resolves with
     * the call stack string (empty string when no handlers are executing).
     */
    ShockwavePlayer.prototype.getCallStack = function() {
        if (!this._worker || !this._workerReady) return Promise.resolve('');
        var self = this;
        return new Promise(function(resolve) {
            var handler = function(e) {
                if (e.data && e.data.type === 'callStack') {
                    self._worker.removeEventListener('message', handler);
                    resolve(e.data.callStack || '');
                }
            };
            self._worker.addEventListener('message', handler);
            self._worker.postMessage({ type: 'getCallStack' });
        });
    };

    /**
     * Trigger a test Lingo error to exercise the movie's alertHook error dialog.
     * Call this after the movie is playing to verify error dialog appearance.
     */
    ShockwavePlayer.prototype.triggerTestError = function() {
        if (this._worker && this._workerReady) {
            this._worker.postMessage({ type: 'triggerTestError' });
        }
    };

    ShockwavePlayer.prototype.setTempo = function(tempo) {
        var nextTempo = parseInt(tempo, 10);
        if (!(nextTempo >= 0)) return;
        this._tempoOverride = nextTempo;
        if (nextTempo > 0) {
            this._lastTempo = nextTempo;
        }
        if (this._worker && this._workerReady) {
            this._worker.postMessage({ type: 'setTempo', tempo: nextTempo });
        }
    };

    ShockwavePlayer.prototype.destroy = function() {
        this._stopLoop();
        this._stopCursorLoop();
        if (this._mobileKeyboardInput && this._mobileKeyboardInput.parentNode) {
            this._mobileKeyboardInput.parentNode.removeChild(this._mobileKeyboardInput);
            this._mobileKeyboardInput = null;
        }
        if (this._worker) { this._worker.terminate(); this._worker = null; }
    };

    // --- Animation loop ---

    ShockwavePlayer.prototype._startLoop = function() {
        this._stopLoop();
        this._loadingPhase = false;
        this._tickCount = 0;
        this._lastRenderTime = 0;
        this._loopSeq = this._loadSeq;
        this._startNormalLoop();
    };

    ShockwavePlayer.prototype._startNormalLoop = function() {
        var self = this;
        var ticking = false;
        function loop() {
            if (!self._playing) return;
            var tempo = self._lastTempo || 15;
            var ms = 1000.0 / (tempo > 0 ? tempo : 15);
            if (!ticking) {
                var tickStart = performance.now();
                ticking = true;
                self._doTick().then(function() {
                    ticking = false;
                    if (self._playing) {
                        // Compensate only for the work done in this tick.
                        var elapsed = performance.now() - tickStart;
                        var delay = Math.max(0, ms - elapsed);
                        self._normalTimerId = setTimeout(loop, delay);
                    }
                }).catch(function(err) {
                    ticking = false;
                    console.error('[LS] tick error:', err);
                    if (self._playing) {
                        self._normalTimerId = setTimeout(loop, ms);
                    }
                });
            }
        }
        this._normalTimerId = setTimeout(loop, 0);
    };

    ShockwavePlayer.prototype._stopLoop = function() {
        if (this._animFrameId) {
            cancelAnimationFrame(this._animFrameId);
            this._animFrameId = null;
        }
        if (this._fastTimerId) {
            clearTimeout(this._fastTimerId);
            this._fastTimerId = null;
        }
        if (this._normalTimerId) {
            clearTimeout(this._normalTimerId);
            this._normalTimerId = null;
        }
    };

    // --- Tick: send work to the worker, await the frame response, then render ---

    ShockwavePlayer.prototype._doTick = async function() {
        // Always render — the fast-loading loop completes in few ticks so
        // skipping frames would hide the loading screen (Sulake logo).
        var skipRender = false;
        this._worker.postMessage({ type: 'tick', skipRender: skipRender });
        var result = await this._waitFor('frame');
        if (!result) return;

        // Update local state from worker response
        this._lastTempo       = result.tempo       || this._lastTempo;
        this._lastFrame       = result.lastFrame   || this._lastFrame;
        this._lastFrameCount  = result.frameCount  || this._lastFrameCount;
        this._lastSpriteCount = result.spriteCount || 0;

        // Cache the base frame (no cursor) for 60fps cursor compositing
        if (result.rgba && result.width > 0 && result.height > 0) {
            this._baseFrame = new ImageData(result.rgba, result.width, result.height);
            this._cursorDirty = true;
        }

        // Cache cursor bitmap data from worker; start/stop cursor loop as needed
        var hadCursor = !!this._cursorBitmap;
        this._cursorBitmap = result.cursorBitmap || null;
        if (this._cursorBitmap && !hadCursor) {
            this._startCursorLoop();
        } else if (!this._cursorBitmap && hadCursor) {
            this._stopCursorLoop();
        }

        // Cache text caret and selection info from worker
        this._caretInfo = result.caretInfo || null;
        this._selectionRects = result.selectionRects || null;
        this._cursorDirty = true;

        // Debug overlay during loading phase (helps diagnose mobile issues without DevTools)
        if (this._loadingPhase && this._opts.debugOverlay) {
            // Blit base frame first for debug overlay
            if (this._baseFrame) this._ctx.putImageData(this._baseFrame, 0, 0);
            var ctx = this._ctx;
            var text = 'tick:' + (this._tickCount || 0) +
                       ' sprites:' + this._lastSpriteCount +
                       ' frame:' + this._lastFrame;
            ctx.save();
            ctx.font = '11px monospace';
            ctx.fillStyle = 'rgba(0,0,0,0.5)';
            ctx.fillRect(0, 0, ctx.measureText(text).width + 8, 16);
            ctx.fillStyle = '#0f0';
            ctx.fillText(text, 4, 12);
            ctx.restore();
        } else {
            // Composite and blit immediately (rAF loop also runs for mouse moves between ticks)
            this._compositeCursorAndBlit();
        }

        // Update cursor based on what sprite the mouse is over
        // Director cursor codes: -1=arrow, 0=default, 1=ibeam, 2=crosshair, 3=crossbar, 4=wait, 5=bitmap (rendered in frame), 6=pointer (button)
        var cursorMap = { '-1': 'default', '0': 'default', '1': 'text', '2': 'crosshair', '3': 'move', '4': 'wait', '5': 'none', '6': 'pointer' };
        var cursor = cursorMap[String(result.cursorType)] || 'default';
        if (this._canvas.style.cursor !== cursor) {
            this._canvas.style.cursor = cursor;
        }

        if (result.debugLog && this._opts.onDebugLog) {
            this._opts.onDebugLog(result.debugLog);
        }

        if (this._opts.onFrame) {
            this._opts.onFrame(this._lastFrame, this._lastFrameCount);
        }

        // Mirror worker's playing flag to drive our loop
        if (!result.playing && !result.enginePlaying && this._playing) {
            console.log('[LS] _doTick: stopping — playing=' + result.playing + ' enginePlaying=' + result.enginePlaying);
            this._playing = false;
            this._stopLoop();
            this._stopCursorLoop();
        }
    };

    // --- 60fps cursor compositing ---

    /**
     * Composite the cursor bitmap onto the cached base frame and blit to canvas.
     * Called from the rAF loop on every mouse move, and after each tick.
     */
    ShockwavePlayer.prototype._compositeCursorAndBlit = function() {
        if (!this._baseFrame) return;
        var base = this._baseFrame;
        var cur = this._cursorBitmap;

        if (!cur && !this._caretInfo && !this._selectionRects) {
            // No bitmap cursor, text caret, or selection — just blit the base frame
            this._ctx.putImageData(base, 0, 0);
            this._cursorDirty = false;
            return;
        }

        // Create a working copy of the base frame to overlay cursor onto
        var w = base.width;
        var h = base.height;
        if (!this._compositeData || this._compositeData.width !== w || this._compositeData.height !== h) {
            this._compositeData = this._ctx.createImageData(w, h);
        }
        var dst = this._compositeData.data;
        var src = base.data;
        dst.set(src);

        // Overlay cursor at current mouse position
        if (cur) {
            var drawX = this._mouseX - cur.regX;
            var drawY = this._mouseY - cur.regY;
            var cw = cur.w;
            var ch = cur.h;
            var crgba = cur.rgba;

            for (var cy = 0; cy < ch; cy++) {
                var dstY = drawY + cy;
                if (dstY < 0 || dstY >= h) continue;
                for (var cx = 0; cx < cw; cx++) {
                    var dstX = drawX + cx;
                    if (dstX < 0 || dstX >= w) continue;

                    var srcOff = (cy * cw + cx) * 4;
                    var alpha = crgba[srcOff + 3];
                    if (alpha === 0) continue;

                    var dstOff = (dstY * w + dstX) * 4;
                    if (alpha === 255) {
                        dst[dstOff]     = crgba[srcOff];
                        dst[dstOff + 1] = crgba[srcOff + 1];
                        dst[dstOff + 2] = crgba[srcOff + 2];
                        dst[dstOff + 3] = 255;
                    } else {
                        // Alpha blend
                        var invA = 255 - alpha;
                        dst[dstOff]     = (crgba[srcOff]     * alpha + dst[dstOff]     * invA) / 255 | 0;
                        dst[dstOff + 1] = (crgba[srcOff + 1] * alpha + dst[dstOff + 1] * invA) / 255 | 0;
                        dst[dstOff + 2] = (crgba[srcOff + 2] * alpha + dst[dstOff + 2] * invA) / 255 | 0;
                        dst[dstOff + 3] = 255;
                    }
                }
            }
        }

        // Draw selection highlight (semi-transparent blue rectangles)
        var selRects = this._selectionRects;
        if (selRects) {
            for (var ri = 0; ri < selRects.length; ri++) {
                var r = selRects[ri];
                for (var ry = 0; ry < r.h; ry++) {
                    var py = r.y + ry;
                    if (py < 0 || py >= h) continue;
                    for (var rx = 0; rx < r.w; rx++) {
                        var px = r.x + rx;
                        if (px < 0 || px >= w) continue;
                        var off = (py * w + px) * 4;
                        // Invert colors (Director-style selection highlight)
                        dst[off]     = 255 - dst[off];
                        dst[off + 1] = 255 - dst[off + 1];
                        dst[off + 2] = 255 - dst[off + 2];
                    }
                }
            }
        }

        // Draw text caret (1px black vertical line)
        var caret = this._caretInfo;
        if (caret && caret.h > 0) {
            for (var ci = 0; ci < caret.h; ci++) {
                var caretY = caret.y + ci;
                if (caretY < 0 || caretY >= h) continue;
                var caretX = caret.x;
                if (caretX < 0 || caretX >= w) continue;
                var off = (caretY * w + caretX) * 4;
                dst[off] = 0; dst[off+1] = 0; dst[off+2] = 0; dst[off+3] = 255;
            }
        }

        this._ctx.putImageData(this._compositeData, 0, 0);
        this._cursorDirty = false;
    };

    /**
     * Start the cursor rendering loop, only when a custom bitmap cursor is active.
     * Uses requestAnimationFrame (screen refresh rate) by default, or a fixed
     * interval if options.cursorFps is set (e.g. cursorFps: 60).
     */
    ShockwavePlayer.prototype._startCursorLoop = function() {
        if (this._cursorRafId) return; // already running
        if (!this._cursorBitmap) return; // no custom cursor — nothing to do
        var self = this;
        if (this._cursorFps > 0) {
            // Fixed interval mode
            var ms = 1000 / this._cursorFps;
            this._cursorRafId = setInterval(function() {
                if (self._cursorDirty && self._cursorBitmap && self._baseFrame) {
                    self._compositeCursorAndBlit();
                }
            }, ms);
            this._cursorUsingInterval = true;
        } else {
            // requestAnimationFrame mode (screen refresh rate)
            this._cursorUsingInterval = false;
            function cursorFrame() {
                if (self._cursorDirty && self._cursorBitmap && self._baseFrame) {
                    self._compositeCursorAndBlit();
                }
                self._cursorRafId = requestAnimationFrame(cursorFrame);
            }
            this._cursorRafId = requestAnimationFrame(cursorFrame);
        }
    };

    ShockwavePlayer.prototype._stopCursorLoop = function() {
        if (this._cursorRafId) {
            if (this._cursorUsingInterval) {
                clearInterval(this._cursorRafId);
            } else {
                cancelAnimationFrame(this._cursorRafId);
            }
            this._cursorRafId = null;
        }
    };

    // --- Utilities ---

    function _clone(obj) {
        var r = {};
        for (var k in obj) r[k] = obj[k];
        return r;
    }

    return { create: create };
})();
