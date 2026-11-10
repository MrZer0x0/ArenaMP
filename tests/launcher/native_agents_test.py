"""Integration checks against compiled agents; loopback only, no public master.
Usage: python3 tests/launcher/native_agents_test.py stage/bin
"""
import hashlib
import hmac
import http.server
import json
import os
from pathlib import Path
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
import uuid

bindir = Path(sys.argv[1]).resolve()
suffix = '.exe' if os.name == 'nt' else ''
publisher = bindir / ('arena-server-agent' + suffix)
client = bindir / ('arena-relay-agent' + suffix)
assert publisher.is_file() and client.is_file(), 'Build both agents first'
posts = queue.Queue()
sid = str(uuid.uuid4())
key = os.urandom(32).hex()
tokens = {b'H': os.urandom(16), b'C': os.urandom(16)}
relay = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
relay.bind(('127.0.0.1', 0))
relay_port = relay.getsockname()[1]
host_session = dict(id=sid, key=key, token=tokens[b'H'].hex(), relay_port=relay_port)

class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_): pass
    def reply(self, data):
        body = json.dumps(data).encode()
        self.send_response(200)
        self.send_header('Content-Type', 'application/json')
        self.send_header('Content-Length', str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def do_POST(self):
        data = json.loads(self.rfile.read(int(self.headers['Content-Length'])))
        posts.put(data)
        self.reply(dict(ok=True, id='test'))
    def do_GET(self):
        self.reply(dict(sessions=[host_session]))

master = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Handler)
threading.Thread(target=master.serve_forever, daemon=True).start()

def forward():
    peers = {}
    while True:
        try: frame, address = relay.recvfrom(65535)
        except OSError: return
        if len(frame) < 37 or frame[:4] != b'AMR1': continue
        role = frame[4:5]
        if role not in tokens: continue
        expected = hmac.new(tokens[role], frame[:21] + frame[37:], hashlib.sha256).digest()[:16]
        if not hmac.compare_digest(expected, frame[21:37]): continue
        peers[role] = address
        other = peers.get(b'C' if role == b'H' else b'H')
        if other and len(frame) > 37:
            relay.sendto(b'AMR1D' + frame[5:21] + frame[37:], other)
threading.Thread(target=forward, daemon=True).start()
game = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
game.bind(('127.0.0.1', 0))
def echo():
    while True:
        try:
            data, address = game.recvfrom(65535)
            game.sendto(data, address)
        except OSError: return
threading.Thread(target=echo, daemon=True).start()
processes = []
def spawn(args):
    proc = subprocess.Popen([str(x) for x in args], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    processes.append(proc)
    return proc

def stop(proc):
    if proc.poll() is None:
        proc.terminate()
        try: proc.wait(timeout=3)
        except subprocess.TimeoutExpired: proc.kill(); proc.wait(timeout=3)

try:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        home = root / 'server'
        (home / 'data/custom').mkdir(parents=True)
        (home / 'data/custom/arenaMasterStatus.json').write_text(json.dumps(dict(timestamp=time.time(), players=0)))
        cfg = root / 'native.cfg'
        cfg.write_text('[General]\nhostname=Test\nmaximumPlayers=10\nport=%d\n' % game.getsockname()[1])
        owner = root / 'owner.json'
        owner.write_text(json.dumps(dict(server_id='test', owner_token='a'*64,
            server_home=str(home), native_config=str(cfg), master_url='http://127.0.0.1:%d' % master.server_port)))
        content = [{'Morrowind.esm': ['123', '456']}, {'Плагин.esp': []}]
        args = [publisher, '--config', owner, '--server-root', root,
                '--relay-host', '127.0.0.1', '--relay-port', str(relay_port)]
        for manifest in (content, dict(formatVersion=2, content=content, groundcover=[{'Grass.esp': ['789']}])):
            (home / 'data/requiredDataFiles.json').write_text('// comment\n'+json.dumps(manifest))
            proc = spawn(args + ['--no-relay'])
            payload = posts.get(timeout=8)
            assert [f['name'] for f in payload['files']] == ['Morrowind.esm', 'Плагин.esp']
            assert payload['files'][0]['allowed_hashes'] == ['123', '456']
            assert payload['files'][1]['allowed_hashes'] == []
            assert payload['relay_probe'] is True
            stop(proc)
        print('PASS: legacy/v2 manifests, ordered files, hashes, optional groundcover excluded')
        host = spawn(args)
        posts.get(timeout=8)
        session = root / 'session.json'
        session.write_text(json.dumps(dict(host_session, token=tokens[b'C'].hex(), relay_probe=True)))
        ready = root / 'ready'
        tunnel = spawn([client, '--session-file', session, '--ready-file', ready,
                        '--relay-host', '127.0.0.1', '--relay-port', str(relay_port)])
        deadline = time.monotonic() + 14
        while not ready.exists() and time.monotonic() < deadline:
            assert tunnel.poll() is None, 'Client exited before handshake'
            time.sleep(.05)
        assert ready.exists(), 'Encrypted host/client probe did not complete'
        probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        probe.settimeout(5)
        for size in (1, 1024, 4097):
            data = os.urandom(size)
            probe.sendto(data, ('127.0.0.1', int(ready.read_text())))
            assert probe.recv(65535) == data
        probe.close()
        print('PASS: authenticated relay, encrypted handshake, bidirectional fragmented game packets')
finally:
    for proc in reversed(processes): stop(proc)
    master.shutdown()
    master.server_close()
    relay.close()
    game.close()
