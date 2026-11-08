"""Fixture-only JSON-RPC and preview focus regression check; noshow skips UI."""
import ctypes as c
from ctypes import wintypes as w
import json
import os
from pathlib import Path
import queue
import subprocess
import sys
import threading
import time

ROOT = Path(__file__).resolve().parents[2]
URL = 'https://example.com/hello'
user32 = c.WinDLL('user32', use_last_error=True)
user32.GetForegroundWindow.restype = w.HWND
user32.GetWindowThreadProcessId.argtypes = [w.HWND, c.POINTER(w.DWORD)]
user32.IsWindowVisible.argtypes = [w.HWND]
user32.PostMessageW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
user32.AllowSetForegroundWindow.argtypes = [w.DWORD]

class GUIInfo(c.Structure):
    _fields_ = [('cbSize', w.DWORD), ('flags', w.DWORD), ('active', w.HWND),
                ('focus', w.HWND), ('capture', w.HWND), ('menu', w.HWND),
                ('move', w.HWND), ('caret', w.HWND), ('rect', w.RECT)]

user32.GetGUIThreadInfo.argtypes = [w.DWORD, c.POINTER(GUIInfo)]

def windows(pid):
    found = []
    callback = c.WINFUNCTYPE(w.BOOL, w.HWND, w.LPARAM)
    @callback
    def visit(hwnd, _):
        owner = w.DWORD()
        user32.GetWindowThreadProcessId(hwnd, c.byref(owner))
        if owner.value == pid and user32.IsWindowVisible(hwnd):
            found.append(hwnd)
        return True
    user32.EnumWindows(visit, 0)
    return found

def wait_for(fn, message):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        result = fn()
        if result:
            return result
        time.sleep(.04)
    raise AssertionError(message)

def main():
    env = dict(os.environ, WS_APP_DIR=os.environ.get('QR_WS_DIR', r'C:\Program Files\WinShun'))
    responses = queue.Queue()
    errors = []
    proc = subprocess.Popen([str(ROOT / 'qr.exe')], env=env, cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    def read():
        for line in iter(proc.stdout.readline, b''):
            responses.put(json.loads(line))
    threading.Thread(target=read, daemon=True).start()
    threading.Thread(target=lambda: errors.extend(proc.stderr.read().decode('utf-8', 'replace').splitlines()), daemon=True).start()
    seq = 0
    def rpc(method, params):
        nonlocal seq
        seq += 1
        proc.stdin.write((json.dumps({'jsonrpc': '2.0', 'id': seq, 'method': method, 'params': params}) + '\n').encode())
        proc.stdin.flush()
        while True:
            message = responses.get(timeout=10)
            if message.get('id') == seq:
                assert 'error' not in message, message
                return message['result']
    try:
        rpc('initialize', {'settings': {}, 'locale': 'zh-CN', 'theme': 'light', 'pluginDir': str(ROOT)})
        row = rpc('query', {'command': 'qr', 'text': URL, 'limit': 20})['items'][0]
        assert row['data']['payload'] == URL
        assert rpc('query', {'command': 'qrwifi', 'text': ''})['items'] == []
        old_action = rpc('action', {'command': 'qrwifi', 'action': 'copyImage', 'item': row})
        assert not old_action.get('effects'), old_action
        copied = rpc('action', {'command': 'qr', 'action': 'copyImage', 'item': row})
        assert copied.get('effects'), copied
        assert user32.IsClipboardFormatAvailable(8)
        if 'noshow' not in sys.argv:
            for _ in range(3):
                user32.AllowSetForegroundWindow(proc.pid)
                rpc('action', {'command': 'qr', 'action': 'preview', 'item': row})
                hwnd = wait_for(lambda: windows(proc.pid), 'preview never appeared')[0]
                wait_for(lambda: user32.GetForegroundWindow() == hwnd, 'preview did not take foreground')
                info = GUIInfo(cbSize=c.sizeof(GUIInfo))
                thread = user32.GetWindowThreadProcessId(hwnd, None)
                assert user32.GetGUIThreadInfo(thread, c.byref(info)) and info.focus == hwnd, 'preview has no keyboard focus'
                user32.PostMessageW(hwnd, 0x100, 0x1b, 0)
                wait_for(lambda: not windows(proc.pid), 'Escape did not close preview')
        print('PASS: text QR, removed command/action, clipboard, preview foreground/focus/Esc (UI unless noshow)')
    finally:
        proc.stdin.close()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
        if errors:
            print('\n'.join(errors[-8:]))

if __name__ == '__main__':
    main()
