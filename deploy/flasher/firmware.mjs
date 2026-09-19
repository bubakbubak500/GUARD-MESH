// SPDX-License-Identifier: GPL-3.0-or-later
export const MAX_BYTES = 32 * 1024 * 1024;

// Structural screening only: not a signature or board-identity check.
export function validateMergedImage(buffer) {
  const bytes = new Uint8Array(buffer);
  const view = new DataView(buffer);
  if (bytes.length < 0x10000 + 24 || bytes.length > MAX_BYTES) {
    throw new Error('Očekávám kompletní sloučený obraz do 32 MiB, nikoliv samotný firmware.bin.');
  }
  function checkImage(offset) {
    return offset + 24 <= bytes.length && bytes[offset] === 0xe9
      && bytes[offset + 1] >= 1 && bytes[offset + 1] <= 16
      && view.getUint16(offset + 12, true) === 9; // Espressif chip ID: ESP32-S3
  }
  if (!checkImage(0)) throw new Error('Chybí bootloader ESP32-S3 na adrese 0.');
  let appFound = false;
  for (let pos = 0x8000; pos < 0x8c00; pos += 32) {
    const magic = view.getUint16(pos, true);
    if (magic === 0xffff || magic === 0xebeb) break;
    if (magic !== 0x50aa) throw new Error('Neplatná tabulka oddílů na adrese 0x8000.');
    const type = bytes[pos + 2];
    const offset = view.getUint32(pos + 4, true);
    const size = view.getUint32(pos + 8, true);
    if (!size || offset + size > MAX_BYTES) throw new Error('Neplatný rozsah oddílu v obrazu.');
    if (type === 0 && offset >= 0x10000 && checkImage(offset)) appFound = true;
  }
  if (!appFound) throw new Error('V obrazu chybí aplikace ESP32-S3 podle tabulky oddílů.');
  return { chipFamily: 'ESP32-S3', bytes: bytes.length };
}

export function makeManifest(board, fileName, firmwareUrl) {
  return {
    name: `GUARD-MESH / ${board}`, version: fileName,
    new_install_prompt_erase: true, new_install_improv_wait_time: 0,
    builds: [{ chipFamily: 'ESP32-S3', parts: [{ path: firmwareUrl, offset: 0 }] }],
  };
}
