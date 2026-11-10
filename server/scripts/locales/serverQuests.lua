-- ArenaMP X054: player-facing strings of the server quest system.
--
-- Quest *content* (names, journal entries, NPC lines, answer choices) is not
-- here: that lives in each quest's own JSON under "translations", so a quest
-- author can add a language without touching server code. This dictionary only
-- covers the wrapper text the engine itself produces.

local dictionary = {
    EN = {
        journal_updated = "Journal updated: {name}",
        journal_line = "{name} - {text}",
        quest_started = "Started: {name} (stage {stage})",
        quest_advanced = "{name} -> stage {stage}",
        quest_advanced_state = "{name} -> stage {stage} [{state}]",
        quest_cannot_start = "Cannot start {name}: {reason}",
        requirement_failed = "Requirement not met - {reason}",

        requirement_unknown = "unknown requirement",
        requirement_item = "item \"{refId}\": need {need}, you have {have}",
        requirement_gold = "gold: need {op}{need}, you have {have}",
        requirement_level = "level {op}{value}",
        requirement_stat = "{kind} {key} {op}{value}",
        requirement_faction = "faction {key} {op}{value}",
        requirement_quest_completed = "finish quest {questId} first",
        requirement_quest_stage = "quest {questId} {op}{value}",
        requirement_generic = "{kind} {op}{value}"
    },
    RU = {
        journal_updated = "Дневник обновлён: {name}",
        journal_line = "{name} — {text}",
        quest_started = "Начато: {name} (этап {stage})",
        quest_advanced = "{name} -> этап {stage}",
        quest_advanced_state = "{name} -> этап {stage} [{state}]",
        quest_cannot_start = "Нельзя начать {name}: {reason}",
        requirement_failed = "Не выполнено условие — {reason}",

        requirement_unknown = "неизвестное условие",
        requirement_item = "предмет \"{refId}\": нужно {need}, у тебя {have}",
        requirement_gold = "золото: нужно {op}{need}, у тебя {have}",
        requirement_level = "уровень {op}{value}",
        requirement_stat = "{kind} {key} {op}{value}",
        requirement_faction = "фракция {key} {op}{value}",
        requirement_quest_completed = "сначала заверши квест {questId}",
        requirement_quest_stage = "квест {questId} {op}{value}",
        requirement_generic = "{kind} {op}{value}"
    }
}

return dictionary
