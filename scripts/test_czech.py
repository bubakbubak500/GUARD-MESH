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
print(f'Czech catalog: {len(cs)} keys, full existing-catalog coverage; placeholders and technical terms OK.')
