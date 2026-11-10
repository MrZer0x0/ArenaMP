# Индекс серверных команд

[Сервер](05-server.md) · [Оглавление](INDEX.md)

Индекс статических вызовов `customCommandHooks.registerCommand` в текущем дереве. Динамически регистрируемые команды могут отсутствовать. Аргументы, ранг и условия проверяйте в обработчике; наличие строки не выдаёт права. Старые и новые обработчики могут перекрывать друг друга по порядку загрузки.

| Команда | Обработчик / начало выражения | Файл, строка |
|---|---|---|
| `/?` | `commandHelp)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/PUSHRESETS` | `pushCellResetsEarly)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/RESET` | `pushForCellReset)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/RESETALL` | `pushResetAllCells)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/ResetAll` | `pushResetAllCells)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/addhome` | `commandAddHome)` | [server/scripts/houseHelper.lua](../../server/scripts/houseHelper.lua) |
| `/admin` | `commandAdmin)` | [server/scripts/adminInterface.lua](../../server/scripts/adminInterface.lua) |
| `/ban` | `defaultCommands.ban)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/banlist` | `defaultCommands.banlist)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/cells` | `defaultCommands.cells)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/chatcolor` | `processCommand)` | [server/scripts/chatColorHelper.lua](../../server/scripts/chatColorHelper.lua) |
| `/chathelp` | `commandHelp)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/clean` | `periodicCellResets.cleanCurrentCell)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/cleanunowned` | `cmdCleanUnowned)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/cleanuphomes` | `cmdCleanUnowned)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/clear` | `periodicCellResets.clearCurrentCell)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/color` | `function(pid) showColorMenu(pid) end)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/craft` | `function(pid) showMenu(pid) end)` | [server/scripts/craftEconomic.lua](../../server/scripts/craftEconomic.lua) |
| `/deleteprofile` | `function(pid, cmd)` | [server/scripts/profileHelper.lua](../../server/scripts/profileHelper.lua) |
| `/do` | `commandAction)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/economy` | `function(pid) showMenu(pid) end)` | [server/scripts/craftEconomic.lua](../../server/scripts/craftEconomic.lua) |
| `/furniture` | `function(pid, cmd)` | [server/scripts/arenaFurniture.lua](../../server/scripts/arenaFurniture.lua) |
| `/ghost` | `ToggleGhostCommand)` | [server/scripts/guiHelper.lua](../../server/scripts/guiHelper.lua) |
| `/gmark` | `groupCommand)` | [server/scripts/groupMapHelper.lua](../../server/scripts/groupMapHelper.lua) |
| `/greentext` | `defaultCommands.greentext)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/group` | `processCommand)` | [server/scripts/groupHelper.lua](../../server/scripts/groupHelper.lua) |
| `/groupmark` | `groupCommand)` | [server/scripts/groupMapHelper.lua](../../server/scripts/groupMapHelper.lua) |
| `/groupui` | `processCommand)` | [server/scripts/groupHelper.lua](../../server/scripts/groupHelper.lua) |
| `/gt` | `defaultCommands.greentext)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/home` | `refreshedCommand)` | [server/scripts/houseHelper.lua](../../server/scripts/houseHelper.lua) |
| `/house` | `refreshedCommand)` | [server/scripts/houseHelper.lua](../../server/scripts/houseHelper.lua) |
| `/houselink` | `function(pid, cmd)` | [server/scripts/houseHelper.lua](../../server/scripts/houseHelper.lua) |
| `/invite` | `defaultCommands.inviteAlly)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/ipaddresses` | `defaultCommands.ipaddresses)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/ips` | `defaultCommands.ipaddresses)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/join` | `defaultCommands.joinTeam)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/l` | `defaultCommands.localMessage)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/leave` | `defaultCommands.leaveTeam)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/list` | `defaultCommands.players)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/local` | `defaultCommands.localMessage)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/mapmark` | `personalCommand)` | [server/scripts/groupMapHelper.lua](../../server/scripts/groupMapHelper.lua) |
| `/me` | `commandAction)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/me` | `defaultCommands.me)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/message` | `defaultCommands.msg)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/msg` | `defaultCommands.msg)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/mws` | `commandMwScript)` | [server/scripts/mwScriptHelper.lua](../../server/scripts/mwScriptHelper.lua) |
| `/mwscript` | `commandMwScript)` | [server/scripts/mwScriptHelper.lua](../../server/scripts/mwScriptHelper.lua) |
| `/nametest` | `function(pid, cmd)` | [server/scripts/customCommandHooks.lua](../../server/scripts/customCommandHooks.lua) |
| `/nickcolor` | `processCommand)` | [server/scripts/chatColorHelper.lua](../../server/scripts/chatColorHelper.lua) |
| `/overridedestination` | `defaultCommands.overrideDestination)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/owner` | `function(pid, cmd)` | [server/scripts/helperAreana.lua](../../server/scripts/helperAreana.lua) |
| `/panel` | `commandAdmin)` | [server/scripts/adminInterface.lua](../../server/scripts/adminInterface.lua) |
| `/playerlistui` | `processCommand)` | [server/scripts/playerListHelper.lua](../../server/scripts/playerListHelper.lua) |
| `/players` | `defaultCommands.players)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/popup` | `commandPopup)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/profile` | `command)` | [server/scripts/profileHelper.lua](../../server/scripts/profileHelper.lua) |
| `/profileui` | `uiCommand)` | [server/scripts/profileHelper.lua](../../server/scripts/profileHelper.lua) |
| `/pushresets` | `pushCellResetsEarly)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/quest` | `processCommand)` | [server/scripts/serverQuestSystem.lua](../../server/scripts/serverQuestSystem.lua) |
| `/quests` | `function(pid, cmd)` | [server/scripts/serverQuestSystem.lua](../../server/scripts/serverQuestSystem.lua) |
| `/queststudio` | `function(pid, cmd)` | [server/scripts/serverQuestSystem.lua](../../server/scripts/serverQuestSystem.lua) |
| `/ranktest` | `function(pid, cmd)` | [server/scripts/customCommandHooks.lua](../../server/scripts/customCommandHooks.lua) |
| `/regions` | `defaultCommands.regions)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/reset` | `pushForCellReset)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/resetAll` | `pushResetAllCells)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/resetall` | `pushResetAllCells)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/resetcell` | `defaultCommands.resetCell)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/resets` | `periodicCellResets.viewResets)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/resett` | `periodicCellResets.ResetThisCell)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/resetthis` | `periodicCellResets.ResetThisCell)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/rp` | `commandRP)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/runstartup` | `defaultCommands.runStartup)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/s` | `commandSpeech)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/savecell` | `periodicCellResets.saveCell)` | [server/scripts/resetHelrer.lua](../../server/scripts/resetHelrer.lua) |
| `/say` | `commandSpeech)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/setmodel` | `defaultCommands.setPlayerModel)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/sh` | `commandSpeech)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/shout` | `commandSpeech)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/test` | `function(pid, cmd)` | [server/scripts/customCommandHooks.lua](../../server/scripts/customCommandHooks.lua) |
| `/try` | `commandAction)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/unban` | `defaultCommands.unban)` | [server/scripts/defaultCommands.lua](../../server/scripts/defaultCommands.lua) |
| `/w` | `commandSpeech)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/whisper` | `commandSpeech)` | [server/scripts/coreChat.lua](../../server/scripts/coreChat.lua) |
| `/дом` | `refreshedCommand)` | [server/scripts/houseHelper.lua](../../server/scripts/houseHelper.lua) |


## Серверный API, не команда чата

`require("economyCoefficients").SetPlayer(pid, values)` — заменить персональную таблицу коэффициентов, сохранить профиль и отправить обновление клиенту. Пустая таблица сбрасывает сохранённые персональные надбавки. Подробнее — [настройка системы](11-systems-reference.md).
