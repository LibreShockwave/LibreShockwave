#!/usr/bin/env node
'use strict';

/**
 * Headless Chromium test: Navigator -> Welcome Lobby room-entry error repro.
 *
 * WASM equivalent of NavigatorWelcomeLobbyErrorTest.java.
 *
 * Flow:
 * 1. Load Habbo DCR with SSO ticket to auto-login
 * 2. Wait for the Hotel Navigator window to appear
 * 3. Click (425, 82) — room selector area
 * 4. Wait ~30 ticks
 * 5. Click (635, 137) — "Enter" button
 * 6. Wait up to 450 ticks for a room-entry error
 * 7. Tick 200 extra frames for room update cycle
 *
 * Usage (via Gradle):
 *   ./gradlew :player-wasm:runWasmWelcomeLobbyErrorTest
 *   ./gradlew :player-wasm:runWasmWelcomeLobbyErrorTest -PoutputDir=C:/tmp/lobby-test
 *
 * Args: distPath dcrFile castDir [outputDir]
 */

const puppeteer = require('puppeteer');
const http = require('http');
const fs = require('fs');
const path = require('path');

// ---------------------------------------------------------------------------
// Args
// ---------------------------------------------------------------------------
const distPath  = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const dcrFile   = process.argv[3] || 'C:/xampp/htdocs/dcr/14.1_b8/habbo.dcr';
const castDir   = process.argv[4] || 'C:/xampp/htdocs/dcr/14.1_b8';
const outputDir = process.argv[5] || path.resolve(process.cwd(), 'frames_welcome_lobby_error');

// Click coordinates (matching Java test)
const FIRST_CLICK_X = 425;
const FIRST_CLICK_Y = 82;
const ENTER_CLICK_X = 635;
const ENTER_CLICK_Y = 137;

// Tick delays (matching Java test)
const TICKS_AFTER_FIRST_CLICK = 30;
const TICKS_AFTER_SECOND_CLICK = 3;
const ERROR_WAIT_TICKS = 450;
const POST_ERROR_TICKS = 200;

// Timeouts
const LOGIN_TIMEOUT_POLLS = 600;    // 60 seconds
const NAVIGATOR_TIMEOUT_MS = 120000; // 120 seconds for navigator to appear

// Navigator region
const NAV_REGION_X = 350;
const NAV_REGION_Y = 60;
const NAV_REGION_W = 370;
const NAV_REGION_H = 440;

// Color variety threshold for navigator detection
const CONTENT_VARIETY_THRESHOLD = 20;

// Target error patterns
const TARGET_ERROR_PATTERNS = [
    'User object not found',
    'No good object:',
    'Failed to define room object',
];

// ---------------------------------------------------------------------------
// HTTP server
// ---------------------------------------------------------------------------
const MIME = {
    '.html': 'text/html',
    '.js':   'application/javascript',
    '.wasm': 'application/wasm',
    '.css':  'text/css',
    '.png':  'image/png',
    '.dcr':  'application/x-director',
    '.cct':  'application/x-director',
    '.cst':  'application/x-director',
    '.txt':  'text/plain',
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

            console.log('  [404] ' + url);
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

// ---------------------------------------------------------------------------
// Canvas helpers
// ---------------------------------------------------------------------------
async function captureCanvas(page, filePath) {
    const dataUrl = await page.evaluate(() => {
        const canvas = document.getElementById('stage');
        return canvas ? canvas.toDataURL('image/png') : null;
    });
    if (!dataUrl) return;
    const base64 = dataUrl.replace(/^data:image\/png;base64,/, '');
    fs.writeFileSync(filePath, Buffer.from(base64, 'base64'));
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
            const g = data[i+1] >> 5;
            const b = data[i+2] >> 5;
            buckets.add((r << 6) | (g << 3) | b);
        }
        return buckets.size;
    }, x, y, w, h);
}

/**
 * Dispatch a click (mousemove + mousedown + mouseup) on the canvas at stage coordinates.
 */
async function clickCanvas(page, stageX, stageY) {
    await page.evaluate((x, y) => {
        const canvas = document.getElementById('stage');
        if (!canvas) return;
        const rect = canvas.getBoundingClientRect();
        const scaleX = rect.width / canvas.width;
        const scaleY = rect.height / canvas.height;
        const clientX = rect.left + x * scaleX;
        const clientY = rect.top + y * scaleY;

        canvas.dispatchEvent(new MouseEvent('mousemove', {
            clientX, clientY, bubbles: true,
        }));
        canvas.dispatchEvent(new MouseEvent('mousedown', {
            clientX, clientY, button: 0, bubbles: true,
        }));
        canvas.dispatchEvent(new MouseEvent('mouseup', {
            clientX, clientY, button: 0, bubbles: true,
        }));
    }, stageX, stageY);
}

/**
 * Wait for N ticks to elapse in the WASM player.
 */
async function waitForTicks(page, count) {
    const startTick = await page.evaluate(() => window._testState.tick);
    const target = startTick + count;
    // Each poll at 67ms matches the Java test's Thread.sleep(67) per tick
    for (let i = 0; i < count * 3; i++) {
        await new Promise(r => setTimeout(r, 67));
        const tick = await page.evaluate(() => window._testState.tick);
        if (tick >= target) return;
    }
}

function isTargetError(msg) {
    return TARGET_ERROR_PATTERNS.some(p => msg.includes(p));
}

// ---------------------------------------------------------------------------
// Main test
// ---------------------------------------------------------------------------
async function main() {
    console.log('=== WASM Welcome Lobby Error Test ===');
    console.log('Dist:    ', distPath);
    console.log('DCR:     ', dcrFile);
    console.log('Casts:   ', castDir);
    console.log('Output:  ', outputDir);

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
    console.log('Server:  ', baseUrl);

    const dcrFileName = path.basename(dcrFile);

    let browser;
    try {
        browser = await puppeteer.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-setuid-sandbox'],
        });

        const page = await browser.newPage();

        // Collect errors and console output
        const allErrors = [];
        const targetErrors = [];
        const logs = [];

        page.on('console', msg => {
            const text = msg.text();
            logs.push(text);

            // Capture errors reported by onError callback
            if (text.startsWith('[TEST] Error: ')) {
                const errorMsg = text.substring('[TEST] Error: '.length);
                allErrors.push(errorMsg);
                if (isTargetError(errorMsg)) {
                    targetErrors.push(errorMsg);
                    console.log('  [TargetError] ' + errorMsg);
                } else if (allErrors.length <= 20) {
                    console.log('  [Error] ' + errorMsg);
                }
            } else if (text.includes('[TEST]') || text.includes('[LS]')
                || text.includes('[W]') || text.includes('navigator')
                || text.includes('Navigator') || text.includes('[DEBUG')) {
                console.log('  [page] ' + text);
            }
        });

        // Build test HTML with SSO params
        const html = `<!DOCTYPE html>
<html><body>
<canvas id="stage" width="720" height="540"></canvas>
<script src="${baseUrl}/libreshockwave.js"><\/script>
<script>
    var _testState = { tick: 0, frame: 0, loaded: false, error: null, movieWidth: 0, movieHeight: 0 };

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
        onLoad: function(info) {
            _testState.loaded = true;
            _testState.movieWidth = info.width;
            _testState.movieHeight = info.height;
            console.log('[TEST] Movie loaded: ' + info.width + 'x' + info.height + ', ' + info.frameCount + ' frames');
        },
        onFrame: function(frame, total) {
            _testState.tick++;
            _testState.frame = frame;
        },
        onError: function(msg) {
            _testState.error = msg;
            console.error('[TEST] Error: ' + msg);
        }
    });

    player.load('${baseUrl}/${dcrFileName}');
<\/script>
</body></html>`;

        const htmlPath = path.join(distPath, '_test_lobby_error.html');
        fs.writeFileSync(htmlPath, html);

        await page.goto(`${baseUrl}/_test_lobby_error.html`, { waitUntil: 'domcontentloaded' });
        console.log('Page loaded, waiting for SSO login + navigator...');

        const startTime = Date.now();

        // ----- Step 1: Wait for hotel view -----
        let hotelReady = false;
        for (let i = 0; i < LOGIN_TIMEOUT_POLLS; i++) {
            await new Promise(r => setTimeout(r, 100));

            const state = await page.evaluate(() => window._testState);
            if (!state) continue;

            if (state.loaded && state.tick >= 30) {
                const hasContent = await page.evaluate(() => {
                    const canvas = document.getElementById('stage');
                    if (!canvas) return false;
                    const ctx = canvas.getContext('2d');
                    const data = ctx.getImageData(0, 0, canvas.width, canvas.height).data;
                    let nonBlack = 0;
                    for (let i = 0; i < data.length; i += 40) {
                        if (data[i] > 10 || data[i+1] > 10 || data[i+2] > 10) nonBlack++;
                    }
                    return nonBlack > 100;
                });

                if (hasContent) {
                    hotelReady = true;
                    const elapsed = ((Date.now() - startTime) / 1000).toFixed(1);
                    console.log(`  Hotel view ready (frame=${state.frame}, tick=${state.tick}, ${elapsed}s)`);
                    break;
                }
            }

            if (i % 100 === 0 && i > 0) {
                console.log(`  Waiting... tick=${state.tick} frame=${state.frame} loaded=${state.loaded}`);
            }
        }

        if (!hotelReady) {
            console.error('FAIL: Hotel view did not appear within timeout');
            await captureCanvas(page, path.join(outputDir, 'timeout_hotel.png'));
            process.exitCode = 1;
            try { fs.unlinkSync(htmlPath); } catch(e) {}
            return;
        }

        // Let more frames render for navigator to appear after SSO login
        await new Promise(r => setTimeout(r, 3000));

        // ----- Step 2: Wait for navigator window -----
        let navigatorAppeared = false;
        const navStart = Date.now();
        let pollIdx = 0;

        while (Date.now() - navStart < NAVIGATOR_TIMEOUT_MS) {
            await new Promise(r => setTimeout(r, 2000));

            const colorVariety = await measureColorVariety(page, NAV_REGION_X, NAV_REGION_Y, NAV_REGION_W, NAV_REGION_H);
            const sec = ((Date.now() - navStart) / 1000).toFixed(0);
            console.log(`  Navigator poll +${sec}s: variety=${colorVariety}`);

            if (pollIdx < 5) {
                await captureCanvas(page, path.join(outputDir, `00_nav_poll_${String(pollIdx).padStart(2,'0')}.png`));
                pollIdx++;
            }

            if (colorVariety >= CONTENT_VARIETY_THRESHOLD) {
                navigatorAppeared = true;
                console.log('  Navigator appeared');
                break;
            }
        }

        if (!navigatorAppeared) {
            console.error('FAIL: Navigator did not appear within 120 seconds');
            await captureCanvas(page, path.join(outputDir, 'timeout_navigator.png'));
            process.exitCode = 1;
            try { fs.unlinkSync(htmlPath); } catch(e) {}
            return;
        }

        await captureCanvas(page, path.join(outputDir, '01_navigator_loaded.png'));
        console.log('  Saved navigator screenshot');

        // ----- Step 3: First click — room selector (425, 82) -----
        console.log(`Clicking room selector at (${FIRST_CLICK_X}, ${FIRST_CLICK_Y})...`);
        await captureCanvas(page, path.join(outputDir, '02_before_first_click.png'));
        await clickCanvas(page, FIRST_CLICK_X, FIRST_CLICK_Y);
        await waitForTicks(page, TICKS_AFTER_FIRST_CLICK);
        await captureCanvas(page, path.join(outputDir, '02_after_first_click.png'));

        const stateAfterFirst = await page.evaluate(() => window._testState);
        console.log(`  After first click: tick=${stateAfterFirst.tick} frame=${stateAfterFirst.frame}`);

        // ----- Step 4: Second click — enter button (635, 137) -----
        console.log(`Clicking enter at (${ENTER_CLICK_X}, ${ENTER_CLICK_Y})...`);
        await captureCanvas(page, path.join(outputDir, '03_before_enter_click.png'));
        await clickCanvas(page, ENTER_CLICK_X, ENTER_CLICK_Y);
        await waitForTicks(page, TICKS_AFTER_SECOND_CLICK);
        await captureCanvas(page, path.join(outputDir, '03_after_enter_click.png'));

        const stateAfterEnter = await page.evaluate(() => window._testState);
        console.log(`  After enter click: tick=${stateAfterEnter.tick} frame=${stateAfterEnter.frame}`);

        // ----- Step 5: Wait for room-entry error -----
        console.log(`Ticking up to ${ERROR_WAIT_TICKS} frames waiting for room-entry error...`);
        const errorWaitStart = Date.now();
        let errorTick = 0;

        for (let i = 0; i < ERROR_WAIT_TICKS; i++) {
            await new Promise(r => setTimeout(r, 67));

            if (targetErrors.length > 0) {
                console.log(`  Target error reached at tick +${i}`);
                errorTick = i;
                break;
            }

            if (i % 30 === 0) {
                const state = await page.evaluate(() => window._testState);
                console.log(`  post-enter tick ${i}, frame=${state.frame}, tick=${state.tick}`);
            }

            // Capture diagnostic screenshots during the critical window
            if (i >= 20 && i <= 45 && i % 5 === 0) {
                await captureCanvas(page, path.join(outputDir, `04_wait_tick_${i}.png`));
            }
        }

        // Capture error frame
        const errorLabel = targetErrors.length > 0 ? '05_error_frame' : '05_final_no_target_error';
        await captureCanvas(page, path.join(outputDir, errorLabel + '.png'));

        // ----- Step 6: Tick extra frames for room update cycle -----
        console.log(`Ticking ${POST_ERROR_TICKS} extra frames for room update cycle...`);
        for (let i = 0; i < POST_ERROR_TICKS; i++) {
            await new Promise(r => setTimeout(r, 67));
        }
        await captureCanvas(page, path.join(outputDir, '06_late_snapshot.png'));

        // ----- Results -----
        const totalElapsed = ((Date.now() - startTime) / 1000).toFixed(1);

        // Write error log
        fs.writeFileSync(path.join(outputDir, 'errors.txt'), allErrors.join('\n'));

        // Write full console log
        fs.writeFileSync(path.join(outputDir, 'console.txt'), logs.join('\n'));

        console.log('\n--- Results ---');
        console.log('Total errors:       ', allErrors.length);
        console.log('Target errors:      ', targetErrors.length);
        console.log('Output dir:         ', outputDir);
        console.log('Elapsed:            ', totalElapsed + 's');

        if (targetErrors.length > 0) {
            console.log('\nTarget errors found:');
            targetErrors.forEach(e => console.log('  ' + e));
            console.log(`\nPASS: Captured ${targetErrors.length} target error(s) — room-entry bug reproduced in WASM`);
        } else {
            console.log('\nFAIL: Timed out without hitting the expected room-entry error.');
            console.log('Checked for: "User object not found", "No good object:", "Failed to define room object"');
            console.log('Check screenshots in ' + outputDir);
            process.exitCode = 1;
        }

        try { fs.unlinkSync(htmlPath); } catch(e) {}

    } finally {
        if (browser) await browser.close();
        server.close();
    }
}

main().catch(err => {
    console.error('FATAL:', err);
    process.exit(1);
});
