#!/usr/bin/env node
'use strict';

const fs = require('fs');
const http = require('http');
const path = require('path');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const outputDir = process.argv[3] || path.resolve(process.cwd(), 'frames_native_visual');
const referenceDir = process.env.LS_NATIVE_REFERENCE_DIR || '/opt/git/v14_v31_compare';
const v14Root = process.env.LS_V14_HTDOCS_ROOT || '/opt/git/Kepler-www';
const v1AssetsRoot = process.env.LS_V1_ASSETS_ROOT || '/opt/git/v1_assets/projectorrays_lingo';
const cases = (process.env.LS_NATIVE_VISUAL_CASES || 'v1,v14,v31')
    .split(',')
    .map(value => value.trim().toLowerCase())
    .filter(Boolean);
const traceHandlers = readListEnv('LS_TRACE_HANDLERS');

const V31_BETA_CLIENT_MOVIE_URL =
    'https://images.classichabbo.com/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?';
const V31_BETA_CLIENT_PARAMS = {
    'websocket.mode': 'ws',
    sw1: 'client.allow.cross.domain=1;client.notify.cross.domain=0',
    sw2: 'connection.info.host=verysecret.classichabbo.com;connection.info.port=30100',
    sw3: 'connection.mus.host=verysecret.classichabbo.com;connection.mus.port=38201',
    sw4: 'site.url=;url.prefix=',
    sw5: 'client.reload.url=/client/beta?x=reauthenticate;client.fatal.error.url=/clientutils?key=error',
    sw6: 'client.connection.failed.url=/clientutils?key=connection_failed;external.variables.txt=https://images.classichabbo.com/gamedata/external_variables.txt?',
    sw7: 'external.texts.txt=https://images.classichabbo.com/gamedata/external_texts.txt?',
    sw8: 'use.sso.ticket=1;sso.ticket=vibe-sso-admin-504d3ba4-acdb-4436-b67c-d0752f44f767',
};

const V1 = {
    name: 'v1',
    width: 719,
    height: 540,
    nativePath: path.join(referenceDir, 'v1_native.png'),
    movieUrl: 'http://192.168.122.1/dcr0910/loader.dcr',
    params: {},
    initialMovieProperties: readJsonEnv('LS_V1_INITIAL_MOVIE_PROPERTIES', {}),
    waitText: "Haven't got a Habbo yet?",
    maxPolls: Number(process.env.LS_V1_NATIVE_MAX_POLLS || 900),
    pollMs: 250,
    settleMs: Number(process.env.LS_V1_NATIVE_SETTLE_MS || 1000),
    maxMeanDelta: Number(process.env.LS_V1_NATIVE_MAX_MEAN_DELTA || 38),
    maxBadFraction: Number(process.env.LS_V1_NATIVE_MAX_BAD_FRACTION || 0.28),
    guardRegions: [
        { name: 'cloud_left', x: 57, y: 131, width: 43, height: 42 },
        { name: 'cloud_mid', x: 629, y: 141, width: 43, height: 42 },
        { name: 'cloud_spike', x: 476, y: 84, width: 43, height: 42 },
        { name: 'cloud_left_exact_weak', x: 52, y: 134, width: 43, height: 42 },
        { name: 'cloud_mid_exact_weak', x: 624, y: 139, width: 43, height: 42 },
        { name: 'cloud_left_exact_strong', x: 57, y: 131, width: 43, height: 42 },
        { name: 'cloud_mid_exact_strong', x: 629, y: 141, width: 43, height: 42 },
        { name: 'cloud_spike_exact', x: 476, y: 84, width: 6, height: 5 },
        { name: 'car_left', x: 292, y: 425, width: 40, height: 35 },
        { name: 'car_right', x: 586, y: 417, width: 40, height: 35 },
        { name: 'hotel_flag', x: 42, y: 226, width: 40, height: 34 },
        { name: 'hotel_flag_exact', x: 52, y: 236, width: 20, height: 14 },
        { name: 'register_text_stack', x: 446, y: 132, width: 196, height: 44 },
        { name: 'login_text_stack', x: 446, y: 240, width: 196, height: 108 },
        { name: 'register_heading_text', x: 433, y: 124, width: 224, height: 10 },
        { name: 'register_copy_text', x: 457, y: 155, width: 175, height: 24 },
        { name: 'login_heading_text', x: 435, y: 236, width: 224, height: 10 },
        { name: 'login_name_label_text', x: 457, y: 266, width: 175, height: 12 },
        { name: 'login_password_label_text', x: 457, y: 320, width: 175, height: 12 },
        { name: 'forgot_password_text', x: 446, y: 416, width: 206, height: 10 },
    ],
};

const V14 = {
    name: 'v14',
    width: 720,
    height: 540,
    nativePath: path.join(referenceDir, 'v14_native.png'),
    movieUrl: 'http://192.168.122.1/dcr/14.1_b8/habbo.dcr',
    params: {
        bgColor: '#000000',
        sw1: 'site.url=http://www.habbo.co.uk;url.prefix=http://www.habbo.co.uk',
        sw2: 'connection.info.host=192.168.122.1;connection.info.port=12321',
        sw3: 'client.reload.url=http://192.168.122.1/',
        sw4: 'connection.mus.host=192.168.122.1;connection.mus.port=12322',
        sw5: 'external.variables.txt=http://192.168.122.1/dcr/14.1_b8/external_variables.txt;external.texts.txt=http://192.168.122.1/dcr/14.1_b8/external_texts.txt',
        swRemote: "swSaveEnabled='true' swVolume='true' swRestart='false' swPausePlay='false' swFastForward='false' swTitle='Habbo Hotel' swContextMenu='true'",
        swStretchStyle: 'none',
        swText: '',
    },
    initialBuiltinSymbols: {
        'connection.info.id': 'info',
        'connection.room.id': 'room',
    },
    initialMovieProperties: readJsonEnv('LS_V14_INITIAL_MOVIE_PROPERTIES', {}),
    waitText: "Haven't got a Habbo yet?",
    maxPolls: Number(process.env.LS_V14_NATIVE_MAX_POLLS || 360),
    pollMs: 250,
    maxMeanDelta: Number(process.env.LS_V14_NATIVE_MAX_MEAN_DELTA || 38),
    maxBadFraction: Number(process.env.LS_V14_NATIVE_MAX_BAD_FRACTION || 0.28),
};

const V31 = {
    name: 'v31',
    width: 961,
    height: 540,
    runtimeWidth: 960,
    runtimeHeight: 540,
    nativePath: path.join(referenceDir, 'v31_native.png'),
    enteredPath: path.join('/opt/git/v31_room_load', 'v31_native_rooms_entered.png'),
    movieUrl: V31_BETA_CLIENT_MOVIE_URL,
    params: V31_BETA_CLIENT_PARAMS,
    wait: 'navigator',
    initialMovieProperties: readJsonEnv('LS_V31_INITIAL_MOVIE_PROPERTIES', {}),
    maxPolls: Number(process.env.LS_V31_NATIVE_MAX_POLLS || 900),
    pollMs: 500,
    maxMeanDelta: Number(process.env.LS_V31_NATIVE_MAX_MEAN_DELTA || 42),
    maxBadFraction: Number(process.env.LS_V31_NATIVE_MAX_BAD_FRACTION || 0.32),
    enforceFullFrame: false,
    minNavigatorSprites: Number(process.env.LS_V31_NATIVE_NAV_MIN_SPRITES || 40),
    guardRegions: [
        {
            name: 'navigator_window',
            x: 585,
            y: 20,
            width: 363,
            height: 453,
            maxMeanDelta: Number(process.env.LS_V31_NATIVE_NAV_MAX_MEAN_DELTA || 1.0),
            maxBadFraction: Number(process.env.LS_V31_NATIVE_NAV_MAX_BAD_FRACTION || 0.01),
        },
    ],
};

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

function readJsonEnv(name, fallback) {
    const raw = process.env[name];
    if (!raw || !raw.trim()) {
        return fallback;
    }
    try {
        return JSON.parse(raw);
    } catch (error) {
        throw new Error(`${name} must contain JSON: ${error.message}`);
    }
}

function readListEnv(name) {
    return (process.env[name] || '')
        .split(',')
        .map(value => value.trim())
        .filter(Boolean);
}

function serveFile(res, filePath) {
    const data = readFixtureFile(filePath);
    res.writeHead(200, {
        'Content-Type': MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream',
        'Content-Length': data.length,
        'Access-Control-Allow-Origin': '*',
    });
    res.end(data);
}

function readFixtureFile(filePath) {
    let data = fs.readFileSync(filePath);
    const baseName = path.basename(filePath).toLowerCase();
    if (baseName === 'external_variables.txt') {
        let text = data.toString('utf8');
        const defaults = {
            'client.debug.level': '0',
            'loading.bar.active': '1',
            'net.operation.count': '20',
        };
        const missing = [];
        for (const [key, value] of Object.entries(defaults)) {
            const pattern = new RegExp(`^${key.replace(/[.*+?^${}()|[\]\\]/g, '\\$&')}=`, 'm');
            if (!pattern.test(text)) {
                missing.push(`${key}=${value}`);
            }
        }
        if (missing.length > 0) {
            text = `${missing.join('\n')}\n${text}`;
        }
        text = directorReturnLineEndings(text);
        data = Buffer.from(text, 'utf8');
    } else if (baseName === 'external_texts.txt') {
        data = Buffer.from(directorReturnLineEndings(data.toString('utf8')), 'utf8');
    }
    return data;
}

function directorReturnLineEndings(text) {
    return text.replace(/\r\n/g, '\n').replace(/\r/g, '\n').replace(/\n/g, '\r');
}

function createServer() {
    return new Promise(resolve => {
        const server = http.createServer((req, res) => {
            const urlPath = decodeURIComponent(req.url.split('?')[0]);
            if (urlPath.startsWith('/__native_proxy/')) {
                return proxyHttpFixture(req, res);
            }
            const candidates = [
                path.join(distPath, urlPath),
                path.join(referenceDir, path.basename(urlPath)),
                path.join(path.dirname(V31.enteredPath), path.basename(urlPath)),
                path.join(v14Root, urlPath.replace(/^\/+/, '')),
                path.join(v14Root, 'dcr/14.1_b8', path.basename(urlPath)),
                path.join(v14Root, 'gamedata', path.basename(urlPath)),
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

function proxyHttpFixture(req, res) {
    const targetUrl = 'http://' + req.url.substring('/__native_proxy/'.length);
    const localV1Path = resolveLocalV1ProxyPath(targetUrl);
    if (localV1Path) {
        return serveFile(res, localV1Path);
    }
    http.get(targetUrl, proxyRes => {
        const chunks = [];
        proxyRes.on('data', chunk => chunks.push(chunk));
        proxyRes.on('end', () => {
            const body = Buffer.concat(chunks);
            res.writeHead(proxyRes.statusCode || 502, {
                'Content-Type': proxyRes.headers['content-type'] || 'application/octet-stream',
                'Content-Length': body.length,
                'Access-Control-Allow-Origin': '*',
            });
            res.end(body);
        });
    }).on('error', err => {
        res.writeHead(502, {
            'Content-Type': 'text/plain',
            'Access-Control-Allow-Origin': '*',
        });
        res.end(`Could not proxy ${targetUrl}: ${err.message}`);
    });
}

function resolveLocalV1ProxyPath(targetUrl) {
    let parsed;
    try {
        parsed = new URL(targetUrl);
    } catch (error) {
        return null;
    }
    const baseName = path.basename(parsed.pathname);
    const stem = baseName.replace(/\.(dcr|dir|cct|cst)$/i, '');
    const extension = baseName.toLowerCase().endsWith('.cct') || baseName.toLowerCase().endsWith('.cst') ? '.cst' : '.dir';
    const candidates = [
        path.join(v1AssetsRoot, `${stem}${extension}`),
        path.join(v1AssetsRoot, stem, `${stem}${extension}`),
        path.join(v1AssetsRoot, 'projectorrays_lingo', `${stem}${extension}`),
        path.join(v1AssetsRoot, 'projectorrays_lingo', stem, `${stem}${extension}`),
    ];
    const candidate = candidates.find(candidatePath => fs.existsSync(candidatePath) && fs.statSync(candidatePath).isFile());
    return candidate || null;
}

async function loadBrowser() {
    try {
        const puppeteer = require('puppeteer');
        const browser = await puppeteer.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-setuid-sandbox'],
        });
        return { browser, newPage: () => browser.newPage(), isPuppeteer: true };
    } catch (e) {
        const { chromium } = require('playwright');
        const browser = await chromium.launch({
            headless: true,
            executablePath: process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe',
            args: ['--no-sandbox'],
        });
        return { browser, newPage: () => browser.newPage(), isPuppeteer: false };
    }
}

function assertLoaderMarkdownMatches() {
    const loaderPath = path.join(referenceDir, 'loader.md');
    const text = fs.readFileSync(loaderPath, 'utf8');
    const v1MovieUrl = extract(text, /v1_native\.png output below\s*(https?:\/\/\S+)/, 'v1 movie URL');
    const v14Section = extract(text, /v14_native\.png output below([\s\S]*)$/m, 'v14 loader section');
    const v14Size = extract(v14Section, /id="habbo" width="(\d+)" height="(\d+)"/i, 'v14 object size', 0);
    const v14Width = Number(extract(v14Size, /width="(\d+)"/i, 'v14 width'));
    const v14Height = Number(extract(v14Size, /height="(\d+)"/i, 'v14 height'));
    const v14MovieUrl = extract(v14Section, /<param name="src" value="([^"]+)">/i, 'v14 movie URL');
    const v14Params = {};
    for (const match of v14Section.matchAll(/<param name="([^"]+)" value="([^"]*)">/gi)) {
        if (match[1] !== 'src') {
            v14Params[match[1]] = match[2];
        }
    }

    const v31MovieUrl = extract(text, /var betaClientMovieUrl = "([^"]+)";/, 'v31 movie URL');
    const v31Params = JSON.parse(extract(text, /var betaClientParams = (\{[^\n]+\});/, 'v31 params JSON'));

    assertEqual('v1 movie URL', V1.movieUrl, v1MovieUrl);
    assertEqual('v14 width', V14.width, v14Width);
    assertEqual('v14 height', V14.height, v14Height);
    assertEqual('v14 movie URL', V14.movieUrl, v14MovieUrl);
    assertObjectEqual('v14 params', V14.params, v14Params);
    assertEqual('v31 movie URL', V31.movieUrl, v31MovieUrl);
    assertObjectEqual('v31 params', V31.params, v31Params);
}

function extract(text, pattern, label, group = 1) {
    const match = text.match(pattern);
    if (!match) {
        throw new Error(`loader.md is missing ${label}`);
    }
    return match[group];
}

function assertEqual(label, actual, expected) {
    if (actual !== expected) {
        throw new Error(`${label} does not match loader.md: got ${JSON.stringify(actual)}, expected ${JSON.stringify(expected)}`);
    }
}

function assertObjectEqual(label, actual, expected) {
    const actualJson = JSON.stringify(sortObject(actual));
    const expectedJson = JSON.stringify(sortObject(expected));
    if (actualJson !== expectedJson) {
        throw new Error(`${label} do not match loader.md:\nactual=${actualJson}\nexpected=${expectedJson}`);
    }
}

function sortObject(value) {
    return Object.keys(value).sort().reduce((acc, key) => {
        acc[key] = value[key];
        return acc;
    }, {});
}

function assertFiles() {
    for (const filePath of [
        path.join(distPath, 'shockwave-lib.js'),
        path.join(distPath, 'shockwave-worker.js'),
        V1.nativePath,
        V14.nativePath,
        V31.nativePath,
        V31.enteredPath,
        path.join(v14Root, 'dcr/14.1_b8/habbo.dcr'),
    ]) {
        if (!fs.existsSync(filePath)) {
            throw new Error(`Required file not found: ${filePath}`);
        }
    }
}

function jsString(value) {
    return JSON.stringify(String(value));
}

function legacyFetchRewriteScript(baseUrl, fixture) {
    if (fixture.name !== 'v1' && fixture.name !== 'v14') {
        return '';
    }
    return `<script>
(function() {
    var baseUrl = ${jsString(baseUrl)};
    var originalFetch = window.fetch.bind(window);
    window.fetch = function(input, init) {
        var url = typeof input === 'string' ? input : (input && input.url) || '';
        if (url.indexOf('http://192.168.122.1/') === 0) {
            var rewritten = ${fixture.name === 'v1' ? "baseUrl + '/__native_proxy/' + url.substring('http://'.length)" : "baseUrl + '/' + url.substring('http://192.168.122.1/'.length)"};
            return originalFetch(rewritten, init);
        }
        return originalFetch(input, init);
    };
})();
<\/script>`;
}

function htmlForCase(baseUrl, fixture) {
    return `<!doctype html>
<html><body style="margin:0;background:#000">
<canvas id="beta-client-stage" width="${fixture.runtimeWidth || fixture.width}" height="${fixture.runtimeHeight || fixture.height}"></canvas>
${legacyFetchRewriteScript(baseUrl, fixture)}
<script src="${baseUrl}/shockwave-lib.js"><\/script>
<script>
var _testState = {
    loaded: false,
    error: null,
    tick: 0,
    frame: 0,
    gotoNetPages: [],
    debugLogs: [],
    lastNetworkActivityAt: 0
};
var betaClientMovieUrl = ${jsString(fixture.movieUrl)};
var betaClientParams = ${JSON.stringify(fixture.params, null, 4)};
var betaClientPlayer = LibreShockwave.create("beta-client-stage", {
    basePath: "${baseUrl}/",
    autoplay: true,
    remember: false,
    debugPlayback: true,
    params: betaClientParams,
    initialBuiltinSymbols: ${JSON.stringify(fixture.initialBuiltinSymbols || {})},
    initialMovieProperties: ${JSON.stringify(fixture.initialMovieProperties || {})},
    onLoad: function(info) {
        _testState.loaded = true;
        _testState.info = info;
        _testState.lastNetworkActivityAt = Date.now();
        if (betaClientPlayer) betaClientPlayer.play();
    },
    onError: function(message) {
        _testState.error = String(message);
        console.error("LibreShockwave beta client error:", message);
    },
    onGotoNetPage: function(url, target) {
        _testState.gotoNetPages.push({ url: String(url), target: String(target) });
        _testState.lastNetworkActivityAt = Date.now();
    },
    onFrame: function(frame) {
        _testState.tick++;
        _testState.frame = frame;
    },
    onDebugLog: function(log) {
        var text = String(log);
        _testState.debugLogs.push(text);
        if (_testState.debugLogs.length > 2000) {
            _testState.debugLogs.splice(0, _testState.debugLogs.length - 2000);
        }
        if (text.indexOf('[WORKER] fetch') >= 0
                || text.indexOf('[WORKER] relay') >= 0
                || text.indexOf('[WORKER] preloadCasts') >= 0
                || text.indexOf('[MUS]') >= 0
                || text.indexOf('[NetManager]') >= 0) {
            _testState.lastNetworkActivityAt = Date.now();
        }
        if (text.indexOf('[ScriptError]') >= 0 || text.indexOf('[NetManager]') >= 0 || text.indexOf('[TRACE]') >= 0) {
            console.log(text);
        }
    }
});
window.betaClientPlayer = betaClientPlayer;
${JSON.stringify(traceHandlers)}.forEach(function(name) { betaClientPlayer.addTraceHandler(name); });
window.startNativeVisualCase = function() {
    betaClientPlayer.load(betaClientMovieUrl);
};
<\/script>
</body></html>`;
}

async function installV14RequestMap(page) {
    await page.setRequestInterception(true);
    page.on('request', request => {
        const url = request.url();
        if (!url.startsWith('http://192.168.122.1/') && !url.startsWith('http://localhost/')) {
            request.continue();
            return;
        }
        const parsed = new URL(url);
        const candidates = [
            path.join(v14Root, parsed.pathname.replace(/^\/+/, '')),
            path.join(v14Root, 'dcr/14.1_b8', path.basename(parsed.pathname)),
            path.join(v14Root, 'gamedata', path.basename(parsed.pathname)),
        ];
        const filePath = candidates.find(candidate => fs.existsSync(candidate) && fs.statSync(candidate).isFile());
        if (!filePath) {
            request.respond({ status: 404, body: `No v14 fixture for ${parsed.pathname}` });
            return;
        }
        request.respond({
            status: 200,
            contentType: MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream',
            body: readFixtureFile(filePath),
            headers: { 'Access-Control-Allow-Origin': '*' },
        });
    });
}

async function waitForWorkerReady(page) {
    await page.waitForFunction(() => window.betaClientPlayer && window.betaClientPlayer._workerReady, {
        timeout: 120000,
    });
}

function v14LoginEvidenceReady(evidence) {
    return !!evidence
        && evidence.firstPanelWhite >= 9000
        && evidence.firstPanelBlue >= 5000
        && evidence.firstTextBlack >= 650
        && evidence.loginPanelWhite >= 25000
        && evidence.loginPanelBlue >= 12000;
}

function v1LoginEvidenceReady(evidence) {
    return !!evidence
        && evidence.topLogoNonBlack >= 1500
        && evidence.heroNonBlack >= 20000
        && evidence.firstPanelWhite >= 2000
        && evidence.firstPanelBlue >= 2000
        && evidence.firstPanelBlack >= 500
        && evidence.loginPanelWhite >= 11000
        && evidence.loginPanelBlue >= 12000;
}

async function waitForV1Login(page) {
    let lastState = null;
    let lastEvidence = null;
    let lastBootstrapDiagnostics = '';
    for (let i = 0; i < V1.maxPolls; i++) {
        await new Promise(resolve => setTimeout(resolve, V1.pollMs));
        const result = await page.evaluate(async expectedText => {
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
            function countRegion(x, y, w, h) {
                const region = ctx.getImageData(x, y, w, h).data;
                let black = 0;
                let white = 0;
                let blue = 0;
                let nonBlackRegion = 0;
                for (let p = 0; p < region.length; p += 4) {
                    const r = region[p], g = region[p + 1], b = region[p + 2];
                    if (r < 35 && g < 35 && b < 35) black++;
                    if (r > 235 && g > 235 && b > 235) white++;
                    if (r > 60 && r < 160 && g > 100 && g < 190 && b > 130 && b < 215) blue++;
                    if (r > 10 || g > 10 || b > 10) nonBlackRegion++;
                }
                return { black, white, blue, nonBlack: nonBlackRegion };
            }
            const diagnostics = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
                ? await window.betaClientPlayer.getVisibleTextDiagnostics()
                : '';
            const topLogo = countRegion(18, 18, 230, 120);
            const hero = countRegion(0, 100, 360, 350);
            const firstPanel = countRegion(437, 109, 214, 95);
            const loginPanel = countRegion(437, 225, 214, 220);
            const footer = countRegion(8, 500, 220, 25);
            return {
                state: {
                    loaded: window._testState.loaded,
                    error: window._testState.error,
                    tick: window._testState.tick,
                    frame: window._testState.frame,
                    spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
                    nonBlack,
                    colorBuckets: buckets.size,
                    hasExpectedText: diagnostics.indexOf(expectedText) >= 0,
                    gotoNetPages: window._testState.gotoNetPages.slice(),
                    debugLogs: window._testState.debugLogs.slice(-80),
                },
                evidence: {
                    topLogoNonBlack: topLogo.nonBlack,
                    heroNonBlack: hero.nonBlack,
                    firstPanelWhite: firstPanel.white,
                    firstPanelBlue: firstPanel.blue,
                    firstPanelBlack: firstPanel.black,
                    loginPanelWhite: loginPanel.white,
                    loginPanelBlue: loginPanel.blue,
                    footerWhite: footer.white,
                },
                diagnostics,
            };
        }, V1.waitText);
        lastState = result.state;
        lastEvidence = result.evidence;
        if (i % 40 === 0) {
            lastBootstrapDiagnostics = await page.evaluate(async () => {
                return window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
                    ? await window.betaClientPlayer.getBootstrapDiagnostics()
                    : '';
            });
        }
        if (i % 20 === 0) {
            console.log(`  v1 poll=${i} loaded=${lastState.loaded} tick=${lastState.tick} frame=${lastState.frame} ` +
                `sprites=${lastState.spriteCount} evidence=${JSON.stringify(lastEvidence)}`);
        }
        if (lastState.error) break;
        if (lastState.loaded && v1LoginEvidenceReady(lastEvidence) &&
                lastState.nonBlack > 2000 && lastState.colorBuckets > 30) {
            await new Promise(resolve => setTimeout(resolve, V1.settleMs));
            return { state: lastState, evidence: lastEvidence };
        }
    }
    lastBootstrapDiagnostics = await page.evaluate(async () => {
        return window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
            ? await window.betaClientPlayer.getBootstrapDiagnostics()
            : '';
    });
    throw new Error('v1 login screen was not visible before timeout. Last state: ' +
        JSON.stringify(lastState) + ' evidence=' + JSON.stringify(lastEvidence) +
        '\nBootstrap diagnostics:\n' + lastBootstrapDiagnostics +
        '\nDebug logs:\n' + ((lastState && lastState.debugLogs) || []).join('\n'));
}

async function waitForV14Login(page) {
    let lastState = null;
    let lastTextDiagnostics = '';
    let lastBootstrapDiagnostics = '';
    let lastEvidence = null;
    for (let i = 0; i < V14.maxPolls; i++) {
        await new Promise(resolve => setTimeout(resolve, V14.pollMs));
        const result = await page.evaluate(async expectedText => {
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
            const diagnostics = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
                ? await window.betaClientPlayer.getVisibleTextDiagnostics()
                : '';
            function countRegion(x, y, w, h) {
                const region = ctx.getImageData(x, y, w, h).data;
                let black = 0;
                let white = 0;
                let blue = 0;
                for (let p = 0; p < region.length; p += 4) {
                    const r = region[p], g = region[p + 1], b = region[p + 2];
                    if (r < 35 && g < 35 && b < 35) black++;
                    if (r > 235 && g > 235 && b > 235) white++;
                    if (r > 80 && r < 140 && g > 130 && g < 180 && b > 145 && b < 190) blue++;
                }
                return { black, white, blue };
            }
            const firstPanel = countRegion(455, 100, 190, 90);
            const firstText = countRegion(470, 142, 160, 24);
            const loginPanel = countRegion(445, 229, 212, 220);
            return {
                state: {
                    loaded: window._testState.loaded,
                    error: window._testState.error,
                    tick: window._testState.tick,
                    frame: window._testState.frame,
                    spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
                    nonBlack,
                    colorBuckets: buckets.size,
                    hasExpectedText: diagnostics.indexOf(expectedText) >= 0,
                    debugLogs: window._testState.debugLogs.slice(-80),
                },
                evidence: {
                    firstPanelWhite: firstPanel.white,
                    firstPanelBlue: firstPanel.blue,
                    firstTextBlack: firstText.black,
                    loginPanelWhite: loginPanel.white,
                    loginPanelBlue: loginPanel.blue,
                },
                diagnostics,
            };
        }, V14.waitText);
        lastState = result.state;
        lastEvidence = result.evidence;
        lastTextDiagnostics = result.diagnostics;
        if (i % 40 === 0) {
            lastBootstrapDiagnostics = await page.evaluate(async () => {
                return window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
                    ? await window.betaClientPlayer.getBootstrapDiagnostics()
                    : '';
            });
        }
        if (i % 20 === 0) {
            console.log(`  v14 poll=${i} loaded=${lastState.loaded} tick=${lastState.tick} frame=${lastState.frame} ` +
                `sprites=${lastState.spriteCount} text=${lastState.hasExpectedText} evidence=${JSON.stringify(lastEvidence)}`);
        }
        if (lastState.error) break;
        if (lastState.loaded && (lastState.hasExpectedText || v14LoginEvidenceReady(lastEvidence)) &&
                lastState.nonBlack > 2000 && lastState.colorBuckets > 30) {
            await new Promise(resolve => setTimeout(resolve, 1000));
            return { state: lastState, evidence: lastEvidence, textDiagnostics: lastTextDiagnostics };
        }
    }
    lastBootstrapDiagnostics = await page.evaluate(async () => {
        return window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
            ? await window.betaClientPlayer.getBootstrapDiagnostics()
            : '';
    });
    throw new Error('v14 login screen text was not visible before timeout. Last state: ' +
        JSON.stringify(lastState) + ' evidence=' + JSON.stringify(lastEvidence) +
        '\nVisible text:\n' + lastTextDiagnostics +
        '\nBootstrap diagnostics:\n' + lastBootstrapDiagnostics +
        '\nDebug logs:\n' + ((lastState && lastState.debugLogs) || []).join('\n'));
}

function navigatorEvidenceReady(evidence) {
    return !!evidence
        && evidence.lowerD4DDE1 >= 20000
        && evidence.lowerBlack >= 1000
        && evidence.goBlack >= 20
        && evidence.goWhite < 20;
}

function compactState(state) {
    if (!state) {
        return state;
    }
    const copy = { ...state };
    delete copy.debugLogs;
    return copy;
}

function parseSpriteDiagnostics(text) {
    const lines = String(text || '').split('\n').map(line => line.trim()).filter(Boolean);
    return lines.filter(line => line.startsWith('ch=')).map(line => {
        const get = key => {
            const match = line.match(new RegExp(`${key}=([^\\s]+)`));
            return match ? match[1] : '';
        };
        const locSize = line.match(/loc=([-\d]+,[-\d]+) (\d+)x(\d+)/);
        const loc = locSize ? locSize[1].split(',').map(Number) : [];
        return {
            channel: Number(get('ch')),
            member: get('member'),
            x: loc.length === 2 ? loc[0] : NaN,
            y: loc.length === 2 ? loc[1] : NaN,
            width: locSize ? Number(locSize[2]) : NaN,
            height: locSize ? Number(locSize[3]) : NaN,
            line,
        };
    }).filter(sprite =>
        Number.isFinite(sprite.x) &&
        Number.isFinite(sprite.y) &&
        Number.isFinite(sprite.width) &&
        Number.isFinite(sprite.height) &&
        sprite.channel > 0);
}

async function captureReferenceComparison(page, referenceUrl, expectedWidth, expectedHeight, region) {
    return page.evaluate(async ({ compareReferenceUrl, compareExpectedWidth, compareExpectedHeight, compareRegion }) => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) {
            return { error: 'no-canvas' };
        }

        const ref = await new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => resolve(img);
            img.onerror = () => reject(new Error('Could not load reference image ' + compareReferenceUrl));
            img.src = compareReferenceUrl;
        });

        const crop = compareRegion || {
            x: 0,
            y: 0,
            width: Math.max(1, Math.min(canvas.width, compareExpectedWidth > 0 ? compareExpectedWidth : canvas.width, ref.naturalWidth)),
            height: Math.max(1, Math.min(canvas.height, compareExpectedHeight > 0 ? compareExpectedHeight : canvas.height, ref.naturalHeight)),
        };

        const actual = document.createElement('canvas');
        actual.width = crop.width;
        actual.height = crop.height;
        const actualCtx = actual.getContext('2d');
        actualCtx.fillStyle = '#000';
        actualCtx.fillRect(0, 0, crop.width, crop.height);
        actualCtx.drawImage(canvas, crop.x, crop.y, crop.width, crop.height, 0, 0, crop.width, crop.height);

        const reference = document.createElement('canvas');
        reference.width = crop.width;
        reference.height = crop.height;
        reference.getContext('2d').drawImage(ref, crop.x, crop.y, crop.width, crop.height, 0, 0, crop.width, crop.height);

        const actualData = actualCtx.getImageData(0, 0, crop.width, crop.height).data;
        const referenceData = reference.getContext('2d').getImageData(0, 0, crop.width, crop.height).data;

        let total = 0;
        let max = 0;
        let bad = 0;
        const pixels = crop.width * crop.height;
        const diff = document.createElement('canvas');
        diff.width = crop.width;
        diff.height = crop.height;
        const diffCtx = diff.getContext('2d');
        const diffData = diffCtx.createImageData(crop.width, crop.height);

        for (let p = 0; p < actualData.length; p += 4) {
            const dr = Math.abs(actualData[p] - referenceData[p]);
            const dg = Math.abs(actualData[p + 1] - referenceData[p + 1]);
            const db = Math.abs(actualData[p + 2] - referenceData[p + 2]);
            const delta = (dr + dg + db) / 3;
            total += delta;
            max = Math.max(max, delta);
            if (delta > 64) bad++;
            diffData.data[p] = delta;
            diffData.data[p + 1] = 0;
            diffData.data[p + 2] = 255 - delta;
            diffData.data[p + 3] = 255;
        }
        diffCtx.putImageData(diffData, 0, 0);

        return {
            width: crop.width,
            height: crop.height,
            x: crop.x,
            y: crop.y,
            meanDelta: total / pixels,
            badFraction: bad / pixels,
            maxDelta: max,
            actualPng: actual.toDataURL('image/png'),
            refPng: reference.toDataURL('image/png'),
            diffPng: diff.toDataURL('image/png'),
            tick: window._testState ? window._testState.tick : -1,
            frame: window._testState ? window._testState.frame : -1,
            spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
            error: window._testState ? window._testState.error : null,
        };
    }, {
        compareReferenceUrl: referenceUrl,
        compareExpectedWidth: expectedWidth,
        compareExpectedHeight: expectedHeight,
        compareRegion: region || null,
    });
}

async function waitForReferenceMatch(page, referencePath, baseUrl, stepName, thresholds, options = {}) {
    const referenceUrl = `${baseUrl}/${path.basename(referencePath)}`;
    const maxPolls = options.maxPolls || V31.maxPolls;
    const pollMs = options.pollMs || V31.pollMs;
    const captureTimeoutMs = options.captureTimeoutMs === undefined ? 10000 : options.captureTimeoutMs;
    const expectedWidth = options.expectedWidth || V31.width;
    const expectedHeight = options.expectedHeight || V31.height;
    const region = options.region || null;
    const artifactStem = options.artifactStem || null;
    const abortIf = typeof options.abortIf === 'function' ? options.abortIf : null;
    const abortIfPage = typeof options.abortIfPage === 'function' ? options.abortIfPage : null;
    const resultScore = result => {
        if (!result) {
            return Number.POSITIVE_INFINITY;
        }
        return (result.meanDelta * 1000000) + (result.badFraction * 1000) + result.maxDelta;
    };
    const writeComparisonArtifacts = (stem, result, suffix = '') => {
        if (!stem || !result || !result.actualPng || !result.refPng || !result.diffPng) {
            return;
        }
        fs.writeFileSync(`${stem}${suffix}_actual.png`,
            Buffer.from(result.actualPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        fs.writeFileSync(`${stem}${suffix}_ref.png`,
            Buffer.from(result.refPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        fs.writeFileSync(`${stem}${suffix}_diff.png`,
            Buffer.from(result.diffPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        fs.writeFileSync(`${stem}${suffix}_metrics.json`, JSON.stringify(result, null, 2));
    };
    let lastResult = null;
    let bestResult = null;
    let bestScore = Number.POSITIVE_INFINITY;
    for (let i = 0; i < maxPolls; i++) {
        await new Promise(resolve => setTimeout(resolve, pollMs));
        const result = captureTimeoutMs > 0
            ? await withStepTimeout(
                `${stepName} capture poll=${i}`,
                captureTimeoutMs,
                () => captureReferenceComparison(page, referenceUrl, expectedWidth, expectedHeight, region))
            : await captureReferenceComparison(page, referenceUrl, expectedWidth, expectedHeight, region);
        lastResult = result;
        if (!result || result.error) {
            throw new Error(`[${stepName}] ${result ? result.error : 'missing capture'} at poll=${i}`);
        }
        const score = resultScore(result);
        if (score < bestScore) {
            bestScore = score;
            bestResult = result;
        }
        if (result.meanDelta <= thresholds.maxMeanDelta && result.badFraction <= thresholds.maxBadFraction) {
            return result;
        }
        if (abortIf) {
            const abortReason = abortIf(result, i);
            if (abortReason) {
                throw new Error(`[${stepName}] ${abortReason}. Last result=` + JSON.stringify(result));
            }
        }
        if (abortIfPage) {
            const abortReason = await abortIfPage(page, result, i);
            if (abortReason) {
                throw new Error(`[${stepName}] ${abortReason}. Last result=` + JSON.stringify(result));
            }
        }
        if (i % 20 === 0) {
            console.log(`  [${stepName}] poll=${i} mean=${result.meanDelta.toFixed(2)} ` +
                `bad=${(result.badFraction * 100).toFixed(2)}% max=${result.maxDelta.toFixed(0)} sprites=${result.spriteCount}`);
        }
    }
    writeComparisonArtifacts(artifactStem, lastResult);
    writeComparisonArtifacts(artifactStem, bestResult, '_best');
    throw new Error(`[${stepName}] Did not reach target reference. Last result=` + JSON.stringify(lastResult));
}

async function clickPoint(page, point) {
    await page.mouse.move(point.x, point.y);
    await page.mouse.down({ button: 'left' });
    await page.mouse.up({ button: 'left' });
}

async function findV31NavigatorToolbarPoint(page) {
    const diagnostics = await runPageProbe(
        'toolbar diagnostics',
        () => page.evaluate(async () => {
            const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
                ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
                : '';
            return { windowSprites };
        }));
    const sprites = parseSpriteDiagnostics(diagnostics.windowSprites);
    const navSprite = sprites.find(sprite => sprite.member === 'RoomBarID_int_nav_image');
    if (navSprite) {
        return {
            x: Math.round(navSprite.x + (navSprite.width / 2)),
            y: Math.round(navSprite.y + (navSprite.height / 2)),
            sprite: navSprite,
        };
    }
    return {
        x: 716,
        y: 513,
        sprite: null,
    };
}

async function findWindowSpritePoint(page, memberNames, fallback = null) {
    const diagnostics = await runPageProbe(
        'window sprite diagnostics',
        () => page.evaluate(async () => {
            const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
                ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
                : '';
            return { windowSprites };
        }));
    const sprites = parseSpriteDiagnostics(diagnostics.windowSprites);
    for (const memberName of memberNames) {
        const sprite = sprites.find(candidate => candidate.member === memberName);
        if (sprite) {
            return {
                x: Math.round(sprite.x + (sprite.width / 2)),
                y: Math.round(sprite.y + (sprite.height / 2)),
                sprite,
            };
        }
    }
    return fallback;
}

async function waitForWindowSpritePoint(page, memberNames, maxPolls = 80) {
    for (let i = 0; i < maxPolls; i++) {
        const point = await findWindowSpritePoint(page, memberNames, null);
        if (point) {
            return point;
        }
        await new Promise(resolve => setTimeout(resolve, 150));
    }
    return null;
}

async function waitForWindowSpritePointOrConnectionFailure(page, memberNames, stepName, options = {}) {
    const timeoutMs = options.timeoutMs === undefined ? 20000 : options.timeoutMs;
    const pollMs = options.pollMs === undefined ? 200 : options.pollMs;
    const deadline = Date.now() + timeoutMs;
    while (Date.now() < deadline) {
        const point = await findWindowSpritePoint(page, memberNames, null);
        if (point) {
            return point;
        }
        const diagnostics = await sampleV31ConnectionFailure(page);
        const combined = `${diagnostics.visibleText}\n${diagnostics.windowSprites}`;
        if (/Problems Connecting|connection_problem_window/i.test(combined)) {
            throw new Error(`${stepName} hit connection_problem_window`);
        }
        const remainingMs = deadline - Date.now();
        if (remainingMs <= 0) {
            break;
        }
        await new Promise(resolve => setTimeout(resolve, Math.min(pollMs, remainingMs)));
    }
    const finalPoint = await findWindowSpritePoint(page, memberNames, null);
    if (finalPoint) {
        return finalPoint;
    }
    const finalDiagnostics = await sampleV31ConnectionFailure(page);
    const finalCombined = `${finalDiagnostics.visibleText}\n${finalDiagnostics.windowSprites}`;
    if (/Problems Connecting|connection_problem_window/i.test(finalCombined)) {
        throw new Error(`${stepName} hit connection_problem_window`);
    }
    throw new Error(`${stepName} timed out after ${timeoutMs}ms`);
}

async function sampleV31ConnectionFailure(page) {
    try {
        return await runPageProbe(
            'v31 connection failure diagnostics',
            () => page.evaluate(async () => {
                const text = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
                    ? await window.betaClientPlayer.getVisibleTextDiagnostics()
                    : '';
                const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
                    ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
                    : '';
                return {
                    visibleText: String(text || ''),
                    windowSprites: String(windowSprites || ''),
                };
            }));
    } catch (error) {
        const message = error && error.message ? String(error.message) : String(error);
        if (/v31 connection failure diagnostics timed out/i.test(message)) {
            return {
                visibleText: '',
                windowSprites: '',
                probeError: message,
            };
        }
        throw error;
    }
}

async function waitForV31NetworkIdle(page, stepName, options = {}) {
    const idleMs = options.idleMs === undefined
        ? Number(process.env.LS_V31_NATIVE_NETWORK_IDLE_MS || 2500)
        : options.idleMs;
    const timeoutMs = options.timeoutMs === undefined
        ? Number(process.env.LS_V31_NATIVE_NETWORK_IDLE_TIMEOUT_MS || 60000)
        : options.timeoutMs;
    const minTick = options.minTick === undefined ? 1 : options.minTick;
    return withStepTimeout(stepName, timeoutMs, async () => {
        await page.waitForFunction(
            ({ requiredIdleMs, requiredMinTick }) => {
                if (!window._testState || !window._testState.loaded) {
                    return false;
                }
                if ((window._testState.tick || 0) < requiredMinTick) {
                    return false;
                }
                const lastActivity = window._testState.lastNetworkActivityAt || 0;
                return (Date.now() - lastActivity) >= requiredIdleMs;
            },
            { timeout: timeoutMs, polling: 200 },
            { requiredIdleMs: idleMs, requiredMinTick: minTick });
        return page.evaluate(() => ({
            tick: window._testState ? window._testState.tick : -1,
            frame: window._testState ? window._testState.frame : -1,
            lastNetworkActivityAt: window._testState ? window._testState.lastNetworkActivityAt : 0,
        }));
    });
}

async function waitForV31PostHandshakeTraffic(page, stepName, options = {}) {
    const timeoutMs = options.timeoutMs === undefined
        ? Number(process.env.LS_V31_NATIVE_POST_HANDSHAKE_TIMEOUT_MS || 90000)
        : options.timeoutMs;
    return withStepTimeout(stepName, timeoutMs, async () => {
        await page.waitForFunction(
            () => {
                const logs = window._testState && Array.isArray(window._testState.debugLogs)
                    ? window._testState.debugLogs
                    : [];
                let sawPublicKey = false;
                for (const line of logs) {
                    if (line.indexOf('[TRACE] responseWithPublicKey()') >= 0
                            || line.indexOf('[MUS] send instance=1 bytes=123') >= 0) {
                        sawPublicKey = true;
                        continue;
                    }
                    if (!sawPublicKey) {
                        continue;
                    }
                    const match = /\[MUS\] message instance=\d+ bytes=(\d+)/.exec(line);
                    if (match && Number(match[1]) > 10) {
                        return true;
                    }
                }
                return false;
            },
            { timeout: timeoutMs, polling: 200 });
        return page.evaluate(() => ({
            tick: window._testState ? window._testState.tick : -1,
            frame: window._testState ? window._testState.frame : -1,
            lastNetworkActivityAt: window._testState ? window._testState.lastNetworkActivityAt : 0,
            debugLogs: window._testState && Array.isArray(window._testState.debugLogs)
                ? window._testState.debugLogs.slice(-40)
                : [],
        }));
    });
}

async function executeV31MessageSymbol(page, symbolName, stepName) {
    const result = await runPageProbe(
        stepName,
        () => page.evaluate(async name => {
            if (!window.betaClientPlayer || !window.betaClientPlayer.executeMessageSymbol) {
                return {
                    ok: false,
                    reason: 'executeMessageSymbol API unavailable',
                };
            }
            const ok = await window.betaClientPlayer.executeMessageSymbol(name);
            return {
                ok: !!ok,
                tick: window._testState ? window._testState.tick : -1,
                frame: window._testState ? window._testState.frame : -1,
                lastNetworkActivityAt: window._testState ? window._testState.lastNetworkActivityAt : 0,
            };
        }, symbolName));
    if (!result || !result.ok) {
        throw new Error(`${stepName} failed${result && result.reason ? `: ${result.reason}` : ''}`);
    }
    return result;
}

async function withStepTimeout(stepName, timeoutMs, action) {
    let timer = null;
    try {
        return await Promise.race([
            Promise.resolve().then(action),
            new Promise((_, reject) => {
                timer = setTimeout(() => reject(new Error(`${stepName} timed out after ${timeoutMs}ms`)), timeoutMs);
            }),
        ]);
    } finally {
        if (timer) {
            clearTimeout(timer);
        }
    }
}

async function runPageProbe(probeName, action, timeoutMs = Number(process.env.LS_V31_NATIVE_PAGE_PROBE_TIMEOUT_MS || 5000)) {
    if (!(timeoutMs > 0)) {
        return Promise.resolve().then(action);
    }
    let timer = null;
    try {
        return await Promise.race([
            Promise.resolve().then(action),
            new Promise((_, reject) => {
                timer = setTimeout(() => reject(new Error(`${probeName} timed out after ${timeoutMs}ms`)), timeoutMs);
            }),
        ]);
    } finally {
        if (timer) {
            clearTimeout(timer);
        }
    }
}

async function waitForV31NavigatorFromRoomBar(page, baseUrl, fixture) {
    console.log('  v31 room-bar path: waiting for entered-room reference');
    const enteredMatch = await waitForReferenceMatch(
        page,
        fixture.enteredPath,
        baseUrl,
        'v31_entered_room',
        {
            maxMeanDelta: fixture.maxMeanDelta,
            maxBadFraction: fixture.maxBadFraction,
        },
        {
            expectedWidth: 959,
            expectedHeight: fixture.height,
            captureTimeoutMs: Number(process.env.LS_V31_ENTERED_CAPTURE_TIMEOUT_MS || 30000),
            artifactStem: path.join(outputDir, `${fixture.name}_entered_room_wait_failure`),
        });
    console.log(`  v31 room-bar path: entered-room match tick=${enteredMatch.tick} frame=${enteredMatch.frame} ` +
        `mean=${enteredMatch.meanDelta.toFixed(2)} bad=${(enteredMatch.badFraction * 100).toFixed(2)}%`);
    console.log('  v31 room-bar path: resolving toolbar navigator point');
    const toolbarPoint = await withStepTimeout(
        'v31 toolbar point lookup',
        10000,
        () => findV31NavigatorToolbarPoint(page));
    console.log(`  v31 toolbar navigator point=(${toolbarPoint.x},${toolbarPoint.y})` +
        (toolbarPoint.sprite ? ` member=${toolbarPoint.sprite.member} ch=${toolbarPoint.sprite.channel}` : ' fallback=manual'));
    await clickPoint(page, toolbarPoint);
    await new Promise(resolve => setTimeout(resolve, 1200));
    const clickPublicRooms = process.env.LS_V31_NATIVE_CLICK_PUBLIC_ROOMS === '1';
    if (clickPublicRooms) {
        console.log('  v31 room-bar path: waiting for public-rooms tab');
        let publicRoomsPoint = null;
        try {
            publicRoomsPoint = await waitForWindowSpritePointOrConnectionFailure(
                page,
                ['Hotel Navigator_nav_tb_publicRooms'],
                'v31 public-rooms tab wait',
                { timeoutMs: 15000, pollMs: 200 });
        } catch (error) {
            const message = error && error.message ? String(error.message) : String(error);
            if (/connection_problem_window/i.test(message)) {
                throw error;
            }
            console.log(`  v31 room-bar path: public-rooms tab wait did not resolve (${message}), continuing`);
            publicRoomsPoint = await findWindowSpritePoint(page, ['Hotel Navigator_nav_tb_publicRooms'], null);
        }
        if (publicRoomsPoint) {
            console.log(`  v31 navigator public-rooms point=(${publicRoomsPoint.x},${publicRoomsPoint.y})` +
                ` member=${publicRoomsPoint.sprite.member} ch=${publicRoomsPoint.sprite.channel}`);
            await clickPoint(page, publicRoomsPoint);
            await new Promise(resolve => setTimeout(resolve, 1200));
        } else {
            console.log('  v31 room-bar path: public-rooms tab not found, continuing with current navigator state');
        }
    } else {
        console.log('  v31 room-bar path: skipping public-rooms tab interaction');
    }
    console.log('  v31 room-bar path: waiting for navigator networking to go idle');
    const postNavIdle = await waitForV31NetworkIdle(page, 'v31 room-bar post-nav network idle', { minTick: enteredMatch.tick });
    console.log(`  v31 room-bar path: post-nav idle tick=${postNavIdle.tick} frame=${postNavIdle.frame}`);
    const settleMs = Number(process.env.LS_V31_NATIVE_POST_IDLE_SETTLE_MS || 1200);
    if (settleMs > 0) {
        console.log(`  v31 room-bar path: settling ${settleMs}ms after network idle`);
        await new Promise(resolve => setTimeout(resolve, settleMs));
    }
    const guardRegion = (fixture.guardRegions || [])[0];
    console.log('  v31 room-bar path: waiting for navigator crop reference');
    const navigatorMatch = await waitForReferenceMatch(
        page,
        fixture.nativePath,
        baseUrl,
        'v31_navigator_crop',
        {
            maxMeanDelta: guardRegion ? guardRegion.maxMeanDelta : fixture.maxMeanDelta,
            maxBadFraction: guardRegion ? guardRegion.maxBadFraction : fixture.maxBadFraction,
        },
        {
            region: guardRegion || null,
            maxPolls: fixture.maxPolls,
            expectedWidth: fixture.width,
            expectedHeight: fixture.height,
            artifactStem: path.join(outputDir, `${fixture.name}_${guardRegion ? guardRegion.name : 'navigator'}_wait_failure`),
        });
    const state = await page.evaluate(() => ({
        loaded: window._testState.loaded,
        error: window._testState.error,
        tick: window._testState.tick,
        frame: window._testState.frame,
        gotoNetPages: window._testState.gotoNetPages.slice(),
        debugLogs: window._testState.debugLogs.slice(-20),
        spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
    }));
    return {
        state,
        evidence: {
            enteredRoomMeanDelta: enteredMatch.meanDelta,
            enteredRoomBadFraction: enteredMatch.badFraction,
            navigatorCropMeanDelta: navigatorMatch.meanDelta,
            navigatorCropBadFraction: navigatorMatch.badFraction,
            toolbarPoint: { x: toolbarPoint.x, y: toolbarPoint.y },
        },
    };
}

async function sampleV31NavigatorEvidence(page) {
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
            lowerBlack: lower['000000'] || 0,
            goBlack: go['000000'] || 0,
            goWhite: go.FFFFFF || 0,
        };
    });
}

async function waitForV31Navigator(page, baseUrl, fixture) {
    console.log('  v31 login path: waiting for post-handshake login traffic');
    const postHandshake = await waitForV31PostHandshakeTraffic(page, 'v31 login post-handshake traffic');
    console.log(`  v31 login path: post-handshake traffic tick=${postHandshake.tick} frame=${postHandshake.frame}`);
    console.log('  v31 login path: waiting for hotel-view networking to go idle');
    const preClickIdle = await waitForV31NetworkIdle(page, 'v31 login pre-nav network idle', { minTick: postHandshake.tick });
    console.log(`  v31 login path: pre-nav idle tick=${preClickIdle.tick} frame=${preClickIdle.frame}`);
    const navigatorMessage = String(process.env.LS_V31_NATIVE_NAV_MESSAGE || 'show_hide_navigator').trim() || 'show_hide_navigator';
    console.log(`  v31 login path: executeMessage(#${navigatorMessage})`);
    const messageDispatch = await executeV31MessageSymbol(
        page,
        navigatorMessage,
        `v31 executeMessage(#${navigatorMessage})`);
    console.log(`  v31 login path: message dispatched tick=${messageDispatch.tick} frame=${messageDispatch.frame}`);
    const publicRoomsPoint = null;
    const postNavIdle = await waitForV31NetworkIdle(page, 'v31 login post-nav network idle', { minTick: preClickIdle.tick });
    console.log(`  v31 login path: post-nav idle tick=${postNavIdle.tick} frame=${postNavIdle.frame}`);
    const settleMs = Number(process.env.LS_V31_NATIVE_POST_IDLE_SETTLE_MS || 1200);
    if (settleMs > 0) {
        console.log(`  v31 login path: settling ${settleMs}ms after network idle`);
        await new Promise(resolve => setTimeout(resolve, settleMs));
    }
    const guardRegion = (fixture.guardRegions || [])[0];
    const navigatorMatch = await waitForReferenceMatch(
        page,
        fixture.nativePath,
        baseUrl,
        'v31_login_navigator_crop',
        {
            maxMeanDelta: guardRegion ? guardRegion.maxMeanDelta : fixture.maxMeanDelta,
            maxBadFraction: guardRegion ? guardRegion.maxBadFraction : fixture.maxBadFraction,
        },
        {
            region: guardRegion || null,
            maxPolls: fixture.maxPolls,
            expectedWidth: fixture.width,
            expectedHeight: fixture.height,
            artifactStem: path.join(outputDir, `${fixture.name}_${guardRegion ? guardRegion.name : 'navigator'}_login_wait_failure`),
            async abortIfPage(currentPage, result, poll) {
                if (poll < 5) {
                    return null;
                }
                const diagnostics = await sampleV31ConnectionFailure(currentPage);
                const combined = `${diagnostics.visibleText}\n${diagnostics.windowSprites}`;
                if (/Problems Connecting|connection_problem_window/i.test(combined)) {
                    return `login navigator wait hit connection-problem branch at poll=${poll} ` +
                        `tick=${result.tick} sprites=${result.spriteCount}`;
                }
                return null;
            },
        });
    const state = await page.evaluate(() => ({
        loaded: window._testState.loaded,
        error: window._testState.error,
        tick: window._testState.tick,
        frame: window._testState.frame,
        gotoNetPages: window._testState.gotoNetPages.slice(),
        debugLogs: window._testState.debugLogs.slice(-20),
        lastNetworkActivityAt: window._testState.lastNetworkActivityAt,
        spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
    }));
    return {
        state,
        evidence: {
            preClickIdleTick: preClickIdle.tick,
            navigatorMessage,
            messageDispatchTick: messageDispatch.tick,
            postNavIdleTick: postNavIdle.tick,
            publicRoomsPoint: publicRoomsPoint ? { x: publicRoomsPoint.x, y: publicRoomsPoint.y } : null,
            navigatorCropMeanDelta: navigatorMatch.meanDelta,
            navigatorCropBadFraction: navigatorMatch.badFraction,
        },
    };
}

async function captureAndCompare(page, fixture, baseUrl, outputName, options = {}) {
    const outputPath = path.join(outputDir, `${outputName}.png`);
    const diffPath = path.join(outputDir, `${outputName}_diff.png`);
    return page.evaluate(async ({ referenceUrl, expectedWidth, expectedHeight, outputPathName, diffPathName, guardRegions, captureDiagnostics, freezeBeforeCapture }) => {
        if (freezeBeforeCapture && window.betaClientPlayer && window.betaClientPlayer.pause) {
            window.betaClientPlayer.pause();
            await new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve)));
        }
        const canvas = document.getElementById('beta-client-stage');
        const actual = document.createElement('canvas');
        actual.width = expectedWidth;
        actual.height = expectedHeight;
        const actualCtx = actual.getContext('2d');
        actualCtx.fillStyle = '#000';
        actualCtx.fillRect(0, 0, actual.width, actual.height);
        actualCtx.drawImage(canvas, 0, 0);

        const image = await new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => resolve(img);
            img.onerror = () => reject(new Error('Could not load reference ' + referenceUrl));
            img.src = referenceUrl;
        });
        if (image.naturalWidth !== expectedWidth || image.naturalHeight !== expectedHeight) {
            throw new Error(`Reference dimensions ${image.naturalWidth}x${image.naturalHeight} do not match expected ${expectedWidth}x${expectedHeight}`);
        }

        const reference = document.createElement('canvas');
        reference.width = expectedWidth;
        reference.height = expectedHeight;
        reference.getContext('2d').drawImage(image, 0, 0);

        const actualData = actualCtx.getImageData(0, 0, expectedWidth, expectedHeight);
        const refData = reference.getContext('2d').getImageData(0, 0, expectedWidth, expectedHeight);
        const diff = document.createElement('canvas');
        diff.width = expectedWidth;
        diff.height = expectedHeight;
        const diffCtx = diff.getContext('2d');
        const diffData = diffCtx.createImageData(expectedWidth, expectedHeight);

        let total = 0;
        let bad = 0;
        let max = 0;
        const pixels = expectedWidth * expectedHeight;
        for (let p = 0; p < actualData.data.length; p += 4) {
            const dr = Math.abs(actualData.data[p] - refData.data[p]);
            const dg = Math.abs(actualData.data[p + 1] - refData.data[p + 1]);
            const db = Math.abs(actualData.data[p + 2] - refData.data[p + 2]);
            const delta = (dr + dg + db) / 3;
            total += delta;
            max = Math.max(max, delta);
            if (delta > 64) bad++;
            diffData.data[p] = delta;
            diffData.data[p + 1] = 0;
            diffData.data[p + 2] = 255 - delta;
            diffData.data[p + 3] = 255;
        }
        diffCtx.putImageData(diffData, 0, 0);

        const result = {
            actualPng: actual.toDataURL('image/png'),
            diffPng: diff.toDataURL('image/png'),
            outputPathName,
            diffPathName,
            meanDelta: total / pixels,
            badFraction: bad / pixels,
            maxDelta: max,
            width: expectedWidth,
            height: expectedHeight,
            guardRegions: (guardRegions || []).map(region => {
                const cropActual = document.createElement('canvas');
                cropActual.width = region.width;
                cropActual.height = region.height;
                cropActual.getContext('2d').drawImage(
                    actual,
                    region.x, region.y, region.width, region.height,
                    0, 0, region.width, region.height);

                const cropRef = document.createElement('canvas');
                cropRef.width = region.width;
                cropRef.height = region.height;
                cropRef.getContext('2d').drawImage(
                    reference,
                    region.x, region.y, region.width, region.height,
                    0, 0, region.width, region.height);

                const actualRegion = cropActual.getContext('2d').getImageData(0, 0, region.width, region.height);
                const refRegion = cropRef.getContext('2d').getImageData(0, 0, region.width, region.height);
                const cropDiff = document.createElement('canvas');
                cropDiff.width = region.width;
                cropDiff.height = region.height;
                const cropDiffCtx = cropDiff.getContext('2d');
                const cropDiffData = cropDiffCtx.createImageData(region.width, region.height);
                const sideBySide = document.createElement('canvas');
                sideBySide.width = region.width * 2;
                sideBySide.height = region.height;
                const sideBySideCtx = sideBySide.getContext('2d');
                sideBySideCtx.drawImage(cropActual, 0, 0);
                sideBySideCtx.drawImage(cropRef, region.width, 0);

                let regionTotal = 0;
                let regionBad = 0;
                let regionMax = 0;
                let actualWhitePixels = 0;
                let actualBlackPixels = 0;
                let actualLightPixels = 0;
                let actualDarkPixels = 0;
                let refWhitePixels = 0;
                let refBlackPixels = 0;
                let refLightPixels = 0;
                let refDarkPixels = 0;
                const regionPixels = region.width * region.height;
                for (let p = 0; p < actualRegion.data.length; p += 4) {
                    const ar = actualRegion.data[p];
                    const ag = actualRegion.data[p + 1];
                    const ab = actualRegion.data[p + 2];
                    const aa = actualRegion.data[p + 3];
                    const rr = refRegion.data[p];
                    const rg = refRegion.data[p + 1];
                    const rb = refRegion.data[p + 2];
                    const ra = refRegion.data[p + 3];
                    const dr = Math.abs(ar - rr);
                    const dg = Math.abs(ag - rg);
                    const db = Math.abs(ab - rb);
                    const delta = (dr + dg + db) / 3;
                    regionTotal += delta;
                    regionMax = Math.max(regionMax, delta);
                    if (delta > 64) regionBad++;
                    if (aa > 0) {
                        if (ar === 255 && ag === 255 && ab === 255) actualWhitePixels++;
                        if (ar === 0 && ag === 0 && ab === 0) actualBlackPixels++;
                        if (ar >= 220 && ag >= 220 && ab >= 220) actualLightPixels++;
                        if (ar <= 40 && ag <= 40 && ab <= 40) actualDarkPixels++;
                    }
                    if (ra > 0) {
                        if (rr === 255 && rg === 255 && rb === 255) refWhitePixels++;
                        if (rr === 0 && rg === 0 && rb === 0) refBlackPixels++;
                        if (rr >= 220 && rg >= 220 && rb >= 220) refLightPixels++;
                        if (rr <= 40 && rg <= 40 && rb <= 40) refDarkPixels++;
                    }
                    cropDiffData.data[p] = delta;
                    cropDiffData.data[p + 1] = 0;
                    cropDiffData.data[p + 2] = 255 - delta;
                    cropDiffData.data[p + 3] = 255;
                }
                cropDiffCtx.putImageData(cropDiffData, 0, 0);

                return {
                    name: region.name,
                    x: region.x,
                    y: region.y,
                    width: region.width,
                    height: region.height,
                    maxMeanDelta: region.maxMeanDelta,
                    maxBadFraction: region.maxBadFraction,
                    meanDelta: regionTotal / regionPixels,
                    badFraction: regionBad / regionPixels,
                    maxDelta: regionMax,
                    actualWhitePixels,
                    actualBlackPixels,
                    actualLightPixels,
                    actualDarkPixels,
                    refWhitePixels,
                    refBlackPixels,
                    refLightPixels,
                    refDarkPixels,
                    actualPng: cropActual.toDataURL('image/png'),
                    refPng: cropRef.toDataURL('image/png'),
                    diffPng: cropDiff.toDataURL('image/png'),
                    sideBySidePng: sideBySide.toDataURL('image/png'),
                };
            }),
        };
        if (captureDiagnostics) {
            const visibleText = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
                ? await window.betaClientPlayer.getVisibleTextDiagnostics()
                : '';
            const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
                ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
                : '';
            const bootstrap = window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
                ? await window.betaClientPlayer.getBootstrapDiagnostics()
                : '';
            const debugLogs = window._testState && window._testState.debugLogs
                ? window._testState.debugLogs.slice()
                : [];
            result.diagnostics = {
                visibleText,
                windowSprites,
                bootstrap,
                debugLogs,
                tick: window._testState ? window._testState.tick : -1,
                frame: window._testState ? window._testState.frame : -1,
            };
        }
        return result;
    }, {
        referenceUrl: `${baseUrl}/${path.basename(fixture.nativePath)}`,
        expectedWidth: fixture.width,
        expectedHeight: fixture.height,
        outputPathName: outputPath,
        diffPathName: diffPath,
        guardRegions: fixture.guardRegions || [],
        captureDiagnostics: !!options.captureDiagnostics,
        freezeBeforeCapture: !!options.freezeBeforeCapture,
    }).then(result => {
        fs.writeFileSync(outputPath, Buffer.from(result.actualPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        fs.writeFileSync(diffPath, Buffer.from(result.diffPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        for (const region of result.guardRegions || []) {
            fs.writeFileSync(
                path.join(outputDir, `${outputName}_${region.name}.png`),
                Buffer.from(region.actualPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
            fs.writeFileSync(
                path.join(outputDir, `${outputName}_${region.name}_ref.png`),
                Buffer.from(region.refPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
            fs.writeFileSync(
                path.join(outputDir, `${outputName}_${region.name}_diff.png`),
                Buffer.from(region.diffPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
            fs.writeFileSync(
                path.join(outputDir, `${outputName}_${region.name}_side_by_side.png`),
                Buffer.from(region.sideBySidePng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        }
        return result;
    });
}

function v31FailureLooksRetryable(artifacts, err) {
    artifacts = artifacts || {};
    const combined = [
        artifacts.visibleText || '',
        artifacts.windowSprites || '',
        artifacts.bootstrap || '',
        err && err.message ? err.message : '',
    ].join('\n');
    return /Problems Connecting|Cannot connect to Habbo/i.test(combined)
        || /connection_problem_window|nav_problem_obj/.test(combined)
        || /Loading room_(shadow|back|general_loader_bg|general_loader_text|gen_loaderbar|room_cancel|queue_text)/.test(combined)
        || /entered-room wait hit (connection-problem branch|loading-room branch|stalled plateau)/.test(combined)
        || /v31_entered_room capture poll=\d+ timed out/.test(combined);
}

async function saveFailureArtifacts(page, fixture, outputStem = `${fixture.name}_failure`) {
    try {
        const defaultCaptureTimeoutMs = fixture && fixture.name === 'v31' ? 30000 : 10000;
        const captureTimeoutMs = process.env.LS_NATIVE_FAILURE_CAPTURE_TIMEOUT_MS === undefined
            ? defaultCaptureTimeoutMs
            : Number(process.env.LS_NATIVE_FAILURE_CAPTURE_TIMEOUT_MS);
        const collectArtifacts = () => page.evaluate(async () => {
                async function captureSpriteBitmapPng(name, baked) {
                    if (!window.betaClientPlayer || !window.betaClientPlayer.getSpriteBitmap) {
                        return null;
                    }
                    const bitmap = await window.betaClientPlayer.getSpriteBitmap(name, baked);
                    if (!bitmap || !bitmap.width || !bitmap.height || !bitmap.rgba) {
                        return null;
                    }
                    const canvas = document.createElement('canvas');
                    canvas.width = bitmap.width;
                    canvas.height = bitmap.height;
                    const ctx = canvas.getContext('2d');
                    if (!ctx) {
                        return null;
                    }
                    const rgba = bitmap.rgba instanceof Uint8ClampedArray
                        ? bitmap.rgba
                        : new Uint8ClampedArray(bitmap.rgba);
                    ctx.putImageData(new ImageData(rgba, bitmap.width, bitmap.height), 0, 0);
                    return canvas.toDataURL('image/png');
                }
                const canvas = document.getElementById('beta-client-stage');
                const visibleText = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
                    ? await window.betaClientPlayer.getVisibleTextDiagnostics()
                    : '';
                const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
                    ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
                    : '';
                const bootstrap = window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
                    ? await window.betaClientPlayer.getBootstrapDiagnostics()
                    : '';
                const debugLogs = window._testState && window._testState.debugLogs
                    ? window._testState.debugLogs.slice()
                    : [];
                const gotoNetPages = window._testState && window._testState.gotoNetPages
                    ? window._testState.gotoNetPages.slice()
                    : [];
                const tick = window._testState ? window._testState.tick : -1;
                const frame = window._testState ? window._testState.frame : -1;
                const spriteBitmaps = {};
                if (window.betaClientPlayer && window.betaClientPlayer.getSpriteBitmap) {
                    const targets = [
                        'Hotel Navigator_nav_roomlist',
                        'Hotel Navigator_nav_closeInfo',
                    ];
                    for (const name of targets) {
                        spriteBitmaps[`${name}_dyn`] = await captureSpriteBitmapPng(name, false);
                        spriteBitmaps[`${name}_baked`] = await captureSpriteBitmapPng(name, true);
                    }
                }
                return {
                    png: canvas ? canvas.toDataURL('image/png') : '',
                    visibleText,
                    windowSprites,
                    bootstrap,
                    debugLogs,
                    gotoNetPages,
                    spriteBitmaps,
                    tick,
                    frame,
                };
            });
        const artifacts = captureTimeoutMs > 0
            ? await withStepTimeout(`${fixture.name} failure artifact capture`, captureTimeoutMs, collectArtifacts)
            : await collectArtifacts();
        if (artifacts.png) {
            fs.writeFileSync(
                path.join(outputDir, `${outputStem}.png`),
                Buffer.from(artifacts.png.replace(/^data:image\/png;base64,/, ''), 'base64'));
        }
        fs.writeFileSync(path.join(outputDir, `${outputStem}_visible_text.txt`), artifacts.visibleText || '');
        fs.writeFileSync(path.join(outputDir, `${outputStem}_window_sprites.txt`), artifacts.windowSprites || '');
        fs.writeFileSync(path.join(outputDir, `${outputStem}_bootstrap.txt`), artifacts.bootstrap || '');
        fs.writeFileSync(path.join(outputDir, `${outputStem}_debug_logs.txt`), (artifacts.debugLogs || []).join('\n'));
        fs.writeFileSync(
            path.join(outputDir, `${outputStem}_goto_net_pages.txt`),
            (artifacts.gotoNetPages || []).map(entry => JSON.stringify(entry)).join('\n'));
        for (const [name, dataUrl] of Object.entries(artifacts.spriteBitmaps || {})) {
            if (!dataUrl) {
                continue;
            }
            const safeName = name.replace(/[^A-Za-z0-9._-]+/g, '_');
            fs.writeFileSync(
                path.join(outputDir, `${outputStem}_${safeName}.png`),
                Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'));
        }
        fs.writeFileSync(
            path.join(outputDir, `${outputStem}_capture_state.txt`),
            `tick=${artifacts.tick ?? -1}\nframe=${artifacts.frame ?? -1}\n`);
        return artifacts;
    } catch (artifactErr) {
        try {
            await page.screenshot({ path: path.join(outputDir, `${outputStem}_screenshot.png`) });
        } catch (screenshotErr) {
            fs.writeFileSync(
                path.join(outputDir, `${outputStem}_artifact_errors.txt`),
                `artifactCapture=${artifactErr.message}\nscreenshot=${screenshotErr.message}\n`);
        }
        console.log(`  failed to save failure artifacts: ${artifactErr.message}`);
        return null;
    }
}

async function runCase(browserHandle, baseUrl, fixture) {
    console.log(`\n=== ${fixture.name} native visual regression ===`);
    const htmlPath = path.join(distPath, `_test_native_${fixture.name}.html`);
    fs.writeFileSync(htmlPath, htmlForCase(baseUrl, fixture));
    const maxAttempts = fixture.name === 'v31'
        ? Math.max(1, Number(process.env.LS_V31_NATIVE_CONNECT_ATTEMPTS || 6))
        : 1;
    try {
        for (let attempt = 1; attempt <= maxAttempts; attempt++) {
            const page = await browserHandle.newPage();
            const requestEvents = [];
            page.on('console', msg => {
                const text = msg.text();
                console.log('  [page] ' + text);
            });
            page.on('pageerror', err => {
                console.log('  [pageerror] ' + err.message);
            });
            page.on('requestfailed', request => {
                const failure = request.failure();
                const line = `${request.url()} ${failure ? failure.errorText : ''}`;
                requestEvents.push('failed ' + line);
                console.log('  [requestfailed] ' + line);
            });
            page.on('response', response => {
                const url = response.url();
                if (/shockwave|player-wasm|__native_proxy|\.dcr|\.cst|\.cct|\.gif|\.jpe?g|\.png|external_/i.test(url)) {
                    const line = `${response.status()} ${url}`;
                    requestEvents.push('response ' + line);
                    console.log('  [response] ' + line);
                }
            });
            try {
                if (attempt > 1) {
                    console.log(`  retry attempt ${attempt}/${maxAttempts}`);
                }
                await page.goto(`${baseUrl}/_test_native_${fixture.name}.html`, { waitUntil: 'domcontentloaded' });
                await waitForWorkerReady(page);
                if (fixture.name === 'v14') {
                    await installV14RequestMap(page);
                }
                await page.evaluate(() => window.startNativeVisualCase());
                const v31Path = String(process.env.LS_V31_NATIVE_SOURCE || 'direct').toLowerCase();
                const waitResult = fixture.name === 'v1'
                    ? await waitForV1Login(page)
                    : fixture.name === 'v14'
                        ? await waitForV14Login(page)
                        : v31Path === 'direct'
                            ? await waitForV31Navigator(page, baseUrl, fixture)
                            : await waitForV31NavigatorFromRoomBar(page, baseUrl, fixture);
                const captureDiagnostics = process.env.LS_NATIVE_SAVE_DIAGNOSTICS === '1';
                const comparison = await captureAndCompare(
                    page,
                    fixture,
                    baseUrl,
                    `${fixture.name}_wasm_native_compare`,
                    {
                        captureDiagnostics,
                        freezeBeforeCapture: captureDiagnostics,
                    });
                if (captureDiagnostics) {
                    await saveSuccessDiagnostics(page, fixture, comparison.diagnostics || null, waitResult.state || null);
                }
                console.log(`  waitState=${JSON.stringify(compactState(waitResult.state))}`);
                if (comparison.diagnostics) {
                    console.log(`  compareState=${JSON.stringify({
                        tick: comparison.diagnostics.tick,
                        frame: comparison.diagnostics.frame,
                    })}`);
                }
                if (waitResult.evidence) {
                    console.log(`  evidence=${JSON.stringify(waitResult.evidence)}`);
                }
                console.log(`  visual meanDelta=${comparison.meanDelta.toFixed(2)} ` +
                    `badFraction=${(comparison.badFraction * 100).toFixed(2)}% maxDelta=${comparison.maxDelta.toFixed(0)}`);
                console.log(`  output=${path.join(outputDir, `${fixture.name}_wasm_native_compare.png`)}`);
                console.log(`  diff=${path.join(outputDir, `${fixture.name}_wasm_native_compare_diff.png`)}`);
                for (const region of comparison.guardRegions || []) {
                    console.log(`  guard ${region.name} meanDelta=${region.meanDelta.toFixed(2)} ` +
                        `badFraction=${(region.badFraction * 100).toFixed(2)}% maxDelta=${region.maxDelta.toFixed(0)}`);
                    if (region.name.includes('text')) {
                        console.log(`  guard ${region.name} colors actualWhite=${region.actualWhitePixels} actualBlack=${region.actualBlackPixels} ` +
                            `actualLight=${region.actualLightPixels} actualDark=${region.actualDarkPixels} ` +
                            `refWhite=${region.refWhitePixels} refBlack=${region.refBlackPixels} ` +
                            `refLight=${region.refLightPixels} refDark=${region.refDarkPixels}`);
                    }
                    console.log(`  guard output=${path.join(outputDir, `${fixture.name}_wasm_native_compare_${region.name}.png`)}`);
                    console.log(`  guard ref=${path.join(outputDir, `${fixture.name}_wasm_native_compare_${region.name}_ref.png`)}`);
                    console.log(`  guard diff=${path.join(outputDir, `${fixture.name}_wasm_native_compare_${region.name}_diff.png`)}`);
                    console.log(`  guard sideBySide=${path.join(outputDir, `${fixture.name}_wasm_native_compare_${region.name}_side_by_side.png`)}`);
                    if (region.maxMeanDelta !== undefined && region.meanDelta > region.maxMeanDelta) {
                        throw new Error(`${fixture.name} guard ${region.name} meanDelta ${region.meanDelta.toFixed(2)} exceeds ` +
                            `max ${region.maxMeanDelta}`);
                    }
                    if (region.maxBadFraction !== undefined && region.badFraction > region.maxBadFraction) {
                        throw new Error(`${fixture.name} guard ${region.name} badFraction ${region.badFraction.toFixed(4)} exceeds ` +
                            `max ${region.maxBadFraction}`);
                    }
                }
                if (fixture.enforceFullFrame !== false &&
                        (comparison.meanDelta > fixture.maxMeanDelta || comparison.badFraction > fixture.maxBadFraction)) {
                    throw new Error(`${fixture.name} visual mismatch exceeds threshold: ` +
                        `meanDelta=${comparison.meanDelta.toFixed(2)} max=${fixture.maxMeanDelta}, ` +
                        `badFraction=${comparison.badFraction.toFixed(4)} max=${fixture.maxBadFraction}`);
                }
                return;
            } catch (err) {
                const artifactStem = attempt < maxAttempts
                    ? `${fixture.name}_failure_attempt_${attempt}`
                    : `${fixture.name}_failure`;
                const artifacts = await saveFailureArtifacts(page, fixture, artifactStem);
                if (requestEvents.length > 0) {
                    console.log('  request events:\n' + requestEvents.slice(-40).map(line => '    ' + line).join('\n'));
                }
                const retryable = fixture.name === 'v31' && attempt < maxAttempts && v31FailureLooksRetryable(artifacts, err);
                if (!retryable) {
                    throw err;
                }
                console.log(`  retrying after transient v31 connection failure at tick=${artifacts && artifacts.tick !== undefined ? artifacts.tick : -1} ` +
                    `frame=${artifacts && artifacts.frame !== undefined ? artifacts.frame : -1}`);
            } finally {
                await page.close();
            }
        }
    } finally {
        try { fs.unlinkSync(htmlPath); } catch (e) {}
    }
}

async function saveSuccessDiagnostics(page, fixture, preCapturedArtifacts, waitState) {
    const artifacts = preCapturedArtifacts || await page.evaluate(async () => {
        const visibleText = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
            ? await window.betaClientPlayer.getVisibleTextDiagnostics()
            : '';
        const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
            ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
            : '';
        const bootstrap = window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
            ? await window.betaClientPlayer.getBootstrapDiagnostics()
            : '';
        const debugLogs = window._testState && window._testState.debugLogs
            ? window._testState.debugLogs.slice()
            : [];
        const tick = window._testState ? window._testState.tick : -1;
        const frame = window._testState ? window._testState.frame : -1;
        return { visibleText, windowSprites, bootstrap, debugLogs, tick, frame };
    });
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_visible_text.txt`), artifacts.visibleText || '');
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_window_sprites.txt`), artifacts.windowSprites || '');
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_bootstrap.txt`), artifacts.bootstrap || '');
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_debug_logs.txt`), (artifacts.debugLogs || []).join('\n'));
    fs.writeFileSync(
        path.join(outputDir, `${fixture.name}_capture_state.txt`),
        `tick=${artifacts.tick ?? -1}\nframe=${artifacts.frame ?? -1}\n`);
    if (waitState) {
        fs.writeFileSync(
            path.join(outputDir, `${fixture.name}_wait_state.txt`),
            JSON.stringify(waitState, null, 2) + '\n');
    }
}

async function main() {
    console.log('=== WASM native visual regression test ===');
    console.log('Dist:      ', distPath);
    console.log('References:', referenceDir);
    console.log('v14 root:  ', v14Root);
    console.log('Output:    ', outputDir);
    console.log('Cases:     ', cases.join(','));

    assertLoaderMarkdownMatches();
    assertFiles();
    fs.mkdirSync(outputDir, { recursive: true });

    const { server, port } = await createServer();
    const baseUrl = `http://127.0.0.1:${port}`;
    let browser;
    try {
        const loaded = await loadBrowser();
        browser = loaded.browser;
        for (const name of cases) {
            if (name === 'v1') {
                await runCase(loaded, baseUrl, V1);
            } else if (name === 'v14') {
                await runCase(loaded, baseUrl, V14);
            } else if (name === 'v31') {
                await runCase(loaded, baseUrl, V31);
            } else {
                throw new Error(`Unknown native visual case: ${name}`);
            }
        }
    } finally {
        if (browser) await browser.close();
        server.close();
    }
}

main().catch(err => {
    console.error('FAIL:', err && err.stack ? err.stack : err);
    process.exit(1);
});
