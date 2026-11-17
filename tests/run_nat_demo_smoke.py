#!/usr/bin/env python3
"""Exercise endpoint demo tools on real loopback sockets; does not exercise NAT."""
import importlib.util
from pathlib import Path
import threading
import urllib.request

path = Path(__file__).resolve().parents[1] / "tools" / "nat_demo.py"
spec = importlib.util.spec_from_file_location("nat_demo", path)
demo = importlib.util.module_from_spec(spec)
spec.loader.exec_module(demo)

for protocol in ("tcp", "udp", "http"):
    with demo.make_server(protocol, "127.0.0.1", 0) as server:
        thread = threading.Thread(target=server.serve_forever)
        thread.start()
        port = server.server_address[1]
        try:
            if protocol == "http":
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/", timeout=5) as response:
                    assert response.status == 200 and b"NAT HTTP demo OK" in response.read()
            else:
                demo.run_client(protocol, "127.0.0.1", port, count=2, size=8193 if protocol == "tcp" else 1400)
        finally:
            server.shutdown()
            thread.join()
print("PASS: demo TCP framing/large payload, UDP echo and HTTP endpoints on loopback sockets (not a NAT test)")
