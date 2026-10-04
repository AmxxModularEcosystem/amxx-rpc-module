import json
import socket
import sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 27016
TOKEN = sys.argv[3] if len(sys.argv) > 3 else ""


def call(sock, method, params, req_id):
    req = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params is not None:
        req["params"] = params
    sock.sendall((json.dumps(req, separators=(",", ":")) + "\n").encode())
    buf = b""
    while not buf.endswith(b"\n"):
        try:
            chunk = sock.recv(4096)
        except socket.timeout:
            break
        if not chunk:
            break
        buf += chunk
    if not buf:
        return None
    return json.loads(buf.decode("utf-8", "replace").strip())


s = socket.create_connection((HOST, PORT), timeout=10)
s.settimeout(10)
print("auth:  ", call(s, "rpc.auth", {"token": TOKEN}, 1))

r = call(s, "fake.create", {"name": "DummyOne", "authid": "STEAM_9:9:99999"}, 2)
print("create:", r)
idx = r.get("result", {}).get("index") if isinstance(r.get("result"), dict) else None

print("list:  ", call(s, "fake.list", None, 3))
if idx is not None:
    print("get:   ", call(s, "fake.get", {"index": idx}, 4))
    print("move:  ", call(s, "fake.move", {"index": idx, "forward": 200, "side": 0}, 5))
    print("buttons:", call(s, "fake.buttons", {"index": idx, "press": ["IN_ATTACK"]}, 6))
    print("set:   ", call(s, "fake.set", {"index": idx, "health": 100, "armor": 50}, 7))
    print("remove:", call(s, "fake.remove", {"index": idx}, 8))
print("list2: ", call(s, "fake.list", None, 9))
print("realplayer:", call(s, "fake.get", {"index": 1}, 10))
s.close()
