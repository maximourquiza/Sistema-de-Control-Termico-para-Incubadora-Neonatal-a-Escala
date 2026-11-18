import { parseReference, formatReference } from './protocol.mjs';
import { SerialSession } from './serial.mjs';

const el = id => document.getElementById(id);
const session = new SerialSession(navigator.serial);
let openedAt = 0;
let lastSampleAt = 0;
let lastDutyAt = 0;
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
  if (lastDutyAt && Date.now() - lastDutyAt > 3000) clearDuty();
  const age = Date.now() - (lastSampleAt || openedAt);
  if (age > 3000) {
    status('Sin datos recientes', 'waiting');
    el('sample-status').textContent = hasSample ? 'Última lectura' : 'Sin datos';
    document.querySelector('.temperature-card').classList.add('historical');
  } else if (lastSampleAt) {
    status('Conectado', 'live');
    el('sample-status').textContent = 'En vivo';
    document.querySelector('.temperature-card').classList.remove('historical');
  }
}

function clearDuty() {
  lastDutyAt = 0;
  el('heater-duty').textContent = '—';
  el('pwm-status').textContent = 'Sin datos';
}

el('connect').addEventListener('click', async () => {
  status('Conectando…', 'waiting');
  const connection = session.connect();
  updateButtons();
  try { await connection; }
  catch (error) {
    status('Desconectado');
    message('connection-message', error.name === 'NotFoundError'
      ? 'Selección cancelada.'
      : 'No se pudo conectar. Revisá el HC-05 y el puerto.', 'error');
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
  clearDuty();
  el('confirmed-reference').textContent = '—';
  el('reference-status').textContent = '';
  if (hasSample) el('sample-status').textContent = 'Última conexión';
  status('Esperando datos', 'waiting');
  message('connection-message', '');
  message('send-status', '');
  updateButtons();
});
session.addEventListener('temperature', event => {
  if (!lastSampleAt) message('connection-message', '');
  hasSample = true;
  lastSampleAt = Date.now();
  el('temperature').textContent = event.detail.toFixed(3);
  refreshSampleStatus();
});
session.addEventListener('pwm', event => {
  lastDutyAt = Date.now();
  el('heater-duty').textContent = String(event.detail);
  el('pwm-status').textContent = 'En vivo';
});
session.addEventListener('reference', event => {
  confirmed = event.detail;
  el('confirmed-reference').textContent = `${formatReference(confirmed)} °C`;
  el('reference-status').textContent = '';
});
session.addEventListener('lost', () => {
  connectionIssue = true;
  clearDuty();
  message('connection-message', 'Conexión perdida. Volvé a conectar.', 'error');
});
session.addEventListener('cleanup-error', event => {
  connectionIssue = true;
  message('connection-message', event.detail, 'error');
});
session.addEventListener('closed', () => {
  generation++;
  sending = false;
  status('Desconectado');
  clearDuty();
  if (!connectionIssue) message('connection-message', '');
  if (hasSample) {
    el('sample-status').textContent = 'Última conexión';
    document.querySelector('.temperature-card').classList.add('historical');
  }
  if (confirmed !== null) el('reference-status').textContent = 'Última conexión';
  message('send-status', '');
  updateButtons();
});
el('reference-form').addEventListener('submit', async event => {
  event.preventDefault();
  if (sending) return;
  const value = parseReference(el('reference').value);
  if (value === null) {
    message('send-status', 'Usá 34–37 °C, hasta 3 decimales.', 'error');
    el('reference').focus();
    return;
  }
  const currentGeneration = generation;
  sending = true;
  updateButtons();
  message('send-status', 'Esperando confirmación…');
  try {
    await session.sendReference(value);
    if (generation === currentGeneration) message('send-status', 'Consigna confirmada.', 'success');
  } catch (error) {
    if (generation === currentGeneration) message('send-status', error.message, 'error');
  } finally {
    if (generation === currentGeneration) { sending = false; updateButtons(); }
  }
});

if (!('serial' in navigator) || !window.isSecureContext) {
  el('connect').disabled = true;
  message('connection-message', 'Abrí localhost:8080 en Chrome de escritorio.', 'error');
}
setInterval(refreshSampleStatus, 500);
