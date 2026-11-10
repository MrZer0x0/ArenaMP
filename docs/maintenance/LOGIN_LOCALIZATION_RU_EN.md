# Синхронизация входа и языки / Login synchronization and languages

## Что изменено

Лог предоставленного сервера содержит 42 предупреждения `login total`: 103–412 мс, медиана 169 мс. Во всех этих входах отмечена обработка `houseHelper`: 36–162 мс, медиана 55,5 мс. Эти времена включают работу соответствующего обработчика, а не только отправку пакетов. В логе также есть отдельные задержки чтения карты; дисковый ввод-вывод этим обновлением не заменяется.

Обнаружен конкретный источник лишней работы: обёртка `tes3mp.SendMessage` передавала служебные пакеты в автоматический переводчик. Для русскоязычного получателя он заново объединял словари, сортировал фразы и выполнял подстановки для каждого сообщения. Изменяться могли и структурированные поля: ID ячейки, имя игрока, JSON, английская половина двуязычной записи.

- Пакеты с зарезервированными префиксами `@@AMP_…@@` и `@@ARENA_…@@` передаются без текстовых подстановок. Локализованные поля готовят сами модули, клиент выбирает RU/EN.
- Правила автоперевода обычных сообщений собираются один раз для языка. `RegisterDictionary` сбрасывает кеш при замене/добавлении словаря. Содержимое пользовательских сообщений не кешируется.
- Поиск владения домами пропускает пустых владельцев до преобразования UTF-8. Название дома для списка комнат формируется один раз на объект. Полный список комнат и серверные проверки прав сохраняются.
- В мебельном справочнике сохраняются все 515 ID, цены, категории и стили. Исправлены отображаемые названия, в том числе переносы строк, из-за которых запись не проходила проверку. Добавлены явные RU/EN-подписи категорий; русские строки категорий остаются совместимыми внутренними ключами.
- Генерируемый каталог использует английское имя из справочника, когда старый ручной пресет дублирует русское имя в `nameEn`. Цена и пользовательские правки в `arenaFurnitureManual.json` не перезаписываются. Для неизвестного предмета без перевода используется refId; оба имени можно задать в редакторе.
- Мебель и подписи домов читают выбранный язык клиента. Сообщения управления перезапуском имеют RU/EN-варианты.
- README на русском и английском описывают назначение проекта, запуск, лаунчер, механики, Android, эксплуатацию и границы реализованных возможностей.

## Выполненная проверка

Проверки запускались на включённом в исходники Rubic0n LuaJIT, собранном локально. Полная сборка клиента и игровая сессия с несколькими игроками здесь не выполнялись.

| Изолированная проверка | До | После |
|---|---:|---:|
| 1000 служебных отправок, словарь проекта, транспорт-заглушка | 538 мс | 0,8 мс |
| 200 циклов поиска/состояния домов, реестр 1740 объектов | 317 мс | 41 мс |

Это замеры конкретных функций в локальной среде, **не измерение общего времени входа на боевом сервере**. Состояние домов до и после совпало побайтно после сортировки независимых сообщений. Проверены неизменность служебных полей, адресация/флаги отправки, RU/EN-сообщения, обновление словаря и существующая проверка магазина/прав/ручных правок. Каталог проверен на сохранение ID/цен и допустимость строк. Ключи клиентских RU/EN-словарей совпадают.

После установки перезапустите сервер. В штатном логе сравните `[LoginSync] login total`, `login hooks` и предупреждения `houseHelper` при входе в тот же мир. Если остаются долгие фазы, их имена и длительности укажут следующий источник: диск, другой пользовательский обработчик или сетевой ввод-вывод. Значение `slowPhaseMs` в `config.loginPerformance` определяет порог логирования, а не ограничение длительности обработчика.

## English

The supplied log records 42 slow logins (103–412 ms, median 169 ms), with the housing hook at 36–162 ms (median 55.5 ms). A concrete source of unnecessary work was automatic phrase translation applied to structured control packets. It repeatedly rebuilt/sorted dictionaries and could alter identifiers, JSON and bilingual fields.

Control packets now bypass automatic translation, while ordinary chat/UI translation uses cached rules invalidated by dictionary registration. Housing ownership lookups skip empty owners before UTF-8 folding; room titles are formatted once per property. The same housing state and access rules are retained.

All 515 furniture IDs, prices, categories and styles remain intact. Display labels are cleaned, category metadata is explicitly bilingual, and invalid embedded newlines are removed. Generated stock repairs legacy duplicated Russian/English labels using the reference English name without rewriting manual data. Unknown untranslated mod items use their ID until an operator supplies both names. Furniture and housing UI read the client's language; restart-management messages have RU/EN variants.

The table above contains isolated local measurements, not production login latency. Full client builds and a live multiplayer session were not run. Restart the server after installing and compare its existing `[LoginSync]` phase logs. Disk-related map delays and unrelated custom hooks can still require separate investigation.
