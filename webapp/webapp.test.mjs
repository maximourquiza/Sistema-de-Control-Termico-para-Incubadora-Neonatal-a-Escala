import test from 'node:test';
import assert from 'node:assert/strict';
import { LineBuffer, parseLine, parseReference, formatReference } from './protocol.mjs';
import { SerialSession } from './serial.mjs';

function mockPort() {
  let controller;
  const sent = [];
  const port = {
    readable: new ReadableStream({ start(value) { controller = value; } }),
    writable: new WritableStream({ write(value) { sent.push(new TextDecoder().decode(value)); } }),
    async open(options) { this.options = options; },
    async close() { this.closed = true; },
    receive(text) { controller.enqueue(new TextEncoder().encode(text)); },
    lose() { controller.close(); },
    sent
  };
  return port;
}
async function setup(timeoutMs = 1000) {
  const port = mockPort();
  const session = new SerialSession({ requestPort: async () => port }, timeoutMs);
  await session.connect();
  return { port, session };
}
const nextEvent = (session, name) => new Promise(resolve => session.addEventListener(name, resolve, { once: true }));
const flush = () => new Promise(resolve => setImmediate(resolve));

test('consignas: límites, coma, precisión y entradas inválidas', () => {
  for (const [text, expected] of [['34',34000], ['37.000',37000], ['36,5',36500], ['36.005',36005], [' 35.25 ',35250]]) {
    assert.equal(parseReference(text), expected);
  }
  for (const text of ['33.999','37.001','36.0001','36.','36,5.0','3e1','hola','','36 °C']) assert.equal(parseReference(text), null);
  assert.equal(formatReference(36005), '36.005');
});
test('líneas fragmentadas, agrupadas, CRLF y trama excesiva', () => {
  const buffer = new LineBuffer();
  assert.deepEqual(buffer.push('25.'), []);
  assert.deepEqual(buffer.push('300\r\nREF=36'), ['25.300']);
  assert.deepEqual(buffer.push('500\nERR REF\r-1.250\n'), ['REF=36500','ERR REF','-1.250']);
  assert.deepEqual(buffer.push('x'.repeat(200) + '\n26.000\n'), ['26.000']);
  assert.deepEqual(parseLine('-1.250'), { type: 'temperature', value: -1.25 });
  assert.equal(parseLine('CTRL REF=36.000'), null);
  assert.equal(parseLine('REF=99999'), null);
});
test('apertura a 9600 8N1 y cancelación del selector', async () => {
  const { port, session } = await setup();
  assert.deepEqual(port.options, { baudRate:9600, dataBits:8, stopBits:1, parity:'none', flowControl:'none' });
  await session.disconnect();
  assert.equal(port.closed, true);
  const cancelled = new SerialSession({ requestPort: async () => { throw new DOMException('Cancelado','NotFoundError'); } });
  await assert.rejects(cancelled.connect(), { name: 'NotFoundError' });
  assert.equal(cancelled.connecting, false);
});
test('puerto ocupado no deja una sesión abierta', async () => {
  const port = mockPort();
  port.open = async () => { throw new DOMException('Ocupado','NetworkError'); };
  const session = new SerialSession({ requestPort: async () => port });
  await assert.rejects(session.connect(), { name: 'NetworkError' });
  assert.equal(session.port, null);
  assert.equal(session.connecting, false);
});
test('escribir no confirma: requiere REF coincidente entre temperaturas', async () => {
  const { port, session } = await setup();
  const temperatures = [];
  session.addEventListener('temperature', e => temperatures.push(e.detail));
  let confirmed = false;
  const sending = session.sendReference(36500).then(() => { confirmed = true; });
  await flush();
  assert.deepEqual(port.sent, ['36.500\r\n']);
  assert.equal(confirmed, false);
  port.receive('25.000\r\nREF=36000\r\n');
  await flush();
  assert.equal(confirmed, false);
  await assert.rejects(session.sendReference(36000), /pendiente/);
  port.receive('REF=365');
  port.receive('00\r\n25.100\n');
  await sending;
  assert.deepEqual(temperatures, [25, 25.1]);
  await session.disconnect();
});
test('rechazo, timeout sin reenvío y confirmación tardía', async () => {
  const { port, session } = await setup(25);
  const rejected = assert.rejects(session.sendReference(36500), /rechazó/);
  port.receive('ERR REF\r\n');
  await rejected;
  await assert.rejects(session.sendReference(36000), /Sin confirmación/);
  assert.equal(port.sent.length, 2);
  let late;
  session.addEventListener('reference', e => { late = e.detail; });
  port.receive('REF=36000\r\n');
  await flush();
  assert.equal(late, 36000);
  assert.equal(session.pending, null);
  await session.disconnect();
});
test('desconexión cancela envío y libera los streams', async () => {
  const { port, session } = await setup();
  const cancelled = assert.rejects(session.sendReference(36500), /cancelado/);
  await flush();
  await session.disconnect();
  await cancelled;
  assert.equal(port.readable.locked, false);
  assert.equal(port.writable.locked, false);
  assert.equal(port.closed, true);
  await assert.rejects(session.sendReference(36000), /Conectá/);
});
test('pérdida del enlace y reconexión manual con nueva sesión', async () => {
  const first = mockPort();
  const second = mockPort();
  let count = 0;
  const session = new SerialSession({ requestPort: async () => ++count === 1 ? first : second });
  await session.connect();
  const closed = nextEvent(session, 'closed');
  first.lose();
  await closed;
  assert.equal(session.port, null);
  assert.equal(count, 1);
  await session.connect();
  assert.equal(session.port, second);
  await session.disconnect();
});
