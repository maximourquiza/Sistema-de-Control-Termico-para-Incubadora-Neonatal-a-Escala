import http from 'node:http';
import { readFile } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';

const root = path.dirname(fileURLToPath(import.meta.url));
const types = { '.html': 'text/html', '.css': 'text/css', '.js': 'text/javascript', '.mjs': 'text/javascript' };
const assets = new Set(['index.html', 'styles.css', 'app.js', 'protocol.mjs', 'serial.mjs']);
const server = http.createServer(async (request, response) => {
  const pathname = new URL(request.url, 'http://localhost').pathname;
  const name = pathname === '/' ? 'index.html' : pathname.slice(1);
  if (!['GET', 'HEAD'].includes(request.method) || !assets.has(name)) {
    response.writeHead(404).end('No encontrado');
    return;
  }
  try {
    const body = await readFile(path.join(root, name));
    response.writeHead(200, {
      'Content-Type': `${types[path.extname(name)]}; charset=utf-8`,
      'Cache-Control': 'no-store',
      'X-Content-Type-Options': 'nosniff',
      'Content-Security-Policy': "default-src 'self'; script-src 'self'; style-src 'self'; object-src 'none'; base-uri 'none'; frame-ancestors 'none'"
    });
    response.end(request.method === 'HEAD' ? undefined : body);
  } catch { response.writeHead(500).end('No se pudo cargar el archivo.'); }
});
server.on('error', error => {
  console.error(error.code === 'EADDRINUSE'
    ? 'El puerto 8080 está ocupado. Cerrá el servidor anterior o la aplicación que lo usa y reintentá.'
    : `No se pudo iniciar el servidor: ${error.message}`);
  process.exitCode = 1;
});
server.listen(8080, '127.0.0.1', () => console.log('Webapp disponible en http://localhost:8080\nCerrá PuTTY. En Chrome, pulsá Conectar y elegí el HC-05 (actualmente COM5).\nCtrl+C para detener el servidor.'));
