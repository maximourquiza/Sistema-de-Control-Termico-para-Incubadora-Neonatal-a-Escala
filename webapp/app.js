import { parseReference, formatReference } from './protocol.mjs';
import { SerialSession } from './serial.mjs';

const el = id => document.getElementById(id);
const session = new SerialSession(navigator.serial);
let openedAt = 0;
let lastSampleAt = 0;
let hasSample = false;
let confirmed = null;
let sending = false;
let generation = 0;
let connectionIssue = false;

function message(id, text, kind = '') {
  el(id).textContent = text;
  el(id).className = `message ${kind}`;
}
function status(text, kind = '') {
  el('connection-status').textContent = text;
  el('status-dot').className = `dot ${kind}`;
}
function updateButtons() {
  el('connect').disabled = Boolean(session.port || session.connecting || session.closing);
  el('disconnect').disabled = !session.port || Boolean(session.closing);
  el('send').disabled = !session.port || Boolean(session.closing) || sending;
}
function refreshSampleStatus() {
  if (!session.port) return;
  const age = Date.now() - (lastSampleAt || openedAt);
  if (age > 3000) {
    status('Puerto abierto, sin mediciones recientes', 'waiting');
    el('sample-status').textContent = hasSample ? 'Sin datos por más de 3 segundos. Última lectura histórica.' : 'No se recibieron mediciones. Revisá la placa y el enlace Bluetooth.';
    document.querySelector('.temperature-card').classList.add('historical');
  } else if (lastSampleAt) {
    status('Recibiendo datos', 'live');
    el('sample-status').textContent = 'Lectura recibida del chip.';
    document.querySelector('.temperature-card').classList.remove('historical');
  }
}

el('connect').addEventListener('click', async () => {
  status('Seleccionando y abriendo puerto…', 'waiting');
  const connection = session.connect();
  updateButtons();
  try { await connection; }
  catch (error) {
    status('Desconectado');
    message('connection-message', error.name === 'NotFoundError'
      ? 'Selección cancelada. Pulsá Conectar para reintentar.'
      : 'No se pudo abrir el puerto. Cerrá PuTTY u otras aplicaciones que usen el COM del HC-05 y verificá que esté emparejado.', 'error');
  }
  updateButtons();
});
el('disconnect').addEventListener('click', async () => {
  const closing = session.disconnect();
  status('Desconectando…', 'waiting');
  updateButtons();
  await closing;
  updateButtons();
});
session.addEventListener('opened', () => {
  connectionIssue = false;
  generation++;
  openedAt = Date.now();
  lastSampleAt = 0;
  confirmed = null;
  el('confirmed-reference').textContent = 'Desconocida';
  el('reference-status').textContent = 'Todavía no se recibió una confirmación en esta conexión.';
  if (hasSample) el('sample-status').textContent = 'Lectura de la conexión anterior; esperando una nueva medición.';
  status('Puerto abierto, esperando datos', 'waiting');
  message('connection-message', 'Puerto abierto a 9600 baudios. Esperando temperaturas del chip.');
  message('send-status', 'Podés enviar una temperatura objetivo entre 34 y 37 °C.');
  updateButtons();
});
session.addEventListener('temperature', event => {
  if (!lastSampleAt) message('connection-message', 'Recibiendo temperaturas del chip. El puerto serie está abierto desde esta página.');
  hasSample = true;
  lastSampleAt = Date.now();
  el('temperature').textContent = event.detail.toFixed(3);
  refreshSampleStatus();
});
session.addEventListener('reference', event => {
  confirmed = event.detail;
  el('confirmed-reference').textContent = `${formatReference(confirmed)} °C`;
  el('reference-status').textContent = 'Última confirmación recibida del chip en esta conexión.';
});
session.addEventListener('lost', () => {
  connectionIssue = true;
  message('connection-message', 'Se perdió el enlace con el HC-05. Reconectá manualmente con Conectar.', 'error');
});
session.addEventListener('cleanup-error', event => {
  connectionIssue = true;
  message('connection-message', event.detail, 'error');
});
session.addEventListener('closed', () => {
  generation++;
  sending = false;
  status('Desconectado');
  if (!connectionIssue) message('connection-message', 'Puerto cerrado. Pulsá Conectar para abrir nuevamente el enlace con el HC-05.');
  if (hasSample) {
    el('sample-status').textContent = 'Última lectura histórica. No hay conexión activa.';
    document.querySelector('.temperature-card').classList.add('historical');
  }
  if (confirmed !== null) el('reference-status').textContent = 'Confirmación histórica. El estado actual del chip es desconocido.';
  message('send-status', 'Sin conexión. Cualquier envío pendiente fue cancelado.');
  updateButtons();
});
el('reference-form').addEventListener('submit', async event => {
  event.preventDefault();
  if (sending) return;
  const value = parseReference(el('reference').value);
  if (value === null) {
    message('send-status', 'Ingresá un valor entre 34 y 37 °C con hasta tres decimales.', 'error');
    el('reference').focus();
    return;
  }
  const currentGeneration = generation;
  sending = true;
  updateButtons();
  message('send-status', `Enviando ${formatReference(value)} °C. Esperando confirmación…`);
  try {
    await session.sendReference(value);
    if (generation === currentGeneration) message('send-status', `El chip confirmó ${formatReference(value)} °C.`, 'success');
  } catch (error) {
    if (generation === currentGeneration) message('send-status', error.message, 'error');
  } finally {
    if (generation === currentGeneration) { sending = false; updateButtons(); }
  }
});

if (!('serial' in navigator) || !window.isSecureContext) {
  el('connect').disabled = true;
  message('connection-message', 'Web Serial no está disponible. Abrí esta página en Chrome de escritorio desde http://localhost:8080.', 'error');
}
setInterval(refreshSampleStatus, 500);
