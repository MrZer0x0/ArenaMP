# Авторы и сторонние компоненты

[Оглавление](INDEX.md) · [Корневые уведомления](../../THIRD_PARTY_NOTICES.md)

## Основа

OpenMW предоставляет движок, TES3MP — сетевую и серверную основу, EncoreMP — часть баланса и сопутствующий контент. Исторический список авторов сохранён в [AUTHORS.md](../../AUTHORS.md), подробные благодарности TES3MP — в [исходном документе](../upstream/TES3MP_CREDITS.md). Упоминание проекта не означает одобрения форка.

## Анимации и игровые ресурсы

Ниже сохранены сведения о включённых материалах и адаптациях. Источник идеи не считается включённым кодом. Полные авторские тексты рядом с файлами имеют собственную область применения.


| Проект и авторы | Использование в текущем дереве | Где сохранены сведения / первичный источник |
|---|---|---|
| **Morrowind Animation Overhaul 0.72**, Parsoma | Материалы третьего лица; выбор применимого humanoid-скелета, fallback для зверей/нестандартных моделей | `docs/animation-pack/upstream/MAO_readme.txt`; [Nexus 60253](https://www.nexusmods.com/morrowind/mods/60253) |
| **ReAnimation v3**, Max Yari / MaxYari | KF/YAML и исходные материалы; первый вид, выбор варианта атаки и сетевые канонические имена | `docs/animation-pack/upstream/ReAnimation_v3/`; [Nexus 52596](https://www.nexusmods.com/morrowind/mods/52596) |
| **Oblivion Style Spellcasting / OSSC**, skrow42 / Antigravity; анимационный вклад MaxYari и dubiousnpc | Жесты и нативный быстрый каст; исходные Lua сохранены для происхождения, не загружаются как современная OpenMW Lua-система | `docs/animation-pack/upstream/OSSC/README.md`, `LICENSE`; [Nexus 58653](https://www.nexusmods.com/morrowind/mods/58653) |
| **N'Garde**, Arrean и участники из upstream credits | Адаптация активного блока/парирования и защитные анимации; не заявлена полная совместимость со всеми его Lua-модами | `files/vfs/animations/ngarde_credits/`; [репозиторий и wiki автора](https://gitlab.com/Arrean/openmwluaparry) |
| **OpenMW Dynamic Animations 1.14c**, taitechnic | Совместимые KF/YAML/NIF, профили движения и нативные контроллеры | `files/vfs/animations/README-Dynamic-Animations-1.14c.txt`; [Nexus 57633](https://www.nexusmods.com/morrowind/mods/57633) |
| **OpenMW Dynamic Actors**, taitechnic; вклад GrumblingVomit и Vidi Aquam в исходные анимации | Материалы idle/социальных жестов и интеграция; не весь прежний пользовательский Lua-интерфейс | `files/vfs/animations/README-OpenMW-Dynamic-Actors.txt`; [Nexus 54782](https://www.nexusmods.com/morrowind/mods/54782) |
| **Consuming Animated 1.5.1**, Foxunder | Анимации/пропсы/звук употребления; C++ state machine и NPC-путь | `files/vfs/animations/README-Consuming-Animated.txt`; [Nexus 59069](https://www.nexusmods.com/morrowind/mods/59069) |
| **Interactions Animated 1.1**, Foxunder; атрибуцию сверять с исходным пакетом | Материалы взаимодействий, подбор/чтение/передача; native hooks и cleanup | `files/vfs/animations/README-Interactions-Animated.txt`; [Nexus 59117](https://www.nexusmods.com/morrowind/mods/59117) |
| **Sit Down Please 3.5.1**, авторский коллектив исходного мода; точный состав версии требует подтверждения пакетом | Безопасные сидячие/социальные анимации; не полный AI маршрутизации мебели и лекций | `files/vfs/animations/README-Sit-Down-Please-3.5.1.txt`; [Nexus 59046](https://www.nexusmods.com/morrowind/mods/59046) |
| **Third-person alternate attacks**, переданный сопровождающим пакет с альтернативными атаками третьего лица | Отдельный слой full-body/KNA и локальное преобразование имён групп | `files/vfs/animations/arenamp/thirdperson_alt/README_ARENAMP.txt`; точный автор и исходный URL в сохранённой заметке не установлены |
| **Footprints**, abot, и OpenMW-вариант следов | Исходные данные/поверхности и нативная интеграция следов; не установка MWSE в ArenaMP | `docs/footprints/abot-readme.txt`, `OpenMW-Footprints-LICENSE`, `data.lua`, `surfaces.lua` |
| **OpenMWLuaPhysics / physics sounds**, MaxYari / Maksim Eremenko | Нативные звуки физических взаимодействий и связанные ресурсы | `files/vfs/sound/physics/README-ArenaMW-Port.txt` |

У N'Garde дополнительно сохранены благодарности S3ctor, Ownlyme, SaintJ, Dubiousnpc, Cybvep, Greatness7, Axemagister, Xerxse, авторам SFX и локализаций — см. полный `UPSTREAM_README.txt`. У OSSC отдельно отмечены Fallchildren, Moneymitch и Ralts. Эта краткая таблица не отменяет полные credits пакетов.

Для Sit Down Please локальная заметка прямо требует проверить разрешения перед распространением. Для third-person alternate/части звука не хватает сведений об авторстве и условиях. Такие пункты остаются незакрытой частью публикационного аудита, даже если технически файлы уже встроены. Нельзя выдавать архив за полностью очищенный от лицензионных неопределённостей.


## Источники идей


| Работа | Автор | Что послужило ориентиром | Что не включалось |
|---|---|---|---|
| [Enchanting Remastered / Enchanting UI Lua OpenMW](https://www.nexusmods.com/morrowind/mods/59279) | tachyonhiggs | Более удобное редактирование зачарования | Код, иконки, скрипты и архив исходного мода |
| [Economy Plus — Taxes, Trade, Mercantile Perks, Fences](https://www.nexusmods.com/morrowind/mods/60369) | zenocite | Региональная торговля, налоги, комиссия и инвестиции | Таблицы цен, активы, плагины и код исходного мода |

Новый Craft and Economic реализован отдельно под GPL-3.0-or-later. Отсылка к модам не делает их GPL и не выдаёт право вкладывать их архивы. UIToolkit, PyYAML и Python, упомянутые в credits этих проектов, не становятся скопированными зависимостями Craft and Economic. Подробная исходная заметка: AUTHORS_AND_LICENSES (`docs/craft-economic/AUTHORS_AND_LICENSES.md`).

Не включайте в credits каждый мод из списка совместимости N'Garde как будто он встроен в ArenaMP: список рекомендаций не является перечнем заимствованных материалов. Аналогично реализация «в духе Elden Ring» не использует заявленных ресурсов FromSoftware.


## Шрифты, интерфейс и библиотеки


| Компонент | Условия/примечание |
|---|---|
| OpenMW/TES3MP C++ и производные файлы | Корневая GPL v3; уточнение «or later» определяется заголовками конкретных файлов |
| Базовые TES3MP CoreScripts | Сохранён `server/LICENSE` MIT; новые файлы могут иметь отдельное указание |
| OSSC | Вложенные AGPL v3 тексты в `docs/animation-pack/upstream/OSSC/LICENSE` и `files/vfs/animations/OSSC_LICENSE.txt` |
| N'Garde | Сохранён GPL-текст в `files/vfs/animations/ngarde_credits/LICENSE.txt` |
| OpenMoji | CC BY-SA 4.0 для производного emoji-атласа, исходная атрибуция в THIRD_PARTY_NOTICES |
| DejaVu Sans Mono Bold | Собственные условия в `files/mygui/DejaVuSansMono-LICENSE.txt` |
| Russo One | Файл лицензии в `docs/license/` |
| Inventory Extender / иконки | MIT © 2026 Ralts; сохранены LICENSE/CREDITS, вклад TPABOBAP, Sergey Rudyi и авторов Flaticon |
| Voice DSP | Отдельное уведомление `files/shaders/ArenaMP-Voice-DSP-LICENSE.txt` и лицензии используемых библиотек |
| CrabNet/RakNet, Breakpad, Base64, OICS, osgQt и прочие | Собственные условия соответствующих проектов; закреплённые источники описаны в DEPENDENCY_PROVENANCE.md |
| Rubic0n LuaJIT | MIT и уведомления в `extern/LuaJIT/COPYRIGHT` |


## Публикация материалов

Корневой LICENSE и отдельные LICENSE/COPYRIGHT/NOTICE сохранены. OSSC содержит собственные AGPL-уведомления; его условия рассматриваются отдельно от общей лицензии движка. Модификации и ассеты не получают новую лицензию от одного включения в ArenaMP.

Реестр сохраняет известные вопросы разрешений для части ресурсов. Перед их публичной перепаковкой сопровождающий должен подтвердить применимые условия конкретного пакета. Этот документ не заменяет отсутствующего разрешения и не заявляет завершённую проверку всех активов.

Данные Morrowind принадлежат соответствующим правообладателям и предоставляются пользователем отдельно. Лицензия движка не является лицензией игры.

## Исходники выпуска

Workflow прикладывает исходники репозитория на коммите сборки вместе со скриптами и уведомлениями. Загружаемые зависимости перечислены в workflow и [реестре происхождения](DEPENDENCY_PROVENANCE.md). Соблюдение условий каждого включённого компонента остаётся частью подготовки выпуска.

## Участие ИИ

ИИ-помощник использовался для анализа, подготовки кода/проверок и документации. Авторство исходных проектов и материалов сохраняется. Решения о сборке и публикации принимает сопровождающий; участие ИИ не меняет лицензии.
