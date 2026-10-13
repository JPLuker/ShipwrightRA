#!/usr/bin/env python3
"""Compile the production adapter against real Shipwright headers/compile flags."""
import argparse
import json
from pathlib import Path
import shlex
import subprocess
import tempfile

parser = argparse.ArgumentParser()
parser.add_argument('--build', type=Path, required=True)
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
commands = json.loads((args.build / 'compile_commands.json').read_text())
entry = next(c for c in commands if c['file'].endswith('/retroachievements/N64MemoryAdapter.cpp'))
original = entry.get('arguments') or shlex.split(entry['command'])
flags = []
skip = False
for arg in original:
    if skip:
        skip = False
        continue
    if arg in ('-o', '-MF', '-MT', '-MQ'):
        skip = True
    elif arg not in ('-c', '-MD', '-MMD', '-MP', entry['file']):
        flags.append(arg)
with tempfile.TemporaryDirectory() as temp:
    objects = []
    for index, path in enumerate((Path(entry['file']), root / 'tests/retroachievements/memory_adapter.cpp')):
        obj = str(Path(temp) / f'test{index}.o')
        subprocess.run([*flags, '-I' + str(Path(entry['file']).parent), '-c', str(path), '-o', obj],
                       cwd=entry['directory'], check=True)
        objects.append(obj)
    exe = str(Path(temp) / 'memory-test')
    subprocess.run(['g++', '-pthread', *objects, '-o', exe], check=True)
    subprocess.run([exe], check=True)
