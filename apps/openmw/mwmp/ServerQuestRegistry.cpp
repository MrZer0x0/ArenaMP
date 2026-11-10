#include "ServerQuestRegistry.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace
{
    std::string lowerAscii(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        return value;
    }

    int hexValue(char c)
    {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    }

    std::string percentDecode(const std::string& value)
    {
        std::string out;
        out.reserve(value.size());
        for (std::size_t i = 0; i < value.size(); ++i)
        {
            if (value[i] == '%' && i + 2 < value.size())
            {
                const int hi = hexValue(value[i + 1]);
                const int lo = hexValue(value[i + 2]);
                if (hi >= 0 && lo >= 0)
                {
                    out.push_back(static_cast<char>((hi << 4) | lo));
                    i += 2;
                    continue;
                }
            }
            out.push_back(value[i]);
        }
        return out;
    }

    std::vector<std::string> splitTabs(const std::string& payload)
    {
        std::vector<std::string> fields;
        std::size_t begin = 0;
        while (true)
        {
            const std::size_t end = payload.find('\t', begin);
            fields.push_back(percentDecode(payload.substr(begin, end == std::string::npos ? end : end - begin)));
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
        return fields;
    }

    bool toBool(const std::string& value)
    {
        return value == "1" || lowerAscii(value) == "true" || lowerAscii(value) == "yes";
    }

    std::string normalizeQuestCell(std::string value)
    {
        value = lowerAscii(value);
        static const std::string instanceMarker = " - instance for ";
        const std::size_t marker = value.find(instanceMarker);
        if (marker != std::string::npos)
            value.resize(marker);

        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            value.pop_back();
        std::size_t begin = 0;
        while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])))
            ++begin;
        if (begin != 0)
            value.erase(0, begin);
        return value;
    }
}

namespace mwmp
{
    std::string ServerQuestTopic::token() const
    {
        return "@ArenaQuest:" + questId + ":" + topicId;
    }

    ServerQuestRegistry& ServerQuestRegistry::get()
    {
        static ServerQuestRegistry registry;
        return registry;
    }

    bool ServerQuestRegistry::cellsMatch(const std::string& expectedCell, const std::string& actualCell)
    {
        if (expectedCell.empty())
            return true;
        return normalizeQuestCell(expectedCell) == normalizeQuestCell(actualCell);
    }

    void ServerQuestRegistry::clear()
    {
        mTopics.clear();
        mQuestStates.clear();
    }

    ServerQuestState* ServerQuestRegistry::findQuestState(const std::string& questId)
    {
        for (ServerQuestState& state : mQuestStates)
            if (state.questId == questId)
                return &state;
        return nullptr;
    }

    ServerQuestEditorQuest* ServerQuestRegistry::findEditorQuest(const std::string& questId)
    {
        for (ServerQuestEditorQuest& quest : mEditor.quests)
            if (quest.id == questId)
                return &quest;
        return nullptr;
    }

    ServerQuestEditorStage* ServerQuestRegistry::findEditorStage(ServerQuestEditorQuest& quest, int stage)
    {
        for (ServerQuestEditorStage& item : quest.stages)
            if (item.index == stage)
                return &item;
        return nullptr;
    }

    ServerQuestEditorChoice* ServerQuestRegistry::findEditorChoice(ServerQuestEditorQuest& quest,
        const std::string& scope, int stage, const std::string& choiceId)
    {
        if (scope == "offer")
        {
            for (ServerQuestEditorChoice& choice : quest.offerChoices)
                if (choice.id == choiceId)
                    return &choice;
            return nullptr;
        }

        ServerQuestEditorStage* stageData = findEditorStage(quest, stage);
        if (stageData == nullptr)
            return nullptr;
        for (ServerQuestEditorChoice& choice : stageData->choices)
            if (choice.id == choiceId)
                return &choice;
        return nullptr;
    }

    ServerQuestRegistry::TransportEvent ServerQuestRegistry::handleTransport(
        const std::string& payload, ServerQuestResponse* response)
    {
        const std::vector<std::string> fields = splitTabs(payload);
        if (fields.empty())
            return TransportEvent::None;

        if (fields[0] == "CLEAR")
        {
            clear();
            return TransportEvent::None;
        }

        if (fields[0] == "END")
            return TransportEvent::SyncComplete;

        // ---------------- X039 MyGUI Quest Studio transport ----------------
        if (fields[0] == "EDITOR_CLEAR")
        {
            mEditor = ServerQuestEditorModel();
            mEditor.available = true;
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_META" && fields.size() >= 4)
        {
            mEditor.available = true;
            mEditor.canPublish = toBool(fields[1]);
            mEditor.isAdmin = toBool(fields[2]);
            mEditor.selectedQuestId = fields[3];
            if (fields.size() >= 5)
                mEditor.notice = fields[4];
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_QUEST" && fields.size() >= 12)
        {
            ServerQuestEditorQuest quest;
            quest.id = fields[1];
            quest.name = fields[2];
            quest.status = fields[3];
            quest.author = fields[4];
            quest.progressMode = fields[5];
            quest.version = std::atoi(fields[6].c_str());
            quest.giverRefId = fields[7];
            quest.cell = fields[8];
            quest.uniqueIndex = fields[9];
            quest.initialStage = std::atoi(fields[10].c_str());
            quest.offerDialogue = fields[11];
            if (!quest.id.empty())
                mEditor.quests.push_back(quest);
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_TOPIC" && fields.size() >= 5)
        {
            ServerQuestEditorQuest* quest = findEditorQuest(fields[1]);
            if (quest != nullptr)
            {
                ServerQuestEditorTopic topic;
                topic.id = fields[2];
                topic.text = fields[3];
                topic.enabled = toBool(fields[4]);
                quest->topics.push_back(topic);
            }
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_STAGE" && fields.size() >= 7)
        {
            ServerQuestEditorQuest* quest = findEditorQuest(fields[1]);
            if (quest != nullptr)
            {
                ServerQuestEditorStage stage;
                stage.index = std::atoi(fields[2].c_str());
                stage.journal = fields[3];
                stage.dialogue = fields[4];
                stage.complete = toBool(fields[5]);
                stage.fail = toBool(fields[6]);
                quest->stages.push_back(stage);
            }
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_NEXT" && fields.size() >= 4)
        {
            ServerQuestEditorQuest* quest = findEditorQuest(fields[1]);
            if (quest != nullptr)
            {
                ServerQuestEditorStage* stage = findEditorStage(*quest, std::atoi(fields[2].c_str()));
                if (stage != nullptr)
                    stage->next.push_back(std::atoi(fields[3].c_str()));
            }
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_CHOICE" && fields.size() >= 9)
        {
            ServerQuestEditorQuest* quest = findEditorQuest(fields[1]);
            if (quest != nullptr)
            {
                ServerQuestEditorChoice choice;
                choice.scope = fields[2];
                choice.stage = std::atoi(fields[3].c_str());
                choice.id = fields[4];
                choice.text = fields[5];
                choice.action = fields[6];
                choice.hasTargetStage = toBool(fields[7]);
                choice.targetStage = std::atoi(fields[8].c_str());
                if (choice.scope == "offer")
                    quest->offerChoices.push_back(choice);
                else
                {
                    ServerQuestEditorStage* stage = findEditorStage(*quest, choice.stage);
                    if (stage != nullptr)
                        stage->choices.push_back(choice);
                }
            }
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_REQ" && fields.size() >= 12)
        {
            ServerQuestEditorQuest* quest = findEditorQuest(fields[1]);
            if (quest != nullptr)
            {
                ServerQuestEditorRequirement req;
                req.scope = fields[2];
                req.stage = std::atoi(fields[3].c_str());
                req.choiceId = fields[4];
                req.index = std::atoi(fields[5].c_str());
                req.type = fields[6];
                req.op = fields[7];
                req.value = fields[8];
                req.refId = fields[9];
                req.questId = fields[10];
                req.key = fields[11];
                if (req.scope == "stage")
                {
                    ServerQuestEditorStage* stage = findEditorStage(*quest, req.stage);
                    if (stage != nullptr) stage->requirements.push_back(req);
                }
                else
                {
                    const std::string choiceScope = req.scope == "offerChoice" ? "offer" : "stage";
                    ServerQuestEditorChoice* choice = findEditorChoice(*quest, choiceScope, req.stage, req.choiceId);
                    if (choice != nullptr) choice->requirements.push_back(req);
                }
            }
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_REWARD" && fields.size() >= 11)
        {
            ServerQuestEditorQuest* quest = findEditorQuest(fields[1]);
            if (quest != nullptr)
            {
                ServerQuestEditorStage* stage = findEditorStage(*quest, std::atoi(fields[2].c_str()));
                if (stage != nullptr)
                {
                    ServerQuestEditorReward reward;
                    reward.stage = stage->index;
                    reward.index = std::atoi(fields[3].c_str());
                    reward.type = fields[4];
                    reward.amount = fields[5];
                    reward.refId = fields[6];
                    reward.count = fields[7];
                    reward.key = fields[8];
                    reward.value = fields[9];
                    reward.message = fields[10];
                    stage->rewards.push_back(reward);
                }
            }
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_VALID" && fields.size() >= 4)
        {
            ServerQuestEditorQuest* quest = findEditorQuest(fields[1]);
            if (quest != nullptr)
            {
                if (fields[2] == "E") quest->validationErrors.push_back(fields[3]);
                else quest->validationWarnings.push_back(fields[3]);
            }
            return TransportEvent::None;
        }
        if (fields[0] == "EDITOR_END")
            return TransportEvent::EditorSyncComplete;

        // ---------------- player-visible quest transport ----------------
        if (fields[0] == "STATE" && fields.size() >= 7)
        {
            ServerQuestState state;
            state.questId = fields[1];
            state.questName = fields[2];
            state.giverRefId = fields[3];
            state.cell = fields[4];
            state.state = fields[5];
            state.stage = std::atoi(fields[6].c_str());
            if (!state.questId.empty())
                mQuestStates.push_back(state);
            return TransportEvent::None;
        }

        if (fields[0] == "JOURNAL" && fields.size() >= 5)
        {
            ServerQuestState* state = findQuestState(fields[1]);
            if (state != nullptr)
            {
                ServerQuestJournalEntry entry;
                entry.stage = std::atoi(fields[2].c_str());
                entry.date = fields[3];
                entry.text = fields[4];
                entry.questName = state->questName;
                if (fields.size() >= 6)
                    entry.vanillaAnchor = std::atoll(fields[5].c_str());
                if (fields.size() >= 7)
                    entry.epoch = std::atoll(fields[6].c_str());
                if (!entry.text.empty())
                    state->journal.push_back(entry);
            }
            return TransportEvent::None;
        }

        if (fields[0] == "QUEST" && fields.size() >= 9)
        {
            ServerQuestTopic topic;
            topic.questId = fields[1];
            topic.giverRefId = fields[2];
            topic.cell = fields[3];
            topic.topicId = fields[4];
            topic.text = fields[5];
            topic.state = fields[6];
            topic.stage = std::atoi(fields[7].c_str());
            topic.dialogue = fields[8];
            if (fields.size() >= 10)
                topic.questName = fields[9];
            if (!topic.questId.empty() && !topic.giverRefId.empty() && !topic.topicId.empty() && !topic.text.empty())
                mTopics.push_back(topic);
            return TransportEvent::None;
        }

        if (fields[0] == "RESPONSE" && fields.size() >= 8)
        {
            if (response != nullptr)
            {
                response->valid = true;
                response->questId = fields[1];
                response->topicId = fields[2];
                response->giverRefId = fields[3];
                response->cell = fields[4];
                response->text = fields[5];
                response->state = fields[6];
                response->stage = std::atoi(fields[7].c_str());
                response->choices.clear();

                const int choiceCount = fields.size() >= 9 ? std::max(0, std::atoi(fields[8].c_str())) : 0;
                std::size_t cursor = 9;
                for (int i = 0; i < choiceCount && cursor + 1 < fields.size(); ++i, cursor += 2)
                {
                    ServerQuestChoice choice;
                    choice.id = fields[cursor];
                    choice.text = fields[cursor + 1];
                    if (!choice.id.empty() && !choice.text.empty())
                        response->choices.push_back(choice);
                }
            }
            return TransportEvent::Response;
        }

        return TransportEvent::None;
    }

    std::vector<ServerQuestTopic> ServerQuestRegistry::getTopics(
        const std::string& giverRefId, const std::string& cell) const
    {
        std::vector<ServerQuestTopic> result;
        const std::string giver = lowerAscii(giverRefId);
        for (const ServerQuestTopic& topic : mTopics)
        {
            if (lowerAscii(topic.giverRefId) != giver)
                continue;
            if (!cellsMatch(topic.cell, cell))
                continue;
            result.push_back(topic);
        }
        return result;
    }
}
