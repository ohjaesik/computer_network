#!/usr/bin/env python3
"""Check MFC project/resource wiring, without claiming a Windows compilation."""
from collections import Counter
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET

root = Path(__file__).resolve().parents[1]
source = root / 'ipc2019'
ns = {'ms': 'http://schemas.microsoft.com/developer/msbuild/2003'}
project = ET.parse(source / 'ipc2019.vcxproj')
filters = ET.parse(source / 'ipc2019.vcxproj.filters')
available = {path.relative_to(source).as_posix().casefold() for path in source.rglob('*') if path.is_file()}
for kind, suffix in [('ClCompile', '.cpp'), ('ClInclude', '.h')]:
    registered = [node.attrib['Include'] for node in project.findall(f'.//ms:ItemGroup/ms:{kind}', ns)]
    mapped = [node.attrib['Include'] for node in filters.findall(f'.//ms:ItemGroup/ms:{kind}', ns)]
    assert len(set(registered)) == len(registered), f'Duplicate project {kind}'
    assert set(registered) == set(mapped), f'Project/filters mismatch: {kind}'
    for name in registered:
        assert name.replace('\\', '/').casefold() in available, f'Missing source: {name}'
    for name in ('IPRouter', 'IPLayer', 'ARPLayer', 'ARPDlg', 'NetworkPackets'):
        if kind == 'ClCompile' and name == 'NetworkPackets':
            continue
        assert name + suffix in registered, f'Not registered: {name + suffix}'
    assert ('ipc2019DlgScroll.cpp' if kind == 'ClCompile' else 'DialogScrollLayout.h') in registered

header = (source / 'resource.h').read_text(encoding='utf-8-sig')
ids = dict(re.findall(r'^#define\s+((?:IDC_|IDD_)\w+)\s+(-?\d+)', header, re.M))
ids['IDC_STATIC'] = '-1'  # Standard MFC ID comes from afxres.h.
resource = (source / 'ipc2019.rc').read_text(encoding='utf-16')
for name in set(re.findall(r'\b(?:IDC_|IDD_)\w+', resource)):
    assert name in ids, f'Unknown resource ID: {name}'

dialogs = dict((name, (int(width), int(height), body)) for name, width, height, body in re.findall(
    r'(IDD_\w+) DIALOGEX \d+, \d+, (\d+), (\d+)\n.*?\nBEGIN\n(.*?)\nEND', resource, re.S))
main_style = re.search(r'IDD_IPC2019_DIALOG DIALOGEX[^\n]+\nSTYLE ([^\n]+)', resource).group(1)
for style in ('WS_THICKFRAME', 'WS_MAXIMIZEBOX'):
    assert style in main_style, f'Scrollable main window missing style: {style}'
# [assignment6] group box 내부까지 부모 배경을 지워야 이동 전 입력칸/글자의 잔상이 남지 않는다.
assert 'WS_CLIPCHILDREN' not in main_style, 'Group-box background must participate in scroll repaint'
page_controls = {}
for name in ('IDD_IPC2019_DIALOG', 'IDD_ARP_DIALOG'):
    width, height, body = dialogs[name]
    controls = re.findall(r'^\s*(\w+)\s+.*?\b(IDC_\w+),\s*(?:"[^"]*",[^,\n]+,)?\s*(\d+),(\d+),(\d+),(\d+)', body, re.M)
    values = [control for _, control, *_ in controls if control != 'IDC_STATIC']
    assert len(set(values)) == len(values), f'Duplicate dialog control: {name}'
    # Original resource.h has unused aliases such as IDC_EDIT1; only instantiated controls must be unique.
    numeric = [ids[control] for control in values]
    assert len(set(numeric)) == len(numeric), f'Duplicate numeric dialog ID: {name}'
    page_controls[name] = set(values)
    for kind, control, x, y, w, h in controls:
        x, y, w, h = map(int, (x, y, w, h))
        # COMBOBOX height is the drop-down list, not its collapsed rectangle.
        assert x + w <= width and y + (18 if kind == 'COMBOBOX' else h) <= height, f'Out of dialog: {control}'

for filename, dialog in [('ipc2019DlgNetwork.cpp', 'IDD_IPC2019_DIALOG'), ('ARPDlg.cpp', 'IDD_ARP_DIALOG')]:
    text = (source / filename).read_text(encoding='utf-8-sig')
    for name in set(re.findall(r'\bIDC_\w+', text)):
        assert name in page_controls[dialog], f'Missing UI control: {filename} / {name}'

# [assignment6] child Dialog 생성 후 가상 배치를 기록해야 ARP 화면도 외부 스크롤에 포함된다.
main_cpp = (source / 'ipc2019Dlg.cpp').read_text(encoding='utf-8-sig')
assert main_cpp.index('InitIpUi();') < main_cpp.index('InitDialogScroll();')
for message in ('ON_WM_SIZE()', 'ON_WM_VSCROLL()', 'ON_WM_HSCROLL()', 'ON_WM_MOUSEWHEEL()', 'ON_WM_GETMINMAXINFO()'):
    assert message in main_cpp, f'Missing scroll message handler: {message}'
assert 'if (::GetFocus() != m_lastScrollFocus) RevealFocusedControl();' in main_cpp

# [assignment6] 새 중계 구현에서도 기존 과제 4 주석을 원문 그대로 보존했는지 검사한다.
baseline = None
for revision in ('a33ccf64e78d32280a2d1663f7d1f7502508f145', 'cd6d6c3344d7fe14e607efd072000b80078d8895'):
    if subprocess.run(['git', 'cat-file', '-e', revision + '^{commit}'], cwd=root, capture_output=True).returncode == 0:
        baseline = revision
        break
assert baseline is not None, 'Comment check needs repository history, not a source-only ZIP'
for path in source.glob('*'):
    if path.suffix not in ('.cpp', '.h', '.rc'):
        continue
    result = subprocess.run(['git', 'show', f'{baseline}:{path.relative_to(root).as_posix()}'],
                            cwd=root, capture_output=True)
    if result.returncode:
        continue
    encoding = 'utf-16' if path.suffix == '.rc' else 'utf-8-sig'
    old = Counter(line.strip() for line in result.stdout.decode(encoding).splitlines() if '[assignment4]' in line)
    new = Counter(line.strip() for line in path.read_text(encoding=encoding).splitlines() if '[assignment4]' in line)
    assert not old - new, f'Original assignment4 comments changed: {path.name}'

print('PASS: project/filters, source registration, resource/UI bindings/bounds, scroll initialization/messages and original assignment4 comments')
