"""Synthetic TUI exit over loopback SSH; no shell or user data exposed."""

import re
import socket
import sys
import threading
import time

sys.dont_write_bytecode = True

import paramiko

from synchronized_ssh_server import Server


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
        with paramiko.Transport(connection, default_window_size=32768) as transport:
            transport.add_server_key(key)
            server = Server()
            transport.start_server(server=server)
            channel = transport.accept(10)
            if channel is None or not server.shell.wait(10):
                raise RuntimeError("synthetic terminal request missing")
            channel.settimeout(0.05)
            channel.sendall(b"\x1b[?1049h\x1b[?1003;1006hTUI_READY")
            buffer = b""
            exited = False
            reports = 0
            padding_remaining = 512 * 1024
            while not finish.is_set():
                try:
                    data = channel.recv(65536)
                except socket.timeout:
                    continue
                if not data:
                    break
                buffer += data
                if not exited:
                    # Validate live tracking before testing the exit boundary.
                    boundary = buffer.find(b"EXIT\r")
                    before_exit = buffer if boundary < 0 else buffer[:boundary]
                    match = re.search(rb"\x1b\[<\d+;\d+;\d+[Mm]", before_exit)
                    if match:
                        print("TRACKED_MOUSE", flush=True)
                        buffer = buffer[match.end():]
                    boundary = buffer.find(b"EXIT\r")
                    if boundary < 0:
                        continue
                    buffer = buffer[boundary + 5:]
                    exited = True
                    reset = b"\x1b[?1006l\x1b[?1003l\x1b[?1049lSHELL_READY"
                    if sys.argv[1] == "bytewise":
                        for byte in reset:
                            channel.sendall(bytes([byte]))
                    else:
                        channel.sendall(reset)
                    print("RESET_SENT", flush=True)
                    # Fill the small receive window so the production worker
                    # must encounter a ready reset between queued writes.
                    time.sleep(0.35)
                # Only synthetic padding, mouse packets and a keyboard marker
                # are accepted; do not print arbitrary terminal input.
                padding = min(padding_remaining, len(buffer))
                if buffer[:padding] != b"P" * padding:
                    raise RuntimeError("unexpected synthetic padding")
                buffer = buffer[padding:]
                padding_remaining -= padding
                while (match := re.search(rb"\x1b\[<\d+;\d+;\d+[Mm]", buffer)):
                    reports += 1
                    print("MOUSE_AFTER_EXIT", flush=True)
                    buffer = buffer[match.end():]
                if b"POST_EXIT\r" in buffer:
                    print("KEYBOARD_AFTER_EXIT", flush=True)
                    buffer = b""
            print(f"DONE reports_after_exit={reports}", flush=True)
            channel.send_exit_status(0)
            channel.close()


if __name__ == "__main__":
    main()
