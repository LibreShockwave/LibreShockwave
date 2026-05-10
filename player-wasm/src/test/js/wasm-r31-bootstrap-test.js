#!/usr/bin/env node
'use strict';

/**
 * Headless browser regression for r31 bootstrap movies.
 *
 * Reproduces the embed pattern that used to stay black:
 *   shockwave-lib.js -> worker file -> WASM
 * with an onLoad callback that calls play() explicitly.
 *
 * Args: distPath htdocsRoot dcrUrlPath [outputDir]
 */

const http = require('http');
const fs = require('fs');
const path = require('path');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const htdocsRoot = process.argv[3] || 'C:/xampp/htdocs';
const dcrUrlPath = process.argv[4] || '/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr';
const outputDir = process.argv[5] || path.resolve(process.cwd(), 'frames_r31_bootstrap');

const MAX_POLLS = 180;
const POLL_MS = 250;
const SPRITE_THRESHOLD = 1;
const NON_BLACK_THRESHOLD = 100;
const COLOR_BUCKET_THRESHOLD = 5;
const LOG_PREVIEW_LIMIT = 1200;

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

function preview(text) {
    text = String(text);
    return text.length > LOG_PREVIEW_LIMIT
        ? text.slice(0, LOG_PREVIEW_LIMIT) + `... <truncated ${text.length - LOG_PREVIEW_LIMIT} chars>`
        : text;
}

function createServer() {
    return new Promise((resolve) => {
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

        server.listen(0, '127.0.0.1', () => {
            resolve({ server, port: server.address().port });
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
        const executablePath = process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe';
        const browser = await chromium.launch({
            headless: true,
            executablePath,
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
    if (!dataUrl) return;
    fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'));
}

async function main() {
    console.log('=== WASM r31 Bootstrap Test ===');
    console.log('Dist:   ', distPath);
    console.log('Htdocs: ', htdocsRoot);
    console.log('DCR:    ', dcrUrlPath);
    console.log('Output: ', outputDir);

    if (!fs.existsSync(path.join(distPath, 'shockwave-lib.js'))) {
        console.error('FAIL: shockwave-lib.js not found in dist. Run assembleWasm first.');
        process.exit(1);
    }

    const dcrFile = path.join(htdocsRoot, dcrUrlPath.replace(/^\/+/, ''));
    if (!fs.existsSync(dcrFile)) {
        console.error('FAIL: DCR file not found: ' + dcrFile);
        process.exit(1);
    }

    fs.mkdirSync(outputDir, { recursive: true });

    const { server, port } = await createServer();
    const baseUrl = `http://127.0.0.1:${port}`;
    const movieUrl = `${baseUrl}${dcrUrlPath}?`;

    const html = `<!doctype html>
<html><body>
<canvas id="beta-client-stage" width="720" height="540"></canvas>
<script src="${baseUrl}/shockwave-lib.js"><\/script>
<script>
var betaClientMovieUrl = "${movieUrl}";
var betaClientParams = {
    "sw1":"client.allow.cross.domain=1;client.notify.cross.domain=0",
    "sw2":"connection.info.host=127.0.0.1;connection.info.port=30000",
    "sw3":"connection.mus.host=127.0.0.1;connection.mus.port=38101",
    "sw4":"site.url=;url.prefix=",
    "sw5":"client.reload.url=/client/beta?x=reauthenticate;client.fatal.error.url=/clientutils?key=error",
    "sw6":"client.connection.failed.url=/clientutils?key=connection_failed;external.variables.txt=${baseUrl}/gamedata/external_variables.txt?",
    "sw7":"external.texts.txt=${baseUrl}/gamedata/external_texts.txt?",
    "sw8":"use.sso.ticket=1;sso.ticket=vibe-sso-admin-ef39b7ab-e16b-4ea5-b83e-c8ee6a8a7076"
};
var _testState = { loaded: false, error: null, tick: 0, frame: 0, debugLogs: [] };
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
            if (betaClientPlayer) {
                betaClientPlayer.play();
            }
        },
        onError: function(message) {
            _testState.error = message;
            console.error("LibreShockwave beta client error:", message);
        },
        onFrame: function(frame) {
            _testState.tick++;
            _testState.frame = frame;
        },
        onDebugLog: function(log) {
            _testState.debugLogs.push(log);
            if (log.indexOf('[ScriptError]') >= 0 || log.indexOf('[CastLib]') >= 0 ||
                    log.indexOf('castDataRequestCallback') >= 0 || log.indexOf('[Lifecycle]') >= 0 ||
                    log.indexOf('[TRACE]') >= 0 || log.indexOf('[MUS]') >= 0 ||
                    log.indexOf('[NetManager]') >= 0) {
                console.log(log);
            }
        }
    });
    window.betaClientPlayer = betaClientPlayer;
    [
        'lifecycle',
        'prepareMovie',
        'initializeAndRun',
        'getV',
        'constructObjectManager',
        'createObject',
        'constructConnectionManager',
        'connect',
        'createVisualThread',
        'createVisualizer',
        'showProgram',
        'changeRoom',
        'enterRoom',
        'assetDownloadCallbacks',
        'importFileToCast',
        'removeActiveTask',
        'updateState',
        'prepareFrame',
        'receiveUpdate',
        'updateQueue',
        'update',
        'registerDownloadCallback',
        'startCastLoad',
        'loadVariables',
        'LoadTexts'
    ].forEach(function(name) { betaClientPlayer.addTraceHandler(name); });
    betaClientPlayer.load(betaClientMovieUrl);
} catch (error) {
    _testState.error = String(error);
    console.error("LibreShockwave beta client error:", error);
}
<\/script>
</body></html>`;

    const htmlPath = path.join(distPath, '_test_r31_bootstrap.html');
    fs.writeFileSync(htmlPath, html);

    let browser;
    let page;
    try {
        const loaded = await loadBrowser();
        browser = loaded.browser;
        page = await loaded.newPage();

        page.on('console', msg => {
            const text = msg.text();
            if (text.includes('[LS]') || text.includes('[WORKER]') || text.includes('LibreShockwave')) {
                console.log('  [page] ' + preview(text));
            }
        });

        await page.goto(`${baseUrl}/_test_r31_bootstrap.html`, { waitUntil: 'domcontentloaded' });

        let finalState = null;
        for (let i = 0; i < MAX_POLLS; i++) {
            await new Promise(r => setTimeout(r, POLL_MS));
            const state = await page.evaluate(() => {
                const canvas = document.getElementById('beta-client-stage');
                const ctx = canvas.getContext('2d');
                const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
                let nonBlack = 0;
                const colorBuckets = new Set();
                for (let p = 0; p < data.length; p += 40) {
                    const r = data[p], g = data[p + 1], b = data[p + 2];
                    if (r > 10 || g > 10 || b > 10) nonBlack++;
                    colorBuckets.add((r >> 5) + ',' + (g >> 5) + ',' + (b >> 5));
                }
                return {
                    loaded: window._testState.loaded,
                    error: window._testState.error,
                    tick: window._testState.tick,
                    frame: window._testState.frame,
                    debugLogs: window._testState.debugLogs.slice(-20),
                    spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
                    nonBlack,
                    colorBuckets: colorBuckets.size,
                };
            });

            finalState = state;
            if (i % 20 === 0) {
                console.log(`  poll=${i} loaded=${state.loaded} tick=${state.tick} frame=${state.frame} ` +
                    `sprites=${state.spriteCount} nonBlack=${state.nonBlack} colors=${state.colorBuckets}`);
            }

            if (state.error) {
                console.error('FAIL: player error: ' + preview(state.error));
                break;
            }
            if (state.spriteCount >= SPRITE_THRESHOLD &&
                    state.nonBlack > NON_BLACK_THRESHOLD &&
                    state.colorBuckets >= COLOR_BUCKET_THRESHOLD) {
                break;
            }
        }

        await captureCanvas(page, path.join(outputDir, 'r31_bootstrap_final.png'));

        console.log('\n--- Results ---');
        console.log('Loaded:      ', finalState && finalState.loaded);
        console.log('Ticks:       ', finalState && finalState.tick);
        console.log('Frame:       ', finalState && finalState.frame);
        console.log('Sprites:     ', finalState && finalState.spriteCount);
        console.log('Non-black:   ', finalState && finalState.nonBlack);
        console.log('Color buckets:', finalState && finalState.colorBuckets);
        if (finalState && finalState.debugLogs && finalState.debugLogs.length > 0) {
            console.log('\n--- Debug Log Tail ---');
            finalState.debugLogs.forEach(log => console.log(preview(log)));
        }

        const passed = finalState && finalState.loaded && !finalState.error &&
            finalState.spriteCount >= SPRITE_THRESHOLD &&
            finalState.nonBlack > NON_BLACK_THRESHOLD &&
            finalState.colorBuckets >= COLOR_BUCKET_THRESHOLD;

        if (passed) {
            console.log('\nPASS: r31 bootstrap rendered non-black content');
        } else {
            console.log('\nFAIL: r31 bootstrap stayed blank/black');
            process.exitCode = 1;
        }
    } finally {
        if (browser) await browser.close();
        server.close();
        try { fs.unlinkSync(htmlPath); } catch (e) {}
    }
}

main().catch(err => {
    console.error('FATAL:', err);
    process.exit(1);
});
