# hostsim.py - drive the plugin the way the WinShun host does: pipes instead
# of a console, WS_APP_DIR set, then the JSON-RPC sequence (initialize, query,
# action). Reproduces crashes that the unit tests cannot, and reports the exit
# code.
#
# Usage: python tools/verify/hostsim.py [noshow] [settings={"ecc":"H", ...}]
#   default: query qr and qrwifi, copy the image, then preview (a window comes
#   up; the script kills the plugin after 6 s). noshow skips the preview.
import json
import os
import subprocess
import sys
import threading
import time

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
EXE = os.path.join(ROOT, "qr.exe")
WS_DIR = os.environ.get("QR_WS_DIR", r"C:\Program Files\WinShun")

URL = "https://example.com/hello"
PAYLOAD = "WIFI:T:WPA;S:MyNet;P:s3 cret;;"  # what qrwifi builds below


def reader(pipe, tag, out):
    for line in iter(pipe.readline, b""):
        out.append(line)
        print(f"[{tag}] {line.decode('utf-8', 'replace').rstrip()}")


def send(proc, obj):
    text = json.dumps(obj, ensure_ascii=False)
    print(f">>> {text}")
    proc.stdin.write((text + "\n").encode("utf-8"))
    proc.stdin.flush()


def response_with_id(lines, want):
    for line in list(lines):
        try:
            message = json.loads(line.decode("utf-8", "replace"))
        except ValueError:
            continue
        if message.get("id") == want and "result" in message:
            return message["result"]
    return None


def first_saved_row(lines, want):
    """The first qrwifi row an action can run from, or None."""
    result = response_with_id(lines, want)
    if not result:
        return None
    for item in result.get("items", []):
        if item.get("data", {}).get("wifi") and item.get("valid", True):
            return item
    return None


def main():
    do_show = "noshow" not in sys.argv
    settings = {"ecc": "M", "moduleSize": 8, "quietZone": 4,
                "darkColor": "#000000", "lightColor": "#ffffff"}
    for arg in sys.argv[1:]:
        if arg.startswith("settings="):
            settings = json.loads(arg.split("=", 1)[1])
    env = dict(os.environ)
    env["WS_APP_DIR"] = WS_DIR
    proc = subprocess.Popen([EXE], env=env, cwd=ROOT,
                            stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                            stderr=subprocess.PIPE)
    out, err = [], []
    threading.Thread(target=reader, args=(proc.stdout, "out", out), daemon=True).start()
    threading.Thread(target=reader, args=(proc.stderr, "err", err), daemon=True).start()

    send(proc, {"id": 1, "method": "initialize", "params": {
        "settings": settings, "locale": "zh-CN", "theme": "light",
        "pluginDir": ROOT, "dataDir": os.path.join(os.environ["LOCALAPPDATA"], "WinShun")}})
    time.sleep(1.0)
    # qr with text; qrwifi (SSIDs and passwords get escaped); qr empty (the
    # clipboard decides); qrwifi with no SSID (the hint row).
    send(proc, {"id": 2, "method": "query", "params": {
        "queryId": 1, "command": "qr", "trigger": "", "keyword": "qr",
        "text": URL, "raw": "qr " + URL, "limit": 200, "match": []}})
    send(proc, {"id": 3, "method": "query", "params": {
        "queryId": 2, "command": "qrwifi", "trigger": "", "keyword": "qrwifi",
        "text": "MyNet s3; cret", "raw": "qrwifi MyNet s3; cret", "limit": 200, "match": []}})
    send(proc, {"id": 4, "method": "query", "params": {
        "queryId": 3, "command": "qr", "trigger": "", "keyword": "qr",
        "text": "", "raw": "qr ", "limit": 200, "match": []}})
    send(proc, {"id": 5, "method": "query", "params": {
        "queryId": 4, "command": "qrwifi", "trigger": "", "keyword": "qrwifi",
        "text": "", "raw": "qrwifi", "limit": 200, "match": []}})
    time.sleep(0.5)
    # The filter path: only the matching saved network survives.
    send(proc, {"id": 9, "method": "query", "params": {
        "queryId": 5, "command": "qrwifi", "trigger": "", "keyword": "qrwifi",
        "text": "imp", "raw": "qrwifi imp", "limit": 200, "match": []}})
    time.sleep(0.5)

    # The host dedupes rows by item id (Merge.cpp: first of an id wins), so a
    # shared id would collapse the list to its first row. Guard that here.
    result = response_with_id(out, 5)
    ids = [item.get("id") for item in (result or {}).get("items", [])
           if item.get("data", {}).get("wifi")]
    print(f"saved-network ids: {ids}")
    if len(ids) != len(set(ids)):
        print("FAIL: duplicate item ids — the host would drop rows")

    # A saved network row: Enter on it. The row carries only the SSID, so this
    # is what proves the passphrase is read again rather than carried in the
    # item. The row chosen is the first one an action can run from.
    saved = first_saved_row(out, 5)
    if saved is None:
        print("no saved Wi-Fi network row: the action path is not exercised")
    else:
        print(f"saved network row: {saved['title']} ({saved['subtitle']})")
        send(proc, {"id": 8, "method": "action", "params": {
            "command": "qrwifi", "action": "copyImage", "text": "",
            "item": saved}})
        time.sleep(0.5)

    # Enter: the image goes to the clipboard, the row says so. Then the
    # preview window (if do_show).
    send(proc, {"id": 6, "method": "action", "params": {
        "command": "qr", "action": "copyImage", "text": URL,
        "item": {"id": "code", "data": {"payload": URL, "command": "qr", "text": URL}}}})
    time.sleep(0.5)
    if do_show:
        send(proc, {"id": 7, "method": "action", "params": {
            "command": "qr", "action": "preview", "text": URL,
            "item": {"id": "code", "data": {"payload": URL, "command": "qr", "text": URL}}}})
        time.sleep(6.0)  # the preview stays up: kill afterwards

    if proc.poll() is None:
        print("alive after the sequence, killing (the preview must be gone)")
        proc.kill()
    code = proc.wait()
    print(f"exit: {code} ({hex(code & 0xFFFFFFFF)})")
    time.sleep(0.2)
    # 0: clean exit; 1: what Popen.kill() reports on Windows (TerminateProcess
    # with code 1); 0x40010004: the console killed variant.
    return 0 if (code & 0xFFFFFFFF) in (0, 1, 0x40010004) else 1


if __name__ == "__main__":
    sys.exit(main())
