#!/usr/bin/env node
'use strict';

const fs = require('fs');
const http = require('http');
const path = require('path');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const outputDir = process.argv[3] || path.resolve(process.cwd(), 'frames_local_beta_load_budget');
const budgetMs = Number(process.env.LS_BETA_LOAD_BUDGET_MS || 60000);
const idleMs = Number(process.env.LS_BETA_LOAD_IDLE_MS || 1500);
const pollMs = Number(process.env.LS_BETA_LOAD_POLL_MS || 500);
const movieUrl = process.env.LS_BETA_CLIENT_MOVIE_URL
    || 'http://127.0.0.1/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?';

const betaClientParams = {
    'venus.websocket.mode': 'ws',
    sw1: 'client.allow.cross.domain=1;client.notify.cross.domain=0',
    sw2: 'connection.info.host=127.0.0.1;connection.info.port=30100',
    sw3: 'connection.mus.host=127.0.0.1;connection.mus.port=38201',
    sw4: 'site.url=;url.prefix=',
    sw5: 'client.reload.url=/client/beta?x=reauthenticate;client.fatal.error.url=/clientutils?key=error',
    sw6: 'client.connection.failed.url=/clientutils?key=connection_failed;external.variables.txt=http://127.0.01/gamedata/external_variables.txt?',
    sw7: 'external.texts.txt=http://127.0.0.1/gamedata/external_texts.txt?',
    sw8: 'use.sso.ticket=1;sso.ticket=venus-sso-admin-49d457ac-d365-4b13-89bb-56fd077308b5',
    sw9: 'forward.type=2;forward.id=1',
};

const MIME = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.wasm': 'application/wasm',
    '.css': 'text/css',
};

function createServer() {
    return new Promise(resolve => {
        const server = http.createServer((req, res) => {
            const urlPath = decodeURIComponent(req.url.split('?')[0]);
            if (urlPath === '/' || urlPath === '/_test_local_beta_load_budget.html') {
                const body = htmlForCase(`http://127.0.0.1:${server.address().port}`);
                res.writeHead(200, {
                    'Content-Type': 'text/html',
                    'Content-Length': Buffer.byteLength(body),
                });
                res.end(body);
                return;
            }

            const normalized = urlPath.startsWith('/') ? urlPath.substring(1) : urlPath;
            const filePath = path.join(distPath, normalized);
            if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
                const data = fs.readFileSync(filePath);
                res.writeHead(200, {
                    'Content-Type': MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream',
                    'Content-Length': data.length,
                    'Access-Control-Allow-Origin': '*',
                });
                res.end(data);
                return;
            }

            res.writeHead(404);
            res.end('Not found: ' + urlPath);
        });
        server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }));
    });
}

function htmlForCase(baseUrl) {
    return `<!doctype html>
<html><body style="margin:0;background:#000">
<canvas id="beta-client-stage" width="960" height="540"></canvas>
<script src="${baseUrl}/shockwave-lib.js"><\/script>
<script>
var _testState = {
    loaded: false,
    error: null,
    tick: 0,
    frame: 0,
    startedAt: 0,
    loadedAt: 0,
    lastNetworkActivityAt: 0,
    gotoNetPages: [],
    networkRequests: [],
    roomLandscapeRequests: [],
    sawRoomLandscapes: false
};
var betaClientMovieUrl = ${JSON.stringify(movieUrl)};
var betaClientParams = ${JSON.stringify(betaClientParams, null, 4)};
var betaClientPlayer = LibreShockwave.create("beta-client-stage", {
    basePath: "${baseUrl}/",
    autoplay: true,
    remember: false,
    debugPlayback: true,
    skipRenderDuringLoad: true,
    params: betaClientParams,
    onLoad: function(info) {
        _testState.loaded = true;
        _testState.loadedAt = performance.now();
        _testState.info = info;
        if (betaClientPlayer) betaClientPlayer.play();
    },
    onError: function(message) {
        _testState.error = String(message);
        console.error("LibreShockwave beta client error:", message);
    },
    onGotoNetPage: function(url, target) {
        _testState.gotoNetPages.push({ url: String(url), target: String(target) });
    },
    onFrame: function(frame) {
        _testState.tick++;
        _testState.frame = frame;
    },
    onDebugLog: function(log) {
        var text = String(log);
        if (/\\[WORKER\\] (fetch|relay)|\\[NetManager\\]|\\[MUS\\]|\\[MultiuserXtra\\]|preloadCasts|fetchStatus|netProvider/i.test(text)) {
            _testState.lastNetworkActivityAt = performance.now();
            console.log(text);
        }
    }
});
window.betaClientPlayer = betaClientPlayer;
window.startNativeCase = function() {
    _testState.startedAt = performance.now();
    _testState.lastNetworkActivityAt = _testState.startedAt;
    betaClientPlayer.load(betaClientMovieUrl);
};
<\/script>
</body></html>`;
}

async function loadBrowser() {
    try {
        const puppeteer = require('puppeteer');
        const browser = await puppeteer.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-setuid-sandbox'],
        });
        return { browser, newPage: () => browser.newPage() };
    } catch (e) {
        const { chromium } = require('playwright');
        const browser = await chromium.launch({ headless: true, args: ['--no-sandbox'] });
        return { browser, newPage: () => browser.newPage() };
    }
}

function parsePendingCount(bootstrap) {
    const match = String(bootstrap || '').match(/pendingRequests=(\d+)/);
    return match ? Number(match[1]) : 0;
}

function parseUndoneTasks(bootstrap) {
    return String(bootstrap || '').split('\n').filter(line => /task #/.test(line) && /done=false/.test(line));
}

async function readState(page) {
    const state = await page.evaluate(() => ({
        loaded: !!window._testState.loaded,
        error: window._testState.error,
        tick: window._testState.tick,
        frame: window._testState.frame,
        startedAt: window._testState.startedAt,
        loadedAt: window._testState.loadedAt,
        lastNetworkActivityAt: window._testState.lastNetworkActivityAt,
        now: performance.now(),
        gotoNetPages: window._testState.gotoNetPages,
        networkRequests: window._testState.networkRequests,
        roomLandscapeRequests: window._testState.roomLandscapeRequests,
        sawRoomLandscapes: !!window._testState.sawRoomLandscapes,
    }));
    const bootstrap = await page.evaluate(async () => {
        if (!window.betaClientPlayer || !window.betaClientPlayer.getBootstrapDiagnostics) return '';
        return await window.betaClientPlayer.getBootstrapDiagnostics();
    });
    const runtime = await page.evaluate(async () => {
        if (!window.betaClientPlayer || !window.betaClientPlayer.getRuntimeDiagnostics) return {};
        return await window.betaClientPlayer.getRuntimeDiagnostics();
    });
    return {
        ...state,
        bootstrap,
        runtime,
        pendingRequests: parsePendingCount(bootstrap),
        undoneTasks: parseUndoneTasks(bootstrap),
    };
}

async function main() {
    const requiredFiles = [
        path.join(distPath, 'shockwave-lib.js'),
        path.join(distPath, 'shockwave-worker.js'),
    ];
    for (const file of requiredFiles) {
        if (!fs.existsSync(file)) {
            throw new Error('Required dist file not found: ' + file + '. Run ./gradlew :player-wasm:assembleWasm first.');
        }
    }
    fs.mkdirSync(outputDir, { recursive: true });

    const { server, port } = await createServer();
    const { browser, newPage } = await loadBrowser();
    const consoleLog = [];
    let page;
    try {
        page = await newPage();
        page.on('console', msg => {
            const text = msg.text();
            consoleLog.push(text);
            console.log('  [page]', text);
        });
        page.on('request', req => {
            const url = req.url();
            if (/\.dcr\?|external_|\.cct|\.cst/i.test(url)) {
                const line = 'request ' + url;
                consoleLog.push(line);
                console.log('  [network]', url);
                page.evaluate(requestUrl => {
                    if (!window._testState) return;
                    window._testState.networkRequests.push(requestUrl);
                    window._testState.lastNetworkActivityAt = performance.now();
                    if (/hh_room_landscapes\.cct(?:\?|$)/i.test(requestUrl)) {
                        window._testState.sawRoomLandscapes = true;
                        window._testState.roomLandscapeRequests.push(requestUrl);
                    }
                }, url).catch(() => {});
            }
        });
        page.on('requestfailed', req => {
            consoleLog.push('requestfailed ' + req.url());
        });

        await page.goto(`http://127.0.0.1:${port}/_test_local_beta_load_budget.html`, { waitUntil: 'domcontentloaded' });
        await page.waitForFunction(() => window.betaClientPlayer && window.betaClientPlayer._workerReady, { timeout: 180000 });
        await page.evaluate(() => window.startNativeCase());

        const startWall = Date.now();
        let lastState = null;
        while (Date.now() - startWall <= budgetMs) {
            await new Promise(resolve => setTimeout(resolve, pollMs));
            lastState = await readState(page);
            if (lastState.error) {
                throw new Error('Player error before load budget passed: ' + lastState.error);
            }
            const elapsed = lastState.now - lastState.startedAt;
            const idleFor = lastState.now - lastState.lastNetworkActivityAt;
            console.log(`  poll elapsed=${Math.round(elapsed)}ms loaded=${lastState.loaded} tick=${lastState.tick} `
                + `frame=${lastState.frame} pending=${lastState.pendingRequests} undone=${lastState.undoneTasks.length} `
                + `landscapes=${lastState.sawRoomLandscapes} idle=${Math.round(idleFor)}ms `
                + `mus=${JSON.stringify(lastState.runtime && lastState.runtime.mus || {})}`);

            if (lastState.loaded && lastState.pendingRequests === 0
                    && lastState.undoneTasks.length === 0
                    && lastState.sawRoomLandscapes
                    && idleFor >= idleMs) {
                const result = {
                    status: 'PASS',
                    movieUrl,
                    budgetMs,
                    elapsedMs: Math.round(elapsed),
                    loadedAtMs: Math.round(lastState.loadedAt - lastState.startedAt),
                    tick: lastState.tick,
                    frame: lastState.frame,
                    sawRoomLandscapes: lastState.sawRoomLandscapes,
                    roomLandscapeRequests: lastState.roomLandscapeRequests,
                };
                fs.writeFileSync(path.join(outputDir, 'result.json'), JSON.stringify(result, null, 2) + '\n');
                fs.writeFileSync(path.join(outputDir, 'final_bootstrap.txt'), lastState.bootstrap || '');
                fs.writeFileSync(path.join(outputDir, 'runtime_diagnostics.json'), JSON.stringify(lastState.runtime || {}, null, 2) + '\n');
                fs.writeFileSync(path.join(outputDir, 'browser_console.log'), consoleLog.join('\n'));
                console.log('PASS: beta client loaded and reached network idle in ' + result.elapsedMs + 'ms');
                return;
            }
        }

        if (!lastState) {
            lastState = await readState(page);
        }
        fs.writeFileSync(path.join(outputDir, 'final_bootstrap.txt'), lastState.bootstrap || '');
        fs.writeFileSync(path.join(outputDir, 'runtime_diagnostics.json'), JSON.stringify(lastState.runtime || {}, null, 2) + '\n');
        fs.writeFileSync(path.join(outputDir, 'browser_console.log'), consoleLog.join('\n'));
        throw new Error('Beta client did not reach loaded network-idle state under '
            + budgetMs + 'ms. Last state: ' + JSON.stringify({
                loaded: lastState.loaded,
                error: lastState.error,
                tick: lastState.tick,
                frame: lastState.frame,
                pendingRequests: lastState.pendingRequests,
                sawRoomLandscapes: lastState.sawRoomLandscapes,
                runtime: lastState.runtime,
                undoneTasks: lastState.undoneTasks.slice(0, 10),
            }));
    } finally {
        if (page) await page.close();
        await browser.close();
        await new Promise(resolve => server.close(resolve));
    }
}

main().catch(err => {
    console.error(err && err.stack ? err.stack : err);
    process.exit(1);
});
