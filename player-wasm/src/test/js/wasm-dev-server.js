#!/usr/bin/env node
'use strict';

const http = require('http');
const fs = require('fs');
const path = require('path');

const root = path.resolve(process.argv[2] || path.resolve(__dirname, '../../../build/dist'));
const port = Number(process.argv[3] || 4173);

const MIME = {
    '.html': 'text/html',
    '.js': 'application/javascript',
    '.wasm': 'application/wasm',
    '.css': 'text/css',
    '.png': 'image/png',
    '.txt': 'text/plain'
};

const ISOLATION_HEADERS = {
    'Cross-Origin-Opener-Policy': 'same-origin',
    'Cross-Origin-Embedder-Policy': 'require-corp',
    'Cross-Origin-Resource-Policy': 'same-origin',
    'Access-Control-Allow-Origin': '*'
};

function serveFile(res, filePath) {
    const data = fs.readFileSync(filePath);
    res.writeHead(200, {
        ...ISOLATION_HEADERS,
        'Content-Type': MIME[path.extname(filePath).toLowerCase()] || 'application/octet-stream',
        'Content-Length': data.length
    });
    res.end(data);
}

const server = http.createServer((req, res) => {
    const urlPath = decodeURIComponent(req.url.split('?')[0]);
    const relPath = urlPath === '/' ? 'index.html' : urlPath.replace(/^\/+/, '');
    const filePath = path.join(root, relPath);
    if (fs.existsSync(filePath) && fs.statSync(filePath).isFile()) {
        serveFile(res, filePath);
        return;
    }
    res.writeHead(404, ISOLATION_HEADERS);
    res.end('Not found');
});

server.listen(port, '127.0.0.1', () => {
    console.log(`LibreShockwave WASM server: http://127.0.0.1:${port}`);
    console.log(`Serving: ${root}`);
});
