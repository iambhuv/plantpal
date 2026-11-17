import { defineConfig } from 'vite';
import { svelte } from '@sveltejs/vite-plugin-svelte';
import { viteSingleFile } from 'vite-plugin-singlefile';
import { readFileSync, writeFileSync, mkdirSync } from 'node:fs';
import { gzipSync } from 'node:zlib';
import { fileURLToPath } from 'node:url';
import { dirname } from 'node:path';
import mock from './mock.js';

const dist = fileURLToPath(new URL('./dist/index.html', import.meta.url));
const out = fileURLToPath(new URL('../main/www/index.html.gz', import.meta.url));

// Gzip the single-file build into the firmware embed path.
const gzipToFirmware = {
  name: 'gzip-to-firmware',
  apply: 'build',
  closeBundle(error) {
    if (error) return;
    mkdirSync(dirname(out), { recursive: true });
    const gz = gzipSync(readFileSync(dist), { level: 9 });
    gz[9] = 0x03; // gzip OS byte fixed to Unix so the .gz is identical on macOS/Linux
    writeFileSync(out, gz);
    console.log(`main/www/index.html.gz: ${gz.length} bytes`);
  },
};

const isMock = process.env.DEVICE === 'mock';

export default defineConfig({
  // DEVICE=mock serves a fake API (web/mock.js, dev server only); otherwise proxy to the board.
  plugins: [svelte(), viteSingleFile(), gzipToFirmware, isMock && mock()],
  build: { target: 'es2019' },
  server: {
    proxy: isMock ? {} : { '/api': { target: process.env.DEVICE || 'http://plantpal.local', changeOrigin: true } },
  },
});
