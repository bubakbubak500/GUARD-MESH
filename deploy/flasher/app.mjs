// SPDX-License-Identifier: GPL-3.0-or-later
import './vendor/esp-web-tools/install-button.js';
import { MAX_BYTES, validateMergedImage, makeManifest } from './firmware.mjs';
const $ = id => document.getElementById(id);
const TDECK = 'LilyGo T-Deck / T-Deck Plus';
let valid = false, firmwareUrl, manifestUrl, generation = 0, fileName = '';
let token, activeBuild, loadedBuild, buildBoard, starting = false;
let versions = [], autoSelect = true;
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
    const manifest = makeManifest($('board').value, fileName, firmwareUrl);
    manifestUrl = URL.createObjectURL(new Blob([JSON.stringify(manifest)], { type: 'application/json' }));
    $('installer').setAttribute('manifest', manifestUrl);
  } else $('installer').removeAttribute('manifest');
}
async function loadImage(file, expectedGeneration) {
  try {
    if (file.size > MAX_BYTES) throw new Error('Obraz je větší než 32 MiB.');
    const buffer = await file.arrayBuffer();
    if (expectedGeneration !== generation) return;
    const info = validateMergedImage(buffer);
    firmwareUrl = URL.createObjectURL(new Blob([buffer], { type: 'application/octet-stream' }));
    fileName = file.name;
    valid = true;
    $('file-status').textContent = `${fileName} · ${(info.bytes / 1048576).toFixed(2)} MiB · ${info.chipFamily} · struktura ověřena`;
    $('file-status').className = 'ok';
  } catch (error) {
    if (expectedGeneration !== generation) return;
    $('file-status').textContent = error.message;
    $('file-status').className = 'error';
  }
  update();
}
$('firmware').addEventListener('change', async () => {
  const current = ++generation;
  reset();
  buildBoard = undefined;
  autoSelect = false;
  $('version').value = '';
  const file = $('firmware').files[0];
  $('file-status').className = '';
  if (!file) { $('file-status').textContent = 'Žádný soubor není vybraný.'; return; }
  $('file-status').textContent = 'Kontroluji obraz…';
  await loadImage(file, current);
});
$('board').addEventListener('change', () => {
  $('confirm').checked = false;
  if (buildBoard && $('board').value !== buildBoard) {
    ++generation;
    reset();
    $('file-status').textContent = 'Sestavený obraz je pouze pro T-Deck. Pro jinou desku vyber vlastní soubor.';
  }
  update();
});
$('confirm').addEventListener('change', update);
function versionLabel(v) {
  return `${new Date(v.finished * 1000).toLocaleString('cs-CZ')} · ${v.source || 'local'} · ${v.id.slice(0, 8)}`;
}
async function chooseVersion(v) {
  autoSelect = false;
  const current = ++generation;
  reset();
  if (!v) { $('file-status').textContent = 'Vyber sestavení nebo vlastní soubor.'; return; }
  $('file-status').textContent = 'Načítám vybrané sestavení…';
  try {
    const result = await fetch(v.firmware_url);
    if (!result.ok) throw new Error('Uložený obraz chybí nebo je poškozený. Vyber jinou verzi.');
    const buffer = await result.arrayBuffer();
    const digest = await crypto.subtle.digest('SHA-256', buffer);
    const hash = Array.from(new Uint8Array(digest), b => b.toString(16).padStart(2, '0')).join('');
    if (hash !== v.sha256) throw new Error('Kontrolní součet obrazu nesouhlasí. Instalace zablokována.');
    if (current !== generation) return;
    $('board').value = TDECK;
    $('version').value = v.id;
    buildBoard = TDECK;
    $('firmware').value = '';
    $('version-info').textContent = `Vybráno: ${versionLabel(v)} · aplikace ${(v.app_bytes / 1048576).toFixed(2)} MiB`;
    await loadImage(new File([buffer], `T-Deck-${v.id.slice(0, 8)}.bin`), current);
    if (valid) loadedBuild = v.id;
  } catch (error) {
    if (current !== generation) return;
    $('file-status').textContent = error.message;
    $('file-status').className = 'error';
  }
}
$('version').addEventListener('change', () => chooseVersion(versions.find(v => v.id === $('version').value)));

async function pollBuild() {
  const requestGeneration = generation;
  try {
    const response = await fetch('/api/build');
    if (!response.ok) throw new Error('Build server není dostupný. Restartuj Start-Flasher.cmd.');
    const state = await response.json();
    if (requestGeneration !== generation) return;
    token = state.token;
    activeBuild = state.status === 'building';
    $('build').disabled = activeBuild || starting;
    $('firmware').disabled = activeBuild || starting;
    $('board').disabled = activeBuild || starting;
    $('version').disabled = activeBuild || starting;
    const newVersions = state.versions || [];
    if (newVersions.map(v => v.id).join() !== versions.map(v => v.id).join()) {
      const selected = $('version').value;
      versions = newVersions;
      $('version').replaceChildren(new Option('Vyber uložené sestavení…', ''));
      for (const v of versions) $('version').add(new Option(versionLabel(v), v.id));
      $('version').value = selected;
    }
    $('build-log').textContent = state.log || 'Protokol sestavení se objeví zde.';
    $('build-status').textContent = state.status === 'building'
      ? 'Sestavuji T-Deck… První sestavení stahuje závislosti a může trvat několik minut.'
      : state.status === 'failed' ? `Sestavení selhalo: ${state.error}`
      : state.status === 'ready'
        ? `Hotovo ${new Date(state.finished * 1000).toLocaleString('cs-CZ')} · aplikace ${(state.app_bytes / 1048576).toFixed(2)} / 3,875 MiB`
        : 'Připraveno sestavit aktuální lokální zdroje pro T-Deck.';
    if (activeBuild && valid) {
      ++generation;
      reset();
      autoSelect = true;
      $('file-status').textContent = 'Čekám na úspěšné sestavení.';
    }
    if (state.status === 'failed') autoSelect = false;
    if (state.status === 'ready' && autoSelect) {
      await chooseVersion(versions.find(v => v.id === state.id));
    }
  } catch (error) {
    $('build-status').textContent = error.message;
    $('build').disabled = !token;
  }
}
$('build').addEventListener('click', async () => {
  if (!token || activeBuild || starting) return;
  starting = true;
  autoSelect = true;
  $('version').value = '';
  $('build').disabled = true;
  ++generation;
  reset();
  $('file-status').textContent = 'Čekám na nové sestavení…';
  try {
    const response = await fetch('/api/build', { method: 'POST', headers: { 'X-Flasher-Token': token } });
    if (!response.ok && response.status !== 409) throw new Error('Build nelze spustit. Obnov stránku a zkus to znovu.');
  } catch (error) {
    $('build-status').textContent = error.message;
  } finally {
    starting = false;
    await pollBuild();
  }
});
async function watchBuild() {
  await pollBuild();
  setTimeout(watchBuild, 1500);
}
watchBuild();
$('browser-status').textContent = location.protocol === 'file:'
  ? 'Spusť Start-Flasher.cmd nebo python scripts/local-flasher.py.'
  : !('serial' in navigator) ? 'Web Serial zde není dostupné. Otevři adresu v desktopovém Chrome nebo Edge.'
  : 'Web Serial je dostupné. K rádiu se připojíš až po kliknutí a výběru portu.';
