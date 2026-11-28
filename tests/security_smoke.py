import argparse
import ctypes as C
import os
from pathlib import Path

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--dll', type=Path, required=True)
    args = parser.parse_args()
    cookie = os.add_dll_directory(str(args.dll.resolve().parent)) if os.name == 'nt' else None
    lib = C.CDLL(str(args.dll.resolve()))
    ptr = C.POINTER(C.c_ubyte)
    lib.sg_create.argtypes = [C.c_int, ptr]; lib.sg_create.restype = C.c_void_p
    lib.sg_destroy.argtypes = [C.c_void_p]; lib.sg_destroy.restype = None
    lib.sg_keygen.argtypes = [ptr, ptr]; lib.sg_keygen.restype = C.c_int
    lib.sg_ready.argtypes = [C.c_void_p]; lib.sg_ready.restype = C.c_int
    lib.sg_retry.argtypes = [C.c_void_p, ptr, C.c_int]; lib.sg_retry.restype = C.c_int
    lib.sg_seal.argtypes = [C.c_void_p, ptr, C.c_int, ptr, C.c_int]; lib.sg_seal.restype = C.c_int
    lib.sg_input.argtypes = [C.c_void_p, ptr, C.c_int, ptr, C.c_int, C.POINTER(C.c_int)]
    lib.sg_input.restype = C.c_int
    contexts = []
    def check(condition, text):
        if not condition: raise AssertionError(text)
    def array(b): return (C.c_ubyte * len(b)).from_buffer_copy(b)
    def create(server, key):
        h = lib.sg_create(server, array(key)); check(h, 'create')
        contexts.append(h); return h
    def keys():
        a, b = (C.c_ubyte * 32)(), (C.c_ubyte * 32)()
        check(lib.sg_keygen(a, b) == 1, 'keygen'); return bytes(a), bytes(b)
    def retry(h):
        out = (C.c_ubyte * 1069)(); n = lib.sg_retry(h, out, len(out))
        check(n > 0, 'retry'); return bytes(out[:n])
    def feed(h, b):
        out = (C.c_ubyte * 1069)(); action = C.c_int()
        n = lib.sg_input(h, array(b), len(b), out, len(out), C.byref(action))
        return n, action.value, bytes(out[:max(0, n)])
    def seal(h, b):
        out = (C.c_ubyte * 1069)()
        n = lib.sg_seal(h, array(b), len(b), out, len(out))
        check(n > 0, 'seal'); return bytes(out[:n])
    def ready_pair(private, public):
        client, server = create(0, public), create(1, private)
        hello = retry(client)
        n, action, answer = feed(server, hello)
        check(n == 149 and action == 1, 'server hello')
        check(feed(server, hello)[2] == answer, 'duplicate hello must be stable')
        n, action, finished = feed(client, answer)
        check(n > 0 and action == 1 and not lib.sg_ready(client), 'wait server finished')
        n, action, final = feed(server, finished)
        check(n > 0 and action == 1 and lib.sg_ready(server), 'client finished')
        # Имитируем потерю ServerFinished и повторяем ClientFinished с новым counter.
        again = retry(client); check(again != finished, 'fresh nonce for retry')
        n, action, final = feed(server, again)
        check(n > 0 and action == 1, 'lost finished recovery')
        feed(client, final); check(lib.sg_ready(client), 'client ready')
        return client, server
    try:
        private, public = keys()
        client, server = ready_pair(private, public)
        message = b'GAME-UDP-PR6-TEST'
        encrypted = seal(client, message)
        check(message not in encrypted and len(encrypted) == len(message) + 45, 'ciphertext/overhead')
        check(feed(server, encrypted)[1:] == (2, message), 'round trip c2s')
        check(feed(server, encrypted)[0] < 0, 'replay rejected')
        check(feed(client, seal(server, message))[1:] == (2, message), 'round trip s2c')
        record = seal(client, message)
        for offset in (4, 5, 21, 29, len(record)-1):
            altered = bytearray(record); altered[offset] ^= 1
            check(feed(server, altered)[0] < 0, 'tamper rejected at ' + str(offset))
        # Поддельный большой counter не должен сдвинуть окно.
        check(feed(server, record)[1:] == (2, message), 'window unchanged after invalid tags')
        first, second = seal(client, b'first'), seal(client, b'second')
        check(feed(server, second)[2] == b'second', 'newer first')
        check(feed(server, first)[2] == b'first', 'reorder inside window')
        old = seal(client, b'old')
        for i in range(65): feed(server, seal(client, b'advance'))
        check(feed(server, old)[0] < 0, 'old outside window')
        maximum = bytes(range(256)) * 4
        check(feed(server, seal(client, maximum))[2] == maximum, '1024 byte payload')
        check(feed(server, b'plaintext')[0] < 0, 'plaintext rejected')
        check(feed(server, encrypted[:-1])[0] < 0, 'truncated packet')
        other_client, other_server = ready_pair(private, public)
        check(feed(other_server, seal(client, message))[0] < 0, 'session isolation')
        check(feed(other_server, seal(other_client, message))[2] == message, 'second session')
        _, wrong_public = keys()
        bad_client, real_server = create(0, wrong_public), create(1, private)
        answer = feed(real_server, retry(bad_client))[2]
        check(feed(bad_client, answer)[0] < 0 and not lib.sg_ready(bad_client), 'wrong pin')
        print('PASS: handshake, retries, AES-GCM, replay, tamper, ordering, size, sessions, pin')
    finally:
        for ctx in contexts: lib.sg_destroy(ctx)
        if cookie: cookie.close()

if __name__ == '__main__':
    main()
