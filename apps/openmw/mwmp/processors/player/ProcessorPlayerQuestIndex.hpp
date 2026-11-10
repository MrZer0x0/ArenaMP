#ifndef OPENMW_PROCESSORPLAYERQUESTINDEX_HPP
#define OPENMW_PROCESSORPLAYERQUESTINDEX_HPP

#include <algorithm>
#include <exception>

#include "apps/openmw/mwmp/Main.hpp"
#include "apps/openmw/mwmp/Networking.hpp"
#include "apps/openmw/mwmp/LocalPlayer.hpp"
#include "apps/openmw/mwmp/QuestItemIndex.hpp"

#include "../PlayerProcessor.hpp"

namespace mwmp
{
    /// ArenaMP X013: the client is an oracle for the quest item index, never an
    /// authority. It answers exactly what the server asked for and makes no
    /// decision about phasing itself.
    class ProcessorPlayerQuestIndex final : public PlayerProcessor
    {
    public:
        ProcessorPlayerQuestIndex()
        {
            BPP_INIT(ID_PLAYER_QUEST_INDEX)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (!isLocal())
                return;

            const unsigned char mode = player->questIndex.mode;

            if (player->questIndex.stage != QuestIndexData::STAGE_REQUEST)
                return;

            LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "Received ID_PLAYER_QUEST_INDEX request, mode %i", (int)mode);

            if (mode == QuestIndexData::MODE_OFF)
            {
                // The server already owns an accepted index for this content set.
                // Keep the local classifier switched off so this session never
                // scans the ESM files at all.
                QuestItemIndex::get().setEnabled(false);
                return;
            }

            QuestItemIndex& index = QuestItemIndex::get();
            index.setEnabled(true);

            try
            {
                const std::string contentKey = QuestItemIndex::getContentKey();
                const std::vector<std::string>& entries = index.getSortedEntries();
                const std::string& indexHash = index.getIndexHash();

                if (contentKey.empty())
                {
                    LOG_APPEND(TimedLog::LOG_WARN, "- no content key available, skipping quest index reply");
                    return;
                }

                const unsigned int total = static_cast<unsigned int>(entries.size());
                const unsigned int perChunk = QuestIndexData::sMaxChunkEntries;
                const unsigned int chunkCount = mode == QuestIndexData::MODE_UPLOAD
                    ? (total + perChunk - 1) / perChunk
                    : 0;

                if (total > QuestIndexData::sMaxEntries || chunkCount > QuestIndexData::sMaxChunks)
                {
                    LOG_APPEND(TimedLog::LOG_WARN, "- quest index too large to upload (%u entries)", total);
                    return;
                }

                LocalPlayer* localPlayer = getLocalPlayer();

                auto send = [&](unsigned char stage, unsigned int chunkIndex,
                    const std::vector<std::string>& payload)
                {
                    localPlayer->questIndex.stage = stage;
                    localPlayer->questIndex.mode = mode;
                    localPlayer->questIndex.contentKey = contentKey;
                    localPlayer->questIndex.indexHash = indexHash;
                    localPlayer->questIndex.entryCount = total;
                    localPlayer->questIndex.chunkIndex = chunkIndex;
                    localPlayer->questIndex.chunkCount = chunkCount;
                    localPlayer->questIndex.entries = payload;

                    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_QUEST_INDEX)->setPlayer(localPlayer);
                    Main::get().getNetworking()->getPlayerPacket(ID_PLAYER_QUEST_INDEX)->Send();

                    localPlayer->questIndex.entries.clear();
                };

                const std::vector<std::string> empty;
                send(QuestIndexData::STAGE_HANDSHAKE, 0, empty);

                if (mode != QuestIndexData::MODE_UPLOAD)
                    return;

                for (unsigned int chunk = 0; chunk < chunkCount; ++chunk)
                {
                    const unsigned int begin = chunk * perChunk;
                    const unsigned int end = std::min(begin + perChunk, total);
                    send(QuestIndexData::STAGE_CHUNK, chunk,
                        std::vector<std::string>(entries.begin() + begin, entries.begin() + end));
                }

                send(QuestIndexData::STAGE_END, chunkCount, empty);

                LOG_APPEND(TimedLog::LOG_INFO, "- uploaded %u quest index entries in %u chunks, hash %s",
                    total, chunkCount, indexHash.c_str());
            }
            catch (const std::exception& e)
            {
                LOG_APPEND(TimedLog::LOG_ERROR, "- quest index generation failed safely: %s", e.what());
                index.setEnabled(false);
                return;
            }
            catch (...)
            {
                LOG_APPEND(TimedLog::LOG_ERROR, "- quest index generation failed safely with an unknown exception");
                index.setEnabled(false);
                return;
            }
        }
    };
}

#endif //OPENMW_PROCESSORPLAYERQUESTINDEX_HPP
