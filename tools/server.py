"""
Echo server for testing Rev.Client / Carvera.Interface bring-up.

Usage:
    python tools/server.py

Listens on localhost:9999. Echoes every received line back to the sender
and prints it to the console. Disconnect by closing the client connection,
or Ctrl-C to shut down the server.
"""

import socket
import threading

HOST = "127.0.0.1"
PORT = 9999


def handle_client(conn: socket.socket, addr):
    print(f"[server] Client connected: {addr}")
    try:
        with conn:
            buf = b""
            while True:
                chunk = conn.recv(4096)
                if not chunk:
                    break
                buf += chunk
                # Process all complete lines in the buffer.
                while b"\n" in buf:
                    line, buf = buf.split(b"\n", 1)
                    text = line.decode(errors="replace").strip()
                    print(f"[server] < {text}")
                    response = f"ok: {text}\n"
                    conn.sendall(response.encode())
                    print(f"[server] > {response.strip()}")
    except (ConnectionResetError, BrokenPipeError):
        pass
    print(f"[server] Client disconnected: {addr}")


def main():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as srv:
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind((HOST, PORT))
        srv.listen()
        print(f"[server] Listening on {HOST}:{PORT}")
        try:
            while True:
                conn, addr = srv.accept()
                t = threading.Thread(target=handle_client, args=(conn, addr), daemon=True)
                t.start()
        except KeyboardInterrupt:
            print("\n[server] Shutting down.")


if __name__ == "__main__":
    main()
