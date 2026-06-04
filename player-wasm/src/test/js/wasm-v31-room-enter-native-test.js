#!/usr/bin/env node
'use strict';

const fs = require('fs');
const http = require('http');
const path = require('path');
const { spawnSync } = require('child_process');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const outputDir = process.argv[3] || path.resolve(process.cwd(), 'frames_v31_room_enter');
const referenceDir = process.env.LS_V31_ROOM_REFERENCE_DIR || '/opt/git/v31_room_load';
const nativeVisualReferenceDir = process.env.LS_NATIVE_REFERENCE_DIR || '/opt/git/v14_v31_compare';
const maxNativePolls = Number(process.env.LS_V31_ROOM_PHOTO_MAX_POLLS || 1200);
const pollMs = Number(process.env.LS_V31_ROOM_PHOTO_POLL_MS || 500);
const settleMs = Number(process.env.LS_V31_ROOM_PHOTO_SETTLE_MS || 1200);
const runLegacyMovieRegressions = process.env.LS_V31_ROOM_PHOTO_SKIP_LEGACY_VISUALS !== '1';

    const V31 = {
    name: 'v31_room_photo',
    width: 960,
    height: 540,
    nativePath: path.join(referenceDir, 'v31_native.png'),
    roomsPath: path.join(referenceDir, 'v31_native_rooms.png'),
    enteredPath: path.join(referenceDir, 'v31_native_rooms_entered.png'),
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
        sw9: 'forward.type=2;forward.id=1',
    },
    maxMeanDelta: 42,
    maxBadFraction: 0.32,
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

function toJson(value) {
    return JSON.stringify(String(value));
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
            const pattern = new RegExp(`^${key.replace(/[.*+?^${}()|[\\]\\/g, '\\\\$&')}=`, 'm');
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

function serveFile(res, filePath) {
    const data = readFixtureFile(filePath);
    res.writeHead(200, {
        'Content-Type': MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream',
        'Content-Length': data.length,
        'Access-Control-Allow-Origin': '*',
    });
    res.end(data);
}

function createServer() {
    return new Promise(resolve => {
        const server = http.createServer((req, res) => {
            const urlPath = decodeURIComponent(req.url.split('?')[0]);
            if (urlPath.startsWith('/__native_proxy/')) {
                return proxyHttp(req, res);
            }
            const normalized = urlPath.startsWith('/') ? urlPath.substring(1) : urlPath;
            const candidates = [
                path.join(distPath, normalized),
                path.join(referenceDir, normalized),
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

function proxyHttp(req, res) {
    const targetUrl = 'http://' + req.url.substring('/__native_proxy/'.length);
    const protocol = targetUrl.startsWith('https:') ? require('https') : require('http');
    protocol.get(targetUrl, proxyRes => {
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
        const browser = await chromium.launch({
            headless: true,
            executablePath: process.env.CHROME_PATH || 'C:/Program Files/Google/Chrome/Application/chrome.exe',
            args: ['--no-sandbox'],
        });
        return { browser, newPage: () => browser.newPage() };
    }
}

function htmlForCase(baseUrl, fixture) {
    return `<!doctype html>\n<html><body style="margin:0;background:#000">\n<canvas id="beta-client-stage" width="${fixture.width}" height="${fixture.height}"></canvas>\n<script src="${baseUrl}/shockwave-lib.js"><\/script>\n<script>\nvar _testState = { loaded: false, error: null, tick: 0, frame: 0, gotoNetPages: [] };\nvar betaClientMovieUrl = ${toJson(fixture.movieUrl)};\nvar betaClientParams = ${JSON.stringify(fixture.params, null, 4)};\nvar betaClientPlayer = LibreShockwave.create("beta-client-stage", {\n    basePath: "${baseUrl}/",\n    autoplay: true,\n    remember: false,\n    debugPlayback: true,\n    params: betaClientParams,\n    onLoad: function(info) {\n        _testState.loaded = true;\n        _testState.info = info;\n        if (betaClientPlayer) betaClientPlayer.play();\n    },\n    onError: function(message) {\n        _testState.error = String(message);\n        console.error("LibreShockwave beta client error:", message);\n    },\n    onGotoNetPage: function(url, target) {\n        _testState.gotoNetPages.push({ url: String(url), target: String(target) });\n    },\n    onFrame: function(frame) {\n        _testState.tick++;\n        _testState.frame = frame;\n    },\n    onDebugLog: function(log) {\n        var text = String(log);\n        if (text.indexOf('[ScriptError]') >= 0 || text.indexOf('[NetManager]') >= 0 || text.indexOf('[TRACE]') >= 0) {\n            console.log(text);\n        }\n    }\n});\nwindow.betaClientPlayer = betaClientPlayer;\nwindow.startNativeCase = function() { betaClientPlayer.load(betaClientMovieUrl); };\n<\/script>\n</body></html>`;
}

async function waitForWorkerReady(page) {
    await page.waitForFunction(() => window.betaClientPlayer && window.betaClientPlayer._workerReady, { timeout: 180000 });
}

function intersects(sprite, x, y, w, h) {
    return sprite.x + sprite.width > x && sprite.x < x + w && sprite.y + sprite.height > y && sprite.y < y + h;
}

function parseSpriteDiagnostics(text) {
    const lines = String(text || '').split('\n').map(line => line.trim()).filter(Boolean);
    return lines.filter(line => line.startsWith('ch=')).map(line => {
        const get = key => {
            const m = line.match(new RegExp(`${key}=([^\\s]+)`));
            return m ? m[1] : '';
        };
        const locSize = line.match(/loc=([-\d]+,[-\d]+) (\d+)x(\d+)/);
        const loc = locSize ? locSize[1].split(',').map(Number) : [];
        const width = locSize ? Number(locSize[2]) : NaN;
        const height = locSize ? Number(locSize[3]) : NaN;
        const sprite = {
            channel: Number(get('ch')),
            z: Number(get('z')),
            x: loc.length === 2 ? loc[0] : NaN,
            y: loc.length === 2 ? loc[1] : NaN,
            width,
            height,
            type: get('type'),
            ink: get('ink'),
            blend: get('blend'),
            member: get('member'),
            castName: get('castName'),
            dynName: get('dynName'),
            line,
        };
        return sprite;
    }).filter(s => Number.isFinite(s.x) && Number.isFinite(s.width) && Number.isFinite(s.height) && s.channel > 0);
}

function parseTextDiagnostics(text) {
    const result = new Map();
    const lines = String(text || '').split('\n');
    let current = null;
    for (const line of lines) {
        const chMatch = line.match(/^ch=(\d+)/);
        if (chMatch) {
            current = Number(chMatch[1]);
        }
        const textMatch = line.match(/text=\"([^\"]*)\"/);
        if (textMatch && current !== null) {
            const existing = result.get(current);
            result.set(current, existing ? `${existing}\n${textMatch[1]}` : textMatch[1]);
        }
    }
    return result;
}

function toCandidates(points, labelPrefix, memberName) {
    return points.map((point, idx) => ({
        channel: 0,
        x: point[0],
        y: point[1],
        w: point[2],
        h: point[3],
        text: `${labelPrefix}_${idx + 1}`,
        member: memberName,
    }));
}

const MANUAL_GO_CANDIDATES = toCandidates([
    [671, 441, 24, 20],
    [876, 183, 70, 24],
    [895, 441, 70, 24],
    [870, 122, 76, 32],
    [870, 146, 76, 32],
    [870, 170, 76, 32],
    [870, 194, 76, 32],
], 'manual_go', 'manual');

const MANUAL_PHOTO_CANDIDATES = toCandidates([
    [387, 70, 160, 160],
    [401, 78, 160, 160],
    [415, 86, 160, 160],
    [429, 94, 160, 160],
    [393, 106, 160, 160],
    [409, 114, 160, 160],
    [425, 122, 160, 160],
    [441, 130, 160, 160],
], 'manual_photo', 'manual');

function prependUniqueCandidates(baseCandidates, extraCandidates) {
    const deduped = new Set();
    const result = [];
    for (const candidate of [...extraCandidates, ...baseCandidates]) {
        const key = `${candidate.x},${candidate.y},${candidate.w},${candidate.h}`;
        if (deduped.has(key)) {
            continue;
        }
        deduped.add(key);
        result.push(candidate);
    }
    return result;
}

function collectByRules(sprites, textByChannel, mode) {
    const byMode = {
        rooms: s => s.y < 220 && s.height >= 10 && s.width >= 40 && s.width <= 930 && s.height <= 140,
        go: s => s.x > 600 && s.width > 20 && s.width < 220 && s.height > 8 && s.height < 180 && s.y > 90 && s.y < 520,
        photo: s => s.width > 20 && s.height > 20,
        all: _ => true,
    };
    const hasModeHint = typeof byMode[mode] === 'function' ? byMode[mode] : (() => true);

    const base = sprites.filter(hasModeHint).filter(s => s.x + s.width > 0 && s.y + s.height > 0);
    const textHints = new Map();

    if (mode === 'rooms') {
        for (const sprite of base) {
            const label = String(textByChannel.get(sprite.channel) || '').toLowerCase();
            const name = `${sprite.member} ${sprite.castName} ${sprite.dynName}`.toLowerCase();
            if (name.includes('room') || label.includes('room')) {
                textHints.set(sprite.channel, true);
            }
        }
    }

    if (mode === 'photo') {
        const stageW = V31.width;
        const stageH = V31.height;
        const sx = stageW / 959.0;
        const sy = stageH / 540.0;
        const hintX = Math.round(415 * sx);
        const hintY = Math.round(101 * sy);
        const hintW = Math.max(1, Math.round(175 * sx));
        const hintH = Math.max(1, Math.round(194 * sy));
        const minX = Math.max(0, hintX - 6);
        const minY = Math.max(0, hintY - 6);
        const maxX = Math.min(stageW, hintX + hintW + 6);
        const maxY = Math.min(stageH, hintY + hintH + 6);
        for (const sprite of base) {
            if (intersects(sprite, minX, minY, maxX - minX, maxY - minY)) {
                textHints.set(sprite.channel, true);
            }
        }
    }

    const prioritized = mode === 'rooms'
        ? base.sort((a, b) => (textHints.get(b.channel) ? 1 : 0) - (textHints.get(a.channel) ? 1 : 0) || (b.width * b.height - a.width * a.height))
        : base.sort((a, b) => (b.width * b.height - a.width * a.height));

    if (mode === 'go' && prioritized.length === 0) {
        return base
            .filter(s => s.x > 600 && s.y > 90 && s.width > 20 && s.width < 260 && s.height > 8 && s.height < 200)
            .map(s => ({
                channel: s.channel,
                x: Math.round(s.x + Math.max(1, s.width / 2)),
                y: Math.round(s.y + Math.max(1, s.height / 2)),
                w: s.width,
                h: s.height,
                line: s.line,
                member: s.member,
                text: textByChannel.get(s.channel) || '',
            }))
            .filter((candidate, idx, all) => all.findIndex(other => other.x === candidate.x && other.y === candidate.y) === idx)
            .sort((a, b) => (b.width * b.height) - (a.width * a.height));
    }

    return prioritized.map(s => ({
        channel: s.channel,
        x: Math.round(s.x + Math.max(1, s.width / 2)),
        y: Math.round(s.y + Math.max(1, s.height / 2)),
        w: s.width,
        h: s.height,
        line: s.line,
        member: s.member,
        text: textByChannel.get(s.channel) || '',
    })).filter((candidate, idx, all) => all.findIndex(other => other.x === candidate.x && other.y === candidate.y) === idx);
}

async function collectDiagnostics(page) {
    const [windowSprites, visibleText] = await Promise.all([
        page.evaluate(() => window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics ? window.betaClientPlayer.getWindowSpriteDiagnostics() : ''),
        page.evaluate(() => window.betaClientPlayer ? window.betaClientPlayer.getVisibleTextDiagnostics() : ''),
    ]);
    const sprites = parseSpriteDiagnostics(windowSprites);
    const textByChannel = parseTextDiagnostics(visibleText);
    return { sprites, textByChannel };
}

async function captureAndCompare(page, referenceUrl, expectedWidth, expectedHeight) {
    return page.evaluate(async ({ referenceUrl, expectedWidth, expectedHeight }) => {
        const canvas = document.getElementById('beta-client-stage');
        if (!canvas) {
            return { error: 'no-canvas' };
        }
        let width = canvas.width;
        let height = canvas.height;

        const ref = await new Promise((resolve, reject) => {
            const img = new Image();
            img.onload = () => resolve(img);
            img.onerror = () => reject(new Error('Could not load reference image'));
            img.src = referenceUrl;
        });
        if (!ref.naturalWidth || !ref.naturalHeight) {
            return { error: `Reference dimensions unavailable for ${referenceUrl}` };
        }
        const compareWidth = Math.max(1, Math.min(width, expectedWidth > 0 ? expectedWidth : width, ref.naturalWidth));
        const compareHeight = Math.max(1, Math.min(height, expectedHeight > 0 ? expectedHeight : height, ref.naturalHeight));

        width = compareWidth;
        height = compareHeight;

        const actual = document.createElement('canvas');
        actual.width = width;
        actual.height = height;
        const actualCtx = actual.getContext('2d');
        actualCtx.fillStyle = '#000';
        actualCtx.fillRect(0, 0, width, height);
        actualCtx.drawImage(canvas, 0, 0);

        const reference = document.createElement('canvas');
        reference.width = width;
        reference.height = height;
        reference.getContext('2d').drawImage(ref, 0, 0);

        const actualData = actualCtx.getImageData(0, 0, width, height).data;
        const referenceData = reference.getContext('2d').getImageData(0, 0, width, height).data;

        let total = 0;
        let max = 0;
        let bad = 0;
        let matching = 0;
        const pixels = width * height;
        const diff = document.createElement('canvas');
        diff.width = width;
        diff.height = height;
        const diffCtx = diff.getContext('2d');
        const diffData = diffCtx.createImageData(width, height);

        for (let p = 0; p < actualData.length; p += 4) {
            const dr = Math.abs(actualData[p] - referenceData[p]);
            const dg = Math.abs(actualData[p + 1] - referenceData[p + 1]);
            const db = Math.abs(actualData[p + 2] - referenceData[p + 2]);
            const delta = (dr + dg + db) / 3;
            total += delta;
            max = Math.max(max, delta);
            if (delta <= 8) matching++;
            if (delta > 64) bad++;
            diffData.data[p] = delta;
            diffData.data[p + 1] = 0;
            diffData.data[p + 2] = 255 - delta;
            diffData.data[p + 3] = 255;
        }
        diffCtx.putImageData(diffData, 0, 0);

        return {
            width,
            height,
            meanDelta: total / pixels,
            badFraction: bad / pixels,
            matchFraction: matching / pixels,
            maxDelta: max,
            actualPng: actual.toDataURL('image/png'),
            diffPng: diff.toDataURL('image/png'),
            tick: window._testState.tick,
            frame: window._testState.frame,
            spriteCount: window.betaClientPlayer ? window.betaClientPlayer._lastSpriteCount || 0 : 0,
            errors: window._testState.error,
            reference: {
                naturalWidth: ref.naturalWidth,
                naturalHeight: ref.naturalHeight,
            },
        };
    }, { referenceUrl, expectedWidth, expectedHeight });
}

function referenceUrl(baseUrl, referencePath) {
    return `${baseUrl}/${path.basename(referencePath)}`;
}

async function waitForMatch(page, referencePath, baseUrl, step, thresholdDelta, thresholdBad, maxPolls = maxNativePolls, expectedWidth, expectedHeight) {
    let lastResult = null;
    for (let i = 0; i < maxPolls; i++) {
        await new Promise(resolve => setTimeout(resolve, pollMs));
        const result = await captureAndCompare(page, referenceUrl(baseUrl, referencePath), expectedWidth, expectedHeight);
        lastResult = result;
        if (!result || result.error) {
            throw new Error(`[${step}] ${result ? result.error : 'missing capture'} at tick=${i}`);
        }
        if (result.meanDelta <= thresholdDelta && result.badFraction <= thresholdBad) {
            return result;
        }
        if (i % 20 === 0) {
            const diag = await page.evaluate(() => ({ tick: window._testState.tick, frame: window._testState.frame, spriteCount: window.betaClientPlayer ? window.betaClientPlayer._lastSpriteCount || 0 : 0 }));
            console.log(`  [${step}] poll=${i} mean=${result.meanDelta.toFixed(2)} bad=${(result.badFraction * 100).toFixed(2)}% match=${(result.matchFraction * 100).toFixed(2)} sprites=${diag.spriteCount}`);
        }
    }
    throw new Error(`[${step}] Did not reach target reference. lastMean=${lastResult ? lastResult.meanDelta : 'n/a'} lastBad=${lastResult ? lastResult.badFraction : 'n/a'}`);
}

async function saveComparison(page, refPath, outBase, result) {
    if (!result) return;
    fs.writeFileSync(path.join(outputDir, `${outBase}.png`), Buffer.from(result.actualPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
    fs.writeFileSync(path.join(outputDir, `${outBase}_diff.png`), Buffer.from(result.diffPng.replace(/^data:image\/png;base64,/, ''), 'base64'));
    fs.writeFileSync(path.join(outputDir, `${outBase}_compare.txt`), JSON.stringify({
        reference: refPath,
        meanDelta: result.meanDelta,
        badFraction: result.badFraction,
        matchFraction: result.matchFraction,
        maxDelta: result.maxDelta,
        tick: result.tick,
        frame: result.frame,
        spriteCount: result.spriteCount,
    }, null, 2));
}

async function clickPoint(page, point) {
    await page.mouse.move(point.x, point.y);
    await page.mouse.down({ button: 'left' });
    await page.mouse.up({ button: 'left' });
    await new Promise(resolve => setTimeout(resolve, settleMs));
}

async function clickUntilMatch(page, candidates, targetRefPath, stepName, baseUrl, maxAttempts = 24, expectedWidth, expectedHeight) {
    const seen = new Set();
    for (let i = 0; i < candidates.length && i < maxAttempts; i++) {
        const candidate = candidates[i];
        const key = `${candidate.x},${candidate.y},${candidate.w},${candidate.h}`;
        if (seen.has(key)) continue;
        seen.add(key);

        console.log(`  [${stepName}] trying ch=${candidate.channel} ${candidate.text || ''} member=${candidate.member} point=(${candidate.x},${candidate.y}) size=${candidate.w}x${candidate.h}`);
        await clickPoint(page, candidate);
        try {
            const match = await waitForMatch(page, targetRefPath, baseUrl, `${stepName}_attempt_${i}`, V31.maxMeanDelta, V31.maxBadFraction, 220, expectedWidth, expectedHeight);
            await saveComparison(page, targetRefPath, `${stepName}_match`, match);
            return match;
        } catch (err) {
            console.log(`  [${stepName}] attempt ${i} did not match: ${err.message}`);
            fs.writeFileSync(path.join(outputDir, `${stepName}_attempt_${i}_err.txt`), `${err.stack || err.message}\n`);
        }
    }
    throw new Error(`[${stepName}] No candidates reached target ref after ${maxAttempts} attempts.`);
}

async function findAndDumpCandidates(page, mode) {
    const { sprites, textByChannel } = await collectDiagnostics(page);
    const candidates = collectByRules(sprites, textByChannel, mode);
    fs.writeFileSync(path.join(outputDir, `${mode}_candidates.txt`), candidates.map(candidate => {
        const text = candidate.text ? candidate.text.replace(/\r/g, ' ').replace(/\n/g, ' | ') : '';
        return `channel=${candidate.channel} x=${candidate.x} y=${candidate.y} w=${candidate.w} h=${candidate.h} member=${candidate.member} text=${text}`;
    }).join('\n'));
    return candidates;
}

async function roomEnterTestV31(page, baseUrl) {
    console.log('=== roomEnterTestV31 ===');
    console.log('Waiting for entered-room stage...');
    const enteredTarget = await waitForMatch(page, V31.enteredPath, baseUrl, 'roomEnterTestV31_rooms_entered', V31.maxMeanDelta, V31.maxBadFraction, maxNativePolls, 959, 540);
    await saveComparison(page, V31.enteredPath, 'roomEnterTestV31_03_entered', enteredTarget);
    await saveComparison(page, V31.enteredPath, '03_entered', enteredTarget);

    const roomDiagnostics = await page.evaluate(async () => {
        const visibleText = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
            ? await window.betaClientPlayer.getVisibleTextDiagnostics()
            : '';
        const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
            ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
            : '';
        return { visibleText, windowSprites };
    });
    fs.writeFileSync(path.join(outputDir, 'roomEnterTestV31_visible_text.txt'), roomDiagnostics.visibleText || '');
    fs.writeFileSync(path.join(outputDir, 'roomEnterTestV31_window_sprites.txt'), roomDiagnostics.windowSprites || '');

    console.log('PASS: roomEnterTestV31 reached entered-room reference without clicking Rooms or Go.');
    return enteredTarget;
}

async function main() {
    console.log('=== WASM v31 room-enter regression ===');
    if (runLegacyMovieRegressions) {
        runLegacyNativeVisualRegression();
    }
    const requiredFiles = [
        path.join(distPath, 'shockwave-lib.js'),
        path.join(distPath, 'shockwave-worker.js'),
        V31.nativePath,
        V31.roomsPath,
        V31.enteredPath,
    ];
    for (const file of requiredFiles) {
        if (!fs.existsSync(file)) {
            throw new Error('Required file not found: ' + file);
        }
    }
    fs.mkdirSync(outputDir, { recursive: true });
    const { server, port } = await createServer();
    const baseUrl = `http://127.0.0.1:${port}`;
    const { browser, newPage } = await loadBrowser();

    let page;
    try {
        const htmlPath = path.join(distPath, '_test_v31_room_photo_hotspot.html');
        fs.writeFileSync(htmlPath, htmlForCase(baseUrl, V31));

        page = await newPage();
        const log = [];
        page.on('console', msg => {
            const text = msg.text();
            log.push(text);
            console.log('  [page]', text);
        });
        page.on('requestfailed', req => {
            log.push(`requestfailed ${req.url()}`);
        });

        await page.goto(`${baseUrl}/_test_v31_room_photo_hotspot.html`, { waitUntil: 'domcontentloaded' });
        await waitForWorkerReady(page);
        await page.evaluate(() => window.startNativeCase());

        const enteredTarget = await roomEnterTestV31(page, baseUrl);

        console.log('PASS: v31 room-enter regression reached expected reference.');
        fs.writeFileSync(path.join(outputDir, 'result.txt'), JSON.stringify({
            status: 'PASS',
            test: 'roomEnterTestV31',
            enteredRoomPath: path.join(outputDir, 'roomEnterTestV31_03_entered.png'),
            reference: V31.enteredPath,
            meanDelta: enteredTarget.meanDelta,
            badFraction: enteredTarget.badFraction,
            matchFraction: enteredTarget.matchFraction,
            maxDelta: enteredTarget.maxDelta,
        }, null, 2) + '\n');

        const diagnostics = await page.evaluate(async () => {
            const visibleText = window.betaClientPlayer && window.betaClientPlayer.getVisibleTextDiagnostics
                ? await window.betaClientPlayer.getVisibleTextDiagnostics()
                : '';
            const windowSprites = window.betaClientPlayer && window.betaClientPlayer.getWindowSpriteDiagnostics
                ? await window.betaClientPlayer.getWindowSpriteDiagnostics()
                : '';
            const bootstrap = window.betaClientPlayer && window.betaClientPlayer.getBootstrapDiagnostics
                ? await window.betaClientPlayer.getBootstrapDiagnostics()
                : '';
            return {
                visibleText,
                windowSprites,
                bootstrap,
                tick: window._testState.tick,
                frame: window._testState.frame,
            };
        });
        fs.writeFileSync(path.join(outputDir, 'final_visible_text.txt'), diagnostics.visibleText || '');
        fs.writeFileSync(path.join(outputDir, 'final_window_sprites.txt'), diagnostics.windowSprites || '');
        fs.writeFileSync(path.join(outputDir, 'final_bootstrap.txt'), diagnostics.bootstrap || '');
        fs.writeFileSync(path.join(outputDir, 'final_state.txt'), JSON.stringify({ tick: diagnostics.tick, frame: diagnostics.frame }, null, 2));
        fs.writeFileSync(path.join(outputDir, 'browser_console.log'), log.join('\n'));
    } finally {
        if (page) {
            await page.close();
        }
        await browser.close();
        try { await server.close(); } catch (e) {}
    }
}

function runLegacyNativeVisualRegression() {
    const nativeScript = path.join(__dirname, 'wasm-native-visual-regression-test.js');
    const legacyOutputDir = path.join(outputDir, 'legacy-native-visual');
    const requiredFiles = [
        nativeScript,
        path.join(nativeVisualReferenceDir, 'v1_native.png'),
        path.join(nativeVisualReferenceDir, 'v14_native.png'),
        path.join(nativeVisualReferenceDir, 'loader.md'),
    ];
    for (const file of requiredFiles) {
        if (!fs.existsSync(file)) {
            throw new Error('Required legacy visual regression file not found: ' + file);
        }
    }

    console.log('=== WASM v1/v14 offline native visual regression ===');
    console.log('  using reference dir:', nativeVisualReferenceDir);
    console.log('  no host/proxy connection is required for this check');

    const result = spawnSync(process.execPath, [nativeScript, distPath, legacyOutputDir], {
        cwd: process.cwd(),
        env: {
            ...process.env,
            LS_NATIVE_VISUAL_CASES: 'v1,v14',
            LS_NATIVE_REFERENCE_DIR: nativeVisualReferenceDir,
        },
        stdio: 'inherit',
    });
    if (result.error) {
        throw result.error;
    }
    if (result.status !== 0) {
        throw new Error(`Legacy v1/v14 native visual regression failed with exit code ${result.status}`);
    }
}

main().catch(err => {
    console.error('FAIL:', err && err.stack ? err.stack : err);
    process.exitCode = 1;
});
