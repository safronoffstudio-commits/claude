#!/usr/bin/env python3
"""Независимый MTProto-сервер (сторона сервера рукопожатия создания auth_key).
Служит для проверки реализации MTProto в SamOS без доступа к серверам Telegram.
Реализует: resPQ, req_DH_params (RSA_PAD + AES-IGE), set_client_DH_params, dh_gen_ok."""
import os, socket, struct, hashlib, sys
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
from cryptography.hazmat.primitives import serialization

HERE = os.path.dirname(os.path.abspath(__file__))
key = serialization.load_pem_private_key(open(os.path.join(HERE,"test_server_key.pem"),'rb').read(), password=None)
priv = key.private_numbers()
pub = key.public_key().public_numbers()
N, E, D = pub.n, pub.e, priv.d

def ser_bytes(b):
    L=len(b); out=bytes([L])+b if L<254 else bytes([254])+struct.pack('<I',L)[:3]+b
    while len(out)%4: out+=b'\x00'
    return out
def fp():
    nb=N.to_bytes(256,'big'); eb=E.to_bytes(3,'big')
    return struct.unpack('<q', hashlib.sha1(ser_bytes(nb)+ser_bytes(eb)).digest()[-8:])[0] & 0xFFFFFFFFFFFFFFFF
FP=fp()

# AES-256-IGE поверх ECB
def aes_ecb(key32):
    return Cipher(algorithms.AES(key32), modes.ECB()).encryptor(), Cipher(algorithms.AES(key32), modes.ECB()).decryptor()
def ige_decrypt(data, key32, iv):
    dec=Cipher(algorithms.AES(key32), modes.ECB()).decryptor()
    x_prev=iv[16:32]; y_prev=iv[0:16]; out=b''
    for i in range(0,len(data),16):
        c=data[i:i+16]
        t=bytes(a^b for a,b in zip(c,x_prev))
        m=dec.update(t)
        m=bytes(a^b for a,b in zip(m,y_prev))
        out+=m; x_prev=m; y_prev=c
    return out
def ige_encrypt(data, key32, iv):
    enc=Cipher(algorithms.AES(key32), modes.ECB()).encryptor()
    x_prev=iv[16:32]; y_prev=iv[0:16]; out=b''
    for i in range(0,len(data),16):
        m=data[i:i+16]
        t=bytes(a^b for a,b in zip(m,y_prev))
        c=enc.update(t)
        c=bytes(a^b for a,b in zip(c,x_prev))
        out+=c; y_prev=c; x_prev=m
    return out

# RFC3526 2048-bit MODP, g=2
DH_P=int("FFFFFFFFFFFFFFFFC90FDAA22168C234C4C6628B80DC1CD129024E088A67CC74"
 "020BBEA63B139B22514A08798E3404DDEF9519B3CD3A431B302B0A6DF25F14374FE1356D6D51C245"
 "E485B576625E7EC6F44C42E9A637ED6B0BFF5CB6F406B7EDEE386BFB5A899FA5AE9F24117C4B1FE6"
 "49286651ECE45B3DC2007CB8A163BF0598DA48361C55D39A69163FA8FD24CF5F83655D23DCA3AD96"
 "1C62F356208552BB9ED529077096966D670C354E4ABC9804F1746C08CA18217C32905E462E36CE3B"
 "E39E772C180E86039B2783A2EC07A28FB5C55DF06F4C52C9DE2BCBF6955817183995497CEA956AE5"
 "15D2261898FA051015728E5A8AACAA68FFFFFFFFFFFFFFFF",16)
DH_G=2

def read_frame(c):
    b=recvn(c,1)
    if b==b'\xef':           # abridged init
        c.mode='abridged'; return read_frame(c)
    if b==b'\xee':           # intermediate init (0xeeeeeeee)
        recvn(c,3); c.mode='intermediate'; return read_frame(c)
    if getattr(c,'mode',None)=='abridged' or b[0]!=0xee:
        l=b[0]
        if l==0x7f: l=struct.unpack('<I',recvn(c,3)+b'\x00')[0]
        if not hasattr(c,'mode'): c.mode='abridged'
        return recvn(c, l*4)
    # intermediate
    length=struct.unpack('<I', b+recvn(c,3))[0]
    return recvn(c,length)

def send_frame(c, data):
    if c.mode=='abridged':
        l=len(data)//4
        c.sendall((bytes([l]) if l<0x7f else b'\x7f'+struct.pack('<I',l)[:3])+data)
    else:
        c.sendall(struct.pack('<I',len(data))+data)

def recvn(c,n):
    d=b''
    while len(d)<n:
        x=c.recv(n-len(d))
        if not x: raise EOFError()
        d+=x
    return d

def plain(body):
    import time
    return struct.pack('<q',0)+struct.pack('<q',(int(time.time())<<32))+struct.pack('<I',len(body))+body
def unplain(msg):
    return msg[20:20+struct.unpack('<I',msg[16:20])[0]]

def primes_pq():
    import random
    def isp(n):
        if n<2: return False
        for p in [2,3,5,7,11,13,17,19,23,29,31]:
            if n%p==0: return n==p
        d=n-1; r=0
        while d%2==0: d//=2; r+=1
        for a in [2,3,5,7,11,13,17,19,23,29,31]:
            x=pow(a,d,n)
            if x in (1,n-1): continue
            for _ in range(r-1):
                x=x*x%n
                if x==n-1: break
            else: return False
        return True
    while True:
        p=random.getrandbits(31)|1
        while not isp(p): p+=2
        q=random.getrandbits(31)|1
        while not isp(q): q+=2
        if p>q: p,q=q,p
        if p!=q: return p,q,p*q

def handle(c):
    c.mode=None
    nonce=None; server_nonce=os.urandom(16); new_nonce=None
    p=q=pq=None; a=None; g_a=None
    while True:
        msg=read_frame(c)
        body=unplain(msg)
        cons=struct.unpack('<I',body[:4])[0]
        if cons==0xbe7e8ef1:          # req_pq_multi
            nonce=body[4:20]
            p,q,pq=primes_pq()
            out=struct.pack('<I',0x05162463)+nonce+server_nonce
            out+=ser_bytes(pq.to_bytes((pq.bit_length()+7)//8,'big'))
            out+=struct.pack('<I',0x1cb5c415)+struct.pack('<I',1)+struct.pack('<q',FP if FP<2**63 else FP-2**64)
            send_frame(c, plain(out))
        elif cons==0xd712e4be:        # req_DH_params
            off=4+16+16
            # p,q строки
            def rdstr(b,o):
                l=b[o]; o+=1
                if l==254: l=b[o]|b[o+1]<<8|b[o+2]<<16; o+=3
                v=b[o:o+l]; o+=l
                while o%4: o+=1
                return v,o
            _,off=rdstr(body,off); _,off=rdstr(body,off)
            off+=8   # fingerprint
            enc,off=rdstr(body,off)
            # RSA decrypt
            m=int.from_bytes(enc,'big')
            dec=pow(m,D,N).to_bytes(256,'big')
            temp_key_xor=dec[:32]; aes_enc=dec[32:]
            temp_key=bytes(x^y for x,y in zip(temp_key_xor, hashlib.sha256(aes_enc).digest()))
            data_with_hash=ige_decrypt(aes_enc, temp_key, b'\x00'*32)
            data_pad_rev=data_with_hash[:192]
            data_with_padding=data_pad_rev[::-1]
            inner=data_with_padding  # p_q_inner_data + padding, TL self-delimiting
            assert struct.unpack('<I',inner[:4])[0]==0x83c95aec, hex(struct.unpack('<I',inner[:4])[0])
            o=4
            _,o=rdstr(inner,o); _,o=rdstr(inner,o); _,o=rdstr(inner,o)
            n2=inner[o:o+16]; o+=16; sn=inner[o:o+16]; o+=16; new_nonce=inner[o:o+32]
            # server_DH_inner_data
            a=int.from_bytes(os.urandom(256),'big')
            g_a=pow(DH_G,a,DH_P)
            sdi=struct.pack('<I',0xb5890dba)+nonce+server_nonce+struct.pack('<I',DH_G)
            sdi+=ser_bytes(DH_P.to_bytes(256,'big'))+ser_bytes(g_a.to_bytes(256,'big'))
            import time; sdi+=struct.pack('<I',int(time.time()))
            answer=hashlib.sha1(sdi).digest()+sdi
            answer+=os.urandom((-len(answer))%16)
            tkey,tiv=derive(new_nonce,server_nonce)
            enc_ans=ige_encrypt(answer,tkey,tiv)
            out=struct.pack('<I',0xd0e8075c)+nonce+server_nonce+ser_bytes(enc_ans)
            send_frame(c, plain(out))
        elif cons==0xf5045f1f:        # set_client_DH_params
            off=4+16+16
            l=body[off]; off+=1
            if l==254: l=body[off]|body[off+1]<<8|body[off+2]<<16; off+=3
            enc=body[off:off+l]
            tkey,tiv=derive(new_nonce,server_nonce)
            dec=ige_decrypt(enc,tkey,tiv)
            ci=dec[20:]
            assert struct.unpack('<I',ci[:4])[0]==0x6643b654, hex(struct.unpack('<I',ci[:4])[0])
            o=4+16+16+8
            l=ci[o]; o+=1
            if l==254: l=ci[o]|ci[o+1]<<8|ci[o+2]<<16; o+=3
            g_b=int.from_bytes(ci[o:o+l],'big')
            auth_key=pow(g_b,a,DH_P).to_bytes(256,'big')
            akid=hashlib.sha1(auth_key).digest()[-8:]
            print("СЕРВЕР: auth_key_id =", akid.hex(), flush=True)
            aux=hashlib.sha1(auth_key).digest()[:8]
            nnh=hashlib.sha1(new_nonce+b'\x01'+aux).digest()[4:20]
            out=struct.pack('<I',0x3bcbf734)+nonce+server_nonce+nnh
            send_frame(c, plain(out))
            # перейти в зашифрованный режим
            salt=bytes(x^y for x,y in zip(new_nonce[:8], server_nonce[:8]))
            encrypted_loop(c, auth_key, salt)
            return

def mtp_kdf(auth_key, msg_key, x):
    a=hashlib.sha256(msg_key+auth_key[x:x+36]).digest()
    b=hashlib.sha256(auth_key[40+x:40+x+36]+msg_key).digest()
    key=a[:8]+b[8:24]+a[24:32]
    iv=b[:8]+a[8:24]+b[24:32]
    return key,iv

def encrypted_loop(c, auth_key, salt):
    akid=hashlib.sha1(auth_key).digest()[-8:]
    session_id=None
    import time
    while True:
        pkt=read_frame(c)
        assert pkt[:8]==akid, "auth_key_id не совпал"
        msg_key=pkt[8:24]
        key,iv=mtp_kdf(auth_key, msg_key, 0)   # x=0: клиент->сервер
        plain=ige_decrypt(pkt[24:], key, iv)
        full=hashlib.sha256(auth_key[88:88+32]+plain).digest()
        assert full[8:24]==msg_key, "msg_key не совпал"
        session_id=plain[8:16]
        msg_id=struct.unpack('<q',plain[16:24])[0]
        blen=struct.unpack('<I',plain[28:32])[0]
        body=plain[32:32+blen]
        cons=struct.unpack('<I',body[:4])[0]
        if cons==0x7abe77ec:   # ping
            ping_id=body[4:12]
            print("СЕРВЕР: получен ping, отвечаю pong", flush=True)
            resp=struct.pack('<I',0x347773c5)+struct.pack('<q',msg_id)+ping_id
            send_encrypted(c, auth_key, salt, session_id, resp, 0)
        else:
            print("СЕРВЕР: метод 0x%08x (не обрабатывается)" % cons, flush=True)
            return

def send_encrypted(c, auth_key, salt, session_id, body, seq):
    import time
    msg_id=(int(time.time())<<32)
    plain=salt+session_id+struct.pack('<q',msg_id)+struct.pack('<I',seq)+struct.pack('<I',len(body))+body
    plain+=os.urandom((-len(plain))%16 if (-len(plain))%16>=12 else (-len(plain))%16+16)
    msg_key=hashlib.sha256(auth_key[88+8:88+8+32]+plain).digest()[8:24]  # x=8: сервер->клиент
    key,iv=mtp_kdf(auth_key, msg_key, 8)
    enc=ige_encrypt(plain, key, iv)
    akid=hashlib.sha1(auth_key).digest()[-8:]
    send_frame(c, akid+msg_key+enc)

def derive(new_nonce, server_nonce):
    a=hashlib.sha1(new_nonce+server_nonce).digest()
    b=hashlib.sha1(server_nonce+new_nonce).digest()
    cc=hashlib.sha1(new_nonce+new_nonce).digest()
    key=a+b[:12]
    iv=b[12:20]+cc+new_nonce[:4]
    return key,iv

class Conn:
    def __init__(self, sock): self.s=sock; self.mode=None
    def sendall(self,d): self.s.sendall(d)
    def recv(self,n): return self.s.recv(n)
    def close(self): self.s.close()

def main():
    port=int(sys.argv[1]) if len(sys.argv)>1 else 7777
    s=socket.socket(); s.setsockopt(socket.SOL_SOCKET,socket.SO_REUSEADDR,1)
    s.bind(('0.0.0.0',port)); s.listen(5)
    print(f"MTProto-сервер на :{port}, отпечаток ключа 0x{FP:016x}", flush=True)
    while True:
        sock,_=s.accept()
        c=Conn(sock)
        try: handle(c)
        except Exception as e:
            import traceback; print("СЕРВЕР: ошибка:", repr(e), flush=True)
            traceback.print_exc()
        finally: c.close()

if __name__=="__main__": main()
