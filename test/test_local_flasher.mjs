// SPDX-License-Identifier: GPL-3.0-or-later
// Run: node --test test/test_local_flasher.mjs
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { makeManifest, validateMergedImage } from '../deploy/flasher/firmware.mjs';

// Structural fixture, not executable firmware; never flash it to a device.
function mergedFixture() {
  const data = new Uint8Array(0x10100).fill(0xff);
  const view = new DataView(data.buffer);
  for (const offset of [0, 0x10000]) {
    data[offset] = 0xe9; data[offset + 1] = 1;
    view.setUint16(offset + 12, 9, true);
  }
  view.setUint16(0x8000, 0x50aa, true);
  data[0x8002] = 0;
  view.setUint32(0x8004, 0x10000, true);
  view.setUint32(0x8008, 0x200000, true);
  return data;
}
test('accepts the expected merged S3 layout', () => {
  assert.equal(validateMergedImage(mergedFixture().buffer).chipFamily, 'ESP32-S3');
});
test('rejects app-only, empty, oversized and wrong-chip images', () => {
  for (const data of [new Uint8Array(), new Uint8Array(32 * 1024 * 1024 + 1), mergedFixture().slice(0x10000)]) {
    assert.throws(() => validateMergedImage(data.buffer));
  }
  const data = mergedFixture(); data[12] = 18;
  assert.throws(() => validateMergedImage(data.buffer), /bootloader/);
});
test('rejects missing partitions, missing app and out-of-range partitions', () => {
  for (const mutate of [d => d[0x8000] = 0, d => d[0x10000] = 0,
    d => new DataView(d.buffer).setUint32(0x8004, 0xffff0000, true)]) {
    const data = mergedFixture(); mutate(data);
    assert.throws(() => validateMergedImage(data.buffer));
  }
});
test('manifest uses selected local blob, explicit chip and erase prompt', () => {
  const manifest = makeManifest('T-Deck', 'local.bin', 'blob:http://localhost/test');
  assert.deepEqual(manifest.builds, [{ chipFamily: 'ESP32-S3', parts: [{ path: 'blob:http://localhost/test', offset: 0 }] }]);
  assert.equal(manifest.new_install_prompt_erase, true);
  assert.equal(manifest.new_install_improv_wait_time, 0);
});
