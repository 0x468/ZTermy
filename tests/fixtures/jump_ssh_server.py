"""Loopback-only jump/PTY fixture, no real shell or arbitrary forwarding."""

import socket
import sys
import threading
import time

sys.dont_write_bytecode = True

import paramiko
from synchronized_ssh_server import Server


def pump(source, destination):
    try:
        while True:
            data = source.recv(32768)
            if not data:
                break
            destination.sendall(data)
    except (OSError, EOFError):
        pass
    finally:
        destination.close()


class JumpServer(Server):
    def __init__(self, destination, mode):
        super().__init__()
        self.destination = destination
        self.mode = mode

    def check_auth_password(self, username, password):
        if self.mode == "auth-failure" and self.destination is None:
            return paramiko.AUTH_FAILED
        return super().check_auth_password(username, password)

    def check_channel_direct_tcpip_request(self, chanid, origin, destination):
        if self.mode == "refuse" or destination != ("127.0.0.1", self.destination):
            return paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED
        return paramiko.OPEN_SUCCEEDED


def serve(listener, destination, mode, finish, errors):
    try:
        connection, _ = listener.accept()
        with paramiko.Transport(connection) as transport:
            transport.add_server_key(paramiko.RSAKey.generate(2048))
            server = JumpServer(destination, mode)
            transport.start_server(server=server)
            channel = transport.accept(12)
            if channel is None:
                return  # Expected authentication, forwarding or host-key refusal.
            if destination is not None:
                upstream = socket.create_connection(("127.0.0.1", destination), timeout=10)
                upstream.settimeout(None)
                threading.Thread(target=pump, args=(channel, upstream), daemon=True).start()
                threading.Thread(target=pump, args=(upstream, channel), daemon=True).start()
            elif server.shell.wait(10):
                channel.sendall(b"JUMP_PTY_READY\r\n")
                print("PTY_READY", flush=True)
            while transport.is_active() and not finish.wait(0.05):
                pass
    except (OSError, EOFError, paramiko.SSHException):
        pass  # Host-key rejection/cancellation closes an in-progress handshake.
    except Exception as error:
        errors.append(type(error).__name__)


def main():
    finish = threading.Event()
    errors = []
    listeners = []
    for _ in range(3):
        listener = socket.socket()
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        listeners.append(listener)
    ports = [listener.getsockname()[1] for listener in listeners]
    for index, listener in enumerate(listeners):
        destination = ports[index + 1] if index < 2 else None
        threading.Thread(target=serve, args=(listener, destination, sys.argv[1], finish, errors), daemon=True).start()
    print(" ".join(map(str, ports)), flush=True)
    for command in sys.stdin:
        if command.strip() == "finish":
            break
    finish.set()
    for listener in listeners:
        listener.close()
    if errors:
        raise RuntimeError("fixture failed: " + ",".join(errors))


if __name__ == "__main__":
    main()
