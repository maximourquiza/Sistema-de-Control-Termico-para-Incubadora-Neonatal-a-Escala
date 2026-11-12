import { LineBuffer, parseLine, formatReference } from './protocol.mjs';

export class SerialSession extends EventTarget {
  constructor(serial, timeoutMs = 3000) {
    super();
    this.serial = serial;
    this.timeoutMs = timeoutMs;
    this.port = null;
    this.pending = null;
    this.closing = null;
  }

  emit(type, detail) { this.dispatchEvent(new CustomEvent(type, { detail })); }

  async connect() {
    if (this.port || this.connecting || this.closing) return;
    this.connecting = true;
    let port;
    try {
      port = await this.serial.requestPort();
      await port.open({ baudRate: 9600, dataBits: 8, stopBits: 1, parity: 'none', flowControl: 'none' });
      this.port = port;
      this.reader = port.readable.getReader();
      this.writer = port.writable.getWriter();
      this.emit('opened');
      this.readTask = this.readLoop(port, this.reader);
    } catch (error) {
      if (this.port) await this.disconnect();
      else if (port) await port.close().catch(() => {});
      throw error;
    } finally { this.connecting = false; }
  }

  async readLoop(port, reader) {
    const decoder = new TextDecoder();
    const buffer = new LineBuffer();
    let failure;
    try {
      while (this.port === port) {
        const { value, done } = await reader.read();
        if (done) break;
        for (const line of buffer.push(decoder.decode(value, { stream: true }))) {
          const message = parseLine(line);
          if (!message) continue;
          if (message.type === 'reference') {
            this.emit('reference', message.value);
            if (this.pending?.value === message.value) this.settle();
          } else if (message.type === 'rejected' && this.pending) {
            this.settle(new Error('El chip rechazó la consigna (ERR REF).'));
          }
          if (message.type === 'temperature') this.emit('temperature', message.value);
        }
      }
    } catch (error) { failure = error; }
    finally {
      reader.releaseLock();
      if (this.port === port) {
        this.emit('lost', failure);
        // No esperar aquí: disconnect espera que termine este bucle.
        void this.disconnect();
      }
    }
  }

  settle(error) {
    const pending = this.pending;
    if (!pending) return;
    this.pending = null;
    clearTimeout(pending.timer);
    if (error) pending.reject(error);
    else pending.resolve();
  }

  async sendReference(value) {
    if (!this.port || this.closing) throw new Error('Conectá el HC-05 antes de enviar.');
    if (this.pending) throw new Error('Ya hay una consigna pendiente.');
    if (!Number.isInteger(value) || value < 34000 || value > 37000) throw new Error('Consigna fuera de rango.');
    const acknowledgement = new Promise((resolve, reject) => {
      this.pending = { value, resolve, reject, timer: setTimeout(() => {
        this.settle(new Error('Sin confirmación en 3 segundos. El chip podría haber recibido la consigna; no se reenvió automáticamente.'));
      }, this.timeoutMs) };
    });
    // Instalar el manejador antes de escribir, incluso si falla o se desconecta.
    acknowledgement.catch(() => {});
    try { await this.writer.write(new TextEncoder().encode(`${formatReference(value)}\r\n`)); }
    catch { this.settle(new Error('No se pudo enviar la consigna. Revisá la conexión.')); }
    return acknowledgement;
  }

  disconnect() {
    if (this.closing) return this.closing;
    if (!this.port) return Promise.resolve();
    const port = this.port;
    const reader = this.reader;
    const writer = this.writer;
    this.port = null;
    this.settle(new Error('Conexión cerrada: envío cancelado.'));
    this.closing = (async () => {
      try {
        await reader?.cancel().catch(() => {});
        await this.readTask;
        if (writer) {
          await writer.abort().catch(() => {});
          writer.releaseLock();
        }
        await port.close();
      } catch {
        this.emit('cleanup-error', 'No se pudo cerrar completamente el puerto. Desconectá y reconectá el HC-05 antes de reintentar.');
      } finally {
        this.reader = this.writer = this.readTask = null;
        this.closing = null;
        this.emit('closed');
      }
    })();
    return this.closing;
  }
}
