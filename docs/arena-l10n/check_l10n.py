#!/usr/bin/env python3
"""
ArenaMP: проверка полноты локализации по трём слоям.

    python check_l10n.py <корень AMP> [<корень ArenaMP_Mobile>]

Что сверяется:

1. Движок   — files/vfs/l10n/arenamp/ru.ini против en.ini.
              Наборы ключей обязаны совпадать: недостающий ключ означает
              английский текст в русской сборке (или наоборот).
2. Лаунчер  — все литералы tr("...") в apps/launcher против таблицы
              в components/misc/arenarussiantranslator.hpp.
              Строка без записи останется английской в русской системе.
3. Android  — res/values/strings.xml против res/values-ru/strings.xml,
              включая string-array. Значения настроек (*_values) не
              переводятся и из сверки исключаются.

Код возврата 1, если есть расхождения: удобно воткнуть в сборочный скрипт.
"""

import os
import re
import sys
import xml.etree.ElementTree as ET

INI_KEY = re.compile(r"^\s*([A-Za-z0-9_.]+)\s*=")
TR_CALL = re.compile(r'\btr\(\s*"((?:[^"\\]|\\.)*)"(?:\s*"((?:[^"\\]|\\.)*)")*', re.S)
TR_ALL = re.compile(r'\btr\(\s*((?:"(?:[^"\\]|\\.)*"\s*)+)')
INSERT = re.compile(r'm\.insert\(\s*QString::fromUtf8\(\s*u8"((?:[^"\\]|\\.)*)"', re.S)
# Не текст для игрока, а значения настроек. Соглашение этого проекта:
#   *_default  — значение по умолчанию ("win1251", "touch")
#   *_values   — значения списка
#   *_array    — то же самое (mipmapping_array = trilinear|bilinear)
#   *_entries  — а вот это подписи, они переводятся
# Фильтр только по имени: он обязан давать одинаковый результат для обоих
# языков, иначе сверка начинает ругаться в обе стороны попеременно.
NO_TRANSLATE = re.compile(r"(_values|_default|_array)$")
TECHNICAL_ITEM = re.compile(r"^[A-Za-z0-9_.:+%$-]*$")


def ini_keys(path):
    keys = set()
    if not os.path.isfile(path):
        return None
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            match = INI_KEY.match(line)
            if match:
                keys.add(match.group(1))
    return keys


def joined_literals(blob):
    """Склеивает соседние строковые литералы, как это делает компилятор."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', blob)
    return "".join(parts)


def launcher_strings(root):
    found = {}
    base = os.path.join(root, "apps", "launcher")
    for folder, _, files in os.walk(base):
        for name in files:
            if not name.endswith((".cpp", ".hpp")):
                continue
            path = os.path.join(folder, name)
            text = open(path, encoding="utf-8", errors="replace").read()
            for match in TR_ALL.finditer(text):
                literal = joined_literals(match.group(1))
                if literal:
                    found.setdefault(literal, path)
    return found


def ignored_strings(root):
    """Строки, намеренно одинаковые в обоих языках (docs/arena-l10n/l10n-ignore.txt)."""
    path = os.path.join(root, "docs", "arena-l10n", "l10n-ignore.txt")
    if not os.path.isfile(path):
        return set()
    ignored = set()
    with open(path, encoding="utf-8") as handle:
        for line in handle:
            line = line.rstrip("\n")
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            ignored.add(line)
    return ignored


def translator_entries(root):
    entries = set()
    for name in ("arenarussiantranslator.hpp", "arenarussiantranslator.u025.snippet.hpp"):
        path = os.path.join(root, "components", "misc", name)
        if not os.path.isfile(path):
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        for match in INSERT.finditer(text):
            entries.add(match.group(1))
    return entries


def android_names(path):
    """Имена к переводу, их содержимое и отдельно — технические значения."""
    if not os.path.isfile(path):
        return None, None, None
    names, content, technical = set(), {}, set()
    root = ET.parse(path).getroot()
    for node in root:
        name = node.get("name")
        if not name or node.get("translatable") == "false":
            continue
        if NO_TRANSLATE.search(name):
            technical.add(name)
            continue
        if node.tag == "string-array":
            content[name] = [(child.text or "") for child in node]
        else:
            content[name] = [node.text or ""]
        names.add(name)
    return names, content, technical


def nothing_to_translate(items):
    """Цифры и односложные токены переводить нечего: 32/24/16/8, %1$d."""
    return bool(items) and all(TECHNICAL_ITEM.match((item or "").strip()) for item in items)


def report(title, missing_a, missing_b, label_a, label_b):
    if not missing_a and not missing_b:
        print("OK   %s" % title)
        return 0
    print("ПРОБЛЕМА  %s" % title)
    for key in sorted(missing_a):
        print("   нет в %s: %s" % (label_a, key))
    for key in sorted(missing_b):
        print("   нет в %s: %s" % (label_b, key))
    return 1


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 1
    amp = sys.argv[1]
    mobile = sys.argv[2] if len(sys.argv) > 2 else None
    problems = 0

    l10n = os.path.join(amp, "files", "vfs", "l10n", "arenamp")
    ru = ini_keys(os.path.join(l10n, "ru.ini"))
    en = ini_keys(os.path.join(l10n, "en.ini"))
    if ru is None or en is None:
        print("ПРОПУЩЕНО  движок: ru.ini/en.ini не найдены")
    else:
        print("     движок: ru %d ключей, en %d" % (len(ru), len(en)))
        problems += report("движок ru.ini / en.ini", en - ru, ru - en, "ru.ini", "en.ini")

    strings = launcher_strings(amp)
    if not strings:
        print("ПРОПУЩЕНО  лаунчер: исходники не найдены")
    else:
        entries = translator_entries(amp)
        ignored = ignored_strings(amp)
        missing = {s: p for s, p in strings.items()
                   if s not in entries and s not in ignored and s.strip()}
        print("     лаунчер: %d строк в tr(), %d записей в таблице, %d в списке исключений"
              % (len(strings), len(entries), len(ignored)))
        if missing:
            problems += 1
            print("ПРОБЛЕМА  лаунчер: нет русского перевода")
            for text in sorted(missing):
                short = text if len(text) <= 60 else text[:57] + "..."
                print("   %-62s  %s" % (short.replace("\\n", " "), os.path.relpath(missing[text], amp)))
        else:
            print("OK   лаунчер: у каждой строки есть перевод")

    if mobile:
        res = os.path.join(mobile, "app", "src", "main", "res")
        base, base_content, base_tech = android_names(os.path.join(res, "values", "strings.xml"))
        rus, _, _ = android_names(os.path.join(res, "values-ru", "strings.xml"))
        if base is None or rus is None:
            print("ПРОПУЩЕНО  Android: strings.xml не найдены")
        else:
            print("     Android: values %d имён к переводу (+%d технических), values-ru %d"
                  % (len(base), len(base_tech), len(rus)))
            real = {n for n in base - rus if not nothing_to_translate(base_content.get(n))}
            benign = (base - rus) - real
            extra = rus - base
            if real or extra:
                problems += 1
                print("ПРОБЛЕМА  Android values / values-ru")
                for name in sorted(real):
                    print("   нет русского перевода: %s" % name)
                for name in sorted(extra):
                    print("   есть в values-ru, но нет в values: %s" % name)
            else:
                print("OK   Android: все подписи переведены")
            for name in sorted(benign):
                print("     заметка: %s нет в values-ru, но там только цифры/токены — перевод не нужен" % name)

    print()
    print("расхождений нет" if problems == 0 else "слоёв с расхождениями: %d" % problems)
    return 0 if problems == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
