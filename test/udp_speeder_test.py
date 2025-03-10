import socket
import subprocess
import sys
import threading
import signal
import time
import argparse

LOG_LEVEL_MAP = {
    "never": 0,
    "fatal": 1,
    "error": 2,
    "warn": 3,
    "info": 4,
    "debug": 5,
    "trace": 6
}

class UDPspeederTest:
    def __init__(self, speederv2_path, log_level="info", socket_activate=False, extra_udpspeeder_args="", no_udpspeeder=False):
        self.speederv2_path = speederv2_path
        self.log_level = log_level
        self.socket_activate = socket_activate
        self.extra_udpspeeder_args = extra_udpspeeder_args
        self.no_udpspeeder = no_udpspeeder

    def run_speederv2(self, mode, local_port, remote_ip, remote_port):
        log_level_num = LOG_LEVEL_MAP.get(self.log_level, 4)  # Default to "info" if log_level is not found
        cmd = f"{self.speederv2_path} {mode} -l0.0.0.0:{local_port} -r{remote_ip}:{remote_port} -f20:10 --log-level {log_level_num} {self.extra_udpspeeder_args}"
        if self.socket_activate:
            cmd = f"systemd-socket-activate -l0.0.0.0:{local_port} -d {cmd}"
        print(f"UPDspeeder command: {cmd}")
        
        process = subprocess.Popen(cmd, shell=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        return process

    def start_udpspeeder_server(self, local_port, remote_port):
        return self.run_speederv2("-s", local_port, "127.0.0.1", remote_port)

    def start_udpspeeder_client(self, local_port, remote_ip, remote_port):
        return self.run_speederv2("-c", local_port, remote_ip, remote_port)

    def server(self, udp_speeder_port, server_port):
        # Start UDPspeeder server if not disabled
        if not self.no_udpspeeder:
            udpspeeder_process = self.start_udpspeeder_server(udp_speeder_port, server_port)
            threading.Thread(target=self.log_udpspeeder_output, args=(udpspeeder_process,)).start()

        # Start UDP server
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        sock.bind(("0.0.0.0", server_port))
        print(f"Server listening on port {server_port}")

        def cleanup(signum, frame):
            print("Shutting down server...")
            sock.close()
            if not self.no_udpspeeder:
                udpspeeder_process.terminate()
            sys.exit(0)

        signal.signal(signal.SIGTERM, cleanup)

        while True:
            data, addr = sock.recvfrom(1024)
            print(f"Received message: {data.decode()} from {addr}")
            reply = f"Server reply to {data.decode()}"
            sock.sendto(reply.encode(), addr)
            print(f"Sent reply: {reply} ==============================================================")

    def client(self, us_client_port, us_server_port):
        # Start UDPspeeder client if not disabled
        if not self.no_udpspeeder:
            udpspeeder_process = self.start_udpspeeder_client(us_client_port, "127.0.0.1", us_server_port)
            threading.Thread(target=self.log_udpspeeder_output, args=(udpspeeder_process,)).start()

        # Start UDP client
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        server_address = ("127.0.0.1", us_client_port)
        message = "Hello, Server!"

        def cleanup(signum, frame):
            print("Shutting down client...")
            sock.close()
            if not self.no_udpspeeder:
                udpspeeder_process.terminate()
            sys.exit(0)

        signal.signal(signal.SIGTERM, cleanup)
        sock.settimeout(1.0)

        while True:
            print(f"Sending message: {message}")
            sent = sock.sendto(message.encode(), server_address)

            try:
                data, server = sock.recvfrom(1024)
            except socket.timeout:
                print("Request timed out")
                continue

            print(f"Received reply: {data.decode()} ==============================================================")
            time.sleep(1)

        sock.close()

    def log_udpspeeder_output(self, process):
        for line in process.stdout:
            print(line.decode(), end='')

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="UDPspeeder test script")
    parser.add_argument("--mode", choices=["server", "client"], help="Mode to run: 'server' or 'client'")
    parser.add_argument("--us-server-port", type=int, default=4096, help="UDPspeeder server port")
    parser.add_argument("--us-client-port", type=int, default=3333, help="UDPspeeder client port")
    parser.add_argument("--server-port", type=int, default=7777, help="Server port behind UDPspeeder")
    parser.add_argument("--log-level", choices=LOG_LEVEL_MAP.keys(), default="info", nargs="?", help="Log level: 'never', 'fatal', 'error', 'warn', 'info', 'debug', 'trace'")
    parser.add_argument("--socket-activate", action="store_true", help="Test systemd socket activation")
    parser.add_argument("--extra-args", help="Extra arguments to pass to UDPspeeder")
    parser.add_argument("--no-udpspeeder", action="store_true", help="Do not start UDPspeeder")
    parser.add_argument("--speederv2-path", default="speederv2", help="Path to the speederv2 binary")


    args = parser.parse_args()

    udpspeeder_test = UDPspeederTest(args.speederv2_path, args.log_level, args.socket_activate, args.extra_args, args.no_udpspeeder)

    if args.mode == "server":
        udpspeeder_test.server(args.us_server_port, args.server_port)
    elif args.mode == "client":
        udpspeeder_test.client(args.us_client_port, args.us_server_port)
    else:
        print("Invalid mode. Use 'server' or 'client'.")
        sys.exit(1)