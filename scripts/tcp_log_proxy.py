#!/usr/bin/env python3
"""Logging TCP proxy for diagnosing the JSON-RPC client (e.g. the amxb MCP bridge).

Listens on 127.0.0.1:<listen_port> and forwards traffic to the AmxxRpc module
(<module_host>:<module_port>), printing every line in both directions. Point the
bridge at the proxy to see exactly what it sends and what comes back.

Usage:
    python3 scripts/tcp_log_proxy.py [listen_port] [module_host] [module_port]
Defaults: 27017 127.0.0.1 27016
"""

import socket
import sys
import threading

LISTEN_PORT = int(sys.argv[1]) if len(sys.argv) > 1 else 27017
MODULE_HOST = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
MODULE_PORT = int(sys.argv[3]) if len(sys.argv) > 3 else 27016


def pump(src, dst, label):
    buf = b""
    try:
        while True:
            data = src.recv(4096)
            if not data:
                break
            dst.sendall(data)
            buf += data
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                print("%s %s" % (label, line.decode("utf-8", "replace")))
    except OSError:
        pass
    finally:
        try:
            dst.shutdown(socket.SHUT_WR)
        except OSError:
            pass


def handle(client):
    upstream = socket.create_connection((MODULE_HOST, MODULE_PORT))
    threading.Thread(target=pump, args=(client, upstream, "C->S"), daemon=True).start()
    threading.Thread(target=pump, args=(upstream, client, "S->C"), daemon=True).start()


def main():
    server = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    server.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    server.bind(("127.0.0.1", LISTEN_PORT))
    server.listen(8)
    print("proxy 127.0.0.1:%d -> %s:%d" % (LISTEN_PORT, MODULE_HOST, MODULE_PORT))
    while True:
        client, addr = server.accept()
        print("++ connection from %s:%d" % addr)
        threading.Thread(target=handle, args=(client,), daemon=True).start()


if __name__ == "__main__":
    main()
