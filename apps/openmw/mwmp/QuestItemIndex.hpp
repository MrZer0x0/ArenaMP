#ifndef OPENMW_MWMP_QUESTITEMINDEX_HPP
#define OPENMW_MWMP_QUESTITEMINDEX_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace MWWorld
{
    class Ptr;
    class ESMStore;
}

namespace mwmp
{
    /// Runtime index of item records that are strongly associated with quest progression.
    ///
    /// ArenaMP 0.47 has no explicit QuestItem flag in TES3 records, so the index is
    /// derived from dialogue conditions/result scripts plus ordinary scripts and then
    /// filtered by how many physical sources the item has in the loaded content set.
    /// The result is intentionally conservative: common resources used by quests remain
    /// ordinary shared loot, while unique/rare quest artefacts become phaseable sources.
    class QuestItemIndex
    {
    public:
        static QuestItemIndex& get();

        /// X013: classification is owned by the server. The client only builds
        /// the index when the server explicitly asks it to act as an oracle, so
        /// an ordinary session never pays for the ESM scan and never stalls
        /// mid-gameplay. While disabled, isQuestItem() is a cheap false.
        void setEnabled(bool enabled);
        bool isEnabled() const { return mEnabled; }

        bool isQuestItem(const std::string& refId);

        /// Sorted, lowercased list of phaseable item records. Empty unless the
        /// index is enabled and built. The order is the canonical one used for
        /// hashing, so every client with the same content produces the same
        /// byte sequence and therefore the same hash.
        const std::vector<std::string>& getSortedEntries();

        /// Hash of the canonical entry list; identifies the classification.
        const std::string& getIndexHash();

        /// Hash of the ordered content file list; identifies the load order.
        static std::string getContentKey();

        /// Dual modular hash, mirrored byte for byte in server/scripts/questIndexStore.lua.
        /// Do not replace it with a wider/faster hash without changing both sides.
        static std::string arenaHash(const std::string& value);
        std::string makeWorldSourceId(const MWWorld::Ptr& ptr);
        std::string makeContainerSourceId(const MWWorld::Ptr& container, const MWWorld::Ptr& item);
        std::string makeContainerSourceId(const MWWorld::Ptr& container, const std::string& itemRefId);
        std::string makeContainerSourceIdFallback(const std::string& cellDescription,
            const std::string& containerRefId, unsigned int refNum, unsigned int mpNum,
            const MWWorld::Ptr& item);

    private:
        struct Candidate
        {
            int evidence = 0;
            int sources = 0;
        };

        QuestItemIndex() = default;
        void build();
        void finalize();
        void addEvidence(const MWWorld::ESMStore& store, const std::string& refId, int score);
        void scanContentSources(const MWWorld::ESMStore& store);

        bool mEnabled = false;
        bool mBuilt = false;
        bool mBuilding = false;
        std::unordered_map<std::string, Candidate> mCandidates;
        std::unordered_set<std::string> mQuestItems;
        std::vector<std::string> mSortedEntries;
        std::string mIndexHash;
    };
}

#endif
