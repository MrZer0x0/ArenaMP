#ifndef OPENMW_QUESTINDEXAPI_HPP
#define OPENMW_QUESTINDEXAPI_HPP

#include "../Types.hpp"

#define QUESTINDEXAPI \
    SCRIPT_API_ENTRY("GetQuestIndexStage", QuestIndexFunctions::GetQuestIndexStage),\
    SCRIPT_API_ENTRY("GetQuestIndexMode", QuestIndexFunctions::GetQuestIndexMode),\
    SCRIPT_API_ENTRY("GetQuestIndexContentKey", QuestIndexFunctions::GetQuestIndexContentKey),\
    SCRIPT_API_ENTRY("GetQuestIndexHash", QuestIndexFunctions::GetQuestIndexHash),\
    SCRIPT_API_ENTRY("GetQuestIndexEntryCount", QuestIndexFunctions::GetQuestIndexEntryCount),\
    SCRIPT_API_ENTRY("GetQuestIndexChunkIndex", QuestIndexFunctions::GetQuestIndexChunkIndex),\
    SCRIPT_API_ENTRY("GetQuestIndexChunkCount", QuestIndexFunctions::GetQuestIndexChunkCount),\
    SCRIPT_API_ENTRY("GetQuestIndexChunkSize", QuestIndexFunctions::GetQuestIndexChunkSize),\
    SCRIPT_API_ENTRY("GetQuestIndexChunkEntry", QuestIndexFunctions::GetQuestIndexChunkEntry),\
    \
    SCRIPT_API_ENTRY("SendQuestIndexRequest", QuestIndexFunctions::SendQuestIndexRequest)

class QuestIndexFunctions
{
public:

    /**
     * \brief Get the stage of the last quest index packet received from a player.
     *
     * 1 - handshake, 2 - chunk, 3 - end of upload.
     *
     * \param pid The player ID.
     * \return The stage.
     */
    static unsigned int GetQuestIndexStage(unsigned short pid) noexcept;

    /**
     * \brief Get the mode the player is answering in.
     *
     * 0 - off, 1 - verify only, 2 - full upload.
     *
     * \param pid The player ID.
     * \return The mode.
     */
    static unsigned int GetQuestIndexMode(unsigned short pid) noexcept;

    /**
     * \brief Get the hash identifying the player's ordered content file list.
     *
     * \param pid The player ID.
     * \return The content key.
     */
    static const char *GetQuestIndexContentKey(unsigned short pid) noexcept;

    /**
     * \brief Get the hash the player declares for its whole index.
     *
     * This is only a declaration. The server recomputes it over the received
     * payload before accepting an upload.
     *
     * \param pid The player ID.
     * \return The declared index hash.
     */
    static const char *GetQuestIndexHash(unsigned short pid) noexcept;

    /**
     * \brief Get the total number of entries the player says its index has.
     *
     * \param pid The player ID.
     * \return The entry count.
     */
    static unsigned int GetQuestIndexEntryCount(unsigned short pid) noexcept;

    /**
     * \brief Get the index of the chunk in the last received packet.
     *
     * \param pid The player ID.
     * \return The chunk index.
     */
    static unsigned int GetQuestIndexChunkIndex(unsigned short pid) noexcept;

    /**
     * \brief Get the total number of chunks in the player's upload.
     *
     * \param pid The player ID.
     * \return The chunk count.
     */
    static unsigned int GetQuestIndexChunkCount(unsigned short pid) noexcept;

    /**
     * \brief Get the number of entries in the last received chunk.
     *
     * \param pid The player ID.
     * \return The chunk size.
     */
    static unsigned int GetQuestIndexChunkSize(unsigned short pid) noexcept;

    /**
     * \brief Get an entry from the last received chunk.
     *
     * \param pid The player ID.
     * \param index The index of the entry in the chunk.
     * \return The item record id.
     */
    static const char *GetQuestIndexChunkEntry(unsigned short pid, unsigned int index) noexcept;

    /**
     * \brief Ask a player to act as a quest index oracle.
     *
     * \param pid The player ID.
     * \param mode 0 to switch the client's classifier off, 1 to ask only for a
     *             handshake, 2 to ask for the full index.
     * \return void
     */
    static void SendQuestIndexRequest(unsigned short pid, unsigned int mode) noexcept;
};

#endif //OPENMW_QUESTINDEXAPI_HPP
