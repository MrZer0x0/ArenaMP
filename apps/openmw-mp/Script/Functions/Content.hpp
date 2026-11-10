#ifndef ARENAMP_CONTENT_API_HPP
#define ARENAMP_CONTENT_API_HPP

#define CONTENTCATALOGAPI \
    SCRIPT_API_ENTRY("ContentCatalogGetVersion", ContentFunctions::GetVersion),\
    SCRIPT_API_ENTRY("ContentCatalogBegin", ContentFunctions::Begin),\
    SCRIPT_API_ENTRY("ContentCatalogAddFile", ContentFunctions::AddFile),\
    SCRIPT_API_ENTRY("ContentCatalogCommit", ContentFunctions::Commit),\
    SCRIPT_API_ENTRY("ContentCatalogGetError", ContentFunctions::GetError),\
    SCRIPT_API_ENTRY("ContentCatalogGetInfo", ContentFunctions::GetInfo),\
    SCRIPT_API_ENTRY("ContentCatalogGetRecord", ContentFunctions::GetRecord),\
    SCRIPT_API_ENTRY("ContentCatalogGetSubrecord", ContentFunctions::GetSubrecord),\
    SCRIPT_API_ENTRY("ContentCatalogGetSubrecordChunk", ContentFunctions::GetSubrecordChunk),\
    SCRIPT_API_ENTRY("ContentCatalogSearch", ContentFunctions::Search),\
    SCRIPT_API_ENTRY("ContentCatalogGetRecordCount", ContentFunctions::GetRecordCount),\
    SCRIPT_API_ENTRY("ContentCatalogNormalizeId", ContentFunctions::NormalizeId),\
    SCRIPT_API_ENTRY("ContentCatalogGetCellReference", ContentFunctions::GetCellReference)

// Keep the complete native export list. The nine-argument animation function
// uses a dedicated Lua stack binding because bundled LuaBridge supports eight.
#define CONTENTAPI CONTENTCATALOGAPI, \
    SCRIPT_API_ENTRY("SendContentActorAnimation", ContentFunctions::SendActorAnimation)

class ContentFunctions
{
public:
    static unsigned int GetVersion() noexcept;
    static bool Begin(const char* encoding, const char* expectedKey) noexcept;
    static bool AddFile(const char* path) noexcept;
    static bool Commit() noexcept;
    static const char* GetError() noexcept;
    static const char* GetInfo() noexcept;
    static const char* GetRecord(const char* type, const char* id) noexcept;
    static const char* GetSubrecord(const char* type, const char* id, const char* tag,
        unsigned int occurrence, const char* format) noexcept;
    static const char* GetSubrecordChunk(const char* type, const char* id, const char* tag,
        unsigned int occurrence, unsigned int offset, unsigned int limit) noexcept;
    static const char* Search(const char* type, const char* prefix, unsigned int offset, unsigned int limit) noexcept;
    static unsigned int GetRecordCount(const char* type) noexcept;
    static const char* NormalizeId(const char* id) noexcept;
    static const char* GetCellReference(const char* cell, unsigned int number, const char* refId) noexcept;
    static bool SendActorAnimation(unsigned short pid, const char* cell, unsigned int refNum,
        unsigned int mpNum, const char* refId, const char* group, unsigned int mode,
        unsigned int count, bool persist) noexcept;
};
#endif
