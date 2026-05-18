#!/usr/bin/env node
'use strict';

const puppeteer = require('puppeteer');
const http = require('http');
const fs = require('fs');
const path = require('path');

const distPath = process.argv[2] || path.resolve(__dirname, '../../../build/dist');

const MIME = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.wasm': 'application/wasm',
    '.css': 'text/css'
};

function serveFile(res, filePath) {
    const data = fs.readFileSync(filePath);
    res.writeHead(200, {
        'Content-Type': MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream',
        'Content-Length': data.length,
        'Cross-Origin-Opener-Policy': 'same-origin',
        'Cross-Origin-Embedder-Policy': 'require-corp',
        'Cross-Origin-Resource-Policy': 'same-origin',
        'Access-Control-Allow-Origin': '*'
    });
    res.end(data);
}

function createServer() {
    return new Promise((resolve) => {
        const server = http.createServer((req, res) => {
            const urlPath = decodeURIComponent(req.url.split('?')[0]);
            const relPath = urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, '');
            const filePath = path.join(distPath, relPath);
            if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
                serveFile(res, filePath);
                return;
            }
            res.writeHead(404, {
                'Cross-Origin-Opener-Policy': 'same-origin',
                'Cross-Origin-Embedder-Policy': 'require-corp'
            });
            res.end('Not found');
        });
        server.listen(0, '127.0.0.1', () => resolve({ server, port: server.address().port }));
    });
}

async function main() {
    if (!fs.existsSync(path.join(distPath, 'libreshockwave.js'))) {
        console.error('FAIL: libreshockwave.js not found in dist. Run assembleWasm first.');
        process.exit(1);
    }

    const { server, port } = await createServer();
    let browser;
    try {
        browser = await puppeteer.launch({
            headless: true,
            args: ['--no-sandbox', '--disable-setuid-sandbox']
        });
        const page = await browser.newPage();
        const response = await page.goto(`http://127.0.0.1:${port}/index.html`, {
            waitUntil: 'domcontentloaded'
        });
        const headers = response.headers();
        const result = await page.evaluate(() => {
            const sab = new SharedArrayBuffer(16);
            const view = new Int32Array(sab);
            Atomics.store(view, 0, 41);
            return {
                crossOriginIsolated: window.crossOriginIsolated === true,
                hasSharedArrayBuffer: typeof SharedArrayBuffer === 'function',
                atomicsValue: Atomics.add(view, 0, 1),
                finalValue: Atomics.load(view, 0),
                playerLoaded: !!window.LibreShockwave
            };
        });

        if (headers['cross-origin-opener-policy'] !== 'same-origin') {
            throw new Error('COOP header missing or wrong');
        }
        if (headers['cross-origin-embedder-policy'] !== 'require-corp') {
            throw new Error('COEP header missing or wrong');
        }
        if (!result.crossOriginIsolated || !result.hasSharedArrayBuffer) {
            throw new Error('SharedArrayBuffer unavailable because page is not isolated');
        }
        if (result.atomicsValue !== 41 || result.finalValue !== 42) {
            throw new Error('Atomics probe failed');
        }
        if (!result.playerLoaded) {
            throw new Error('LibreShockwave did not load under COOP/COEP');
        }
        console.log('PASS: COOP/COEP isolation enables SharedArrayBuffer and Atomics');
    } finally {
        if (browser) await browser.close();
        server.close();
    }
}

main().catch((err) => {
    console.error('FAIL:', err && err.stack ? err.stack : err);
    process.exit(1);
});
