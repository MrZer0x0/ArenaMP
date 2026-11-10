#!/usr/bin/env python3
"""
ArenaMP: проверка статуса сервера с ПК, мимо лаунчера.

Шлёт ровно тот же offline-ping RakNet, что и лаунчер с Android, и печатает,
что вернулось. Слот игрока не занимает, вход не требуется.

    python arena_status_check.py <адрес> <игровой_порт>
    python arena_status_check.py 127.0.0.1 25565

Зачем: разделить две совершенно разные причины.
  * Ответ приходит в обоих тестах  -> сервер и сеть в порядке, виноват
    лаунчер (см. фикс serverstatusquery.hpp).
  * Ответа нет нигде               -> до сервера не доходит: firewall на ПК,
    закрытый UDP-порт, не тот адрес. Правка кода не поможет.
  * Ответ есть только в тесте 1    -> подтверждается диагноз про двухстековый
    сокет: адрес отправителя приходит как ::ffff:x.x.x.x.
"""

import socket
import sys
import os

MAGIC = bytes([0, 255, 255, 0, 254, 254, 254, 254, 253, 253, 253, 253, 18, 52, 86, 120])


def decode(packet: bytes, token: bytes):
    """Тот же разбор, что в serverstatus.hpp и ServerStatusMonitor.kt."""
    if not packet or len(packet) > 512 or packet[0] != 0x1C:
        return None
    for width in (8, 4):
        header = 1 + width + 8 + 16
        if len(packet) < header:
            continue
        if packet[1:1 + width] != token[:width]:
            continue
        if packet[1 + width + 8:header] != MAGIC:
            continue
        tail = packet[header:].decode("ascii", "replace")
        if not tail.startswith("AMPSTATUS1|"):
            return ("old", tail)
        fields = tail.split("|")
        if len(fields) != 4:
            return ("old", tail)
        return ("amp", int(fields[1]), int(fields[2]), int(fields[3]))
    return None


def probe(host: str, port: int, dual_stack: bool):
    token = os.urandom(8)
    try:
        if dual_stack:
            sock = socket.socket(socket.AF_INET6, socket.SOCK_DGRAM)
            sock.setsockopt(socket.IPPROTO_IPV6, socket.IPV6_V6ONLY, 0)
            sock.bind(("::", 0))
            target = "::ffff:" + socket.gethostbyname(host)
        else:
            sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
            sock.bind(("0.0.0.0", 0))
            target = socket.gethostbyname(host)
    except OSError as error:
        return "сокет не создан: %s" % error

    sock.settimeout(3.0)
    try:
        for width in (8, 4):
            sock.sendto(bytes([1]) + token[:width] + MAGIC + bytes(8), (target, port))
        while True:
            packet, addr = sock.recvfrom(1024)
            answer = decode(packet, token)
            if answer is None:
                continue
            source = addr[0]
            if answer[0] == "old":
                return "ответ есть, источник %s — сервер доступен, но статус не публикует (старая сборка)" % source
            return ("ответ есть, источник %s — игроков %d/%d, аптайм %dд %dч %dм"
                    % (source, answer[1], answer[2],
                       answer[3] // 86400, (answer[3] // 3600) % 24, (answer[3] // 60) % 60))
    except socket.timeout:
        return "ответа нет за 3 с"
    except OSError as error:
        return "ошибка сети: %s" % error
    finally:
        sock.close()


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 1
    host, port = sys.argv[1], int(sys.argv[2])
    print("Проверяю %s:%d\n" % (host, port))
    print("1. обычный IPv4-сокет           : %s" % probe(host, port, False))
    print("2. двухстековый сокет (как Qt)  : %s" % probe(host, port, True))
    print()
    print("Если сработал только тест 1 — подтверждается диагноз:")
    print("лаунчер получал ответ, но выбрасывал его из-за сравнения адресов.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
