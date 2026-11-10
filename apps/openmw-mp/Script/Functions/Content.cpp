#include "Content.hpp"
#include "../../content/ContentCatalog.hpp"

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <extern/PicoSHA2/picosha2.h>
#include <components/to_utf8/to_utf8.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <apps/openmw-mp/CellController.hpp>
#include <apps/openmw-mp/Cell.hpp>
#include <apps/openmw-mp/Networking.hpp>
#include <apps/openmw-mp/Player.hpp>
#include <apps/openmw-mp/Utils.hpp>

namespace
{
    ArenaContent::Catalog catalog;
    std::vector<std::string> pendingFiles;
    std::string pendingEncoding, pendingKey, lastError, result;
    ArenaContent::Catalog::Decode activeDecode;
    bool building = false;
    const char* safe(const char* s) { return s ? s : ""; }
    ArenaContent::Catalog::Decode decoder(const std::string& encoding)
    {
        if (encoding == "utf8") return [](const std::string& s) {
            for (std::size_t i=0;i<s.size();)
            {
                const auto c=static_cast<unsigned char>(s[i++]);
                if (c<0x80) continue;
                unsigned int trailing=0;
                if (c>=0xc2 && c<=0xdf) trailing=1;
                else if (c>=0xe0 && c<=0xef) trailing=2;
                else if (c>=0xf0 && c<=0xf4) trailing=3;
                else throw std::runtime_error("invalid UTF-8 plugin text");
                if (s.size()-i<trailing) throw std::runtime_error("truncated UTF-8 plugin text");
                const auto next=static_cast<unsigned char>(s[i]);
                if ((c==0xe0 && next<0xa0) || (c==0xed && next>0x9f)
                    || (c==0xf0 && next<0x90) || (c==0xf4 && next>0x8f)) throw std::runtime_error("invalid UTF-8 scalar");
                while (trailing--) if ((static_cast<unsigned char>(s[i++])&0xc0)!=0x80) throw std::runtime_error("invalid UTF-8 continuation");
            }
            return s;
        };
        auto encoder = std::make_shared<ToUTF8::Utf8Encoder>(ToUTF8::calculateEncoding(encoding));
        return [encoder](const std::string& s) { return encoder->getUtf8(s); };
    }
}

unsigned int ContentFunctions::GetVersion() noexcept { return 1; }
bool ContentFunctions::Begin(const char* encoding, const char* expectedKey) noexcept
{
    try
    {
        decoder(safe(encoding));pendingEncoding=safe(encoding);pendingKey=safe(expectedKey);
        if (!pendingKey.empty() && (pendingKey.size()!=64 || pendingKey.find_first_not_of("0123456789abcdef")!=std::string::npos))
            throw std::runtime_error("expectedKey must be a lowercase SHA-256 hex digest");
        pendingFiles.clear();building=true;lastError.clear();return true;
    }
    catch (const std::exception& e) {building=false;lastError=e.what();return false;}
}
bool ContentFunctions::AddFile(const char* path) noexcept
{
    try
    {
        if (!building) throw std::runtime_error("ContentCatalogBegin required");
        if (!path || !*path || pendingFiles.size()>=1024) throw std::runtime_error("invalid content path/file count");
        pendingFiles.emplace_back(path);lastError.clear();return true;
    }
    catch (const std::exception& e) {lastError=e.what();return false;}
}
bool ContentFunctions::Commit() noexcept
{
    try
    {
        if (!building) throw std::runtime_error("ContentCatalogBegin required");
        const auto decode=decoder(pendingEncoding);
        catalog.load(pendingFiles,pendingEncoding,decode,
            [](const std::string& bytes) {return picosha2::hash256_hex_string(bytes);},pendingKey);
        activeDecode=decode;building=false;pendingFiles.clear();lastError.clear();return true;
    }
    catch (const std::exception& e) {building=false;lastError=e.what();return false;}
}
const char* ContentFunctions::GetError() noexcept { return lastError.c_str(); }
const char* ContentFunctions::GetInfo() noexcept
{
    try {result=catalog.infoJson();lastError.clear();return result.c_str();}
    catch (const std::exception& e) {lastError=e.what();return "null";}
}
const char* ContentFunctions::GetRecord(const char* type,const char* id) noexcept
{
    try
    {
        result=catalog.recordJson(safe(type),safe(id));
        if (result.size()>4u*1024u*1024u) throw std::runtime_error("record_response_too_large");
        lastError.clear();return result.c_str();
    }
    catch (const std::exception& e) {lastError=e.what();return "null";}
}
const char* ContentFunctions::GetCellReference(const char* cell,unsigned int number,const char* refId) noexcept
{
    try {result=catalog.referenceJson(safe(cell),number,safe(refId));lastError.clear();return result.c_str();}
    catch (const std::exception& e) {lastError=e.what();return "null";}
}
const char* ContentFunctions::GetSubrecord(const char* type,const char* id,const char* tag,
    unsigned int occurrence,const char* format) noexcept
{
    try
    {
        if (!activeDecode) throw std::runtime_error("catalog_not_loaded");
        result=catalog.subrecord(safe(type),safe(id),safe(tag),occurrence,safe(format),activeDecode);
        lastError.clear();return result.c_str();
    }
    catch (const std::exception& e) {lastError=e.what();return "";}
}
const char* ContentFunctions::Search(const char* type,const char* prefix,unsigned int offset,unsigned int limit) noexcept
{
    try
    {
        result="[";bool comma=false;
        for (const auto& id : catalog.ids(safe(type),safe(prefix),offset,limit))
        {if (comma) result+=',';comma=true;result+=ArenaContent::Catalog::quote(id);}
        result+=']';lastError.clear();return result.c_str();
    }
    catch (const std::exception& e) {lastError=e.what();return "[]";}
}
const char* ContentFunctions::GetSubrecordChunk(const char* type,const char* id,const char* tag,
    unsigned int occurrence,unsigned int offset,unsigned int limit) noexcept
{
    try
    {
        result=catalog.subrecordChunk(safe(type),safe(id),safe(tag),occurrence,offset,limit);
        lastError.clear();return result.c_str();
    }
    catch (const std::exception& e) {lastError=e.what();return "null";}
}
unsigned int ContentFunctions::GetRecordCount(const char* type) noexcept
{
    try {lastError.clear();return static_cast<unsigned int>(catalog.count(safe(type)));}
    catch (const std::exception& e) {lastError=e.what();return 0;}
}
const char* ContentFunctions::NormalizeId(const char* id) noexcept
{
    try {result=ArenaContent::Catalog::fold(safe(id));lastError.clear();return result.c_str();}
    catch (const std::exception& e) {lastError=e.what();return "";}
}

bool ContentFunctions::SendActorAnimation(unsigned short pid,const char* cell,unsigned int refNum,
    unsigned int mpNum,const char* refId,const char* group,unsigned int mode,unsigned int count,bool persist) noexcept
{
    try
    {
        auto* player=Players::getPlayer(pid);
        const std::string name=safe(group);
        if (!player || !cell || !*cell || (!refNum && !mpNum) || !refId || !*refId
            || name.empty() || name.size()>128 || name.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 _-")!=std::string::npos
            || mode>2 || count<1 || count>1000) throw std::runtime_error("invalid_animation_arguments");
        auto esmCell=Utils::getCellFromDescription(cell);
        auto* loaded=CellController::get()->getCell(&esmCell);
        if (!loaded || !loaded->containsActor(static_cast<int>(refNum),static_cast<int>(mpNum)))
            throw std::runtime_error("actor_not_loaded");
        const auto* actor=loaded->getActor(static_cast<int>(refNum),static_cast<int>(mpNum));
        if (!actor || ArenaContent::Catalog::fold(actor->refId)!=ArenaContent::Catalog::fold(refId))
            throw std::runtime_error("actor_identity_mismatch");
        const auto players=loaded->getPlayers();
        if (std::find(players.begin(),players.end(),player)==players.end()) throw std::runtime_error("recipient_has_no_cell");
        // Stable storage: packet controllers retain the pointer after Send().
        static mwmp::BaseActorList list;
        // BaseActorList's legacy constructor leaves action/isValid uninitialized;
        // do not copy a temporary containing those indeterminate members.
        list.baseActors.clear();list.cell=esmCell;list.guid=player->guid;
        list.count=0;list.action=mwmp::BaseActorList::SET;list.isValid=true;
        mwmp::BaseActor entry;entry.refId=refId;entry.refNum=refNum;entry.mpNum=mpNum;
        entry.animation.groupname=name;entry.animation.mode=mode;entry.animation.count=count;entry.animation.persist=persist;
        list.baseActors.push_back(entry);list.count=1;
        auto* packet=mwmp::Networking::get().getActorPacketController()->GetPacket(ID_ACTOR_ANIM_PLAY);
        packet->setActorList(&list);packet->Send(player->guid);lastError.clear();return true;
    }
    catch (const std::exception& e) {lastError=e.what();return false;}
}
