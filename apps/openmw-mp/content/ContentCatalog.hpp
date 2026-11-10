#ifndef ARENAMP_CONTENT_CATALOG_HPP
#define ARENAMP_CONTENT_CATALOG_HPP

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ArenaContent
{
    struct Subrecord { std::string tag, bytes; };
    struct InventoryItem { std::string refId; std::int32_t count = 0; };
    struct Record
    {
        std::string type, id, source, parent;
        unsigned int pluginIndex = 0;
        std::uint32_t flags = 0;
        bool deleted = false;
        std::map<std::string, std::string> text;
        std::map<std::string, double> data;
        std::vector<InventoryItem> inventory;
        std::vector<int> slots;
        std::vector<std::string> spells;
        std::vector<std::map<std::string, double>> effects;
        std::vector<Subrecord> subrecords;
    };
    struct Plugin
    {
        std::string name, sha256, author, description;
        double version=0;
        std::uint32_t fileType=0, declaredRecords=0;
        int format=0;
        std::size_t sizeBytes=0;
        std::vector<std::string> masters;
    };
    struct CellReference
    {
        std::uint32_t number = 0;
        std::string refId, owner, faction, destination;
        double x=0, y=0, z=0, destinationX=0, destinationY=0;
        bool deleted=false, positioned=false, teleport=false;
    };

    // No graphics, world state or client trust is required by this reader.
    // Digest must be SHA-256 in production; injected to reuse the server's PicoSHA2.
    class Catalog
    {
    public:
        using Digest = std::function<std::string(const std::string&)>;
        using Decode = std::function<std::string(const std::string&)>;
        static constexpr std::size_t MaxFileBytes = 256u * 1024u * 1024u;
        static constexpr std::size_t MaxRecordBytes = 16u * 1024u * 1024u;
        static constexpr std::size_t MaxCatalogBytes = 512u * 1024u * 1024u;

        // Whole reload is transactional. A failure leaves the old catalog intact.
        void load(const std::vector<std::string>& paths, const std::string& encoding,
            const Decode& decode, const Digest& digest, const std::string& expectedKey = "");
        const Record* find(const std::string& type, const std::string& id) const;
        std::vector<std::string> ids(const std::string& type, const std::string& prefix,
            unsigned int offset, unsigned int limit) const;
        std::size_t count(const std::string& type = "") const;
        std::string recordJson(const std::string& type, const std::string& id) const;
        std::string referenceJson(const std::string& cell, unsigned int number, const std::string& refId) const;
        std::string infoJson() const;
        std::string subrecord(const std::string& type, const std::string& id,
            const std::string& tag, unsigned int occurrence, const std::string& format,
            const Decode& decode) const;
        std::string subrecordChunk(const std::string& type, const std::string& id,
            const std::string& tag, unsigned int occurrence, unsigned int offset, unsigned int limit) const;
        const std::string& key() const { return mKey; }
        static std::string fold(std::string text);
        static std::string quote(const std::string& text);
    private:
        std::map<std::pair<std::string, std::string>, Record> mRecords;
        std::map<std::string, std::map<std::uint64_t, CellReference>> mCellReferences;
        std::vector<Plugin> mPlugins;
        std::map<std::string, std::size_t> mCounts;
        std::size_t mCount=0;
        std::string mKey, mEncoding;
        unsigned int mUnindexed = 0;
    };
}
#endif
