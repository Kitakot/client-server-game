import argparse
import random
import selectors
import socket
import time

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mode', choices=['pass', 'tamper', 'replay', 'loss'], default='pass')
    parser.add_argument('--loss', type=float, default=0.05)
    parser.add_argument('--seed', type=int, default=6)
    args = parser.parse_args()
    if not 0 <= args.loss <= 1: parser.error('--loss must be between 0 and 1')
    rng = random.Random(args.seed)
    target = ('127.0.0.1', 54000)
    listener = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    listener.bind(('127.0.0.1', 54010)); listener.setblocking(False)
    selector = selectors.DefaultSelector(); selector.register(listener, selectors.EVENT_READ, None)
    peers = {}
    changed = set()
    def forward(sock, data, destination, label, key):
        if args.mode == 'loss' and rng.random() < args.loss:
            print('DROP', label, len(data)); return
        application = len(data) >= 45 and data[:5] == b'SG\x01\x0b\x03'
        if label == 'c2s' and application and key not in changed:
            if args.mode == 'tamper':
                altered = bytearray(data); altered[29] ^= 1; data = bytes(altered)
                changed.add(key); print('TAMPER first ciphertext byte', key)
            elif args.mode == 'replay':
                sock.sendto(data, destination); changed.add(key); print('REPLAY exact datagram', key)
        sock.sendto(data, destination)
    print('Local probe: 127.0.0.1:54010 -> 127.0.0.1:54000; Ctrl+C to stop')
    try:
        while True:
            for event, _ in selector.select(timeout=1):
                if event.fileobj is listener:
                    data, client = listener.recvfrom(65536)
                    if client not in peers:
                        if len(peers) >= 16: continue
                        upstream = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
                        upstream.bind(('127.0.0.1', 0)); upstream.setblocking(False)
                        peers[client] = upstream
                        selector.register(upstream, selectors.EVENT_READ, client)
                    forward(peers[client], data, target, 'c2s', client)
                else:
                    data, source = event.fileobj.recvfrom(65536)
                    if source == target:
                        forward(listener, data, event.data, 's2c', event.data)
    except KeyboardInterrupt:
        pass
    finally:
        selector.close(); listener.close()
        for sock in peers.values(): sock.close()

if __name__ == '__main__':
    main()
