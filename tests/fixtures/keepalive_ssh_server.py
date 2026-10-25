"""Loopback-only SSH liveness fixture. No commands or real user shells run.

Drop encrypted server output while keeping TCP open and accepting client writes:
this reproduces a blackholed connection, not a graceful close or send error.
"""

import socket
import sys
import threading
import time

sys.dont_write_bytecode = True

import paramiko
from synchronized_ssh_server import Server


class OutputGate:
    def __init__(self, sock):
        self.sock = sock
        self.drop = threading.Event()

    def send(self, data):
        if self.drop.is_set():
            return len(data)
        return self.sock.send(data)

    def __getattr__(self, name):
        return getattr(self.sock, name)


def main():
    finish = threading.Event()

    def controls():
        for command in sys.stdin:
            if command.strip() == "finish":
                finish.set()

    threading.Thread(target=controls, daemon=True).start()
    key = paramiko.RSAKey.generate(2048)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        print(listener.getsockname()[1], flush=True)
        connection, _ = listener.accept()
        gate = OutputGate(connection)
        with paramiko.Transport(gate) as transport:
            transport.add_server_key(key)
            server = Server()
            transport.start_server(server=server)
            channel = transport.accept(10)
            if channel is None or not server.shell.wait(10):
                raise RuntimeError("fixture shell missing")
            channel.sendall(b"KEEPALIVE_READY\r\n")
            # Let the shell-success and initial frame reach the client first.
            time.sleep(0.25)
            if sys.argv[1] == "blackhole":
                gate.drop.set()
                print("BLACKHOLE", flush=True)
            else:
                print("RESPONDING", flush=True)
            if not finish.wait(15):
                raise RuntimeError("fixture completion missing")


if __name__ == "__main__":
    main()
