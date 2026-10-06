#!/usr/bin/env python3
"""Compile the production protocol layers with a small POSIX test platform, without MFC/Npcap."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / 'ipc2019'
with tempfile.TemporaryDirectory(prefix='ipc-protocol-tests-') as directory:
    temporary = Path(directory)
    text = (source / 'stdafx.h').read_text(encoding='utf-8-sig')
    # Constants are imported from production, never copied into a second manually maintained definition.
    start = text.index('#define MAX_LAYER_NUMBER')
    end = text.index('// Microsoft Visual C++ will insert')
    (temporary / 'ProtocolConstants.h').write_text(text[start:end], encoding='utf-8')
    executable = temporary / 'protocol_tests'
    files = ['BaseLayer.cpp', 'LayerManager.cpp', 'EthernetLayer.cpp', 'ARPLayer.cpp', 'IPLayer.cpp', 'IPRouter.cpp', 'ChatAppLayer.cpp', 'FileAppLayer.cpp']
    command = ['g++', '-std=c++17', '-g', '-O1', '-pthread', '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
               '-I', str(temporary), '-I', str(source), '-include', str(root/'tests/portable/PlatformShim.h'),
               *(str(source/name) for name in files), str(root/'tests/protocol_stack_tests.cpp'), '-o', str(executable)]
    subprocess.run(command, check=True)
    # LeakSanitizer cannot attach under some managed/ptrace-restricted runners. Address/UB
    # checks still run; only the unsupported leak subprocess is disabled for this test binary.
    environment = os.environ.copy()
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    subprocess.run([str(executable)], check=True, env=environment, cwd=temporary)
