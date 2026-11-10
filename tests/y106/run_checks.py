#!/usr/bin/env python3
"""Compile production resume methods and packets against an in-memory transport.

Engine/UI and RakNet I/O are doubles; production C++ method bodies, BasePacket
and PacketPreInit are compiled unchanged. This is not an engine/NDK build or a
real UDP outage test. No server data is opened or written.
"""
from pathlib import Path
import os, shutil, subprocess, tempfile

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

def method(path, signature):
    source = (ROOT / path).read_text()
    start = source.index(signature)
    opening = source.index('{', start)
    # These method bodies contain no braces in strings except paired log text.
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

server = 'apps/openmw-mp/Networking.cpp'
client = 'apps/openmw/mwmp/Networking.cpp'
parts = [(ROOT / 'components/openmw-mp/Packets/BasePacket.cpp').read_text(),
         (ROOT / 'components/openmw-mp/Packets/PacketPreInit.cpp').read_text(),
         method('apps/openmw-mp/Cell.cpp', 'void Cell::rebindPlayerGuid('),
         method('apps/openmw-mp/CellController.cpp', 'void CellController::rebindPlayerGuid('),
         method('apps/openmw-mp/Player.cpp', 'bool Players::rebindPlayerGuid('),
         'namespace server {\nusing namespace mwmp;']
for sig in ['std::string Networking::generateResumeToken(', 'void Networking::expireSuspendedPlayers(',
            'bool Networking::preInit(', 'void Networking::update(',
            'void Networking::processSystemPacket(', 'void Networking::processPlayerPacket(',
            'void Networking::processActorPacket(', 'void Networking::processObjectPacket(',
            'void Networking::processWorldstatePacket(', 'void Networking::sendPlayerSnapshot(',
            'void Networking::exchangePlayerSnapshots(', 'void Networking::completeSessionResume(',
            'void Networking::disconnectPlayer(', 'void Networking::suspendPlayer(']:
    parts.append(method(server, sig))
# Exercise the actual event cases and pre-init routing from mainLoop, without
# compiling its OS signals, launcher worker or endless loop.
source = (ROOT / server).read_text()
cases = []
for first, following in [('ID_NEW_INCOMING_CONNECTION', 'ID_NO_FREE_INCOMING_CONNECTIONS'),
                         ('ID_DISCONNECTION_NOTIFICATION', 'ID_CONNECTION_LOST'),
                         ('ID_CONNECTION_LOST', 'ID_SND_RECEIPT_ACKED')]:
    begin = source.index('                case ' + first + ':', source.index('int Networking::mainLoop()'))
    end = source.index('                case ' + following + ':', begin)
    cases.append(source[begin:end])
begin = source.index('                    // Y106: PREINIT is meaningful')
end = source.index('                    break;', begin)
parts.append('void Networking::deliver(RakNet::Packet* packet) {\nswitch(packet->data[0]) {\n'
             + '\n'.join(cases) + '\ndefault: { RakNet::BitStream bsIn(packet->data+1,packet->length-1,false);'
             + 'bsIn.IgnoreBytes(RakNet::RakNetGUID::size());\n' + source[begin:end] + '\nbreak;}}}\n}')
parts.append('namespace client {\nusing namespace mwmp;')
for sig in ['void Networking::update(', 'void Networking::beginConnectionRestore(',
            'bool Networking::completeConnectionRestore(', 'void Networking::failConnectionRestore(',
            'void Networking::setGameplaySendEnabled(', 'void Networking::preInit(']:
    parts.append(method(client, sig))
parts.append('}')
for sig in ['void LocalPlayer::beginLoginSync(', 'void LocalPlayer::markLoginSyncReceived(']:
    parts.append(method('apps/openmw/mwmp/LocalPlayer.cpp', sig))

with tempfile.TemporaryDirectory(prefix='arenamp-y106-') as temp:
    temp = Path(temp)
    for name in ['RakNetTypes.h', 'BitStream.h', 'RakPeer.h', 'PacketPriority.h', 'MessageIdentifiers.h']:
        (temp / name).write_text('#pragma once\n#include "transport.hpp"\n')
    (temp / 'transport.hpp').write_text((HERE / 'transport.hpp').read_text())
    log = temp / 'components/openmw-mp/TimedLog.hpp'
    log.parent.mkdir(parents=True)
    log.write_text('#pragma once\n#define LOG_MESSAGE_SIMPLE(...) ((void)0)\n'
                   '#define LOG_MESSAGE(...) ((void)0)\n#define LOG_APPEND(...) ((void)0)\n')
    cpp = temp / 'test.cpp'
    cpp.write_text('#include "harness.hpp"\n' + '\n'.join(parts) + '\n'
                   + (HERE / 'scenarios.cpp').read_text())
    compiler = shutil.which('g++')
    if not compiler:
        raise SystemExit('A C++17 compiler is required')
    flags = [compiler, '-std=c++17', '-Wall', '-Wextra', '-Werror', '-Wno-unused-parameter',
             '-Wno-unused-variable', '-Wno-sign-compare', '-O1', '-g', '-I', str(temp), '-I', str(HERE),
             '-I', str(ROOT), '-I', str(ROOT / 'components/openmw-mp/Packets')]
    if os.environ.get('ARENA_Y106_SANITIZE'):
        flags += ['-fsanitize=address,undefined', '-fno-omit-frame-pointer']
    exe = temp / 'test'
    subprocess.run(flags + [str(cpp), '-o', str(exe)], check=True)
    subprocess.run([str(exe)], check=True)

# Frame traffic must stop before voice, cell updates and script flushes.
frame = method('apps/openmw/mwmp/Main.cpp', 'void Main::frame(')
assert frame.index('if (!get().getNetworking()->isConnected())') < frame.index('getVoiceChat()->update')
assert frame.index('getVoiceChat()->update') < frame.index('ScriptController::flushQueuedLocalChanges')
connect = method(client, 'void Networking::connect(')
assert 'mContentChecksums.clear()' in connect and 'std::random_device' in connect
assert 'mSessionIdentity = identity.str()' in connect
print('PASS frame traffic gate and in-memory identity lifetime', flush=True)
