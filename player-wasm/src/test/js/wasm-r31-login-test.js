#!/usr/bin/env node
'use strict';

/**
 * Headless r31 SSO login diagnostic.
 *
 * Replays the Classichabbo r31 embed params and records the browser-side MUS
 * WebSocket path.
 *
 * Args: distPath htdocsRoot dcrUrlPathOrUrl [outputDir]
 */

const crypto = require('crypto');
const fs = require('fs');
const http = require('http');
const net = require('net');
const path = require('path');
const tls = require('tls');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const htdocsRoot = process.argv[3] || 'C:/xampp/htdocs';
const dcrUrlPath = process.argv[4] || 'http://localhost/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?';
const outputDir = process.argv[5] || path.resolve(process.cwd(), 'frames_r31_login');

const INFO_HOST = process.env.R31_INFO_HOST || '127.0.0.1';
const INFO_PORT = Number(process.env.R31_INFO_PORT || 30100);
const MUS_HOST = process.env.R31_MUS_HOST || '127.0.0.1';
const MUS_PORT = Number(process.env.R31_MUS_PORT || 39101);
const GAME_WEBSOCKET_URL = process.env.R31_GAME_WEBSOCKET_URL || buildWebSocketUrl(INFO_HOST, INFO_PORT);
const MUS_WEBSOCKET_URL = process.env.R31_MUS_WEBSOCKET_URL || buildWebSocketUrl(MUS_HOST, MUS_PORT);
const GAME_WEBSOCKET_ENDPOINT = parseWebSocketEndpoint(GAME_WEBSOCKET_URL);
const MUS_WEBSOCKET_ENDPOINT = parseWebSocketEndpoint(MUS_WEBSOCKET_URL);
const SSO_TICKET = process.env.R31_SSO_TICKET || 'vibe-sso-admin-ada3abaf-dfdb-4110-be88-6a22495f109b';
const USE_SSO = process.env.R31_USE_SSO !== '0';
const LOGIN_USERNAME = process.env.R31_LOGIN_USERNAME || 'admin';
const LOGIN_PASSWORD = process.env.R31_LOGIN_PASSWORD || 'admin';
const MANUAL_LOGIN_DELAY_MS = Number(process.env.R31_MANUAL_LOGIN_DELAY_MS || 18000);
const EXTERNAL_VARIABLES_URL = process.env.R31_EXTERNAL_VARIABLES_URL || 'http://localhost/gamedata/external_variables.txt?';
const EXTERNAL_TEXTS_URL = process.env.R31_EXTERNAL_TEXTS_URL || 'http://localhost/gamedata/external_texts.txt?';

const MAX_POLLS = Number(process.env.R31_MAX_POLLS || 900);
const TRACE_R31 = process.env.R31_TRACE === '1';
const CLICK_CATALOGUE = process.env.R31_CLICK_CATALOGUE === '1';
const CLICK_CATALOGUE_CANDY = process.env.R31_CLICK_CATALOGUE_CANDY === '1';
const CLICK_CATALOGUE_ASIAN = process.env.R31_CLICK_CATALOGUE_ASIAN === '1';
const CLICK_CATALOGUE_CANDY_PRODUCT = process.env.R31_CLICK_CATALOGUE_CANDY_PRODUCT === '1';
const CATALOGUE_CANDY_WAIT_MS = Number(process.env.R31_CATALOGUE_CANDY_WAIT_MS || 15000);
const CATALOGUE_ASIAN_WAIT_MS = Number(process.env.R31_CATALOGUE_ASIAN_WAIT_MS || 30000);
const EXIT_AFTER_ASIAN_CAPTURE = process.env.R31_EXIT_AFTER_ASIAN_CAPTURE === '1';
const ASIAN_CAPTURE_HOVER_X = process.env.R31_ASIAN_CAPTURE_HOVER_X;
const ASIAN_CAPTURE_HOVER_Y = process.env.R31_ASIAN_CAPTURE_HOVER_Y;
const ASIAN_POST_WAIT_MS = Number(process.env.R31_ASIAN_POST_WAIT_MS || 0);
const POLL_MS = 500;

function isAbsoluteUrl(value) {
    return /^https?:\/\//i.test(String(value || ''));
}

function resolveBrowserUrl(value, baseUrl) {
    if (isAbsoluteUrl(value)) {
        return value;
    }
    return baseUrl + '/' + String(value || '').replace(/^\/+/, '');
}

function jsString(value) {
    return JSON.stringify(String(value));
}

function writeProgress(label, details) {
    const line = `${new Date().toISOString()} ${label}${details === undefined ? '' : ' ' + details}\n`;
    fs.appendFileSync(path.join(outputDir, 'r31_progress.log'), line);
}

function parseWebSocketEndpoint(value) {
    const url = new URL(value);
    const secure = url.protocol === 'wss:';
    return {
        host: url.hostname,
        port: Number(url.port || (secure ? 443 : 80)),
        secure,
    };
}

const MIME = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.wasm': 'application/wasm',
    '.css': 'text/css',
    '.png': 'image/png',
    '.dcr': 'application/x-director',
    '.cct': 'application/x-director',
    '.cst': 'application/x-director',
    '.txt': 'text/plain',
    '.xml': 'application/xml',
};

function serveFile(res, filePath) {
    const ext = path.extname(filePath).toLowerCase();
    const data = fs.readFileSync(filePath);
    res.writeHead(200, {
        'Content-Type': MIME[ext] || 'application/octet-stream',
        'Content-Length': data.length,
        'Access-Control-Allow-Origin': '*',
    });
    res.end(data);
}

function createServer() {
    return new Promise(resolve => {
        const server = http.createServer((req, res) => {
            const urlPath = decodeURIComponent(req.url.split('?')[0]);
            const candidates = [
                path.join(distPath, urlPath),
                path.join(htdocsRoot, urlPath.replace(/^\/+/, '')),
            ];
            for (const filePath of candidates) {
                if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
                    return serveFile(res, filePath);
                }
            }
            res.writeHead(404);
            res.end('Not found: ' + urlPath);
        });
        server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }));
    });
}

function tcpProbe(host, port, timeoutMs = 5000) {
    return new Promise(resolve => {
        const started = Date.now();
        const socket = net.createConnection({ host, port });
        const done = result => {
            socket.destroy();
            resolve(Object.assign({ elapsedMs: Date.now() - started }, result));
        };
        socket.setTimeout(timeoutMs);
        socket.once('connect', () => done({ ok: true }));
        socket.once('timeout', () => done({ ok: false, error: 'timeout' }));
        socket.once('error', err => done({ ok: false, error: err.message }));
    });
}

function websocketUpgradeProbe(host, port, timeoutMs = 5000, secure = false) {
    return new Promise(resolve => {
        const started = Date.now();
        const key = crypto.randomBytes(16).toString('base64');
        let response = '';
        const socket = secure
            ? tls.connect({ host, port, servername: host })
            : net.createConnection({ host, port });
        const done = result => {
            socket.destroy();
            resolve(Object.assign({ elapsedMs: Date.now() - started }, result));
        };
        socket.setTimeout(timeoutMs);
        socket.once('connect', () => {
            socket.write([
                'GET / HTTP/1.1',
                `Host: ${host}:${port}`,
                'Upgrade: websocket',
                'Connection: Upgrade',
                `Sec-WebSocket-Key: ${key}`,
                'Sec-WebSocket-Version: 13',
                '',
                '',
            ].join('\r\n'));
        });
        socket.on('data', chunk => {
            response += chunk.toString('latin1');
            if (response.includes('\r\n\r\n')) {
                const status = response.split('\r\n', 1)[0];
                done({ ok: /^HTTP\/1\.[01] 101\b/.test(status), status, preview: response.slice(0, 160) });
            }
        });
        socket.once('timeout', () => done({ ok: false, error: 'timeout', preview: response.slice(0, 160) }));
        socket.once('error', err => done({ ok: false, error: err.message, preview: response.slice(0, 160) }));
        socket.once('close', () => {
            if (!response) done({ ok: false, error: 'closed without response' });
        });
    });
}

async function loadBrowser() {
    try {
        const puppeteer = require('puppeteer');
        const browser = await puppeteer.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-setuid-sandbox'],
        });
        return { browser, newPage: () => browser.newPage() };
    } catch (puppeteerError) {
        const { chromium } = require('playwright');
        const browser = await chromium.launch({
            headless: true,
            executablePath: process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe',
            args: ['--no-sandbox'],
        });
        return { browser, newPage: () => browser.newPage() };
    }
}

async function captureCanvas(page, filePath) {
    const dataUrl = await page.evaluate(({ preferDisplayedCanvas }) => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) return null;
        const copy = document.createElement('canvas');
        copy.width = 960;
        copy.height = 540;
        const ctx = copy.getContext('2d');
        ctx.fillStyle = '#000';
        ctx.fillRect(0, 0, copy.width, copy.height);
        const baseFrame = window.betaClientPlayer && window.betaClientPlayer._baseFrame;
        if (preferDisplayedCanvas || !baseFrame) {
            ctx.drawImage(canvas, 0, 0);
        } else {
            ctx.putImageData(baseFrame, 0, 0);
        }
        return copy.toDataURL('image/png');
    }, { preferDisplayedCanvas: process.env.R31_CAPTURE_DISPLAYED_CANVAS === '1' });
    if (dataUrl) {
        fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'));
    }
}

async function captureCanvasBoth(page, basePath, displayedPath) {
    await captureCanvas(page, basePath);
    const previous = process.env.R31_CAPTURE_DISPLAYED_CANVAS;
    process.env.R31_CAPTURE_DISPLAYED_CANVAS = '1';
    try {
        await captureCanvas(page, displayedPath);
    } finally {
        if (previous === undefined) {
            delete process.env.R31_CAPTURE_DISPLAYED_CANVAS;
        } else {
            process.env.R31_CAPTURE_DISPLAYED_CANVAS = previous;
        }
    }
}

async function captureCanvasDirect(page, filePath) {
    const dataUrl = await page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        return canvas ? canvas.toDataURL('image/png') : null;
    });
    if (dataUrl) {
        fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'));
    }
}

async function captureCanvasRegion(page, filePath, rect) {
    const dataUrl = await page.evaluate(({ x, y, width, height }) => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) return null;
        const crop = document.createElement('canvas');
        crop.width = width;
        crop.height = height;
        crop.getContext('2d').drawImage(canvas, x, y, width, height, 0, 0, width, height);
        return crop.toDataURL('image/png');
    }, rect);
    if (dataUrl) {
        fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'));
    }
}

async function clickCanvasAt(page, x, y) {
    await page.evaluate(async ({ x, y }) => {
        const canvas = document.getElementById('beta-client-stage');
        const rect = canvas.getBoundingClientRect();
        const clientX = rect.left + x;
        const clientY = rect.top + y;
        const dispatch = (type, buttons) => canvas.dispatchEvent(new MouseEvent(type, {
            bubbles: true,
            cancelable: true,
            clientX,
            clientY,
            button: 0,
            buttons,
        }));
        dispatch('mousemove', 0);
        await new Promise(resolve => setTimeout(resolve, 50));
        for (const [type, buttons] of [['mousedown', 1], ['mouseup', 0], ['click', 0]]) {
            canvas.dispatchEvent(new MouseEvent(type, {
                bubbles: true,
                cancelable: true,
                clientX,
                clientY,
                button: 0,
                buttons,
            }));
        }
    }, { x, y });
}

async function moveCanvasAt(page, x, y) {
    await page.evaluate(({ x, y }) => {
        const canvas = document.getElementById('beta-client-stage');
        const rect = canvas.getBoundingClientRect();
        canvas.dispatchEvent(new MouseEvent('mousemove', {
            bubbles: true,
            cancelable: true,
            clientX: rect.left + x,
            clientY: rect.top + y,
            button: 0,
            buttons: 0,
        }));
    }, { x, y });
}


async function typeCanvasText(page, text) {
    const value = String(text || '');
    await page.evaluate((value) => {
        const player = window.betaClientPlayer || window.player;
        if (!player || !player._worker || !player._workerReady) return;
        player._worker.postMessage({ type: 'paste', text: value });
    }, value);
    await new Promise(r => setTimeout(r, 100));
}

async function performPasswordLogin(page) {
    console.log(`  Typing legacy login credentials for ${LOGIN_USERNAME}`);
    await clickCanvasAt(page, 550, 303);
    await typeCanvasText(page, LOGIN_USERNAME);
    await clickCanvasAt(page, 550, 359);
    await typeCanvasText(page, LOGIN_PASSWORD);
    await clickCanvasAt(page, 549, 397);
}

async function sampleNavigatorEvidence(page) {
    return page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) return null;
        const ctx = canvas.getContext('2d');

        function countRegion(x, y, w, h) {
            const data = ctx.getImageData(x, y, w, h).data;
            const counts = Object.create(null);
            for (let p = 0; p < data.length; p += 4) {
                const key = [data[p], data[p + 1], data[p + 2]]
                    .map(v => v.toString(16).toUpperCase().padStart(2, '0'))
                    .join('');
                counts[key] = (counts[key] || 0) + 1;
            }
            return counts;
        }

        const lower = countRegion(596, 366, 340, 90);
        const go = countRegion(875, 130, 21, 14);
        return {
            lowerD4DDE1: lower.D4DDE1 || 0,
            lowerDBDBDB: lower.DBDBDB || 0,
            lowerBlack: lower['000000'] || 0,
            goC0C0C0: go.C0C0C0 || 0,
            goBlack: go['000000'] || 0,
            goWhite: go.FFFFFF || 0,
        };
    });
}

function navigatorEvidenceReady(evidence) {
    return !!evidence
        && evidence.lowerD4DDE1 >= 20000
        && evidence.lowerBlack >= 1000
        && evidence.goBlack >= 20
        && evidence.goWhite < 20;
}

async function sampleCandyProductEvidence(page) {
    return page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) return null;
        const ctx = canvas.getContext('2d');
        const centers = [
            [211, 247],
            [250, 247],
            [289, 247],
            [211, 288],
            [250, 288],
            [289, 288],
            [211, 327],
            [250, 327],
        ];
        const samples = centers.map(([cx, cy]) => {
            const data = ctx.getImageData(cx - 6, cy - 6, 13, 13).data;
            let dark = 0;
            let paleGray = 0;
            const colors = new Set();
            for (let p = 0; p < data.length; p += 4) {
                const r = data[p], g = data[p + 1], b = data[p + 2];
                colors.add(`${r},${g},${b}`);
                if (r < 50 && g < 50 && b < 50) dark++;
                if (r >= 200 && r <= 245 && g >= 200 && g <= 245 && b >= 200 && b <= 245
                        && Math.max(r, g, b) - Math.min(r, g, b) < 12) {
                    paleGray++;
                }
            }
            return { dark, paleGray, colors: colors.size };
        });
        const header = ctx.getImageData(200, 48, 290, 62).data;
        let headerPink = 0;
        for (let p = 0; p < header.length; p += 4) {
            const r = header[p], g = header[p + 1], b = header[p + 2];
            if (r > 200 && g >= 70 && g <= 180 && b >= 120 && b <= 220) {
                headerPink++;
            }
        }
        return {
            loadingPlaceholders: samples.filter(sample =>
                sample.dark >= 70 && sample.paleGray >= 80 && sample.colors <= 3).length,
            candyHeaderVisible: headerPink >= 1000,
            headerPink,
            samples,
        };
    });
}

async function sampleFurniShopEvidence(page) {
    return page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) return null;
        const ctx = canvas.getContext('2d');
        const data = ctx.getImageData(526, 267, 128, 22).data;
        let dark = 0;
        let lightGray = 0;
        for (let p = 0; p < data.length; p += 4) {
            const r = data[p], g = data[p + 1], b = data[p + 2];
            if (r < 40 && g < 40 && b < 40) dark++;
            if (r >= 170 && r <= 230 && g >= 170 && g <= 230 && b >= 170 && b <= 230
                    && Math.max(r, g, b) - Math.min(r, g, b) < 12) {
                lightGray++;
            }
        }
        return {
            candyRowVisible: dark >= 15 && lightGray >= 1000,
            dark,
            lightGray,
        };
    });
}

async function waitForFurniShop(page, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let evidence = null;
    do {
        evidence = await sampleFurniShopEvidence(page);
        if (evidence && evidence.candyRowVisible) {
            return evidence;
        }
        await new Promise(r => setTimeout(r, 250));
    } while (Date.now() < deadline);
    return evidence;
}

async function waitForCandyProducts(page, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let evidence = null;
    do {
        evidence = await sampleCandyProductEvidence(page);
        if (evidence && evidence.candyHeaderVisible && evidence.loadingPlaceholders === 0) {
            return evidence;
        }
        await new Promise(r => setTimeout(r, 500));
    } while (Date.now() < deadline);
    return evidence;
}

async function waitForCandyHeader(page, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let evidence = null;
    do {
        evidence = await sampleCandyProductEvidence(page);
        if (evidence && evidence.candyHeaderVisible) {
            return evidence;
        }
        await new Promise(r => setTimeout(r, 250));
    } while (Date.now() < deadline);
    return evidence;
}

async function sampleCatalogueWindowEvidence(page) {
    return page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) return null;
        const ctx = canvas.getContext('2d');
        const title = ctx.getImageData(166, 22, 356, 20).data;
        let paleGray = 0;
        let black = 0;
        for (let p = 0; p < title.length; p += 4) {
            const r = title[p], g = title[p + 1], b = title[p + 2];
            if (r >= 220 && r <= 250 && g >= 220 && g <= 250 && b >= 220 && b <= 250
                    && Math.max(r, g, b) - Math.min(r, g, b) < 8) {
                paleGray++;
            }
            if (r < 30 && g < 30 && b < 30) {
                black++;
            }
        }
        return {
            catalogueVisible: paleGray > 4000 && black < 500,
            paleGray,
            black,
        };
    });
}

async function waitForCatalogueWindow(page, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let evidence = null;
    do {
        evidence = await sampleCatalogueWindowEvidence(page);
        if (evidence && evidence.catalogueVisible) {
            return evidence;
        }
        await new Promise(r => setTimeout(r, 250));
    } while (Date.now() < deadline);
    return evidence;
}

function buildWebSocketUrl(host, port) {
    const local = /^(localhost|127(?:\.\d{1,3}){3}|\[::1\]|::1)$/i.test(String(host || ''));
    return `${local ? 'ws' : 'wss'}://${host}:${port}`;
}

async function sampleAsianPageEvidence(page) {
    return page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) return null;
        const ctx = canvas.getContext('2d');

        const header = ctx.getImageData(190, 48, 310, 65).data;
        let asianHeaderPixels = 0;
        for (let p = 0; p < header.length; p += 4) {
            const r = header[p], g = header[p + 1], b = header[p + 2];
            if (r >= 180 && g < 80 && b < 80) {
                asianHeaderPixels++;
            }
        }

        const cells = [
            [196, 193, 30, 33], [235, 193, 30, 33], [273, 193, 30, 33],
            [196, 231, 30, 33], [235, 231, 30, 33], [273, 231, 30, 33],
            [196, 270, 30, 33], [235, 270, 30, 33], [273, 270, 30, 33],
        ];
        const cellSaturatedPixels = [];
        let loadedCells = 0;
        for (const [x, y, w, h] of cells) {
            const product = ctx.getImageData(x, y, w, h).data;
            let saturatedPixels = 0;
            for (let p = 0; p < product.length; p += 4) {
                const r = product[p], g = product[p + 1], b = product[p + 2];
                if (Math.max(r, g, b) - Math.min(r, g, b) > 40) {
                    saturatedPixels++;
                }
            }
            cellSaturatedPixels.push(saturatedPixels);
            if (saturatedPixels > 80) {
                loadedCells++;
            }
        }
        return {
            asianHeaderVisible: asianHeaderPixels > 600,
            asianHeaderPixels,
            loadedCells,
            cellSaturatedPixels,
        };
    });
}

async function waitForAsianPage(page, timeoutMs) {
    const deadline = Date.now() + timeoutMs;
    let evidence = null;
    do {
        evidence = await sampleAsianPageEvidence(page);
        if (evidence && evidence.asianHeaderVisible && evidence.loadedCells === 9) {
            return evidence;
        }
        await new Promise(r => setTimeout(r, 500));
    } while (Date.now() < deadline);
    return evidence;
}

function browserHtml(baseUrl) {
    const movieUrl = resolveBrowserUrl(dcrUrlPath, baseUrl);
    const externalVariables = resolveBrowserUrl(EXTERNAL_VARIABLES_URL, baseUrl);
    const externalTexts = resolveBrowserUrl(EXTERNAL_TEXTS_URL, baseUrl);
    const authParamEntry = USE_SSO
        ? `,"sw8":${jsString(`use.sso.ticket=1;sso.ticket=${SSO_TICKET}`)}`
        : '';
    return `<!doctype html>
<html><body>
<canvas id="beta-client-stage" width="960" height="540"></canvas>
<script src="${baseUrl}/shockwave-lib.js"><\/script>
<script>
var betaClientMovieUrl = ${jsString(movieUrl)};
var betaClientParams = {"sw1":"client.allow.cross.domain=1;client.notify.cross.domain=0","sw2":"connection.info.host=${INFO_HOST};connection.info.port=${INFO_PORT}","sw3":"connection.mus.host=${MUS_HOST};connection.mus.port=${MUS_PORT}","sw4":"site.url=;url.prefix=","sw5":"client.reload.url=/client/beta?x=reauthenticate;client.fatal.error.url=/clientutils?key=error","sw6":"client.connection.failed.url=/clientutils?key=connection_failed;external.variables.txt=${externalVariables}","sw7":"external.texts.txt=${externalTexts}"${authParamEntry}};
window._testState = {
    loaded: false,
    error: null,
    tick: 0,
    frame: 0,
    debugLogs: [],
    gotoNetPages: [],
    musOpened: false,
    musClosed: false,
    musErrored: false,
    musSendCount: 0,
    musMessageCount: 0
};
try {
    var betaClientPlayer = LibreShockwave.create("beta-client-stage", {
        basePath: "${baseUrl}/",
        autoplay: true,
        remember: false,
        debugPlayback: true,
        params: betaClientParams,
        onLoad: function(info) {
            _testState.loaded = true;
            _testState.info = info;
            if (betaClientPlayer) betaClientPlayer.play();
        },
        onError: function(message) {
            _testState.error = String(message);
            console.error("LibreShockwave r31 login error:", message);
        },
        onGotoNetPage: function(url, target) {
            _testState.gotoNetPages.push({ url: String(url), target: String(target) });
            console.log("[TEST] gotoNetPage " + url + " target=" + target);
        },
        onFrame: function(frame) { _testState.tick++; _testState.frame = frame; },
        onDebugLog: function(log) {
            var text = String(log);
            _testState.debugLogs.push(text);
            if (text.indexOf("[MUS] open") >= 0) _testState.musOpened = true;
            if (text.indexOf("[MUS] close") >= 0) _testState.musClosed = true;
            if (text.indexOf("[MUS] error") >= 0) _testState.musErrored = true;
            if (text.indexOf("[MUS] send") >= 0) _testState.musSendCount++;
            if (text.indexOf("[MUS] message") >= 0) _testState.musMessageCount++;
            if (String(log).indexOf("[MUS]") >= 0 ||
                    String(log).indexOf("[TRACE]") >= 0 ||
                    String(log).indexOf("castDataRequestCallback") >= 0 ||
                    String(log).indexOf("fetchStatus") >= 0 ||
                    String(log).indexOf("[EH]") >= 0 ||
                    String(log).indexOf("[WORKER] fetch") >= 0 ||
                    String(log).indexOf("[NetManager]") >= 0 ||
                    String(log).indexOf("[ScriptError]") >= 0) {
                console.log(log);
            }
        }
    });
    window.betaClientPlayer = betaClientPlayer;
    if (${TRACE_R31 ? 'true' : 'false'}) {
        ["connect", "constructConnectionManager", "receiveUpdate", "updateState",
            "addPreloadNetThing", "AddNextpreloadNetThing", "DoneCurrentDownLoad",
            "setImportedCast", "removeCastLoadTask", "DoCallBack",
            "securityCastDownloadCallback", "responseWithPublicKey",
            "getLoginParameter", "assign", "powMod", "getString", "getByteArray",
            "setText", "createImgFromTxt",
            "WvUrP88jJ4snglkrhCh3u9vHu0ADDS",
            "forwardToRosettaDisablePage", "handleServerSecretKey",
            "handleCryptoParameters", "sendLogin", "getDomainAndTld", "error", "fatalError"].forEach(function(name) {
            betaClientPlayer.addTraceHandler(name);
        });
    }
    betaClientPlayer.load(betaClientMovieUrl);
} catch (error) {
    _testState.error = String(error);
    console.error("LibreShockwave r31 login error:", error);
}
<\/script>
</body></html>`;
}

function extractClientError(gotoNetPages) {
    for (const entry of gotoNetPages || []) {
        try {
            const url = new URL(entry.url, 'http://local.test');
            if (url.searchParams.get('key') === 'error') {
                return {
                    error: url.searchParams.get('error') || '',
                    musErrorCode: url.searchParams.get('mus_errorcode') || '',
                    host: url.searchParams.get('host') || '',
                    port: url.searchParams.get('port') || '',
                    url: entry.url,
                };
            }
        } catch (e) {
            if (String(entry.url).includes('key=error')) {
                return { error: 'unknown', musErrorCode: '', host: '', port: '', url: entry.url };
            }
        }
    }
    return null;
}

async function main() {
    console.log('=== WASM r31 SSO Login Test ===');
    console.log('Dist:   ', distPath);
    console.log('Htdocs: ', htdocsRoot);
    console.log('DCR:    ', dcrUrlPath);
    console.log('Target: ', `${INFO_HOST}:${INFO_PORT}`);
    console.log('Game WS:', GAME_WEBSOCKET_URL);
    console.log('MUS:    ', `${MUS_HOST}:${MUS_PORT}`);
    console.log('MUS WS: ', MUS_WEBSOCKET_URL);
    console.log('SSO:    ', SSO_TICKET);
    console.log('Output: ', outputDir);

    if (!fs.existsSync(path.join(distPath, 'shockwave-lib.js'))) {
        console.error('FAIL: shockwave-lib.js not found in dist. Run assembleWasm first.');
        process.exit(1);
    }

    if (!isAbsoluteUrl(dcrUrlPath)) {
        const dcrFile = path.join(htdocsRoot, dcrUrlPath.replace(/^\/+/, ''));
        if (!fs.existsSync(dcrFile)) {
            console.error('FAIL: DCR file not found: ' + dcrFile);
            process.exit(1);
        }
    }

    fs.mkdirSync(outputDir, { recursive: true });
    try { fs.unlinkSync(path.join(outputDir, 'r31_progress.log')); } catch (e) {}
    writeProgress('start');

    const gameTcp = { ok: true };
    const gameWs = { ok: true };
    const musTcp = { ok: true };
    const musWs = { ok: true };

    const { server, port } = await createServer();
    const baseUrl = `http://127.0.0.1:${port}`;
    const htmlPath = path.join(distPath, '_test_r31_login.html');
    fs.writeFileSync(htmlPath, browserHtml(baseUrl));

    let browser;
    let finalState = null;
    let navigatorEvidence = null;
    let navigatorDisplayed = false;
    let candyEvidence = null;
    const consoleLines = [];
    const wsEvents = [];
    const httpEvents = [];

    try {
        const loaded = await loadBrowser();
        browser = loaded.browser;
        const page = await loaded.newPage();
        const client = await page.target().createCDPSession();
        await client.send('Network.enable');
        client.on('Network.webSocketWillSendHandshakeRequest', evt => {
            wsEvents.push({ type: 'request', url: evt.request.url });
        });
        client.on('Network.webSocketHandshakeResponseReceived', evt => {
            wsEvents.push({ type: 'response', url: evt.response.url, status: evt.response.status, statusText: evt.response.statusText });
        });
        client.on('Network.webSocketFrameError', evt => {
            wsEvents.push({ type: 'frameError', errorMessage: evt.errorMessage });
        });
        client.on('Network.requestWillBeSent', evt => {
            if (/\b(sec\.cct|external_variables|external_texts|\.cct)(\?|$)/i.test(evt.request.url)) {
                httpEvents.push({ type: 'request', url: evt.request.url });
            }
        });
        client.on('Network.responseReceived', evt => {
            if (/\b(sec\.cct|external_variables|external_texts|\.cct)(\?|$)/i.test(evt.response.url)) {
                httpEvents.push({
                    type: 'response',
                    url: evt.response.url,
                    status: evt.response.status,
                    mimeType: evt.response.mimeType,
                });
            }
        });
        client.on('Network.loadingFailed', evt => {
            httpEvents.push({ type: 'failed', requestId: evt.requestId, errorText: evt.errorText, blockedReason: evt.blockedReason || '' });
        });

        page.on('console', msg => {
            const text = msg.text();
            consoleLines.push(text);
            if (text.includes('[MUS]') || text.includes('[TEST]') || text.includes('LibreShockwave')) {
                console.log('  [page] ' + text);
            }
        });
        page.on('pageerror', err => {
            consoleLines.push('[pageerror] ' + err.message);
            console.log('  [pageerror] ' + err.message);
        });

        writeProgress('before-page-goto', `${baseUrl}/_test_r31_login.html`);
        await page.goto(`${baseUrl}/_test_r31_login.html`, { waitUntil: 'domcontentloaded' });
        writeProgress('after-page-goto');
        if (!USE_SSO) {
            await new Promise(r => setTimeout(r, MANUAL_LOGIN_DELAY_MS));
            await performPasswordLogin(page);
        }

        for (let i = 0; i < MAX_POLLS; i++) {
            await new Promise(r => setTimeout(r, POLL_MS));
            finalState = await page.evaluate(() => {
                const canvas = document.getElementById('beta-client-stage');
                const ctx = canvas.getContext('2d');
                const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
                let nonBlack = 0;
                const buckets = new Set();
                for (let p = 0; p < data.length; p += 80) {
                    const r = data[p], g = data[p + 1], b = data[p + 2];
                    if (r > 10 || g > 10 || b > 10) nonBlack++;
                    buckets.add((r >> 5) + ',' + (g >> 5) + ',' + (b >> 5));
                }
                return {
                    loaded: window._testState.loaded,
                    error: window._testState.error,
                    tick: window._testState.tick,
                    frame: window._testState.frame,
                    gotoNetPages: window._testState.gotoNetPages.slice(),
                    musOpened: window._testState.musOpened,
                    musClosed: window._testState.musClosed,
                    musErrored: window._testState.musErrored,
                    musSendCount: window._testState.musSendCount,
                    musMessageCount: window._testState.musMessageCount,
                    debugLogs: window._testState.debugLogs.slice(-80),
                    spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
                    nonBlack,
                    colorBuckets: buckets.size,
                };
            });

            if (i % 10 === 0) {
                console.log(`  poll=${i} loaded=${finalState.loaded} tick=${finalState.tick} frame=${finalState.frame} ` +
                    `sprites=${finalState.spriteCount} nonBlack=${finalState.nonBlack} colors=${finalState.colorBuckets}`);
                writeProgress('poll', JSON.stringify({
                    i,
                    loaded: finalState.loaded,
                    error: finalState.error,
                    tick: finalState.tick,
                    frame: finalState.frame,
                    sprites: finalState.spriteCount,
                    nonBlack: finalState.nonBlack,
                    colors: finalState.colorBuckets,
                    musOpened: finalState.musOpened,
                    musClosed: finalState.musClosed,
                    musErrored: finalState.musErrored,
                    musSendCount: finalState.musSendCount,
                    musMessageCount: finalState.musMessageCount,
                    gotoNetPages: finalState.gotoNetPages.slice(-3),
                    debugLogs: finalState.debugLogs.slice(-5),
                }));
            }

            const failedPage = finalState.gotoNetPages.some(entry => entry.url.includes('connection_failed'));
            if (finalState.error || failedPage) {
                await new Promise(r => setTimeout(r, 2000));
                break;
            }
            if (finalState.spriteCount >= 70 && finalState.nonBlack > 1000) {
                navigatorEvidence = await sampleNavigatorEvidence(page);
                if (navigatorEvidenceReady(navigatorEvidence)) {
                    navigatorDisplayed = true;
                    await new Promise(r => setTimeout(r, 2000));
                    navigatorEvidence = await sampleNavigatorEvidence(page);
                    break;
                }
                if (i % 20 === 0) {
                    console.log('  Waiting for navigator evidence: ' + JSON.stringify(navigatorEvidence));
                }
            }
        }

        if (!navigatorDisplayed && finalState && !finalState.error) {
            navigatorEvidence = await sampleNavigatorEvidence(page);
            if (navigatorEvidenceReady(navigatorEvidence)) {
                navigatorDisplayed = true;
            } else {
                console.log('  Navigator evidence not ready before final capture: ' + JSON.stringify(navigatorEvidence));
            }
        }

        if (navigatorDisplayed) {
            writeProgress('navigator-ready');
            await captureCanvas(page, path.join(outputDir, 'r31_login_final.png'));
            await captureCanvasRegion(page, path.join(outputDir, 'navigator_libre.png'), {
                x: 585,
                y: 22,
                width: 362,
                height: 453,
            });
            if (CLICK_CATALOGUE) {
                console.log('  Clicking catalogue icon after navigator evidence is ready');
                writeProgress('before-catalogue-click');
                await clickCanvasAt(page, 858, 512);
                writeProgress('after-catalogue-click');
                let catalogueEvidence = await waitForCatalogueWindow(page, 5000);
                if (!catalogueEvidence || !catalogueEvidence.catalogueVisible) {
                    console.log('  Catalogue evidence after first click: ' + JSON.stringify(catalogueEvidence));
                    writeProgress('catalogue-retry', JSON.stringify(catalogueEvidence));
                    await clickCanvasAt(page, 850, 512);
                    catalogueEvidence = await waitForCatalogueWindow(page, 5000);
                }
                console.log('  Catalogue Evidence: ' + JSON.stringify(catalogueEvidence));
                writeProgress('catalogue-evidence', JSON.stringify(catalogueEvidence));
                await captureCanvas(page, path.join(outputDir, 'r31_catalogue_after_click.png'));
                if (CLICK_CATALOGUE_CANDY) {
                    console.log('  Clicking Furni Shop and Candy catalogue entries');
                    await clickCanvasAt(page, 585, 88);
                    await waitForFurniShop(page, 5000);
                    await captureCanvas(page, path.join(outputDir, 'r31_catalogue_after_furni_click.png'));
                    await clickCanvasAt(page, 552, 278);
                    candyEvidence = await waitForCandyHeader(page, 10000);
                    if (CLICK_CATALOGUE_CANDY_PRODUCT && candyEvidence && candyEvidence.candyHeaderVisible) {
                        await clickCanvasAt(page, 211, 247);
                    }
                    candyEvidence = await waitForCandyProducts(page, CATALOGUE_CANDY_WAIT_MS);
                    await captureCanvas(page, path.join(outputDir, 'r31_catalogue_after_candy_click.png'));
                }
                if (CLICK_CATALOGUE_ASIAN) {
                    console.log('  Clicking Furni Shop and Asian catalogue entries');
                    writeProgress('before-furni-click');
                    await clickCanvasAt(page, 585, 88);
                    writeProgress('after-furni-click');
                    await waitForFurniShop(page, 5000);
                    writeProgress('after-furni-wait');
                    await clickCanvasAt(page, 552, 300);
                    writeProgress('after-asian-click');
                    const asianEvidence = await waitForAsianPage(page, CATALOGUE_ASIAN_WAIT_MS);
                    console.log('  Asian Evidence: ' + JSON.stringify(asianEvidence));
                    writeProgress('asian-evidence', JSON.stringify(asianEvidence));
                    if (ASIAN_POST_WAIT_MS > 0) {
                        writeProgress('asian-post-wait', ASIAN_POST_WAIT_MS);
                        await new Promise(r => setTimeout(r, ASIAN_POST_WAIT_MS));
                    }
                    if (ASIAN_CAPTURE_HOVER_X !== undefined && ASIAN_CAPTURE_HOVER_Y !== undefined) {
                        await moveCanvasAt(page, Number(ASIAN_CAPTURE_HOVER_X), Number(ASIAN_CAPTURE_HOVER_Y));
                        await new Promise(r => setTimeout(r, 250));
                    }
                    writeProgress('before-asian-capture');
                    await captureCanvasBoth(page,
                        path.join(outputDir, 'r31_catalogue_after_asian_click.png'),
                        path.join(outputDir, 'r31_catalogue_after_asian_click_displayed.png'));
                    await captureCanvasDirect(page,
                        path.join(outputDir, 'r31_catalogue_after_asian_click_direct.png'));
                    writeProgress('after-asian-capture');
                    const windowSpriteDiagnostics = await page.evaluate(async () => {
                        if (!window.betaClientPlayer || !window.betaClientPlayer.getWindowSpriteDiagnostics) return '';
                        return await window.betaClientPlayer.getWindowSpriteDiagnostics();
                    });
                    fs.writeFileSync(path.join(outputDir, 'r31_window_sprite_diag.txt'), windowSpriteDiagnostics);
                    if (EXIT_AFTER_ASIAN_CAPTURE) {
                        writeProgress('exit-after-asian-capture');
                        return;
                    }
                    const stage = await page.$('#beta-client-stage');
                    if (stage) {
                        await stage.screenshot({ path: path.join(outputDir, 'r31_catalogue_after_asian_click_element.png') });
                    }
                }
                const windowSpriteDiagnostics = await page.evaluate(async () => {
                    if (!window.betaClientPlayer || !window.betaClientPlayer.getWindowSpriteDiagnostics) return '';
                    return await window.betaClientPlayer.getWindowSpriteDiagnostics();
                });
                fs.writeFileSync(path.join(outputDir, 'r31_window_sprite_diag.txt'), windowSpriteDiagnostics);
                finalState = await page.evaluate(() => {
                    const canvas = document.getElementById('beta-client-stage');
                    const ctx = canvas.getContext('2d');
                    const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
                    let nonBlack = 0;
                    const buckets = new Set();
                    for (let p = 0; p < data.length; p += 80) {
                        const r = data[p], g = data[p + 1], b = data[p + 2];
                        if (r > 10 || g > 10 || b > 10) nonBlack++;
                        buckets.add((r >> 5) + ',' + (g >> 5) + ',' + (b >> 5));
                    }
                    return {
                        loaded: window._testState.loaded,
                        error: window._testState.error,
                        tick: window._testState.tick,
                        frame: window._testState.frame,
                        gotoNetPages: window._testState.gotoNetPages.slice(),
                        musOpened: window._testState.musOpened,
                        musClosed: window._testState.musClosed,
                        musErrored: window._testState.musErrored,
                        musSendCount: window._testState.musSendCount,
                        musMessageCount: window._testState.musMessageCount,
                        debugLogs: window._testState.debugLogs.slice(-80),
                        spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
                        nonBlack,
                        colorBuckets: buckets.size,
                    };
                });
            }
        } else {
            await captureCanvas(page, path.join(outputDir, 'r31_login_timeout.png'));
        }
    } finally {
        fs.writeFileSync(path.join(outputDir, 'r31_console_lines.txt'), consoleLines.join('\n'));
        if (browser) await browser.close();
        server.close();
        try { fs.unlinkSync(htmlPath); } catch (e) {}
        try { fs.unlinkSync(path.join(distPath, '_test_r31_external_variables.txt')); } catch (e) {}
    }

    const musLogs = finalState ? finalState.debugLogs.filter(log => log.includes('[MUS]')) : [];
    const consoleMusLogs = consoleLines.filter(log => log.includes('[MUS]'));
    const allMusLogs = musLogs.concat(consoleMusLogs);
    const musSendLines = allMusLogs.filter(log => log.includes('[MUS] send'));
    const musMessageLines = allMusLogs.filter(log => log.includes('[MUS] message'));
    const wsOpened = (finalState && finalState.musOpened) ||
        musLogs.some(log => log.includes('[MUS] open')) ||
        consoleMusLogs.some(log => log.includes('[MUS] open')) ||
        wsEvents.some(evt => evt.type === 'response' && evt.status === 101);
    const wsFailed = (finalState && (finalState.musErrored || finalState.musClosed)) ||
        musLogs.some(log => log.includes('[MUS] error') || log.includes('[MUS] close')) ||
        consoleMusLogs.some(log => log.includes('[MUS] error') || log.includes('[MUS] close')) ||
        wsEvents.some(evt => evt.type === 'response' && evt.status !== 101);
    const clientError = finalState ? extractClientError(finalState.gotoNetPages) : null;
    const connectionFailedPage = finalState && finalState.gotoNetPages.some(entry => entry.url.includes('connection_failed'));
    const visualLoggedIn = finalState && finalState.spriteCount >= 70 && finalState.nonBlack > 1000;
    const ssoSessionEstablished = finalState && finalState.loaded && wsOpened
        && musSendLines.length >= 4 && musMessageLines.length >= 3
        && !wsFailed && !connectionFailedPage && !clientError && !finalState.error;
    const loginPassed = finalState && finalState.loaded && wsOpened && visualLoggedIn && navigatorDisplayed
        && !wsFailed && !connectionFailedPage && !clientError && !finalState.error;
    const candyPassed = !CLICK_CATALOGUE_CANDY
        || (candyEvidence && candyEvidence.candyHeaderVisible && candyEvidence.loadingPlaceholders === 0);

    console.log('\n--- Browser Results ---');
    console.log('Loaded:       ', finalState && finalState.loaded);
    console.log('Ticks:        ', finalState && finalState.tick);
    console.log('Frame:        ', finalState && finalState.frame);
    console.log('Sprites:      ', finalState && finalState.spriteCount);
    console.log('MUS Open:     ', finalState && finalState.musOpened);
    console.log('MUS Traffic:  ', finalState && `${finalState.musSendCount} sends / ${finalState.musMessageCount} messages`);
    console.log('SSO Session:  ', ssoSessionEstablished);
    console.log('Visual Login: ', visualLoggedIn);
    console.log('Navigator:    ', navigatorDisplayed);
    console.log('Nav Evidence: ', JSON.stringify(navigatorEvidence));
    if (CLICK_CATALOGUE_CANDY) {
        console.log('Candy Page:   ', candyPassed);
        console.log('Candy Evidence:', JSON.stringify(candyEvidence));
    }
    console.log('GotoNetPages: ', finalState && JSON.stringify(finalState.gotoNetPages));
    console.log('WebSockets:   ', JSON.stringify(wsEvents));
    console.log('HTTP Events:  ', JSON.stringify(httpEvents.slice(-40)));
    if (musLogs.length > 0) {
        console.log('\n--- MUS Log Tail ---');
        musLogs.slice(-20).forEach(log => console.log(log));
    }
    if (finalState && finalState.debugLogs.length > 0) {
        console.log('\n--- Debug Log Tail ---');
        finalState.debugLogs.slice(-40).forEach(log => console.log(log));
    }

    console.log('\n--- Diagnosis ---');
    if (!gameTcp.ok) {
        console.log(`FAIL: ${INFO_HOST}:${INFO_PORT} is not reachable over TCP.`);
    } else if (!gameWs.ok) {
        console.log(`FAIL: game WebSocket target ${GAME_WEBSOCKET_ENDPOINT.host}:${GAME_WEBSOCKET_ENDPOINT.port} does not accept a WSS upgrade.`);
    } else if (!musTcp.ok) {
        console.log(`FAIL: ${MUS_HOST}:${MUS_PORT} is not reachable over TCP.`);
    } else if (!musWs.ok) {
        console.log(`FAIL: MUS WebSocket target ${MUS_WEBSOCKET_ENDPOINT.host}:${MUS_WEBSOCKET_ENDPOINT.port} does not accept a WSS upgrade.`);
    } else if (clientError && clientError.error === 'cross_domain_castload') {
        console.log(`FAIL: r31 reached the server on ${clientError.host || INFO_HOST}:${clientError.port || INFO_PORT}, then the client raised cross_domain_castload.`);
        console.log('This is past the initial socket connect: the WebSocket opened and exchanged packets before the fatal clientutils redirect.');
        console.log('Check the r31 server/cast cross-domain settings and the cast/load host values returned during bootstrap.');
    } else if (clientError) {
        console.log(`FAIL: r31 redirected to client error "${clientError.error || 'unknown'}" with mus_errorcode=${clientError.musErrorCode || 'n/a'}.`);
    } else if (connectionFailedPage || wsFailed) {
        console.log('FAIL: Browser attempted the MUS WebSocket but the r31 client still reached the connection-failed path.');
    } else if (!loginPassed && !ssoSessionEstablished) {
        if (musSendLines.length < 4 && musMessageLines.length >= 3 && !wsFailed) {
            console.log('FAIL: Login was still inside the slow pure-Lingo crypto stage when the diagnostic timed out.');
            console.log('Increase R31_MAX_POLLS or rerun with a fresh SSO ticket if this point has already been reached.');
        } else {
            console.log('FAIL: Login did not complete before timeout; inspect the MUS logs and final screenshot.');
        }
    } else if (ssoSessionEstablished && visualLoggedIn && !navigatorDisplayed) {
        console.log('FAIL: r31 SSO login completed, but the navigator was not rendered before timeout.');
        console.log('Increase R31_MAX_POLLS and inspect r31_login_timeout.png.');
    } else if (ssoSessionEstablished && !visualLoggedIn) {
        console.log('PASS: r31 SSO connection reached encrypted MUS traffic and stayed open; visual login did not complete before timeout.');
    } else if (!candyPassed) {
        console.log('FAIL: Candy catalogue still has loading placeholders after waiting for dynamic furniture casts.');
    } else {
        console.log('PASS: r31 SSO connection stayed open without the connection-failed path.');
    }

    if (!loginPassed && (!ssoSessionEstablished || visualLoggedIn)) {
        process.exitCode = 1;
    }
    if (!candyPassed) {
        process.exitCode = 1;
    }
}

main().catch(err => {
    console.error('FATAL:', err);
    process.exit(1);
});
