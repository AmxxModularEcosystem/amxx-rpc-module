#!/usr/bin/env python3
"""E2E check for the AmxxRpc YAPB adapter (adaptive: YAPB present or absent).

- If YAPB is absent: bot.available -> {available:false}; every other bot.* -> -32002.
- If YAPB is present: bot.available -> {available:true,...}; bot.list works;
  set/query on a bot index returns ok. (bot.add is queued; a new index is not
  returned synchronously.)

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
        chunk = sock.recv(4096)
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

    rid = 0

    def nxt():
        nonlocal rid
        rid += 1
        return rid

    auth = call(sock, "rpc.auth", {"token": TOKEN}, nxt())
    check((auth or {}).get("result", {}).get("ok") is True, "rpc.auth -> ok")

    available = (call(sock, "bot.available", None, nxt()) or {}).get("result", {})
    is_avail = available.get("available") is True

    if is_avail:
        print("info - YAPB available (version=%s)" % available.get("version"))
        listing = (call(sock, "bot.list", None, nxt()) or {}).get("result")
        check(isinstance(listing, list), "bot.list -> array")
        indices = [b.get("index") for b in listing or [] if isinstance(b, dict)]
        check((call(sock, "bot.add", {"name": "E2EYapb"}, nxt()) or {}).get("result", {}).get("queued") is True,
              "bot.add -> {queued:true}")
        if indices:
            i = indices[0]
            check((call(sock, "bot.status", {"index": i}, nxt()) or {}).get("result", {}).get("index") == i,
                  "bot.status -> index")
            check((call(sock, "bot.look", {"index": i, "origin": [0, 0, 0]}, nxt()) or {}).get("result", {}).get("ok") is True,
                  "bot.look -> ok")
            check((call(sock, "bot.freeze", {"index": i, "frozen": True}, nxt()) or {}).get("result", {}).get("ok") is True,
                  "bot.freeze -> ok")
        else:
            print("info - no bots present; skipping per-bot checks")
    else:
        check(available.get("available") is False, "bot.available -> {available:false}")
        for name, params in (("bot.add", {"name": "YapbBot"}),
                             ("bot.goal", {"index": 1, "origin": [0, 0, 0]}),
                             ("bot.look", {"index": 1, "origin": [0, 0, 0]}),
                             ("bot.freeze", {"index": 1, "frozen": True}),
                             ("bot.status", {"index": 1}),
                             ("bot.list", None)):
            check(error_code(call(sock, name, params, nxt())) == -32002,
                  "%s -> -32002" % name)

    methods = call(sock, "rpc.methods", None, nxt())
    names = {m.get("name") for m in (methods or {}).get("result", [])}
    for name in ("bot.available", "bot.add", "bot.list", "bot.goal",
                 "bot.look", "bot.freeze", "bot.status"):
        check(name in names, "rpc.methods includes %s" % name)

    sock.close()
    print("%d checks, %d failures" % (CHECKS, FAILURES))
    return 0 if FAILURES == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
