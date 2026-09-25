"""One-connection loopback SSH fixture; no shell, files or forwarding exposed.

Requires Paramiko in the explicitly selected test Python environment.
Host key is generated in memory and discarded at exit.
"""

import re
import socket
import sys
import threading

import paramiko


class Server(paramiko.ServerInterface):
    def __init__(self):
        self.shell = threading.Event()

    def check_auth_password(self, username, password):
        return (paramiko.AUTH_SUCCESSFUL
                if (username, password) == ("fixture", "fixture-only")
                else paramiko.AUTH_FAILED)

    def check_channel_request(self, kind, chanid):
        return (paramiko.OPEN_SUCCEEDED if kind == "session"
                else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED)

    def check_channel_pty_request(self, channel, term, width, height,
                                  pixelwidth, pixelheight, modes):
        return True

    def check_channel_shell_request(self, channel):
        self.shell.set()
        return True

    def check_channel_window_change_request(self, channel, width, height,
                                            pixelwidth, pixelheight):
        print("RESIZE", flush=True)
        return True


def main():
    key = paramiko.RSAKey.generate(2048)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(15)
        print(listener.getsockname()[1], flush=True)
        connection, _ = listener.accept()
        with paramiko.Transport(connection) as transport:
            transport.add_server_key(key)
            server = Server()
            transport.start_server(server=server)
            channel = transport.accept(10)
            if channel is None or not server.shell.wait(10):
                raise RuntimeError("shell request missing")
            channel.settimeout(10)
            channel.sendall(b"BASE\r\n")
            for command in sys.stdin.buffer:
                command = command.rstrip(b"\n\r")
                if command == b"begin":
                    channel.sendall(b"\x1b[?2026hPARTIAL\x1b[6n")
                    response = b""
                    while not response.endswith(b"R"):
                        data = channel.recv(128)
                        if not data:
                            raise RuntimeError("EOF before CPR")
                        response += data
                    if not re.fullmatch(rb"\x1b\[\d+;\d+R", response):
                        raise RuntimeError("invalid CPR")
                    print("CPR", flush=True)
                elif command == b"end":
                    channel.sendall(b"FINAL\x1b[?2026l")
                elif command == b"orphan":
                    channel.sendall(b"\x1b[?2026hORPHAN")
                    print("ORPHAN", flush=True)
                elif command == b"next":
                    channel.sendall(b"\x1b[?2026hNEXT")
                    print("NEXT", flush=True)
                elif command == b"exit":
                    channel.sendall(b"\x1b[?2026hBYE")
                    channel.send_exit_status(0)
                    channel.close()
                    return
                else:
                    raise RuntimeError("unexpected fixture command")


if __name__ == "__main__":
    main()
