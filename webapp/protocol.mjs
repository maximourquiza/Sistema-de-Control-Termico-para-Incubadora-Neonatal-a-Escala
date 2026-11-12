export function parseReference(text) {
  const normalized = text.trim().replace(',', '.');
  if (!/^\d{2}(?:\.\d{1,3})?$/.test(normalized)) return null;
  const [whole, fraction = ''] = normalized.split('.');
  const value = Number(whole) * 1000 + Number(fraction.padEnd(3, '0'));
  return value >= 34000 && value <= 37000 ? value : null;
}

export function formatReference(value) {
  return `${Math.trunc(value / 1000)}.${String(value % 1000).padStart(3, '0')}`;
}

export function parseLine(line) {
  if (/^-?\d+\.\d{3}$/.test(line)) {
    const value = Number(line);
    if (Number.isFinite(value)) return { type: 'temperature', value };
  }
  const match = /^REF=(\d+)$/.exec(line);
  if (match) {
    const value = Number(match[1]);
    if (Number.isInteger(value) && value >= 34000 && value <= 37000) {
      return { type: 'reference', value };
    }
  }
  if (line === 'ERR REF') return { type: 'rejected' };
  return null;
}

export class LineBuffer {
  #line = '';
  #discard = false;
  push(text) {
    const lines = [];
    for (const char of text) {
      if (char === '\r' || char === '\n') {
        if (!this.#discard && this.#line) lines.push(this.#line);
        this.#line = '';
        this.#discard = false;
      } else if (!this.#discard) {
        if (this.#line.length < 128) this.#line += char;
        else { this.#line = ''; this.#discard = true; }
      }
    }
    return lines;
  }
}
