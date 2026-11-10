#ifndef OPENMW_MWMP_SERVERQUESTREGISTRY_HPP
#define OPENMW_MWMP_SERVERQUESTREGISTRY_HPP

#include <string>
#include <vector>

namespace mwmp
{
    struct ServerQuestChoice
    {
        std::string id;
        std::string text;
    };

    struct ServerQuestTopic
    {
        std::string questId;
        std::string questName;
        std::string giverRefId;
        std::string cell;
        std::string topicId;
        std::string text;
        std::string state;
        int stage = 0;
        std::string dialogue;

        std::string token() const;
    };

    struct ServerQuestResponse
    {
        bool valid = false;
        std::string questId;
        std::string topicId;
        std::string giverRefId;
        std::string cell;
        std::string text;
        std::string state;
        int stage = 0;
        std::vector<ServerQuestChoice> choices;
    };

    struct ServerQuestJournalEntry
    {
        int stage = 0;
        std::string text;
        std::string date;
        std::string questName;
        long long vanillaAnchor = -1;
        long long epoch = 0;
    };

    struct ServerQuestState
    {
        std::string questId;
        std::string questName;
        std::string giverRefId;
        std::string cell;
        std::string state;
        int stage = 0;
        std::vector<ServerQuestJournalEntry> journal;
    };

    // X039: editor model is deliberately separate from the player-visible quest
    // registry. Only staff receive it, and it is an editable projection of the
    // authoritative definitions stored on the server.
    struct ServerQuestEditorTopic
    {
        std::string id;
        std::string text;
        bool enabled = true;
    };

    struct ServerQuestEditorRequirement
    {
        int index = -1;
        std::string scope;      // stage | stageChoice | offerChoice
        int stage = 0;
        std::string choiceId;
        std::string type;
        std::string op;
        std::string value;
        std::string refId;
        std::string questId;
        std::string key;
    };

    struct ServerQuestEditorReward
    {
        int index = -1;
        int stage = 0;
        std::string type;
        std::string amount;
        std::string refId;
        std::string count;
        std::string key;
        std::string value;
        std::string message;
    };

    struct ServerQuestEditorChoice
    {
        std::string scope; // offer | stage
        int stage = 0;
        std::string id;
        std::string text;
        std::string action;
        int targetStage = 0;
        bool hasTargetStage = false;
        std::vector<ServerQuestEditorRequirement> requirements;
    };

    struct ServerQuestEditorStage
    {
        int index = 0;
        std::string journal;
        std::string dialogue;
        bool complete = false;
        bool fail = false;
        std::vector<int> next;
        std::vector<ServerQuestEditorRequirement> requirements;
        std::vector<ServerQuestEditorReward> rewards;
        std::vector<ServerQuestEditorChoice> choices;
    };

    struct ServerQuestEditorQuest
    {
        std::string id;
        std::string name;
        std::string status;
        std::string author;
        std::string progressMode;
        int version = 0;
        std::string giverRefId;
        std::string cell;
        std::string uniqueIndex;
        int initialStage = 0;
        std::string offerDialogue;
        std::vector<ServerQuestEditorTopic> topics;
        std::vector<ServerQuestEditorChoice> offerChoices;
        std::vector<ServerQuestEditorStage> stages;
        std::vector<std::string> validationErrors;
        std::vector<std::string> validationWarnings;
    };

    struct ServerQuestEditorModel
    {
        bool available = false;
        bool canPublish = false;
        bool isAdmin = false;
        std::string selectedQuestId;
        std::string notice;
        std::vector<ServerQuestEditorQuest> quests;
    };

    class ServerQuestRegistry
    {
    public:
        enum class TransportEvent
        {
            None,
            SyncComplete,
            Response,
            EditorSyncComplete
        };

        static constexpr int TransportGuiId = -35036;

        static ServerQuestRegistry& get();
        static bool cellsMatch(const std::string& expectedCell, const std::string& actualCell);

        TransportEvent handleTransport(const std::string& payload, ServerQuestResponse* response = nullptr);
        std::vector<ServerQuestTopic> getTopics(const std::string& giverRefId, const std::string& cell) const;
        const std::vector<ServerQuestState>& getQuestStates() const { return mQuestStates; }
        const ServerQuestEditorModel& getEditorModel() const { return mEditor; }
        void clear();

    private:
        ServerQuestState* findQuestState(const std::string& questId);
        ServerQuestEditorQuest* findEditorQuest(const std::string& questId);
        ServerQuestEditorStage* findEditorStage(ServerQuestEditorQuest& quest, int stage);
        ServerQuestEditorChoice* findEditorChoice(ServerQuestEditorQuest& quest, const std::string& scope,
            int stage, const std::string& choiceId);

        std::vector<ServerQuestTopic> mTopics;
        std::vector<ServerQuestState> mQuestStates;
        ServerQuestEditorModel mEditor;
    };
}

#endif
