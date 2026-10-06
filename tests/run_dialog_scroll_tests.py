#!/usr/bin/env python3
"""Check production scroll math and C++ types. This is not a Windows/MFC build."""
from pathlib import Path
import os
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
source = root / 'ipc2019'
with tempfile.TemporaryDirectory(prefix='ipc-scroll-tests-') as directory:
    temporary = Path(directory)
    # [assignment6] 실제 main header의 새 멤버 선언을 재사용해 구현/선언이 어긋난 경우도 검출한다.
    header = (source / 'ipc2019Dlg.h').read_text(encoding='utf-8-sig')
    members = header[header.index('    struct SCROLL_CHILD'):header.rindex('};')]
    shim = (root / 'tests/portable/ScrollSyntaxShim.h').read_text(encoding='utf-8')
    (temporary / 'ScrollTestWindow.h').write_text(shim + '\nclass Cipc2019Dlg : public CDialogEx { public:\n' + members + '\n};\n')
    production = (source / 'ipc2019DlgScroll.cpp').read_text(encoding='utf-8')
    production = production.replace('#include "pch.h"\n#include "ipc2019Dlg.h"', '#include "ScrollTestWindow.h"')
    unit = temporary / 'scroll_syntax.cpp'
    unit.write_text(production, encoding='utf-8')
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsyntax-only', '-I', str(source), str(unit)], check=True)
    executable = temporary / 'scroll_tests'
    subprocess.run(['g++', '-std=c++17', '-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                    '-I', str(source), str(root / 'tests/dialog_scroll_tests.cpp'), '-o', str(executable)], check=True)
    environment = os.environ.copy()
    environment.setdefault('ASAN_OPTIONS', 'detect_leaks=0')
    subprocess.run([str(executable)], check=True, env=environment)
    print('PASS: production scroll handlers/declarations, LONG/int compatibility and Windows min/max macro syntax')
