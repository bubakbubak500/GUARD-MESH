# SPDX-License-Identifier: GPL-3.0-or-later
"""Validate the Czech catalog's coverage and format-string safety."""
from pathlib import Path
import importlib.util
import re

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gen_lang', ROOT/'scripts/build/gen-lang-builtin.py')
gen = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gen)
cs = gen.read_lang(ROOT/'deploy/apps/lang/cs.lang')
reference = set()
for path in (ROOT/'deploy/apps/lang').glob('*.lang'):
    if path.stem != 'cs': reference.update(gen.read_lang(path))
assert not reference - cs.keys(), f'Missing keys: {reference - cs.keys()}'
fmt = re.compile(r'%(?:\d+\$)?[-+ #0]*(?:\d+|\*)?(?:\.(?:\d+|\*))?(?:hh|ll|[hljztL])?[diuoxXfFeEgGaAcspn%]')
for key, value in cs.items():
    assert fmt.findall(key) == fmt.findall(value), f'Format mismatch: {key!r} -> {value!r}'
    assert '\ufffd' not in value, f'Broken encoding: {key}'
    colors = re.findall(r'#[0-9a-fA-F]{6}\b', key)
    assert colors == re.findall(r'#[0-9a-fA-F]{6}\b', value), f'Color markup mismatch: {key}'
for term in ('Advert', 'Ack', 'Flood', 'Zero-hop', 'Scope', 'Ping', 'Trace', 'SF', 'CR', 'TX', 'GPS', 'Bluetooth', 'Wi-Fi', 'MQTT bridge'):
    assert cs[term] == term, f'Technical term changed: {term}'
assert gen.CODES[-1] == 'cs' and gen.CODES.index('ro') == 13
# Short controls need a stricter budget than wrapping descriptions. Count Unicode
# characters, not UTF-8 bytes; the simulator separately checks real font widths.
compact = (
    'Unfav', 'Cut', 'Copy', 'Paste', 'All', 'Sym', 'Remote', 'Scan', 'Refit',
    'Auto-add', 'Auto-Join', 'Auto chat', 'Auto room', 'Auto sensor', 'Auto repeater',
    'Auto advert', 'Auto-discover', 'Apply auto-poll', 'Save auto-add',
    'Show on map', 'Sightline', 'Share my loc', 'Stop sharing loc', 'Share contact', 'Reset path',
    'Resend', 'Replay', 'Copy secret', 'Share secret', 'Mark read', 'Mark as read',
    'Unmute @', 'Mute msgs', 'Unmute msgs', 'Blocked list', 'Blocked users',
    'On', 'Off', 'on', 'off', 'GPS on', 'GPS off', 'BLE on', 'BLE off',
    'TCP on', 'TCP off', 'Wi-Fi on', 'Wi-Fi off', 'Bluetooth on', 'Bluetooth off',
    'Keyboard light auto', 'Keyboard light on', 'Keyboard light off',
    'Keyboard nav: off', 'Keyboard nav: WASDZ on', 'Auto-refresh on',
    'Auto-refresh off', 'Two beeps: on', 'Two beeps: off', 'Accent popups',
    'Bitrate', 'Latitude', 'Longitude', 'App icon size', 'Reverse scrollball',
    'Lock screen', 'Edge scroll', 'Install update', 'Message info',
)
for key in compact:
    budget = len(key) + max(3, (len(key) + 3) // 4)
    assert len(cs[key]) <= budget, f'Compact label too long ({len(cs[key])}/{budget}): {key} -> {cs[key]}'
print(f'Compact Czech labels: {len(compact)} character budgets OK.')
print(f'Czech catalog: {len(cs)} keys, full existing-catalog coverage; placeholders and technical terms OK.')
