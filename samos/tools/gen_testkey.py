#!/usr/bin/env python3
"""Генерирует фиксированную тестовую пару RSA для локального MTProto-сервера
и C-файл с её публичным ключом для тестовой сборки SamOS."""
import os, hashlib, struct
from cryptography.hazmat.primitives.asymmetric import rsa
from cryptography.hazmat.primitives import serialization

HERE = os.path.dirname(os.path.abspath(__file__))
PEM = os.path.join(HERE, "test_server_key.pem")

def ser_bytes(b):
    L = len(b)
    out = bytes([L]) + b if L < 254 else bytes([254]) + struct.pack('<I', L)[:3] + b
    while len(out) % 4: out += b'\x00'
    return out

def fingerprint(n, e):
    nb = n.to_bytes((n.bit_length()+7)//8, 'big')
    eb = e.to_bytes((e.bit_length()+7)//8, 'big')
    d = hashlib.sha1(ser_bytes(nb) + ser_bytes(eb)).digest()[-8:]
    return struct.unpack('<q', d)[0] & 0xFFFFFFFFFFFFFFFF

if os.path.exists(PEM):
    key = serialization.load_pem_private_key(open(PEM, 'rb').read(), password=None)
else:
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    open(PEM, 'wb').write(key.private_bytes(serialization.Encoding.PEM,
        serialization.PrivateFormat.TraditionalOpenSSL, serialization.NoEncryption()))

pub = key.public_key().public_numbers()
n, e = pub.n, pub.e
fp = fingerprint(n, e)
nb = n.to_bytes(256, 'big')
out = ['// Тестовый RSA-ключ для локального MTProto-сервера (только для mtprototest).',
       '#ifdef MTPROTO_TEST',
       '#include "crypto.h"', '#include "mtproto.h"', '',
       'const TGKey tg_test_keys[1] = {',
       '  { 0x%016xULL, {%s} },' % (fp, ','.join('0x%02x' % x for x in nb)),
       '};', 'const int tg_test_keys_count = 1;', '#endif']
open(os.path.join(os.path.dirname(HERE), 'mtproto', 'mtproto_testkey.c'), 'w').write('\n'.join(out)+'\n')
print("fingerprint = 0x%016x" % fp)
