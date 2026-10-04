#!/usr/bin/env python3
"""TCP smoke client for AmxxRpc Stage 1 (JSON-RPC 2.0 over TCP).

Pure python3 stdlib. Usage:
    python3 scripts/smoke_tcp.py [host] [port] [token]

Exit code 0 when all checks pass, 1 otherwise.
"""

import json
import socket
import sys

HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 27016
TOKEN = sys.argv[3] if len(sys.argv) > 3 else ""

FAILURES = 0
CHECKS = 0


def check(cond, label):
    global FAILURES, CHECKS
    CHECKS += 1
    if cond:
        print("ok   - %s" % label)
    else:
        FAILURES += 1
        print("FAIL - %s" % label)


def connect():
    sock = socket.create_connection((HOST, PORT), timeout=5.0)
    sock.settimeout(5.0)
    return sock


def send_raw(sock, data):
    if isinstance(data, str):
        data = data.encode("utf-8")
    sock.sendall(data)


def send_obj(sock, obj):
    send_raw(sock, json.dumps(obj, separators=(",", ":")) + "\n")


def recv_line(sock):
    buf = b""
    while not buf.endswith(b"\n"):
        chunk = sock.recv(4096)
        if not chunk:
            break
        buf += chunk
    return buf.decode("utf-8", "replace").strip()


def recv_json(sock):
    line = recv_line(sock)
    if not line:
        return None
    try:
        return json.loads(line)
    except ValueError:
        return {"_raw": line}


def rpc(sock, method, params=None, req_id=1):
    req = {"jsonrpc": "2.0", "id": req_id, "method": method}
    if params is not None:
        req["params"] = params
    send_obj(sock, req)
    return recv_json(sock)


def auth(sock, token):
    return rpc(sock, "rpc.auth", {"token": token}, 1)


def test_happy_path():
    print("== happy path ==")
    sock = connect()
    resp = auth(sock, TOKEN)
    check(resp is not None and resp.get("result", {}).get("ok") is True,
          "rpc.auth correct token -> ok")

    resp = rpc(sock, "rpc.ping", None, 2)
    check(resp is not None and resp.get("result", {}).get("pong") is True,
          "rpc.ping -> pong")

    resp = rpc(sock, "rpc.version", None, 3)
    result = (resp or {}).get("result", {})
    check("module" in result and "protocol" in result,
          "rpc.version -> module + protocol")

    resp = rpc(sock, "rpc.methods", None, 4)
    result = (resp or {}).get("result", {})
    names = {m.get("name"): m.get("source") for m in result} if isinstance(result, list) else {}
    check("rpc.ping" in names,
          "rpc.methods -> includes rpc.ping")
    check(names.get("rpc.auth") == "transport",
          "rpc.methods -> rpc.auth shown as transport")

    resp = rpc(sock, "rpc.nope", None, 5)
    check((resp or {}).get("error", {}).get("code") == -32601,
          "unknown method -> -32601")

    sock.close()


def test_bad_token():
    print("== bad token ==")
    sock = connect()
    resp = auth(sock, "wrong-token-wrong-token")
    check((resp or {}).get("error", {}).get("code") == -32001,
          "rpc.auth wrong token -> -32001")
    check(recv_line(sock) == "", "connection closed after auth failure")
    sock.close()


def test_invalid_json():
    print("== invalid json ==")
    sock = connect()
    auth(sock, TOKEN)
    send_raw(sock, "{not json\n")
    resp = recv_json(sock)
    check((resp or {}).get("error", {}).get("code") == -32700,
          "invalid JSON -> -32700")
    check(recv_line(sock) == "", "connection closed after parse error")
    sock.close()


def test_batch_rejected():
    print("== batch ==")
    sock = connect()
    auth(sock, TOKEN)
    send_raw(sock, '[{"jsonrpc":"2.0","id":1,"method":"rpc.ping"}]\n')
    resp = recv_json(sock)
    check((resp or {}).get("error", {}).get("code") == -32600,
          "batch -> -32600")
    sock.close()


def test_preauth_rejected():
    print("== pre-auth ==")
    sock = connect()
    resp = rpc(sock, "rpc.ping", None, 1)
    check((resp or {}).get("error", {}).get("code") == -32001,
          "method before auth -> -32001")
    sock.close()


def main():
    if not TOKEN:
        print("warning: empty token; pass the configured token as argv[3]")
    try:
        test_happy_path()
        test_bad_token()
        test_invalid_json()
        test_batch_rejected()
        test_preauth_rejected()
    except OSError as exc:
        print("connection error: %s" % exc)
        return 1
    print("%d checks, %d failures" % (CHECKS, FAILURES))
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
