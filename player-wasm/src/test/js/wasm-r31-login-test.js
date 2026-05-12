#!/usr/bin/env node
'use strict';

/**
 * Headless r31 SSO login diagnostic.
 *
 * Replays the Classichabbo r31 embed params and records the browser-side MUS
 * WebSocket path. The WASM player can only reach MUS through WebSocket, so this
 * test also preflights raw TCP and WebSocket upgrade separately.
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
const dcrUrlPath = process.argv[4] || 'https://images.classichabbo.com/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?';
const outputDir = process.argv[5] || path.resolve(process.cwd(), 'frames_r31_login');

const INFO_HOST = 'verysecret.classichabbo.com';
const INFO_PORT = 30100;
const MUS_HOST = 'verysecret.classichabbo.com';
const MUS_PORT = 39101;
const GAME_WEBSOCKET_URL = `wss://${INFO_HOST}:${INFO_PORT}`;
const MUS_WEBSOCKET_URL = `wss://${MUS_HOST}:${MUS_PORT}`;
const GAME_WEBSOCKET_ENDPOINT = parseWebSocketEndpoint(GAME_WEBSOCKET_URL);
const MUS_WEBSOCKET_ENDPOINT = parseWebSocketEndpoint(MUS_WEBSOCKET_URL);
const SSO_TICKET = 'vibe-sso-admin-58fd0324-2a2f-4c49-be58-afd4c95085c5';

const MAX_POLLS = Number(process.env.R31_MAX_POLLS || 900);
const TRACE_R31 = process.env.R31_TRACE === '1';
const POLL_MS = 500;

function isAbsoluteUrl(value) {
    return /^https?:\/\//i.test(String(value || ''));
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
    const dataUrl = await page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        return canvas ? canvas.toDataURL('image/png') : null;
    });
    if (dataUrl) {
        fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'));
    }
}

function browserHtml(baseUrl) {
    return `<!doctype html>
<html><body>
<canvas id="beta-client-stage" width="960" height="540"></canvas>
<script src="${baseUrl}/shockwave-lib.js"><\/script>
<script>
var betaClientMovieUrl = "https://images.classichabbo.com/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?";
var betaClientParams = {"sw1":"client.allow.cross.domain=1;client.notify.cross.domain=0","sw2":"connection.info.host=verysecret.classichabbo.com;connection.info.port=30100","sw3":"connection.mus.host=verysecret.classichabbo.com;connection.mus.port=39101","sw4":"site.url=;url.prefix=","sw5":"client.reload.url=/client/beta?x=reauthenticate;client.fatal.error.url=/clientutils?key=error","sw6":"client.connection.failed.url=/clientutils?key=connection_failed;external.variables.txt=https://images.classichabbo.com/gamedata/external_variables.txt?","sw7":"external.texts.txt=https://images.classichabbo.com/gamedata/external_texts.txt?","sw8":"use.sso.ticket=1;sso.ticket=vibe-sso-admin-58fd0324-2a2f-4c49-be58-afd4c95085c5"};
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

    console.log('\n--- Network Preflight ---');
    const [gameTcp, gameWs, musTcp, musWs] = await Promise.all([
        tcpProbe(INFO_HOST, INFO_PORT),
        websocketUpgradeProbe(GAME_WEBSOCKET_ENDPOINT.host, GAME_WEBSOCKET_ENDPOINT.port, 5000, GAME_WEBSOCKET_ENDPOINT.secure),
        tcpProbe(MUS_HOST, MUS_PORT),
        websocketUpgradeProbe(MUS_WEBSOCKET_ENDPOINT.host, MUS_WEBSOCKET_ENDPOINT.port, 5000, MUS_WEBSOCKET_ENDPOINT.secure),
    ]);
    console.log(`Target TCP ${INFO_HOST}:${INFO_PORT}: ${gameTcp.ok ? 'OPEN' : 'FAIL'} ${gameTcp.error || ''}`);
    console.log(`Game WSS   ${GAME_WEBSOCKET_ENDPOINT.host}:${GAME_WEBSOCKET_ENDPOINT.port}: ${gameWs.ok ? 'OPEN' : 'FAIL'} ${gameWs.status || gameWs.error || ''}`);
    console.log(`MUS TCP  ${MUS_HOST}:${MUS_PORT}: ${musTcp.ok ? 'OPEN' : 'FAIL'} ${musTcp.error || ''}`);
    console.log(`MUS WSS  ${MUS_WEBSOCKET_ENDPOINT.host}:${MUS_WEBSOCKET_ENDPOINT.port}: ${musWs.ok ? 'OPEN' : 'FAIL'} ${musWs.status || musWs.error || ''}`);

    const { server, port } = await createServer();
    const baseUrl = `http://127.0.0.1:${port}`;
    const htmlPath = path.join(distPath, '_test_r31_login.html');
    fs.writeFileSync(htmlPath, browserHtml(baseUrl));

    let browser;
    let finalState = null;
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

        await page.goto(`${baseUrl}/_test_r31_login.html`, { waitUntil: 'domcontentloaded' });

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
            }

            const musLog = finalState.debugLogs.find(log => log.includes('[MUS] error') || log.includes('[MUS] close'));
            const failedPage = finalState.gotoNetPages.some(entry => entry.url.includes('connection_failed'));
            if (finalState.error || failedPage || musLog) {
                await new Promise(r => setTimeout(r, 2000));
                break;
            }
            if (finalState.spriteCount >= 70 && finalState.nonBlack > 1000) {
                await new Promise(r => setTimeout(r, 2000));
                break;
            }
        }

        await captureCanvas(page, path.join(outputDir, 'r31_login_final.png'));
    } finally {
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
    const loginPassed = finalState && finalState.loaded && wsOpened && visualLoggedIn
        && !wsFailed && !connectionFailedPage && !clientError && !finalState.error;

    console.log('\n--- Browser Results ---');
    console.log('Loaded:       ', finalState && finalState.loaded);
    console.log('Ticks:        ', finalState && finalState.tick);
    console.log('Frame:        ', finalState && finalState.frame);
    console.log('Sprites:      ', finalState && finalState.spriteCount);
    console.log('MUS Open:     ', finalState && finalState.musOpened);
    console.log('MUS Traffic:  ', finalState && `${finalState.musSendCount} sends / ${finalState.musMessageCount} messages`);
    console.log('SSO Session:  ', ssoSessionEstablished);
    console.log('Visual Login: ', visualLoggedIn);
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
    } else if (ssoSessionEstablished && !visualLoggedIn) {
        console.log('PASS: r31 SSO connection reached encrypted MUS traffic and stayed open; visual login did not complete before timeout.');
    } else {
        console.log('PASS: r31 SSO connection stayed open without the connection-failed path.');
    }

    if (!loginPassed && !ssoSessionEstablished) {
        process.exitCode = 1;
    }
}

main().catch(err => {
    console.error('FATAL:', err);
    process.exit(1);
});
