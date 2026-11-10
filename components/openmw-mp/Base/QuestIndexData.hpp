#ifndef OPENMW_QUESTINDEXDATA_HPP
#define OPENMW_QUESTINDEXDATA_HPP

#include <string>
#include <vector>

namespace mwmp
{
    /// ArenaMP X013: payload of ID_PLAYER_QUEST_INDEX.
    ///
    /// The quest item classification is derived from the loaded ESM/ESP set, which
    /// only the client can read. To keep the server authoritative anyway, clients
    /// act as oracles: they upload the derived index, the server verifies it,
    /// requires agreement from several independent uploads (or one staff upload),
    /// and from then on classifies items itself. Until an index has been accepted
    /// the server behaves exactly like vanilla - phasing stays off (fail-closed).
    struct QuestIndexData
    {
        enum Stage : unsigned char
        {
            STAGE_REQUEST = 0,   // server -> client: "send me this"
            STAGE_HANDSHAKE = 1, // client -> server: content key + index hash + size
            STAGE_CHUNK = 2,     // client -> server: a slice of the entry list
            STAGE_END = 3,       // client -> server: upload finished
            STAGE_INVALID = 255  // set on malformed input, never sent
        };

        enum Mode : unsigned char
        {
            MODE_OFF = 0,    // do nothing, do not even build the index
            MODE_VERIFY = 1, // build and send only the handshake
            MODE_UPLOAD = 2  // build and send the whole index
        };

        // constexpr, not const: these are ODR-used from the packet and both
        // processors, and C++17 makes constexpr static members implicitly inline.
        static constexpr unsigned int sMaxEntries = 100000;
        static constexpr unsigned int sMaxChunks = 8192;
        static constexpr unsigned int sMaxChunkEntries = 128;
        static constexpr unsigned int sMaxEntryLength = 128;
        static constexpr unsigned int sMaxKeyLength = 32;

        unsigned char stage = STAGE_REQUEST;
        unsigned char mode = MODE_OFF;
        std::string contentKey;
        std::string indexHash;
        unsigned int entryCount = 0;
        unsigned int chunkIndex = 0;
        unsigned int chunkCount = 0;
        unsigned int chunkSize = 0;
        std::vector<std::string> entries;
    };
}

#endif //OPENMW_QUESTINDEXDATA_HPP
