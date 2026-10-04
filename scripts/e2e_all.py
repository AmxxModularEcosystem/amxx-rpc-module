import json
import socket
import sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 27016
TOKEN = sys.argv[3] if len(sys.argv) > 3 else ""

failures = 0


def check(ok, label):
    global failures
    print(("ok   - " if ok else "FAIL - ") + label)
    if not ok:
        failures += 1


def connect(token=TOKEN):
    s = socket.create_connection((HOST, PORT), timeout=5.0)
    s.settimeout(5.0)
    if token is not None:
        r = rpc(s, "rpc.auth", {"token": token}, 1)
        check(isinstance(r, dict) and r.get("result", {}).get("ok") is True,
              "rpc.auth -> ok")
    return s


def rpc(s, method, params, req_id):
    req = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params is not None:
        req["params"] = params
    s.sendall((json.dumps(req, separators=(",", ":")) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        try:
            chunk = s.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
    if not buf:
        return None
    return json.loads(buf.decode("utf-8", "replace").strip())


print("== transport/codec ==")
s = connect()
check((rpc(s, "rpc.ping", None, 2) or {}).get("result") is not None, "rpc.ping")
check("module" in (rpc(s, "rpc.version", None, 3) or {}).get("result", {}), "rpc.version")
methods = (rpc(s, "rpc.methods", None, 4) or {}).get("result", [])
names = {m.get("name") for m in methods} if isinstance(methods, list) else set()
check("rpc.ping" in names, "rpc.methods has rpc.ping")
check("bot.available" in names and "fake.create" in names, "rpc.methods has bot.*/fake.*")

print("== fake players ==")
r = rpc(s, "fake.create", {"name": "E2EDummy", "authid": "STEAM_9:9:424242"}, 5)
res = r.get("result", {}) if isinstance(r, dict) else {}
idx = res.get("index")
check(idx is not None and res.get("authid") == "STEAM_9:9:424242", "fake.create -> index+authid")
if idx is not None:
    check(isinstance((rpc(s, "fake.get", {"index": idx}, 6) or {}).get("result"), dict), "fake.get")
    check((rpc(s, "fake.move", {"index": idx, "forward": 100}, 7) or {}).get("result", {}).get("ok") is True, "fake.move")
    check((rpc(s, "fake.remove", {"index": idx}, 8) or {}).get("result", {}).get("ok") is True, "fake.remove")
check((rpc(s, "fake.get", {"index": 1}, 9) or {}).get("error", {}).get("code") == -32602,
      "fake.get real player -> -32602")

print("== bot adapter (no YAPB expected) ==")
avail = (rpc(s, "bot.available", None, 10) or {}).get("result", {})
check(avail.get("available") is False, "bot.available -> {available:false}")
check((rpc(s, "bot.add", {"name": "x"}, 11) or {}).get("error", {}).get("code") == -32002,
      "bot.add -> -32002")
s.close()

print()
print("%d failures" % failures)
sys.exit(1 if failures else 0)
