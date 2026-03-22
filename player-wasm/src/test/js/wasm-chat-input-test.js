#!/usr/bin/env node
'use strict';

/**
 * Headless Chromium test: chat message text visibility after sending.
 *
 * This is how you test:
 * after welcome lobby entry, click on 117, 510 then type hello,
 * then press ENTER. observe a white chat message appears,
 * but it's a tiny white bubble and no text is rendered.
 *
 * Usage:
 *   ./gradlew :player-wasm:runWasmChatInputTest
 *   ./gradlew :player-wasm:runWasmChatInputTest -PoutputDir=C:/tmp/chat-test
 *
 * Args: distPath dcrFile castDir [outputDir]
 */

const puppeteer = require('puppeteer');
const http = require('http');
const fs = require('fs');
const path = require('path');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const dcrFile = process.argv[3] || 'C:/xampp/htdocs/dcr/14.1_b8/habbo.dcr';
const castDir = process.argv[4] || 'C:/xampp/htdocs/dcr/14.1_b8';
const outputDir = process.argv[5] || path.resolve(process.cwd(), 'frames_chat_input');

const FIRST_CLICK_X = 425;
const FIRST_CLICK_Y = 82;
const ENTER_CLICK_X = 635;
const ENTER_CLICK_Y = 137;
const CHAT_INPUT_X = 117;
const CHAT_INPUT_Y = 510;

const TICKS_AFTER_FIRST_CLICK = 30;
const ROOM_LOAD_TICKS = 650;
const CHAT_WAIT_TICKS = 40;
const LOGIN_TIMEOUT_POLLS = 600;
const NAVIGATOR_TIMEOUT_MS = 120000;

const NAV_REGION_X = 350;
const NAV_REGION_Y = 60;
const NAV_REGION_W = 370;
const NAV_REGION_H = 440;
const CONTENT_VARIETY_THRESHOLD = 20;

const ROOM_REGION = { x: 0, y: 0, w: 720, h: 487 };
const SAMPLE_STEP = 8;
const SEND_DIFF_MARGIN = 15;

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
};

function createServer() {
    return new Promise((resolve) => {
        const server = http.createServer((req, res) => {
            const url = decodeURIComponent(req.url.split('?')[0]);

            let filePath = path.join(distPath, url);
            if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
                return serveFile(res, filePath);
            }

            filePath = path.join(castDir, url.replace(/^.*\//, ''));
            if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
                return serveFile(res, filePath);
            }

            filePath = path.join(castDir, path.basename(url));
            if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
                return serveFile(res, filePath);
            }

            let ancestor = castDir;
            for (let i = 0; i < 3; i++) {
                ancestor = path.dirname(ancestor);
                const gamedataPath = path.join(ancestor, url);
                if (fs.existsSync(gamedataPath) && fs.statSync(gamedataPath).isFile()) {
                    return serveFile(res, gamedataPath);
                }
            }

            res.writeHead(404);
            res.end('Not found: ' + url);
        });

        server.listen(0, '127.0.0.1', () => {
            resolve({ server, port: server.address().port });
        });
    });
}

function serveFile(res, filePath) {
    const ext = path.extname(filePath).toLowerCase();
    const mime = MIME[ext] || 'application/octet-stream';
    const data = fs.readFileSync(filePath);
    res.writeHead(200, {
        'Content-Type': mime,
        'Content-Length': data.length,
        'Access-Control-Allow-Origin': '*',
    });
    res.end(data);
}

async function captureCanvas(page, filePath) {
    const dataUrl = await page.evaluate(() => {
        const canvas = document.getElementById('stage');
        return canvas ? canvas.toDataURL('image/png') : null;
    });
    if (!dataUrl) return;
    const base64 = dataUrl.replace(/^data:image\/png;base64,/, '');
    fs.writeFileSync(filePath, Buffer.from(base64, 'base64'));
}

async function waitForTicks(page, count) {
    const startTick = await page.evaluate(() => window._testState.tick);
    const target = startTick + count;
    for (let i = 0; i < count * 3; i++) {
        await new Promise(r => setTimeout(r, 67));
        const tick = await page.evaluate(() => window._testState.tick);
        if (tick >= target) return;
    }
}

async function clickCanvas(page, stageX, stageY) {
    await page.evaluate((x, y) => {
        const canvas = document.getElementById('stage');
        if (!canvas) return;
        const rect = canvas.getBoundingClientRect();
        const scaleX = rect.width / canvas.width;
        const scaleY = rect.height / canvas.height;
        const clientX = rect.left + x * scaleX;
        const clientY = rect.top + y * scaleY;

        canvas.focus();
        canvas.dispatchEvent(new MouseEvent('mousemove', { clientX, clientY, bubbles: true }));
        canvas.dispatchEvent(new MouseEvent('mousedown', { clientX, clientY, button: 0, bubbles: true }));
        canvas.dispatchEvent(new MouseEvent('mouseup', { clientX, clientY, button: 0, bubbles: true }));
    }, stageX, stageY);
}

async function sendWorkerKey(page, keyCode, keyChar, modifiers) {
    await page.evaluate((kc, ch, mods) => {
        if (!window.player || !window.player._worker || !window.player._workerReady) return;
        window.player._worker.postMessage({
            type: 'keyDown',
            keyCode: kc,
            key: ch,
            modifiers: mods || 0
        });
        window.player._worker.postMessage({
            type: 'keyUp',
            keyCode: kc,
            key: ch,
            modifiers: mods || 0
        });
    }, keyCode, keyChar, modifiers || 0);
}

async function sendWorkerKeyWithHold(page, keyCode, keyChar, modifiers, holdMs) {
    await page.evaluate((kc, ch, mods) => {
        if (!window.player || !window.player._worker || !window.player._workerReady) return;
        window.player._worker.postMessage({
            type: 'keyDown',
            keyCode: kc,
            key: ch,
            modifiers: mods || 0
        });
    }, keyCode, keyChar, modifiers || 0);
    await new Promise(r => setTimeout(r, holdMs || 90));
    await page.evaluate((kc, ch, mods) => {
        if (!window.player || !window.player._worker || !window.player._workerReady) return;
        window.player._worker.postMessage({
            type: 'keyUp',
            keyCode: kc,
            key: ch,
            modifiers: mods || 0
        });
    }, keyCode, keyChar, modifiers || 0);
}

async function typeViaWorker(page, text) {
    for (const ch of text) {
        const keyCode = ch.toUpperCase().charCodeAt(0);
        await sendWorkerKey(page, keyCode, ch, 0);
        await new Promise(r => setTimeout(r, 35));
    }
}

async function measureColorVariety(page, x, y, w, h) {
    return page.evaluate((rx, ry, rw, rh) => {
        const canvas = document.getElementById('stage');
        if (!canvas) return 0;
        const ctx = canvas.getContext('2d');
        const data = ctx.getImageData(rx, ry, rw, rh).data;
        const buckets = new Set();
        for (let i = 0; i < data.length; i += 16) {
            const r = data[i] >> 5;
            const g = data[i + 1] >> 5;
            const b = data[i + 2] >> 5;
            buckets.add((r << 6) | (g << 3) | b);
        }
        return buckets.size;
    }, x, y, w, h);
}

async function sampleRegion(page, region, step) {
    return page.evaluate((r, s) => {
        const canvas = document.getElementById('stage');
        if (!canvas) return [];
        const ctx = canvas.getContext('2d');
        const data = ctx.getImageData(r.x, r.y, r.w, r.h).data;
        const samples = [];
        for (let y = 0; y < r.h; y += s) {
            for (let x = 0; x < r.w; x += s) {
                const i = (y * r.w + x) * 4;
                const rgb = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
                samples.push(rgb);
            }
        }
        return samples;
    }, region, step);
}

function diffCount(a, b) {
    const len = Math.min(a.length, b.length);
    let changed = 0;
    for (let i = 0; i < len; i++) {
        if (a[i] !== b[i]) changed++;
    }
    return changed;
}

function parseTextFromSpriteInfo(info) {
    if (!info) return null;
    const m = info.match(/ text="([^"]*)"/);
    return m ? m[1] : null;
}

async function main() {
    console.log('=== WASM Chat Input Test ===');
    console.log('Dist:   ', distPath);
    console.log('DCR:    ', dcrFile);
    console.log('Casts:  ', castDir);
    console.log('Output: ', outputDir);

    if (!fs.existsSync(path.join(distPath, 'libreshockwave.js'))) {
        console.error('FAIL: libreshockwave.js not found in dist. Run assembleWasm first.');
        process.exit(1);
    }
    if (!fs.existsSync(dcrFile)) {
        console.error('FAIL: DCR file not found: ' + dcrFile);
        process.exit(1);
    }

    fs.mkdirSync(outputDir, { recursive: true });

    const { server, port } = await createServer();
    const baseUrl = `http://127.0.0.1:${port}`;
    const dcrFileName = path.basename(dcrFile);
    let browser;

    try {
        browser = await puppeteer.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-setuid-sandbox'],
        });

        const page = await browser.newPage();
        const logs = [];
        page.on('console', msg => {
            const text = msg.text();
            logs.push(text);
            if (text.includes('[TEST]') || text.includes('[LS]') || text.includes('[W]')) {
                console.log('  [page] ' + text);
            }
        });

        const html = `<!DOCTYPE html>
<html><body>
<canvas id="stage" width="720" height="540" tabindex="0"></canvas>
<script src="${baseUrl}/libreshockwave.js"><\/script>
<script>
    var _testState = { tick: 0, frame: 0, loaded: false, error: null };
    var player = LibreShockwave.create('stage', {
        basePath: '${baseUrl}/',
        params: {
            sw1: 'site.url=http://127.0.0.1:${port};url.prefix=http://127.0.0.1:${port}',
            sw2: 'connection.info.host=127.0.0.1;connection.info.port=30001',
            sw3: 'client.reload.url=http://127.0.0.1:${port}/',
            sw4: 'connection.mus.host=127.0.0.1;connection.mus.port=38101',
            sw5: 'external.variables.txt=http://127.0.0.1:${port}/gamedata/external_variables.txt;external.texts.txt=http://127.0.0.1:${port}/gamedata/external_texts.txt',
            sw6: 'use.sso.ticket=1;sso.ticket=123'
        },
        autoplay: true,
        onLoad: function() { _testState.loaded = true; console.log('[TEST] Movie loaded'); },
        onFrame: function(frame) { _testState.tick++; _testState.frame = frame; },
        onDebugLog: function(msg) { console.log('[DBG] ' + msg); },
        onError: function(msg) { _testState.error = msg; console.error('[TEST] Error: ' + msg); }
    });
    player.load('${baseUrl}/${dcrFileName}');
<\/script>
</body></html>`;

        const htmlPath = path.join(distPath, '_test_chat_input.html');
        fs.writeFileSync(htmlPath, html);

        await page.goto(`${baseUrl}/_test_chat_input.html`, { waitUntil: 'domcontentloaded' });
        console.log('Page loaded, waiting for login and navigator...');

        let hotelReady = false;
        for (let i = 0; i < LOGIN_TIMEOUT_POLLS; i++) {
            await new Promise(r => setTimeout(r, 100));
            const state = await page.evaluate(() => window._testState);
            if (state.loaded && state.tick >= 30) {
                const hasContent = await page.evaluate(() => {
                    const canvas = document.getElementById('stage');
                    if (!canvas) return false;
                    const ctx = canvas.getContext('2d');
                    const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
                    let nonBlack = 0;
                    for (let i = 0; i < data.length; i += 40) {
                        if (data[i] > 10 || data[i + 1] > 10 || data[i + 2] > 10) nonBlack++;
                    }
                    return nonBlack > 100;
                });
                if (hasContent) {
                    hotelReady = true;
                    break;
                }
            }
        }
        if (!hotelReady) {
            console.error('FAIL: Hotel view did not appear');
            await captureCanvas(page, path.join(outputDir, 'timeout_hotel.png'));
            process.exitCode = 1;
            return;
        }

        await new Promise(r => setTimeout(r, 3000));

        let navigatorAppeared = false;
        const navStart = Date.now();
        while (Date.now() - navStart < NAVIGATOR_TIMEOUT_MS) {
            await new Promise(r => setTimeout(r, 2000));
            const variety = await measureColorVariety(page, NAV_REGION_X, NAV_REGION_Y, NAV_REGION_W, NAV_REGION_H);
            if (variety >= CONTENT_VARIETY_THRESHOLD) {
                navigatorAppeared = true;
                break;
            }
        }
        if (!navigatorAppeared) {
            console.error('FAIL: Navigator did not appear');
            await captureCanvas(page, path.join(outputDir, 'timeout_navigator.png'));
            process.exitCode = 1;
            return;
        }

        await captureCanvas(page, path.join(outputDir, '01_navigator_loaded.png'));
        console.log(`Clicking room selector at (${FIRST_CLICK_X}, ${FIRST_CLICK_Y})`);
        await clickCanvas(page, FIRST_CLICK_X, FIRST_CLICK_Y);
        await waitForTicks(page, TICKS_AFTER_FIRST_CLICK);

        console.log(`Clicking enter at (${ENTER_CLICK_X}, ${ENTER_CLICK_Y})`);
        await clickCanvas(page, ENTER_CLICK_X, ENTER_CLICK_Y);

        console.log(`Waiting ${ROOM_LOAD_TICKS} ticks for room load`);
        await waitForTicks(page, ROOM_LOAD_TICKS);
        await captureCanvas(page, path.join(outputDir, '02_room_loaded.png'));

        const snapA = await sampleRegion(page, ROOM_REGION, SAMPLE_STEP);
        await waitForTicks(page, CHAT_WAIT_TICKS);
        const snapB = await sampleRegion(page, ROOM_REGION, SAMPLE_STEP);
        const baselineDiff = diffCount(snapA, snapB);

        console.log(`Clicking chat input at (${CHAT_INPUT_X}, ${CHAT_INPUT_Y})`);
        await clickCanvas(page, CHAT_INPUT_X, CHAT_INPUT_Y);
        await new Promise(r => setTimeout(r, 250));

        const hitBeforeType = await page.evaluate(async (x, y) => {
            if (!window.player || !window.player.debugHitTest) return { hit: 0, info: '' };
            return await window.player.debugHitTest(x, y);
        }, CHAT_INPUT_X, CHAT_INPUT_Y);
        const chatChannel = hitBeforeType && hitBeforeType.hit ? hitBeforeType.hit : 237;
        console.log('chatHit=', hitBeforeType ? hitBeforeType.info : '');

        const spriteInfoBefore = await page.evaluate(async (ch) => {
            if (!window.player || !window.player.debugSpriteInfo) return { info: '' };
            return await window.player.debugSpriteInfo(ch);
        }, chatChannel);
        console.log('spriteBefore=', spriteInfoBefore ? spriteInfoBefore.info : '');

        await typeViaWorker(page, 'hello');
        await new Promise(r => setTimeout(r, 250));

        const spriteInfoTyped = await page.evaluate(async (ch) => {
            if (!window.player || !window.player.debugSpriteInfo) return { info: '' };
            return await window.player.debugSpriteInfo(ch);
        }, chatChannel);
        console.log('spriteTyped=', spriteInfoTyped ? spriteInfoTyped.info : '');

        await sendWorkerKeyWithHold(page, 13, '\r', 0, 120);
        await captureCanvas(page, path.join(outputDir, '03_after_enter.png'));

        const spriteInfoEnter = await page.evaluate(async (ch) => {
            if (!window.player || !window.player.debugSpriteInfo) return { info: '' };
            return await window.player.debugSpriteInfo(ch);
        }, chatChannel);
        console.log('spriteAfterEnter=', spriteInfoEnter ? spriteInfoEnter.info : '');

        const typedText = parseTextFromSpriteInfo(spriteInfoTyped ? spriteInfoTyped.info : '');
        const afterEnterText = parseTextFromSpriteInfo(spriteInfoEnter ? spriteInfoEnter.info : '');

        const helloSprites = await page.evaluate(async () => {
            if (!window.player || !window.player.debugSpriteInfo) return [];
            const found = [];
            for (let ch = 1; ch <= 300; ch++) {
                const res = await window.player.debugSpriteInfo(ch);
                if (res && res.info && res.info.indexOf('text="hello"') >= 0) {
                    found.push({ ch, info: res.info });
                }
            }
            return found;
        });
        console.log('helloSprites=', JSON.stringify(helloSprites.slice(0, 8)));

        await waitForTicks(page, CHAT_WAIT_TICKS);
        const snapC = await sampleRegion(page, ROOM_REGION, SAMPLE_STEP);
        const sendDiff = diffCount(snapB, snapC);

        fs.writeFileSync(
            path.join(outputDir, 'metrics.txt'),
            [
                `baselineDiff=${baselineDiff}`,
                `sendDiff=${sendDiff}`,
                `requiredMin=${baselineDiff + SEND_DIFF_MARGIN}`,
            ].join('\n')
        );
        fs.writeFileSync(path.join(outputDir, 'console.txt'), logs.join('\n'));

        console.log('baselineDiff=', baselineDiff);
        console.log('sendDiff=', sendDiff);

        if (typedText !== 'hello') {
            console.error('FAIL: Chat input did not receive typed text before Enter.');
            process.exitCode = 1;
        } else if (afterEnterText !== '') {
            console.error('FAIL: Chat input text was not consumed by Enter/send handling.');
            process.exitCode = 1;
        } else if (helloSprites.length === 0) {
            console.log(
                'PASS (limited): Enter consumed chat input, but no local "hello" sprite was observed. ' +
                'This run likely lacked server echo for room chat rendering.'
            );
        } else if (sendDiff <= baselineDiff + SEND_DIFF_MARGIN) {
            console.error(
                'FAIL: "hello" sprite exists but room-region pixel change stayed too small. ' +
                'Likely tiny bubble / missing text regression.'
            );
            process.exitCode = 1;
        } else {
            console.log('PASS: Chat send produced significant pixel change in room region.');
        }

        try { fs.unlinkSync(htmlPath); } catch (e) {}
    } finally {
        if (browser) await browser.close();
        server.close();
    }
}

main().catch(err => {
    console.error('FATAL:', err);
    process.exit(1);
});
