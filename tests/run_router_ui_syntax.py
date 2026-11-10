#!/usr/bin/env python3
"""Compile the production RouterDlg declarations/body with minimal MFC declarations, not Windows/MFC."""
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / 'ipc2019'
with tempfile.TemporaryDirectory(prefix='ipc-router-ui-') as directory:
    temporary = Path(directory)
    text = (source / 'stdafx.h').read_text(encoding='utf-8-sig')
    constants = text[text.index('#define MAX_LAYER_NUMBER'):text.index('// Microsoft Visual C++ will insert')]
    (temporary / 'ProtocolConstants.h').write_text(constants, encoding='utf-8')
    (temporary / 'afxdialogex.h').write_text('#pragma once\n', encoding='utf-8')
    # Existing CBaseLayer compatibility overloads intentionally leave some parameters unused.
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter', '-fsyntax-only',
                    '-I', str(temporary), '-I', str(source), '-I', str(root/'tests/portable'),
                    '-include', str(root/'tests/portable/RouterSyntaxShim.h'), str(source/'RouterDlg.cpp')], check=True)
print('PASS: production RouterDlg declarations/body and route-entry types with portable MFC declarations (not a Windows build)')
