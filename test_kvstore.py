import socket
import time

def send_cmd(s, cmd):
    print(f"> {cmd.strip()}")
    s.sendall(cmd.encode('utf-8'))
    resp = s.recv(4096).decode('utf-8')
    print(f"< {resp.strip()}")

def main():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.connect(('127.0.0.1', 6379))

    send_cmd(s, "SET user:1 '{\"name\":\"Alice\"}' EX 2\n")
    send_cmd(s, "GET user:1\n")
    send_cmd(s, "TTL user:1\n")
    send_cmd(s, "SET counter 0\n")
    send_cmd(s, "KEYS user:*\n")
    send_cmd(s, "DEL user:1\n")
    send_cmd(s, "GET user:1\n")
    send_cmd(s, "INCR counter\n")
    send_cmd(s, "INCR counter\n")
    send_cmd(s, "GET counter\n")
    
    print("Waiting 2.5 seconds for expiration test...")
    send_cmd(s, "SET exp_key value EX 2\n")
    time.sleep(2.5)
    send_cmd(s, "GET exp_key\n")

    send_cmd(s, "SAVE\n")
    send_cmd(s, "STATS\n")

    s.close()

if __name__ == '__main__':
    main()
