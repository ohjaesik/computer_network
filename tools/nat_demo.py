#!/usr/bin/env python3
"""Assignment 8 endpoint tools. Run on Host A/B, not on the NAT PC's raw NICs.

Only Python's standard library is needed. The tool does not configure routes,
interfaces, firewalls or NAT. Each server prints the peer IP/port it actually sees.
"""
import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import socket
import socketserver
import struct
import time

MAX_TCP_MESSAGE = 1024 * 1024
MAX_UDP_MESSAGE = 1400  # fits the assignment's unfragmented 1500-byte IPv4 MTU


def receive_exact(sock, count):
    """TCP is a byte stream: recv() may return only part of a length or payload."""
    chunks = bytearray()
    while len(chunks) < count:
        part = sock.recv(count - len(chunks))
        if not part:
            raise EOFError("TCP peer closed before the complete message arrived")
        chunks.extend(part)
    return bytes(chunks)


class TcpEcho(socketserver.BaseRequestHandler):
    def handle(self):
        self.request.settimeout(15)
        print(f"TCP peer={self.client_address[0]}:{self.client_address[1]}", flush=True)
        try:
            while True:
                header = receive_exact(self.request, 4)
                length = struct.unpack("!I", header)[0]
                if not 1 <= length <= MAX_TCP_MESSAGE:
                    return
                data = receive_exact(self.request, length)
                self.request.sendall(header + data)
                print(f"TCP echoed {length} bytes", flush=True)
        except (EOFError, OSError):
            pass


class UdpEcho(socketserver.BaseRequestHandler):
    def handle(self):
        data, sock = self.request
        if len(data) > MAX_UDP_MESSAGE:
            return
        print(f"UDP peer={self.client_address[0]}:{self.client_address[1]} bytes={len(data)}", flush=True)
        sock.sendto(data, self.client_address)


class HttpDemo(BaseHTTPRequestHandler):
    # Fixed diagnostic response: this server never exposes local files.
    def do_GET(self):
        body = f"NAT HTTP demo OK\npeer={self.client_address[0]}:{self.client_address[1]}\n".encode()
        self.send_response(200)
        self.send_header("Content-Type", "text/plain; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Connection", "close")
        self.end_headers()
        self.wfile.write(body)


class TcpServer(socketserver.ThreadingTCPServer):
    allow_reuse_address = True
    daemon_threads = True


class UdpServer(socketserver.UDPServer):
    allow_reuse_address = True


def make_server(protocol, bind, port):
    if protocol == "tcp":
        return TcpServer((bind, port), TcpEcho)
    if protocol == "udp":
        return UdpServer((bind, port), UdpEcho)
    return ThreadingHTTPServer((bind, port), HttpDemo)


def run_client(protocol, host, port, *, bind="", source_port=0, count=3, size=257, timeout=5):
    if count < 1 or not 1 <= size <= (MAX_TCP_MESSAGE if protocol == "tcp" else MAX_UDP_MESSAGE):
        raise ValueError("Invalid count/size; UDP must be 1..1400 bytes, TCP 1..1048576 bytes")
    kind = socket.SOCK_STREAM if protocol == "tcp" else socket.SOCK_DGRAM
    with socket.socket(socket.AF_INET, kind) as sock:
        sock.settimeout(timeout)
        if bind or source_port:
            sock.bind((bind or "0.0.0.0", source_port))
        # UDP connect selects the intended peer and rejects responses from a different endpoint.
        sock.connect((host, port))
        print(f"{protocol.upper()} local={sock.getsockname()} target={sock.getpeername()}", flush=True)
        for i in range(count):
            data = bytes((j + i) % 251 for j in range(size))
            started = time.monotonic()
            if protocol == "tcp":
                sock.sendall(struct.pack("!I", len(data)) + data)
                length = struct.unpack("!I", receive_exact(sock, 4))[0]
                if length != len(data):
                    raise RuntimeError(f"Unexpected TCP reply length {length}")
                received = receive_exact(sock, length)
            else:
                sock.send(data)
                received = sock.recv(MAX_UDP_MESSAGE + 1)
            if received != data:
                raise RuntimeError(f"Payload mismatch on exchange {i + 1}")
            elapsed = (time.monotonic() - started) * 1000
            print(f"PASS {i + 1}/{count}: {size} identical bytes, {elapsed:.2f} ms", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    server = commands.add_parser("server", help="TCP/UDP echo or fixed HTTP response server")
    server.add_argument("--protocol", choices=("tcp", "udp", "http"), required=True)
    server.add_argument("--bind", default="0.0.0.0")
    server.add_argument("--port", required=True, type=int)
    client = commands.add_parser("client", help="verify TCP/UDP round trips; use curl.exe for HTTP")
    client.add_argument("--protocol", choices=("tcp", "udp"), required=True)
    client.add_argument("--host", required=True)
    client.add_argument("--port", required=True, type=int)
    client.add_argument("--bind", default="")
    client.add_argument("--source-port", type=int, default=0)
    client.add_argument("--count", type=int, default=3)
    client.add_argument("--size", type=int, default=257)
    client.add_argument("--timeout", type=float, default=5)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be 1..65535")
    try:
        if args.command == "server":
            with make_server(args.protocol, args.bind, args.port) as service:
                print(f"READY {args.protocol.upper()} {service.server_address}; stop with Ctrl+C", flush=True)
                service.serve_forever()
        else:
            if not 0 <= args.source_port <= 65535 or args.timeout <= 0:
                parser.error("Invalid --source-port or --timeout")
            run_client(args.protocol, args.host, args.port, bind=args.bind, source_port=args.source_port,
                       count=args.count, size=args.size, timeout=args.timeout)
    except KeyboardInterrupt:
        pass
    except (OSError, EOFError, RuntimeError, ValueError) as error:
        parser.exit(1, f"FAIL: {error}\n")


if __name__ == "__main__":
    main()
