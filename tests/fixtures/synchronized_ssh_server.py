"""One-connection loopback SSH fixture; no shell, files or forwarding exposed.

Requires Paramiko in the explicitly selected test Python environment.
Host key is generated in memory and discarded at exit.
"""

import base64
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
        print(f"PTY {width} {height}", flush=True)
        return True

    def check_channel_shell_request(self, channel):
        self.shell.set()
        return True

    def check_channel_window_change_request(self, channel, width, height,
                                            pixelwidth, pixelheight):
        print(f"RESIZE {width} {height}", flush=True)
        return True


def send_image(channel, shade):
    payload = base64.b64encode(bytes([shade]) * (128 * 128 * 3))
    for offset in range(0, len(payload), 4096):
        header = (b"a=T,f=24,s=128,v=128,i=88,p=1,C=1,q=2,"
                  if offset == 0 else b"q=2,")
        more = b"m=1;" if offset + 4096 < len(payload) else b"m=0;"
        channel.sendall(b"\x1b_G" + header + more + payload[offset:offset + 4096]
                        + b"\x1b\\")


def await_cursor_reply(channel):
    channel.sendall(b"\x1b[6n")
    response = b""
    while not response.endswith(b"R"):
        data = channel.recv(128)
        if not data:
            raise RuntimeError("EOF before image-burst CPR")
        response += data
    if not re.fullmatch(rb"\x1b\[\d+;\d+R", response):
        raise RuntimeError("invalid image-burst CPR")


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
                elif command == b"image-seed":
                    send_image(channel, 1)
                elif command == b"image-burst":
                    for shade in range(2, 82):
                        send_image(channel, shade)
                    channel.sendall(b"\x1b[HSSH_IMAGE_BURST_COMPLETE")
                    await_cursor_reply(channel)
                    print("IMAGES_DONE", flush=True)
                elif command == b"exit":
                    channel.sendall(b"\x1b[?2026hBYE")
                    channel.send_exit_status(0)
                    channel.close()
                    return
                else:
                    raise RuntimeError("unexpected fixture command")


if __name__ == "__main__":
    main()
