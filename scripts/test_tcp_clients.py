import socket
import threading

HOST = "127.0.0.1"
PORT = 9000
CLIENTS = 100

def run_client(index):
    try:
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.connect((HOST, PORT))

        message = f"client-{index}".encode()
        sock.sendall(message)

        data = sock.recv(1024)

        if data == message:
            print(f"[OK] client-{index}")
        else:
            print(f"[FAIL] client-{index}: {data!r}")

        sock.close()

    except Exception as e:
        print(f"[ERROR] client-{index}: {e}")

threads = []

for i in range(CLIENTS):
    thread = threading.Thread(target=run_client, args=(i,))
    thread.start()
    threads.append(thread)

for thread in threads:
    thread.join()

print("Test finished")
