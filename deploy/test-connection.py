#!/usr/bin/python3

import socket
import sys

def connect_to(host, port):
    try:
        return socket.create_connection((host, port))
    except:
        print(f"Socket connection to {host}:{port} failed!\n")
        return None

def check_socket_and_close(socketInstance):
    if (socketInstance):
        print("OK\n")
        socketInstance.close()
    else:
        print("FAILED\n")
        exit(1)

socket.setdefaulttimeout(10)

if len(sys.argv) <= 2:
    print("Usage: python test-connection.py <host> <port1> [port2]..[portN]")

host = sys.argv[1]
portNdx = 2

while len(sys.argv) > portNdx:
    port = sys.argv[portNdx]
    print(f"Testing connection to {host}:{port}...")
    socketInstance = connect_to(host, port)
    check_socket_and_close(socketInstance)
    portNdx = portNdx + 1
