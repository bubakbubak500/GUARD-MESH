// SPDX-License-Identifier: GPL-3.0-or-later
import './vendor/esp-web-tools/install-button.js';
import { MAX_BYTES, validateMergedImage, makeManifest } from './firmware.mjs';
const $ = id => document.getElementById(id);
let valid = false, firmwareUrl, manifestUrl, generation = 0;
function reset() {
  valid = false;
  $('confirm').checked = false;
  $('install-area').hidden = true;
  $('installer').removeAttribute('manifest');
  for (const url of [firmwareUrl, manifestUrl]) if (url) URL.revokeObjectURL(url);
  firmwareUrl = manifestUrl = undefined;
}
function update() {
  if (manifestUrl) URL.revokeObjectURL(manifestUrl);
  manifestUrl = undefined;
  const ready = valid && $('board').value && $('confirm').checked;
  $('install-area').hidden = !ready;
  if (ready) {
    const manifest = makeManifest($('board').value, $('firmware').files[0].name, firmwareUrl);
    manifestUrl = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }));
    $('installer').setAttribute('manifest', manifestUrl);
  }
}
$('firmware').addEventListener('change', async () => {
  const current = ++generation;
  reset();
  const file = $('firmware').files[0];
  $('file-status').className = '';
  if (!file) { $('file-status').textContent = 'Žádný soubor není vybraný.'; return; }
  $('file-status').textContent = 'Kontroluji obraz…';
  try {
    if (file.size > MAX_BYTES) throw new Error('Obraz je větší než 32 MiB.');
    const buffer = await file.arrayBuffer();
    if (current !== generation) return;
    const info = validateMergedImage(buffer);
    firmwareUrl = URL.createObjectURL(new Blob([buffer], { type: 'application/octet-stream' }));
    valid = true;
    $('file-status').textContent = `${file.name} · ${(info.bytes / 1048576).toFixed(2)} MiB · ${info.chipFamily} · struktura ověřena`;
    $('file-status').className = 'ok';
  } catch (error) {
    if (current !== generation) return;
    $('file-status').textContent = error.message;
    $('file-status').className = 'error';
  }
  update();
});
$('board').addEventListener('change', () => { $('confirm').checked = false; update(); });
$('confirm').addEventListener('change', update);
$('browser-status').textContent = location.protocol === 'file:'
  ? 'Spusť Start-Flasher.cmd nebo python scripts/local-flasher.py.'
  : !('serial' in navigator) ? 'Web Serial zde není dostupné. Otevři adresu v desktopovém Chrome nebo Edge.'
  : 'Web Serial je dostupné. K rádiu se připojíš až po kliknutí a výběru portu.';
