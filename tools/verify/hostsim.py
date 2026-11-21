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
user32.GetWindowRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
user32.GetClientRect.argtypes = [w.HWND, c.POINTER(w.RECT)]
user32.GetDC.restype = w.HDC
user32.GetDC.argtypes = [w.HWND]
user32.ReleaseDC.argtypes = [w.HWND, w.HDC]
user32.GetDpiForWindow.restype = w.UINT
user32.GetDpiForWindow.argtypes = [w.HWND]
user32.GetCursorPos.argtypes = [c.POINTER(w.POINT)]
user32.SetCursorPos.argtypes = [c.c_int, c.c_int]
user32.ClientToScreen.argtypes = [w.HWND, c.POINTER(w.POINT)]
gdi32 = c.WinDLL('gdi32', use_last_error=True)
gdi32.GetPixel.restype = w.DWORD
gdi32.GetPixel.argtypes = [w.HDC, c.c_int, c.c_int]

WM_KEYDOWN = 0x0100

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

def pixel(hwnd, x, y):
    dc = user32.GetDC(hwnd)
    value = gdi32.GetPixel(dc, x, y)
    user32.ReleaseDC(hwnd, dc)
    return value & 0xFF, (value >> 8) & 0xFF, (value >> 16) & 0xFF

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
            # One preview per action: the first stays open while the second
            # appears beside it, and each closes on its own Escape.
            user32.AllowSetForegroundWindow(proc.pid)
            rpc('action', {'command': 'qr', 'action': 'preview', 'item': row})
            first = wait_for(lambda: windows(proc.pid), 'preview never appeared')[0]
            wait_for(lambda: user32.GetForegroundWindow() == first, 'preview did not take foreground')
            info = GUIInfo(cbSize=c.sizeof(GUIInfo))
            thread = user32.GetWindowThreadProcessId(first, None)
            assert user32.GetGUIThreadInfo(thread, c.byref(info)) and info.focus == first, 'preview has no keyboard focus'
            # The close button reddens under the mouse and dulls again when it
            # leaves, as a window's own does. A posted move is not enough: the
            # cursor is really moved and put back, and the colour is sampled at
            # the button's top-left corner, away from the cross.
            client = w.RECT()
            user32.GetClientRect(first, c.byref(client))
            dpi = user32.GetDpiForWindow(first)
            at = (client.right - 44 * dpi // 96 + 6 * dpi // 96, 6 * dpi // 96)
            screen = w.POINT(*at)
            user32.ClientToScreen(first, c.byref(screen))
            origin = w.POINT()
            user32.GetCursorPos(c.byref(origin))
            idle = pixel(first, *at)
            user32.SetCursorPos(screen.x, screen.y)
            wait_for(lambda: pixel(first, *at) == (196, 43, 28), 'close button did not redden under the mouse')
            user32.SetCursorPos(origin.x, origin.y)
            wait_for(lambda: pixel(first, *at) == idle, 'close button stayed red after the mouse left')
            user32.AllowSetForegroundWindow(proc.pid)
            rpc('action', {'command': 'qr', 'action': 'preview', 'item': row})
            second = wait_for(lambda: next((h for h in windows(proc.pid) if h != first), None),
                              'second preview never appeared')
            wait_for(lambda: user32.GetForegroundWindow() == second, 'second preview did not take foreground')

            def rect_of(hwnd):
                r = w.RECT()
                user32.GetWindowRect(hwnd, c.byref(r))
                return (r.left, r.top)

            assert rect_of(first) != rect_of(second), 'windows landed exactly on top of each other'
            user32.PostMessageW(second, 0x100, 0x1b, 0)
            wait_for(lambda: second not in windows(proc.pid), 'Escape did not close the second preview')
            assert first in windows(proc.pid), 'the first preview was closed too'
            user32.PostMessageW(first, 0x100, 0x1b, 0)
            wait_for(lambda: not windows(proc.pid), 'Escape did not close the last preview')
            user32.AllowSetForegroundWindow(proc.pid)
            rpc('action', {'command': 'qr', 'action': 'preview', 'item': row})
            again = wait_for(lambda: windows(proc.pid), 'reopened preview never appeared')
            user32.PostMessageW(again[0], 0x100, 0x1b, 0)
            wait_for(lambda: not windows(proc.pid), 'reopened preview did not close')
        print('PASS: text QR, removed command/action, clipboard, two coexisting previews, foreground/focus/Esc, close hover')
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
