#!/usr/bin/env python3
"""Compile/run isolated LinkServer with dummy account callbacks on loopback."""
from pathlib import Path
import socket, struct, subprocess, tempfile, unittest
ROOT = Path(__file__).resolve().parents[2]
def text(value):
    value = value.encode() if isinstance(value, str) else value
    return bytes([len(value)]) + value
def frame(kind, payload=b''):
    return b'ALK1' + struct.pack('!BBH', kind, 0, len(payload)) + payload
def exact(sock, n):
    data = b''
    while len(data) < n:
        part = sock.recv(n-len(data))
        if not part: raise EOFError
        data += part
    return data
def read(sock):
    magic, kind, flags, size = struct.unpack('!4sBBH', exact(sock, 8))
    assert magic == b'ALK1'
    return kind, exact(sock, size)
class Transport(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        exe = str(Path(cls.tmp.name)/'server')
        subprocess.run(['g++','-std=c++17','-pthread','-Wall','-Wextra','-I',str(ROOT),
            str(ROOT/'apps/openmw-mp/LinkServer.cpp'), str(Path(__file__).with_name('server_fixture.cpp')),
            '-o',exe],check=True)
        with socket.socket() as reserve:
            reserve.bind(('127.0.0.1',0)); cls.port=reserve.getsockname()[1]
        cls.diagnostic=Path(cls.tmp.name)/'ChatServer.log'
        cls.process=subprocess.Popen([exe,str(cls.port),str(cls.diagnostic)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,text=True)
        assert cls.process.stdout.readline().strip() == 'ready'
    @classmethod
    def tearDownClass(cls):
        cls.process.communicate('stop\n',timeout=3)
        assert cls.process.returncode == 0
        cls.tmp.cleanup()
    def connect(self):
        s=socket.create_connection(('127.0.0.1',self.port),timeout=3)
        self.addCleanup(s.close)
        return s
    def login(self, s, proof=b'K'*32, name='Alice'):
        s.sendall(frame(1,struct.pack('!HB',2,0)+text('test')+text(name)))
        kind, payload = read(s)
        self.assertEqual(kind, 2); self.assertEqual(len(payload), 18)
        s.sendall(frame(3,text(name)+b'\0'+text(proof)))
        return read(s)
    def test_01_single_client_not_blocked_by_next_accept(self):
        s=self.connect(); self.assertEqual(self.login(s)[0],4)
        self.assertEqual(read(s)[0],0x10)
        s.sendall(frame(0x40,b'ABCD'))
        self.assertEqual(read(s),(0x41,b'ABCD'))
    def test_02_arbitrary_32_bytes_no_longer_authenticate(self):
        s=self.connect(); self.assertEqual(self.login(s,b'X'*32)[0],5)
    def test_03_fragmented_hello_and_utf8_name(self):
        s=self.connect()
        data=frame(1,struct.pack('!HB',2,0)+text('test')+text('Длинное Русское Имя Игрока'))
        for value in data: s.sendall(bytes([value]))
        self.assertEqual(read(s)[0],2)
        s.sendall(frame(3,text('Длинное Русское Имя Игрока')+b'\0'+text(b'K'*32)))
        self.assertEqual(read(s)[0],4)
    def test_04_idle_connection_does_not_stall_second(self):
        idle=self.connect()
        s=self.connect(); self.assertEqual(self.login(s)[0],4)
    def test_05_chat_broadcast_and_history(self):
        a=self.connect(); b=self.connect()
        for s in (a,b): self.assertEqual(self.login(s)[0],4); read(s)
        msg='Привет "город"\nВторая строка'.encode()
        a.sendall(frame(0x14,struct.pack('!HH',1,len(msg))+msg))
        first=read(a); second=read(b)
        self.assertEqual(first,second); self.assertEqual(first[0],0x15)
        self.assertTrue(first[1].endswith(msg))
        b.sendall(frame(0x12,struct.pack('!HQH',1,0,50)))
        self.assertEqual(read(b)[0],0x13)
    def test_06_reauth_requires_new_connection(self):
        s=self.connect(); self.assertEqual(self.login(s)[0],4); read(s)
        s.sendall(frame(3,text('Alice')+b'\0'+text(b'K'*32)))
        self.assertEqual(s.recv(1),b'')
    def test_07_send_before_auth_is_closed(self):
        s=self.connect(); s.sendall(frame(0x14,b'')); self.assertEqual(s.recv(1),b'')
    def test_08_diagnostics_capture_failure_without_secrets(self):
        s=self.connect(); self.assertEqual(self.login(s,b'X'*32)[0],5)
        content=self.diagnostic.read_text()
        for required in ('LISTENING', 'RECV', 'FRAME', 'SENT', 'HELLO', 'CHALLENGE', 'AUTH_RECEIVED', 'AUTH_FAIL', 'PROOF_MISMATCH'):
            self.assertIn(required,content)
        for private in ('Alice', 'Длинное Русское', 'X'*32, 'K'*32, 'Привет'):
            self.assertNotIn(private,content)
if __name__=='__main__': unittest.main(verbosity=2)
