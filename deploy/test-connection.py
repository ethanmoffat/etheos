#!/usr/bin/python3

import socket
import sys
from time import sleep

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
        return True
    else:
        print("FAILED\n")
        return False

socket.setdefaulttimeout(10)

if len(sys.argv) <= 2:
    print("Usage: python test-connection.py <host> <max_attempts> <port1> [port2]..[portN]")

host = sys.argv[1]
max_attempts = sys.argv[2]
portNdx = 3

attempt = 1
while len(sys.argv) > portNdx:
    try:
        port = sys.argv[portNdx]
        print(f"Testing connection to {host}:{port}...")
        socketInstance = connect_to(host, port)

        if not check_socket_and_close(socketInstance):
            raise Exception()

        portNdx = portNdx + 1
        attempt = 1
    except:
        attempt = attempt + 1

        print(f"Connection to {host}:{port} failed.")
        if attempt > int(max_attempts):
            print("Maximum attempts reached. Failing.")
            exit(1)

        sleep_time = attempt * attempt
        print(f"Waiting {sleep_time} seconds...")
        sleep(sleep_time)
        print(f"Retrying...{attempt}/{max_attempts}")
