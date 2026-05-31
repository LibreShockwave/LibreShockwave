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
    nativePath: path.join(referenceDir, 'v31_native.png'),
    movieUrl: 'https://images.classichabbo.com/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr?',
    params: {
        sw1: 'client.allow.cross.domain=1;client.notify.cross.domain=0',
        sw2: 'connection.info.host=verysecret.classichabbo.com;connection.info.port=30100',
        sw3: 'connection.mus.host=verysecret.classichabbo.com;connection.mus.port=39101',
        sw4: 'site.url=;url.prefix=',
        sw5: 'client.reload.url=/client/beta?x=reauthenticate;client.fatal.error.url=/clientutils?key=error',
        sw6: 'client.connection.failed.url=/clientutils?key=connection_failed;external.variables.txt=https://images.classichabbo.com/gamedata/external_variables.txt?',
        sw7: 'external.texts.txt=https://images.classichabbo.com/gamedata/external_texts.txt?',
        sw8: 'use.sso.ticket=1;sso.ticket=vibe-sso-admin-504d3ba4-acdb-4436-b67c-d0752f44f767',
    },
    wait: 'navigator',
    initialMovieProperties: readJsonEnv('LS_V31_INITIAL_MOVIE_PROPERTIES', {}),
    maxPolls: Number(process.env.LS_V31_NATIVE_MAX_POLLS || 900),
    pollMs: 500,
    maxMeanDelta: Number(process.env.LS_V31_NATIVE_MAX_MEAN_DELTA || 42),
    maxBadFraction: Number(process.env.LS_V31_NATIVE_MAX_BAD_FRACTION || 0.32),
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
<canvas id="beta-client-stage" width="${fixture.width}" height="${fixture.height}"></canvas>
${legacyFetchRewriteScript(baseUrl, fixture)}
<script src="${baseUrl}/shockwave-lib.js"><\/script>
<script>
var _testState = { loaded: false, error: null, tick: 0, frame: 0, gotoNetPages: [], debugLogs: [] };
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
        _testState.debugLogs.push(text);
        if (_testState.debugLogs.length > 2000) {
            _testState.debugLogs.splice(0, _testState.debugLogs.length - 2000);
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

async function waitForV31Navigator(page) {
    let lastState = null;
    let evidence = null;
    for (let i = 0; i < V31.maxPolls; i++) {
        await new Promise(resolve => setTimeout(resolve, V31.pollMs));
        lastState = await page.evaluate(() => {
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
                debugLogs: window._testState.debugLogs.slice(-20),
                spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
                nonBlack,
                colorBuckets: buckets.size,
            };
        });
        if (i % 20 === 0) {
            console.log(`  v31 poll=${i} loaded=${lastState.loaded} tick=${lastState.tick} frame=${lastState.frame} ` +
                `sprites=${lastState.spriteCount} nonBlack=${lastState.nonBlack}`);
        }
        if (lastState.error) break;
        if (lastState.gotoNetPages.some(entry => entry.url.includes('connection_failed') || entry.url.includes('clientutils?key=error'))) {
            break;
        }
        if (lastState.spriteCount >= 70 && lastState.nonBlack > 1000) {
            evidence = await sampleV31NavigatorEvidence(page);
            if (navigatorEvidenceReady(evidence)) {
                await new Promise(resolve => setTimeout(resolve, 1000));
                return { state: lastState, evidence };
            }
        }
    }
    throw new Error('v31 navigator was not visible before timeout. Last state: ' +
        JSON.stringify(lastState) + ' evidence=' + JSON.stringify(evidence));
}

async function captureAndCompare(page, fixture, baseUrl, outputName) {
    const outputPath = path.join(outputDir, `${outputName}.png`);
    const diffPath = path.join(outputDir, `${outputName}_diff.png`);
    return page.evaluate(async ({ referenceUrl, expectedWidth, expectedHeight, outputPathName, diffPathName }) => {
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

        return {
            actualPng: actual.toDataURL('image/png'),
            diffPng: diff.toDataURL('image/png'),
            outputPathName,
            diffPathName,
            meanDelta: total / pixels,
            badFraction: bad / pixels,
            maxDelta: max,
            width: expectedWidth,
            height: expectedHeight,
        };
    }, {
        referenceUrl: `${baseUrl}/${path.basename(fixture.nativePath)}`,
        expectedWidth: fixture.width,
        expectedHeight: fixture.height,
        outputPathName: outputPath,
        diffPathName: diffPath,
    }).then(result => {
        fs.writeFileSync(outputPath, Buffer.from(result.actualPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        fs.writeFileSync(diffPath, Buffer.from(result.diffPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
        return result;
    });
}

async function saveFailureArtifacts(page, fixture) {
    try {
        const artifacts = await page.evaluate(async () => {
            const canvas = document.getElementById('beta-client-stage');
            const visibleText = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
                ? await window.betaClientPlayer.getVisibleTextDiagnostics()
                : '';
            const bootstrap = window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
                ? await window.betaClientPlayer.getBootstrapDiagnostics()
                : '';
            return {
                png: canvas ? canvas.toDataURL('image/png') : '',
                visibleText,
                bootstrap,
            };
        });
        if (artifacts.png) {
            fs.writeFileSync(
                path.join(outputDir, `${fixture.name}_failure.png`),
                Buffer.from(artifacts.png.replace(/^data:image\/png;base64,/, ''), 'base64'));
        }
        fs.writeFileSync(path.join(outputDir, `${fixture.name}_failure_visible_text.txt`), artifacts.visibleText || '');
        fs.writeFileSync(path.join(outputDir, `${fixture.name}_failure_bootstrap.txt`), artifacts.bootstrap || '');
    } catch (artifactErr) {
        console.log(`  failed to save failure artifacts: ${artifactErr.message}`);
    }
}

async function runCase(browserHandle, baseUrl, fixture) {
    console.log(`\n=== ${fixture.name} native visual regression ===`);
    const htmlPath = path.join(distPath, `_test_native_${fixture.name}.html`);
    fs.writeFileSync(htmlPath, htmlForCase(baseUrl, fixture));
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
        await page.goto(`${baseUrl}/_test_native_${fixture.name}.html`, { waitUntil: 'domcontentloaded' });
        await waitForWorkerReady(page);
        if (fixture.name === 'v14') {
            await installV14RequestMap(page);
        }
        await page.evaluate(() => window.startNativeVisualCase());
        const waitResult = fixture.name === 'v1'
            ? await waitForV1Login(page)
            : fixture.name === 'v14'
                ? await waitForV14Login(page)
                : await waitForV31Navigator(page);
        const comparison = await captureAndCompare(page, fixture, baseUrl, `${fixture.name}_wasm_native_compare`);
        if (process.env.LS_NATIVE_SAVE_DIAGNOSTICS === '1') {
            await saveSuccessDiagnostics(page, fixture);
        }
        console.log(`  state=${JSON.stringify(compactState(waitResult.state))}`);
        if (waitResult.evidence) {
            console.log(`  evidence=${JSON.stringify(waitResult.evidence)}`);
        }
        console.log(`  visual meanDelta=${comparison.meanDelta.toFixed(2)} ` +
            `badFraction=${(comparison.badFraction * 100).toFixed(2)}% maxDelta=${comparison.maxDelta.toFixed(0)}`);
        console.log(`  output=${path.join(outputDir, `${fixture.name}_wasm_native_compare.png`)}`);
        console.log(`  diff=${path.join(outputDir, `${fixture.name}_wasm_native_compare_diff.png`)}`);
        if (comparison.meanDelta > fixture.maxMeanDelta || comparison.badFraction > fixture.maxBadFraction) {
            throw new Error(`${fixture.name} visual mismatch exceeds threshold: ` +
                `meanDelta=${comparison.meanDelta.toFixed(2)} max=${fixture.maxMeanDelta}, ` +
                `badFraction=${comparison.badFraction.toFixed(4)} max=${fixture.maxBadFraction}`);
        }
    } catch (err) {
        await saveFailureArtifacts(page, fixture);
        if (requestEvents.length > 0) {
            console.log('  request events:\n' + requestEvents.slice(-40).map(line => '    ' + line).join('\n'));
        }
        throw err;
    } finally {
        await page.close();
        try { fs.unlinkSync(htmlPath); } catch (e) {}
    }
}

async function saveSuccessDiagnostics(page, fixture) {
    const artifacts = await page.evaluate(async () => {
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
        return { visibleText, windowSprites, bootstrap, debugLogs };
    });
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_visible_text.txt`), artifacts.visibleText || '');
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_window_sprites.txt`), artifacts.windowSprites || '');
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_bootstrap.txt`), artifacts.bootstrap || '');
    fs.writeFileSync(path.join(outputDir, `${fixture.name}_debug_logs.txt`), (artifacts.debugLogs || []).join('\n'));
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
