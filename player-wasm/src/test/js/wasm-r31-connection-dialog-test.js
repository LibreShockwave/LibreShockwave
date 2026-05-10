#!/usr/bin/env node
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');
const htdocsRoot = process.argv[3] || 'C:/xampp/htdocs';
const dcrUrlPath = process.argv[4] || '/dcr/r31_20090312_0433_13751_b40895fb6101dbe96dc7b9d6477eeeb4/habbo.dcr';
const outputDir = process.argv[5] || path.resolve(process.cwd(), 'frames_r31_connection_dialog');

const MIME = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.wasm': 'application/wasm',
    '.css': 'text/css',
    '.png': 'image/png',
    '.dcr': 'application/x-director',
    '.cct': 'application/x-director',
    '.txt': 'text/plain',
};

function serveFile(res, filePath) {
    const data = fs.readFileSync(filePath);
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

async function loadBrowser() {
    try {
        const puppeteer = require('puppeteer');
        const browser = await puppeteer.launch({ headless: true, args: ['--no-sandbox', '--disable-setuid-sandbox'] });
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

async function captureCanvas(page, filePath) {
    const dataUrl = await page.evaluate(() => {
        const canvas = document.getElementById('beta-client-stage');
        return canvas ? canvas.toDataURL('image/png') : null;
    });
    if (dataUrl) {
        fs.writeFileSync(filePath, Buffer.from(dataUrl.replace(/^data:image\/png;base64,/, ''), 'base64'));
    }
}

async function main() {
    fs.mkdirSync(outputDir, { recursive: true });
    const { server, port } = await createServer();
    const baseUrl = `http://127.0.0.1:${port}`;
    const movieUrl = `${baseUrl}${dcrUrlPath}?`;
    const htmlPath = path.join(distPath, '_test_r31_connection_dialog.html');

    const html = `<!doctype html>
<html><body>
<canvas id="beta-client-stage" width="720" height="540"></canvas>
<script src="${baseUrl}/shockwave-lib.js"><\/script>
<script>
var _testState = { loaded: false, error: null, tick: 0, frame: 0, gotoNetPages: [] };
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
var betaClientPlayer = LibreShockwave.create("beta-client-stage", {
    basePath: "${baseUrl}/",
    autoplay: true,
    remember: false,
    debugPlayback: true,
    params: betaClientParams,
    onLoad: function() { _testState.loaded = true; betaClientPlayer.play(); },
    onError: function(message) { _testState.error = String(message); },
    onGotoNetPage: function(url, target) { _testState.gotoNetPages.push({ url: String(url), target: String(target) }); },
    onFrame: function(frame) { _testState.tick++; _testState.frame = frame; }
});
window.betaClientPlayer = betaClientPlayer;
betaClientPlayer.load("${movieUrl}");
<\/script>
</body></html>`;
    fs.writeFileSync(htmlPath, html);

    let browser;
    try {
        const loaded = await loadBrowser();
        browser = loaded.browser;
        const page = await loaded.newPage();
        page.on('console', msg => {
            const text = msg.text();
            if (text.includes('[LS]') || text.includes('LibreShockwave')) console.log('  [page] ' + text);
        });

        await page.goto(`${baseUrl}/_test_r31_connection_dialog.html`, { waitUntil: 'domcontentloaded' });
        let found = false;
        for (let i = 0; i < 100; i++) {
            await new Promise(r => setTimeout(r, 1000));
            const state = await page.evaluate(() => {
                const canvas = document.getElementById('beta-client-stage');
                const ctx = canvas.getContext('2d');
                const modalX = 330;
                const modalY = 170;
                const modalW = 310;
                const modalH = 210;
                const data = ctx.getImageData(modalX, modalY, modalW, modalH).data;
                let neutralWhite = 0;
                let black = 0;
                let rowsWithBlack = new Set();
                for (let p = 0; p < data.length; p += 4) {
                    const r = data[p], g = data[p + 1], b = data[p + 2];
                    if (r > 230 && g > 230 && b > 230 && Math.abs(r - g) < 8 && Math.abs(g - b) < 8) neutralWhite++;
                    if (r < 25 && g < 25 && b < 25) {
                        black++;
                        rowsWithBlack.add(Math.floor((p / 4) / modalW));
                    }
                }
                return {
                    tick: window._testState.tick,
                    frame: window._testState.frame,
                    loaded: window._testState.loaded,
                    error: window._testState.error,
                    gotoNetPages: window._testState.gotoNetPages.slice(),
                    spriteCount: window.betaClientPlayer ? (window.betaClientPlayer._lastSpriteCount || 0) : 0,
                    neutralWhite,
                    black,
                    blackRows: rowsWithBlack.size,
                };
            });

            if (i % 2 === 0 || state.neutralWhite > 10000) {
                const name = `r31_dialog_${String(i).padStart(3, '0')}_tick_${state.tick}.png`;
                await captureCanvas(page, path.join(outputDir, name));
                console.log(`  capture ${name}: sprites=${state.spriteCount} white=${state.neutralWhite} black=${state.black} blackRows=${state.blackRows}`);
            }
            if (state.neutralWhite > 18000 && state.black > 600 && state.blackRows > 20) {
                found = true;
                break;
            }
        }

        await captureCanvas(page, path.join(outputDir, 'r31_dialog_final.png'));
        console.log(found ? 'PASS: connection dialog captured' : 'FAIL: connection dialog not detected');
        if (!found) process.exitCode = 1;
    } finally {
        if (browser) await browser.close();
        server.close();
        try { fs.unlinkSync(htmlPath); } catch (e) {}
    }
}

main().catch(err => {
    console.error(err);
    process.exit(1);
});
