import socket

HOST = "127.0.0.1"
PORT = 8080

# Create socket
s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
s.connect((HOST, PORT))

# Send HTTP request (plaintext)
request = (
    "GET /test HTTP/1.1\r\n"
    "Host: localhost\r\n"
    "Connection: close\r\n"
    "\r\n"
)

print("[Client] Sending request...")
s.sendall(request.encode())

# Receive response
response = b""
while True:
    chunk = s.recv(4096)
    if not chunk:
        break
    response += chunk

print("[Client] Received response:\n")
print(response.decode(errors="ignore"))

s.close()