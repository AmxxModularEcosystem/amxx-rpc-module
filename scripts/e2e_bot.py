#!/usr/bin/env python3
"""E2E degradation check for the AmxxRpc Stage 4 YAPB adapter.

Runs against a server WITHOUT YAPB installed: bot.available must report
{available:false} and every other bot.* method must fail with -32002
(service unavailable). rpc.methods must still advertise the bot.* methods.

Usage:
    python3 scripts/e2e_bot.py [host] [port] [token]

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


def error_code(resp):
    return (resp or {}).get("error", {}).get("code")


def main():
    if not TOKEN:
        print("warning: empty token; pass the configured token as argv[3]")
    try:
        sock = socket.create_connection((HOST, PORT), timeout=10)
        sock.settimeout(10)
    except OSError as exc:
        print("connection error: %s" % exc)
        return 1

    auth = call(sock, "rpc.auth", {"token": TOKEN}, 1)
    check((auth or {}).get("result", {}).get("ok") is True, "rpc.auth -> ok")

    available = call(sock, "bot.available", None, 2)
    result = (available or {}).get("result", {})
    check(result.get("available") is False, "bot.available -> {available:false}")

    check(error_code(call(sock, "bot.add", {"name": "YapbBot"}, 3)) == -32002,
          "bot.add -> -32002")
    check(error_code(call(sock, "bot.goal", {"index": 1, "origin": [0, 0, 0]}, 4)) == -32002,
          "bot.goal -> -32002")
    check(error_code(call(sock, "bot.look", {"index": 1, "origin": [0, 0, 0]}, 5)) == -32002,
          "bot.look -> -32002")
    check(error_code(call(sock, "bot.freeze", {"index": 1, "frozen": True}, 6)) == -32002,
          "bot.freeze -> -32002")
    check(error_code(call(sock, "bot.status", {"index": 1}, 7)) == -32002,
          "bot.status -> -32002")
    check(error_code(call(sock, "bot.list", None, 8)) == -32002,
          "bot.list -> -32002")

    methods = call(sock, "rpc.methods", None, 9)
    names = {m.get("name") for m in (methods or {}).get("result", [])}
    for name in ("bot.available", "bot.add", "bot.list", "bot.goal",
                 "bot.look", "bot.freeze", "bot.status"):
        check(name in names, "rpc.methods includes %s" % name)

    sock.close()
    print("%d checks, %d failures" % (CHECKS, FAILURES))
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
