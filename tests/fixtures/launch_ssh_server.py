"""Loopback-only launch acceptance fixture. Never executes received shell input."""
import argparse
import hashlib
import socket
import subprocess
import tempfile
import threading
from pathlib import Path
import paramiko


class Server(paramiko.ServerInterface):
    def __init__(self, key, method):
        self.key = key
        self.method = method
        self.shell = threading.Event()

    def get_allowed_auths(self, username):
        return "password" if self.method == "password" else "publickey"

    def check_auth_password(self, username, password):
        if self.method == "password" and username == "fixture" and password == "fixture-only":
            print("AUTH_OK", flush=True)
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def check_auth_publickey(self, username, key):
        print("PUBLIC_KEY_TYPE " + (key.public_blob.key_type if key.public_blob else key.get_name()), flush=True)
        certificate_matches = (
            self.method == "certificate" and key.public_blob is not None
            and key.public_blob == self.key.public_blob
        )
        if username == "fixture" and key == self.key and (self.method == "private-key" or certificate_matches):
            print("AUTH_OK", flush=True)
            return paramiko.AUTH_SUCCESSFUL
        return paramiko.AUTH_FAILED

    def check_channel_request(self, kind, chanid):
        return paramiko.OPEN_SUCCEEDED if kind == "session" else paramiko.OPEN_FAILED_ADMINISTRATIVELY_PROHIBITED

    def check_channel_pty_request(self, channel, term, width, height, pixelwidth, pixelheight, modes):
        return True

    def check_channel_shell_request(self, channel):
        self.shell.set()
        return True


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--method", choices=["password", "private-key", "certificate"], required=True)
    parser.add_argument("--key-file", required=True)
    args = parser.parse_args()
    key = paramiko.RSAKey.generate(2048)
    key.write_private_key_file(args.key_file, password="fixture-only")
    if args.method == "certificate":
        # Synthetic CA/private material lives only in an automatically removed
        # temp directory. This fixture accepts exactly its minted certificate;
        # Paramiko verifies the client's proof of possession, not CA policy.
        public_path = Path(args.key_file + ".pub")
        certificate_path = Path(args.key_file + "-cert.pub")
        try:
            public_path.write_text(key.get_name() + " " + key.get_base64(), encoding="utf-8")
            with tempfile.TemporaryDirectory(prefix="ztermy-launch-ca-") as directory:
                ca = str(Path(directory) / "ca")
                subprocess.run(["ssh-keygen.exe", "-q", "-t", "rsa", "-b", "2048", "-N", "", "-f", ca],
                               check=True, capture_output=True)
                subprocess.run(["ssh-keygen.exe", "-q", "-s", ca, "-I", "launch-fixture", "-n", "fixture",
                                "-V", "-1m:+5m", str(public_path)], check=True, capture_output=True)
                key.load_certificate(str(certificate_path))
        finally:
            public_path.unlink(missing_ok=True)
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        listener.listen(1)
        listener.settimeout(20)
        print(f"PORT {listener.getsockname()[1]}", flush=True)
        connection, _ = listener.accept()
        with paramiko.Transport(connection) as transport:
            if args.method == "certificate":
                transport._preferred_pubkeys = ("rsa-sha2-512-cert-v01@openssh.com",
                                                "rsa-sha2-256-cert-v01@openssh.com",
                                                "ssh-rsa-cert-v01@openssh.com", "rsa-sha2-512", "rsa-sha2-256",
                                                "ssh-rsa")
            transport.add_server_key(key)
            server = Server(key, args.method)
            transport.start_server(server=server)
            channel = transport.accept(15)
            if channel is None or not server.shell.wait(10):
                raise RuntimeError("SSH shell was not established")
            channel.settimeout(15)
            channel.sendall(b"LAUNCH_FIXTURE_READY\r\n")
            received = b""
            while not received.endswith(b"\r"):
                block = channel.recv(4096)
                if not block:
                    raise RuntimeError("No startup directory request")
                received += block
            # Report only a digest, not terminal input.
            print("STARTUP_SHA256 " + hashlib.sha256(received).hexdigest(), flush=True)
            channel.sendall(b"LAUNCH_DIRECTORY_RECEIVED\r\n")
            while channel.recv(4096):
                pass


if __name__ == "__main__":
    main()
