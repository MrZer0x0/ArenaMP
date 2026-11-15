#ifndef GAME_MWDIALOG_JOURNAL_H
#define GAME_MWDIALOG_JOURNAL_H

#include "../mwbase/journal.hpp"
#include <chrono>

#include "quest.hpp"

namespace MWDialogue
{
    /// \brief The player's journal
    class Journal : public MWBase::Journal
    {
            TEntryContainer mJournal;
            TQuestContainer mQuests;
            TTopicContainer mTopics;
            std::chrono::steady_clock::time_point mLastJournalToast{};
            void notifyJournalUpdated();

        private:

            Quest& getQuest (const std::string& id);

            Topic& getTopic (const std::string& id);

            bool isThere (const std::string& topicId, const std::string& infoId = "") const;

        public:

            Journal();

            void clear() override;

            /*
                Start of tes3mp addition

                Make it possible to check whether a journal entry already exists from elsewhere in the code
            */
            virtual bool hasEntry(const std::string& id, int index);
            /*
                End of tes3mp addition
            */

            /*
                Start of tes3mp change (minor)

                Make it possible to override current time when adding journal entries, by adding
                optional timestamp override arguments
            */
            void addEntry (const std::string& id, int index, const MWWorld::Ptr& actor, int daysPassed = -1, int month = -1, int day = -1) override;
            ///< Add a journal entry.
            /// @param actor Used as context for replacing of escape sequences (%name, etc).
            /*
                End of tes3mp change (major)
            */

            void setJournalIndex (const std::string& id, int index) override;
            ///< Set the journal index without adding an entry.

            int getJournalIndex (const std::string& id) const override;
            ///< Get the journal index.

            void addTopic (const std::string& topicId, const std::string& infoId, const MWWorld::Ptr& actor) override;
            /// \note topicId must be lowercase

            // ArenaMP C24: recreate an empty topic-history object for a topic
            // restored by the server. This keeps @topic# links in journal text
            // clickable after relog without inventing an NPC response that the
            // player never actually heard.
            void ensureKnownTopic(const std::string& topicId);

            /// ArenaMP QM1 (Quest Manager services). The server decides and
            /// persists; these only mirror its decision in the local journal.
            ///
            /// Forget a quest: its entries and its index (so it can start over).
            void resetQuest(const std::string& id);
            /// Remove all journal text (quest entries and topic responses) but
            /// keep every quest's index and finished state and every topic.
            void clearJournalText();
            /// Hide one quest entry (by journal index) while keeping its effect
            /// on the quest's state. Returns true if an entry was removed.
            bool hideEntry(const std::string& id, int index);

            void removeLastAddedTopicResponse (const std::string& topicId, const std::string& actorName) override;
            ///< Removes the last topic response added for the given topicId and actor name.
            /// \note topicId must be lowercase

            TEntryIter begin() const override;
            ///< Iterator pointing to the begin of the main journal.
            ///
            /// \note Iterators to main journal entries will never become invalid.

            TEntryIter end() const override;
            ///< Iterator pointing past the end of the main journal.

            TQuestIter questBegin() const override;
            ///< Iterator pointing to the first quest (sorted by topic ID)

            TQuestIter questEnd() const override;
            ///< Iterator pointing past the last quest.

            TTopicIter topicBegin() const override;
            ///< Iterator pointing to the first topic (sorted by topic ID)
            ///
            /// \note The topic ID is identical with the user-visible topic string.

            TTopicIter topicEnd() const override;
            ///< Iterator pointing past the last topic.

            int countSavedGameRecords() const override;

            void write (ESM::ESMWriter& writer, Loading::Listener& progress) const override;

            void readRecord (ESM::ESMReader& reader, uint32_t type) override;
    };
}

#endif
