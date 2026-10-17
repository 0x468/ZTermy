"""Loopback terminal identity fixture: no shell execution or filesystem access."""

import socket
import sys
import threading
import time

sys.dont_write_bytecode = True

import paramiko

from synchronized_ssh_server import Server


class IdentityServer(Server):
    def check_channel_env_request(self, channel, name, value):
        identity = name in (b"TERM_PROGRAM", b"TERM_PROGRAM_VERSION", b"COLORTERM")
        allowed = sys.argv[1] == "accept" or (sys.argv[1] == "reject-defaults" and not identity)
        # Only controlled fixture names/values can arrive here.
        print("ENV " + name.decode("ascii") + "=" + value.decode("ascii")
              + (" ACCEPT" if allowed else " REJECT"), flush=True)
        return allowed


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
        with paramiko.Transport(connection) as transport:
            transport.add_server_key(key)
            server = IdentityServer()
            transport.start_server(server=server)
            channel = transport.accept(10)
            if channel is None:
                raise RuntimeError("fixture session channel missing")
            deadline = time.monotonic() + 10
            while not server.shell.is_set():
                if channel.closed or finish.is_set() or not transport.is_active():
                    print("NO_SHELL", flush=True)
                    return
                if time.monotonic() > deadline:
                    raise RuntimeError("fixture shell request missing")
                time.sleep(0.01)
            print("SHELL", flush=True)
            channel.settimeout(5)
            query = (b"\x1b[>q\x1b[c\x1b[16t"
                     b"\x1b_Gi=7331,a=q,t=d,f=32,s=1,v=1;AAAA/w==\x1b\\")
            channel.sendall(query)
            response = b""
            while not (b"\x1bP>|ztermy(" in response
                       and b"\x1b[?62;4;22c" in response
                       and b"\x1b[6;16;8t" in response
                       and b"\x1b_Gi=7331;OK\x1b\\" in response):
                packet = channel.recv(4096)
                if not packet or len(response) > 16384:
                    raise RuntimeError("fixture identity response missing")
                response += packet
            print("REPLIES " + response.hex(), flush=True)
            channel.sendall(b"IDENTITY_FIXTURE_READY\r\n")
            if not finish.wait(10):
                raise RuntimeError("fixture completion missing")
            channel.send_exit_status(0)
            channel.close()


if __name__ == "__main__":
    main()
