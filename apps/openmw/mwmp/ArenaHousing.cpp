#include "ArenaHousing.hpp"
#include "GUIController.hpp"

#include <algorithm>
#include <cstdlib>
#include <sstream>
#include <vector>

#include <components/esm/loadappa.hpp>
#include <components/esm/loadarmo.hpp>
#include <components/esm/loadbook.hpp>
#include <components/esm/loadclot.hpp>
#include <components/esm/loadcrea.hpp>
#include <components/esm/loaddoor.hpp>
#include <components/esm/loadcont.hpp>
#include <components/esm/loadingr.hpp>
#include <components/esm/loadligh.hpp>
#include <components/esm/loadlock.hpp>
#include <components/esm/loadmisc.hpp>
#include <components/esm/loadnpc.hpp>
#include <components/esm/loadalch.hpp>
#include <components/esm/loadprob.hpp>
#include <components/esm/loadrepa.hpp>
#include <components/esm/loadweap.hpp>
#include <components/misc/stringops.hpp>
#include <components/translation/translation.hpp>
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/TimedLog.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "../mwworld/action.hpp"
#include "../mwworld/actionteleport.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/ptr.hpp"

#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "ObjectList.hpp"
#include "Worldstate.hpp"

namespace
{
    const char* sControlPrefix = "@@AMP_HOUSE@@";

    // Retire unanswered requests without teleporting. WAIT suspends this timer
    // while a server menu is open; CANCEL or an authoritative teleport retires it.
    const float sRequestTimeout = 4.f;

    // One frame is not enough: the cell the player just entered finishes
    // streaming in over the next few. Sweeping too early finds an empty cell
    // and marks it clean.
    const float sPurgeDelay = 0.75f;

    std::string toKey(const std::string& value)
    {
        return Misc::StringUtils::lowerCase(value);
    }

    // The server represents exterior cells as "x, y". The OpenMW UI description
    // contains a region prefix (for example "Seyda Neen (-2, -9)"), so it must
    // never be used as the protocol identity for INTEREST or CELL_STATE.
    std::string networkCellName(const MWWorld::Ptr& player)
    {
        const auto* cell = player.getCell()->getCell();
        if (cell->isExterior())
            return std::to_string(cell->getGridX()) + ", " + std::to_string(cell->getGridY());
        return cell->getDescription();
    }

    std::vector<std::string> splitOn(const std::string& text, char separator)
    {
        std::vector<std::string> fields;
        std::string current;
        std::istringstream stream(text);
        while (std::getline(stream, current, separator))
            fields.push_back(current);
        return fields;
    }

    // Record types that are swept out of a private home: everyone who used to
    // live there, and everything they left lying around.
    //
    // Doors, statics, containers and activators are deliberately absent.
    // Removing them would leave a house with no way out, no walls and no
    // furniture - the instance inherits the original interior precisely so the
    // building itself does not have to be rebuilt.
    bool isSweepable(const MWWorld::Ptr& ptr)
    {
        const std::string& typeName = ptr.getTypeName();
        return typeName == typeid(ESM::NPC).name()
            || typeName == typeid(ESM::Creature).name()
            || typeName == typeid(ESM::Miscellaneous).name()
            || typeName == typeid(ESM::Weapon).name()
            || typeName == typeid(ESM::Book).name()
            || typeName == typeid(ESM::Ingredient).name()
            || typeName == typeid(ESM::Apparatus).name()
            || typeName == typeid(ESM::Clothing).name()
            || typeName == typeid(ESM::Armor).name()
            || typeName == typeid(ESM::Potion).name()
            || typeName == typeid(ESM::Lockpick).name()
            || typeName == typeid(ESM::Probe).name()
            || typeName == typeid(ESM::Repair).name()
            || typeName == typeid(ESM::Light).name();
    }
}

namespace mwmp
{
    ArenaHousing& ArenaHousing::get()
    {
        static ArenaHousing instance;
        return instance;
    }

    void ArenaHousing::requestFurnitureMenu() const
    {
        sendControl("FURNITURE_UI");
    }

    void ArenaHousing::armFurnitureCapture()
    {
        mCaptureFurniturePending = true;
        // The player is free to aim after closing the menu: the NEXT Activate
        // picks one real world reference instead of using the stale GUI ray.
        MWBase::Environment::get().getWindowManager()->messageBox(
            "#{arenamp=furniture.capture.prompt}");
    }

    bool ArenaHousing::consumeFurnitureCapture(const MWWorld::Ptr& target)
    {
        if (!mCaptureFurniturePending) return false;
        if (target.isEmpty() || !target.isInCell() || target.getClass().isDoor()
            || target.getClass().isActor()) return true; // keep armed
        mCaptureFurniturePending = false;
        // Use the Ptr that triggered THIS Activate event. Re-querying the faced
        // object here races the GUI close / crosshair update on Android and was
        // the reason Capture mode often consumed E without selecting anything.
        const std::string refId=target.getCellRef().getRefId();
        if (refId.find_first_of("|\t\r\n")!=std::string::npos) return true;
        const std::string index=std::to_string(target.getCellRef().getRefNum().mIndex)
            +"-"+std::to_string(target.getCellRef().getMpNum());
        sendFurnitureAction("CAPTURE",refId+"|"+index);
        return true;
    }

    void ArenaHousing::captureFacedFurniture() const
    {
        const MWWorld::Ptr object=MWBase::Environment::get().getWorld()->getFacedObject();
        if (object.isEmpty() || !object.isInCell() || object.getClass().isDoor()
            || object.getClass().isActor()) return;
        const std::string refId=object.getCellRef().getRefId();
        if (refId.find_first_of("|\t\r\n")!=std::string::npos) return;
        const std::string index=std::to_string(object.getCellRef().getRefNum().mIndex)
            +"-"+std::to_string(object.getCellRef().getMpNum());
        sendFurnitureAction("CAPTURE",refId+"|"+index);
    }

    void ArenaHousing::sendFurnitureAction(const std::string& verb,const std::string& arg,
        int page,const std::string& subcategory) const
    {
        // UI commands are not general chat and never supply prices or stock.
        if (arg.find_first_of("\r\n\t") != std::string::npos ||
            subcategory.find_first_of("\r\n\t") != std::string::npos ||
            verb.find_first_of("\r\n\t") != std::string::npos) return;
        // Explicit placeholders survive older Lua split() implementations that
        // discard empty fields (INVENTORY/PLACED have no object argument).
        sendControl("FURNITURE_CMD\t"+verb+"\t"+(arg.empty()?"_":arg)+"\t"+
            std::to_string(page)+"\t"+(subcategory.empty()?"*":subcategory));
    }

    void ArenaHousing::reset()
    {
        mPendingOriginalLocked = false;
        mRooms.clear();
        mItemLocks.clear();
        mHasItemWallet = false;
        mOwnsHome = false;
        mPlacementStaff = false;
        mBottomlessHomeStorage = false;
        mCaptureFurniturePending = false;
        mEntries.clear();
        mPendingState.clear();
        mPendingStateSize = 0;
        mInterestCell.clear();
        mInterestDestinations.clear();
        mInterestAcknowledged = false;
        mInterestDelay = 0.f;
        mHasPendingDoor = false;
        mPendingDoorCell.clear();
        mPendingTimeout = 0.f;
        mPendingPurgeCell.clear();
        mPurgeDelay = 0.f;
        mPendingUnlockCell.clear();mUnlockDelay=0.f;
    }

    void ArenaHousing::sendControl(const std::string& payload) const
    {
        LocalPlayer* localPlayer = Main::get().getLocalPlayer();
        Networking* networking = Main::get().getNetworking();
        if (localPlayer == nullptr || networking == nullptr || !localPlayer->isLoggedIn())
            return;

        localPlayer->chatMessage = std::string(sControlPrefix) + payload;
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(localPlayer);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
    }

    namespace
    {
        // The server escapes a hint line by line: a backslash becomes "\\" and
        // the line separator is sent as "\n". Undo both, the same way GUIChat
        // resolves its own control fields, so a name that really does contain a
        // backslash survives the trip.
        std::string unescapeHint(const std::string& value)
        {
            std::string result;
            result.reserve(value.size());
            for (std::size_t i = 0; i < value.size(); ++i)
            {
                if (value[i] != '\\' || i + 1 >= value.size())
                {
                    result.push_back(value[i]);
                    continue;
                }
                const char next = value[++i];
                result.push_back(next == 'n' ? '\n' : next);
            }
            return result;
        }
    }

    bool ArenaHousing::handleControlMessage(const std::string& payload)
    {
        const std::vector<std::string> fields = splitOn(payload, '\t');
        if (fields.empty())
            return false;

        const std::string& verb = fields[0];

        if (verb == "HITEM_WALLET")
        {
            mHasItemWallet = fields.size() > 1 && fields[1] == "1";
            return true;
        }
        if (verb == "HITEM_STATE")
        {
            if (fields.size() >= 5) mItemLocks[toKey(fields[1]) + "\n" + fields[2]] = fields[4] == "1";
            return true;
        }
        if (verb == "HITEM_MOVE")
        {
            auto* world = MWBase::Environment::get().getWorld();
            const MWWorld::Ptr target = world->getFacedObject();
            if (fields.size() >= 4 && !target.isEmpty() && target.isInCell()
                && target.getCell()->getCell()->getDescription() == fields[1]
                && std::to_string(target.getCellRef().getRefNum().mIndex) + "-" + std::to_string(target.getCellRef().getMpNum()) == fields[2]
                && target.getCellRef().getRefId() == fields[3] && ownsPlace(target))
                world->beginPhysicsGrab(target);
            return true;
        }

        if (verb.compare(0,3,"FS_")==0)
        {
            GUIController* gui=Main::get().getGUIController();
            if (gui) gui->receiveFurnitureMessage(payload);
            return true;
        }

        // H018: ChatMessage strings have a 16-bit byte length. A production
        // registry exceeds it; commit only a complete, ordered snapshot.
        if (verb == "STATE_BEGIN")
        {
            mPendingState.clear();
            mPendingStateSize = 0;
            if (fields.size() == 2)
            {
                std::istringstream sizeStream(fields[1]);
                std::size_t size = 0;
                if ((sizeStream >> size) && sizeStream.eof() && size > 0 && size <= 16 * 1024 * 1024)
                    mPendingStateSize = size;
            }
            return true;
        }
        if (verb == "STATE_PART")
        {
            const std::size_t offset = payload.find('\t');
            if (mPendingStateSize != 0 && offset != std::string::npos)
            {
                const std::size_t size = payload.size() - offset - 1;
                if (size <= mPendingStateSize - mPendingState.size())
                    mPendingState.append(payload, offset + 1, size);
                else
                {
                    mPendingState.clear();
                    mPendingStateSize = 0;
                }
            }
            return true;
        }
        if (verb == "STATE_END")
        {
            if (mPendingStateSize != 0 && mPendingState.size() == mPendingStateSize
                && (mPendingState == "STATE" || mPendingState.compare(0, 6, "STATE\t") == 0
                    || mPendingState.compare(0, 11, "CELL_STATE\t") == 0))
                applyState(mPendingState);
            mPendingState.clear();
            mPendingStateSize = 0;
            return true;
        }
        if (verb == "CELL_ACK")
        {
            if (fields.size() == 2 && toKey(unescapeHint(fields[1])) == toKey(mInterestCell))
                mInterestAcknowledged = true;
            return true;
        }
        if (verb == "STATE" || verb == "CELL_STATE")
        {
            mPendingState.clear();
            mPendingStateSize = 0;
            applyState(payload);
            return true;
        }

        if (verb == "EDIT_ROLE")
        {
            mPlacementStaff = fields.size() == 2 && fields[1] == "1";
            return true;
        }
        if (verb == "CANCEL") { cancelDoor(); return true; }
        if (verb == "WAIT") { mPendingTimeout = -1.f; return true; }
        if (verb == "OWN") { mOwnsHome = fields.size() > 1 && fields[1] == "1"; return true; }
        if (verb == "STORAGE")
        {
            mBottomlessHomeStorage = fields.size() > 1 && fields[1] == "1";
            return true;
        }
        if (verb == "ROOMS") { mRooms.clear(); return true; }
        if (verb == "ROOM")
        {
            if (fields.size() >= 4)
            {
                Room room;
                room.title = unescapeHint(fields[2]);
                room.take = fields[3] == "1";
                // Z006: older servers send four fields; then no room is "mine"
                // and ownership behaves exactly as it did before.
                room.mine = fields.size() >= 5 && fields[4] == "1";
                // Y069: sixth field is a narrow book-pickup capability. It is
                // true for owner/co-owner/friend (or when normal take is enabled),
                // while public visitors can still open/read books without taking.
                room.bookTake = fields.size() >= 6 ? fields[5] == "1" : room.take;
                // Y070: seventh field controls house utilities (beds/containers).
                // Old servers fall back to bookTake so trusted friends still work.
                room.utility = fields.size() >= 7 ? fields[6] == "1" : room.bookTake;
                mRooms[toKey(unescapeHint(fields[1]))] = room;
            }
            return true;
        }

        if (verb == "GO")
        {
            // The server looked at this door and decided the ordinary entry is
            // correct after all. Finish the teleport we refused to do.
            if (fields.size() > 1)
                performHeldTeleport(fields[1]);
            else
                performHeldTeleport(mPendingDoorCell);
            return true;
        }

        if (verb == "UNLOCK_HOME")
        {
            if (fields.size()>1)
            {
                mPendingUnlockCell=fields[1];mUnlockDelay=sPurgeDelay;
            }
            return true;
        }
        if (verb == "PURGE")
        {
            if (fields.size() > 1)
            {
                mPendingPurgeCell = fields[1];
                mPurgeDelay = sPurgeDelay;
            }
            return true;
        }

        return false;
    }

    void ArenaHousing::applyState(const std::string& payload)
    {
        // STATE \t <cell>^<status>^<hint> \t <cell>^<status>^<hint> ...
        std::string statePayload = payload;
        if (payload.compare(0, 11, "CELL_STATE\t") == 0)
        {
            const std::size_t endCell = payload.find('\t', 11);
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            if (endCell == std::string::npos || player.isEmpty() || !player.isInCell()
                || !Misc::StringUtils::ciEqual(unescapeHint(payload.substr(11, endCell - 11)),
                    networkCellName(player)))
                return; // A late reply from the previous cell cannot replace this one.
            // CELL_STATE carries the location header, followed directly by
            // records. The shared parser below expects a leading STATE token.
            statePayload = "STATE\t" + payload.substr(endCell + 1);
        }
        const std::vector<std::string> records = splitOn(statePayload, '\t');
        if (records.empty() || records[0] != "STATE") return;
        mEntries.clear();

        for (std::size_t i = 1; i < records.size(); ++i)
        {
            const std::vector<std::string> parts = splitOn(records[i], '^');
            if (parts.size() < 2 || parts[0].empty())
                continue;

            Entry entry;
            // The cell name is escaped on the wire too, and it is matched
            // against the ESM's own spelling, so it has to come back exactly.
            entry.cell = unescapeHint(parts[0]);
            // Z008: the hint is several lines now - the property's name first,
            // then price and what holding the key will do. A control record is
            // still a single line on the wire, so the breaks travel escaped.
            entry.hint = parts.size() > 2 ? unescapeHint(parts[2]) : std::string();
            entry.titleIsCell = parts.size() > 3 ? parts[3] == "celltitle"
                : entry.hint.substr(0, entry.hint.find('\n')) == entry.cell;

            entry.originalCell = parts.size() > 5 && parts[5] == "original";
            entry.trusted = parts.size() > 4 ? parts[4] == "1" : parts[1] == "mine";

            if (parts[1] == "locked")
                entry.status = Status_Locked;
            else if (parts[1] == "free")
                entry.status = Status_Free;
            else if (parts[1] == "mine")
                entry.status = Status_Mine;
            else if (parts[1] == "owned")
                entry.status = Status_Owned;
            else
                continue;

            mEntries[toKey(entry.cell)] = entry;
        }

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "ArenaMP housing: %d managed destination(s)",
            static_cast<int>(mEntries.size()));
    }

    bool ArenaHousing::isManaged(const std::string& destinationCell) const
    {
        const Entry* entry = find(destinationCell);
        return entry != nullptr && (entry->status != Status_Locked || entry->originalCell);
    }

    const ArenaHousing::Entry* ArenaHousing::find(const std::string& destinationCell) const
    {
        if (destinationCell.empty() || mEntries.empty())
            return nullptr;
        std::map<std::string, Entry>::const_iterator found = mEntries.find(toKey(destinationCell));
        if (found == mEntries.end())
            return nullptr;
        return &found->second;
    }

    namespace
    {
        // The destination the player would actually reach, i.e. after the
        // server's destination overrides have been applied. Housing decisions
        // must be made about that cell, not the one authored in the ESM.
        std::string resolveDestination(const MWWorld::ConstPtr& door)
        {
            if (door.isEmpty() || !door.getCellRef().getTeleport())
                return std::string();

            std::string destination = door.getCellRef().getDestCell();
            if (destination.empty())
                return std::string();

            Worldstate* worldstate = Main::get().getNetworking()->getWorldstate();
            if (worldstate != nullptr && worldstate->destinationOverrides.count(destination) != 0)
                destination = worldstate->destinationOverrides[destination];

            return destination;
        }
    }

    bool ArenaHousing::isManagedDoor(const MWWorld::ConstPtr& door) const
    {
        return isManaged(resolveDestination(door));
    }

    std::string ArenaHousing::hintForDoor(const MWWorld::ConstPtr& door) const
    {
        const Entry* entry = find(resolveDestination(door));
        if (entry == nullptr)
            return std::string();
        std::string hint = entry->hint;
        if (entry->titleIsCell)
        {
            const auto end = hint.find('\n');
            const std::string title = hint.substr(0, end);
            const auto& translations = MWBase::Environment::get().getWindowManager()->getTranslationDataStorage();
            hint.replace(0, title.size(), translations.translateCellName(title));
        }
        return hint;
    }

    bool ArenaHousing::canBypassDoorLock(const MWWorld::ConstPtr& door) const
    {
        const Entry* entry = find(resolveDestination(door));
        return entry != nullptr && entry->trusted;
    }

    bool ArenaHousing::requestForDoor(const MWWorld::ConstPtr& door, bool hold, bool originalKeyUsed)
    {
        const std::string destination = resolveDestination(door);
        if (destination.empty())
            return false;

        LocalPlayer* localPlayer = Main::get().getLocalPlayer();
        if (localPlayer == nullptr || !localPlayer->isLoggedIn())
            return false;

        if (mHasPendingDoor) return true;

        // Remember where this door was going. If the server approves the plain
        // entry, this is what gets used. A timeout cancels the request.
        mPendingOriginalLocked = door.getCellRef().getLockLevel() > 0
            && !originalKeyUsed && !canBypassDoorLock(door);
        mPendingDoorCell = destination;
        mPendingDoorDest = door.getCellRef().getDoorDest();
        mHasPendingDoor = true;
        mPendingTimeout = sRequestTimeout;

        const ESM::Position& dest = mPendingDoorDest;
        std::ostringstream payload;
        payload << "DOOR\t" << destination
                << '\t' << (hold ? '1' : '0')
                << '\t' << dest.pos[0]
                << '\t' << dest.pos[1]
                << '\t' << dest.pos[2]
                << '\t' << dest.rot[0]
                << '\t' << dest.rot[2]
                << '\t' << (mPendingOriginalLocked ? '1' : '0');

        sendControl(payload.str());
        return true;
    }

    void ArenaHousing::performHeldTeleport(const std::string& destinationCell)
    {
        if (!mHasPendingDoor)
            return;

        // Only complete the teleport the player actually asked for. A stale
        // "GO" for another door must not move anybody.
        if (!destinationCell.empty()
            && !Misc::StringUtils::ciEqual(destinationCell, mPendingDoorCell))
            return;

        // A server decision to enter the original must never bypass its ESP lock.
        // Private entry uses an authoritative cell packet and CANCEL instead.
        if (mPendingOriginalLocked)
        {
            cancelDoor();
            MWBase::Environment::get().getWindowManager()->messageBox("#{sLockLevel}");
            return;
        }
        const std::string cell = mPendingDoorCell;
        const ESM::Position dest = mPendingDoorDest;
        mHasPendingDoor = false;
        mPendingDoorCell.clear();
        mPendingTimeout = 0.f;

        // update() runs from the GUI controller, which keeps ticking through
        // loading screens and the main menu. Never reach for the player there.
        if (MWBase::Environment::get().getWorld() == nullptr)
            return;

        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty())
            return;

        MWWorld::ActionTeleport action(cell, dest, true);
        action.execute(player);
    }

    void ArenaHousing::cancelDoor()
    {
        mPendingOriginalLocked = false;
        mHasPendingDoor = false;
        mPendingDoorCell.clear();
        mPendingTimeout = 0.f;
    }

    std::string ArenaHousing::displayName(const std::string& cell) const
    {
        auto found = mRooms.find(toKey(cell));
        return found == mRooms.end() ? cell : found->second.title;
    }

    bool ArenaHousing::canTakeHere() const
    {
        if (MWBase::Environment::get().getWorld() == nullptr) return true;
        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || !player.isInCell()) return true;
        auto found = mRooms.find(toKey(player.getCell()->getCell()->getDescription()));
        return found == mRooms.end() || found->second.take;
    }

    bool ArenaHousing::canUseHouseUtility(const MWWorld::ConstPtr& object) const
    {
        if (object.isEmpty() || !object.isInCell())
            return true;
        const auto found = mRooms.find(toKey(object.getCell()->getCell()->getDescription()));
        return found == mRooms.end() || found->second.utility;
    }

    bool ArenaHousing::isManagedItem(const MWWorld::ConstPtr& object) const
    {
        if (object.isEmpty() || !object.isInCell() || mRooms.find(toKey(object.getCell()->getCell()->getDescription())) == mRooms.end()) return false;
        const auto& type = object.getTypeName();
        const bool portable = type == typeid(ESM::Weapon).name() || type == typeid(ESM::Armor).name()
            || type == typeid(ESM::Clothing).name() || type == typeid(ESM::Potion).name()
            || type == typeid(ESM::Ingredient).name() || type == typeid(ESM::Book).name()
            || type == typeid(ESM::Apparatus).name() || type == typeid(ESM::Lockpick).name()
            || type == typeid(ESM::Probe).name() || type == typeid(ESM::Repair).name()
            || type == typeid(ESM::Miscellaneous).name();
        if (type == typeid(ESM::Light).name())
            return (object.get<ESM::Light>()->mBase->mData.mFlags & ESM::Light::Carry) != 0;
        return portable;
    }

    bool ArenaHousing::itemLocked(const MWWorld::ConstPtr& object) const
    {
        if (!isManagedItem(object)) return false;
        const auto k = toKey(object.getCell()->getCell()->getDescription()) + "\n"
            + std::to_string(object.getCellRef().getRefNum().mIndex) + "-" + std::to_string(object.getCellRef().getMpNum());
        const auto found = mItemLocks.find(k);
        return found != mItemLocks.end() && found->second;
    }

    void ArenaHousing::requestHouseItem(const std::string& verb, const MWWorld::ConstPtr& object) const
    {
        if (!isManagedItem(object)) return;
        const auto id = object.getCellRef().getRefId();
        if (id.find_first_of("\r\n\t") != std::string::npos) return;
        sendControl("HITEM\t" + verb + "\t" + std::to_string(object.getCellRef().getRefNum().mIndex)
            + "-" + std::to_string(object.getCellRef().getMpNum()) + "\t" + id);
    }

    void ArenaHousing::requestItemWallet() const { sendControl("HITEM\tWALLET\t_\t_"); }

    bool ArenaHousing::canTake(const MWWorld::Ptr& object) const
    {
        if (object.isEmpty() || !object.isInCell()) return true;

        auto found = mRooms.find(toKey(object.getCell()->getCell()->getDescription()));
        if (isManagedItem(object))
        {
            // Y069: managed loose items normally use the server transaction, but
            // BOOK/scroll windows have their own native Take button. Let trusted
            // house members use that button; the server still validates the
            // ObjectDelete and persists the tombstone. Visitors keep read-only UI.
            if (object.getTypeName() == typeid(ESM::Book).name())
                return found == mRooms.end() || (found->second.bookTake && !itemLocked(object));
            return false;
        }
        return found == mRooms.end() || found->second.take;
    }

    bool ArenaHousing::ownsPlace(const MWWorld::ConstPtr& object) const
    {
        if (mRooms.empty() || object.isEmpty() || !object.isInCell())
            return false;
        const auto found = mRooms.find(toKey(object.getCell()->getCell()->getDescription()));
        return found != mRooms.end() && found->second.mine;
    }

    bool ArenaHousing::isBottomlessHomeContainer(const MWWorld::ConstPtr& object) const
    {
        return mBottomlessHomeStorage && !object.isEmpty() && object.isInCell()
            && object.getTypeName() == typeid(ESM::Container).name() && ownsPlace(object);
    }

    void ArenaHousing::update(float dt)
    {
        // Discover destinations from the actual loaded cell, including doors the
        // server has never observed. Requests only select public registry hints;
        // they neither register property nor grant entry/editing permissions.
        LocalPlayer* localPlayer = Main::get().getLocalPlayer();
        if (localPlayer && localPlayer->isLoggedIn())
        {
            const MWWorld::Ptr player = MWMechanics::getPlayer();
            if (!player.isEmpty() && player.isInCell())
            {
                const std::string cell = networkCellName(player);
                if (!Misc::StringUtils::ciEqual(cell, mInterestCell))
                {
                    mInterestCell = cell;
                    mInterestDestinations.clear();
                    mInterestAcknowledged = false;
                    mInterestDelay = 0.f;
                    mEntries.clear();
                }
                mInterestDelay -= dt;
                if (mInterestDelay <= 0.f)
                {
                    std::set<std::string> destinations;
                    auto collect = [&destinations](const MWWorld::Ptr& door)
                    {
                        const std::string destination = resolveDestination(door);
                        if (!destination.empty() && destination.size() <= 512
                            && destination.find_first_of("\t\r\n") == std::string::npos)
                            destinations.insert(destination);
                        return true;
                    };
                    if (!mInterestAcknowledged)
                        player.getCell()->forEachType<ESM::Door>(collect);
                    // At exterior borders the faced door may belong to a neighbour.
                    const MWWorld::Ptr faced = MWBase::Environment::get().getWorld()->getFacedObject();
                    if (!faced.isEmpty() && faced.getClass().isDoor()) collect(faced);
                    std::string request = "INTEREST\t" + cell;
                    const std::string prefix = request;
                    bool changed = !mInterestAcknowledged;
                    std::size_t requestCount = 0;
                    for (const std::string& destination : destinations)
                    {
                        if (mInterestDestinations.size() >= 512 && !mInterestDestinations.count(destination)) continue;
                        if (mInterestAcknowledged && mInterestDestinations.count(destination)) continue;
                        if (request.size() + destination.size() + 1 > 12000 || requestCount >= 128)
                        {
                            sendControl(request);
                            request = prefix;
                            requestCount = 0;
                        }
                        request += '\t' + destination;
                        ++requestCount;
                        mInterestDestinations.insert(destination);
                        changed = true;
                    }
                    if (changed) sendControl(request);
                    mInterestDelay = 0.75f;
                }
            }
        }
        if (mHasPendingDoor && mPendingTimeout >= 0.f)
        {
            mPendingTimeout -= dt;
            if (mPendingTimeout <= 0.f)
            {
                // A timeout must never execute a stale original-cell teleport.
                LOG_MESSAGE_SIMPLE(TimedLog::LOG_WARN,
                    "ArenaMP housing: no server answer for door to %s; request cancelled",
                    mPendingDoorCell.c_str());
                cancelDoor();
            }
        }

        if (!mPendingUnlockCell.empty())
        {
            mUnlockDelay-=dt;
            if (mUnlockDelay<=0.f)
            {
                const std::string cell=mPendingUnlockCell;
                mPendingUnlockCell.clear();
                unlockPrivateContainers(cell);
            }
        }
        if (!mPendingPurgeCell.empty())
        {
            mPurgeDelay -= dt;
            if (mPurgeDelay <= 0.f)
            {
                const std::string cell = mPendingPurgeCell;
                mPendingPurgeCell.clear();
                purgeCell(cell);
            }
        }
    }

    void ArenaHousing::unlockPrivateContainers(const std::string& cellDescription)
    {
        MWWorld::Ptr player=MWMechanics::getPlayer();
        if (player.isEmpty() || !player.getCell() || !player.getCell()->getCell() ||
            !Misc::StringUtils::ciEqual(player.getCell()->getCell()->getDescription(),cellDescription)) return;
        std::vector<MWWorld::Ptr> locked;
        player.getCell()->forEach([&locked](MWWorld::Ptr ptr)
        {
            if (!ptr.isEmpty() && ptr.getRefData().getCount()>0 &&
                ptr.getTypeName()==typeid(ESM::Container).name() &&
                ptr.getCellRef().getLockLevel()>0) locked.push_back(ptr);
            return true;
        });
        ObjectList* list=Main::get().getNetworking()->getObjectList();
        for (const MWWorld::Ptr& ptr : locked)
        {
            ptr.getCellRef().unlock();
            list->reset();list->packetOrigin=mwmp::CLIENT_GAMEPLAY;
            list->addObjectLock(ptr,0);list->sendObjectLock();
        }
    }

    void ArenaHousing::purgeCell(const std::string& cellDescription)
    {
        if (MWBase::Environment::get().getWorld() == nullptr)
            return;

        MWWorld::Ptr player = MWMechanics::getPlayer();
        if (player.isEmpty() || player.getCell() == nullptr)
            return;

        MWWorld::CellStore* cellStore = player.getCell();
        if (cellStore->getCell() == nullptr)
            return;

        // Only sweep the cell the player is actually standing in. A delayed
        // purge that arrives after the player has walked out again must not
        // strip whatever cell they walked into.
        if (!Misc::StringUtils::ciEqual(cellStore->getCell()->getDescription(), cellDescription))
            return;

        // Collect first, delete afterwards: CellStore::forEach explicitly
        // forbids adding or removing references while it is iterating.
        std::vector<MWWorld::Ptr> doomed;
        std::vector<MWWorld::Ptr> lockedFixtures;
        cellStore->forEach([&doomed, &lockedFixtures](MWWorld::Ptr ptr)
        {
            if (ptr.isEmpty() || ptr.getRefData().getCount() <= 0)
                return true;
            if ((ptr.getTypeName()==typeid(ESM::Door).name() ||
                ptr.getTypeName()==typeid(ESM::Container).name()) &&
                ptr.getCellRef().getLockLevel()>0)
                lockedFixtures.push_back(ptr);
            if (ptr.getCellRef().getMpNum() != 0 || ptr.getCellRef().getRefNum().mIndex == 0 || !isSweepable(ptr))
                return true;
            // A reference the player has already interacted with may carry a
            // server-side unique index; deleting it is still correct, the
            // server simply records the delete against that index.
            doomed.push_back(ptr);
            return true;
        });

        // Unlock the PRIVATE home's door references only. This does not mutate
        // the original cell and never deletes a teleport or its script.
        ObjectList* objectList = Main::get().getNetworking()->getObjectList();
        for (const MWWorld::Ptr& fixture : lockedFixtures)
        {
            fixture.getCellRef().unlock();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectLock(fixture, 0);
            objectList->sendObjectLock();
        }

        if (doomed.empty())
        {
            sendControl("PURGED\t" + cellDescription + "\t0");
            return;
        }

        for (const MWWorld::Ptr& ptr : doomed)
        {
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectGeneric(ptr);
            objectList->sendObjectDelete();
        }

        LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO,
            "ArenaMP housing: swept %d reference(s) out of %s",
            static_cast<int>(doomed.size()), cellDescription.c_str());

        sendControl("PURGED\t" + cellDescription + "\t" + std::to_string(doomed.size()));
    }
}
