#include <components/openmw-mp/Base/BaseStructs.hpp>
#include <components/openmw-mp/TimedLog.hpp>
#include <components/misc/stringops.hpp>

#include <map>
#include <string>
#include <utility>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwscript/interpretercontext.hpp"

#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "CellController.hpp"
#include "ObjectList.hpp"

#include "ScriptController.hpp"

unsigned short ScriptController::getPacketOriginFromContextType(unsigned short contextType)
{
    if (contextType == Interpreter::Context::CONSOLE)
        return mwmp::CLIENT_CONSOLE;
    else if (contextType == Interpreter::Context::DIALOGUE)
        return mwmp::CLIENT_DIALOGUE;
    else if (contextType == Interpreter::Context::SCRIPT_LOCAL)
        return mwmp::CLIENT_SCRIPT_LOCAL;
    else if (contextType == Interpreter::Context::SCRIPT_GLOBAL)
        return mwmp::CLIENT_SCRIPT_GLOBAL;

    return mwmp::CLIENT_GAMEPLAY;
}

/*
    Start of AMP addition
*/
float ScriptController::sSyncInterval = 0.5f;

namespace
{
    // Packets carry the cell, the packet origin and the originating script name once per
    // list rather than once per object, so everything queued has to be grouped by all three
    struct GroupKey
    {
        std::string cellDescription;
        unsigned short packetOrigin;
        std::string originScriptName;

        bool operator<(const GroupKey& other) const
        {
            if (cellDescription != other.cellDescription)
                return cellDescription < other.cellDescription;
            if (packetOrigin != other.packetOrigin)
                return packetOrigin < other.packetOrigin;
            return originScriptName < other.originScriptName;
        }
    };

    struct PendingObject
    {
        ESM::Cell cell;

        bool isPlayer = false;
        RakNet::RakNetGUID guid;

        std::string refId = "";
        unsigned int refNum = 0;
        unsigned int mpNum = 0;

        std::string scriptId = "";

        // Keyed on variable type and index so repeated writes to the same variable within
        // one interval collapse into the single value that was current when we flushed
        std::map<std::pair<char, int>, mwmp::ClientVariable> variables;
    };

    std::map<GroupKey, std::map<std::string, PendingObject> > sPending;
    float sTimeSinceFlush = 0.0f;

    std::string getObjectKey(const MWWorld::Ptr& ptr, const std::string& scriptId)
    {
        if (ptr == MWBase::Environment::get().getWorld()->getPlayerPtr())
            return "player/" + Misc::StringUtils::lowerCase(scriptId);

        return std::to_string(ptr.getCellRef().getRefNum().mIndex) + "-" +
            std::to_string(ptr.getCellRef().getMpNum()) + "/" + Misc::StringUtils::lowerCase(scriptId);
    }
}

bool ScriptController::hasAuthorityOverPtr(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty())
        return false;

    if (!mwmp::Main::isInitialized() || !mwmp::Main::get().getLocalPlayer()->isLoggedIn())
        return false;

    // Scripts running on our own character, or on items in our own inventory, are only
    // advanced by us. Items inside NPC inventories and world containers still belong to
    // the authority of their cell; treating every ContainerStore as local authority would
    // let multiple clients advance the same scripted item at once.
    const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();

    if (ptr == player)
        return true;

    if (ptr.getContainerStore() == &player.getClass().getContainerStore(player))
        return true;

    if (ptr.mCell == nullptr)
        return false;

    mwmp::CellController* cellController = mwmp::Main::get().getCellController();

    if (cellController == nullptr)
        return false;

    const ESM::Cell& cell = *ptr.mCell->getCell();

    // If no client has claimed this cell, fall back to the old behaviour of letting
    // everyone report, so that mechanisms in actor-free cells keep working
    if (!cellController->isInitializedCell(cell))
        return true;

    return cellController->hasLocalAuthority(cell);
}

void ScriptController::queueLocalChange(const MWWorld::Ptr& ptr, unsigned short packetOrigin,
    const std::string& originScriptName, int internalIndex, mwmp::VARIABLE_TYPE variableType,
    int intValue, float floatValue)
{
    if (ptr.isEmpty() || internalIndex < 0)
        return;

    if (!mwmp::Main::isInitialized() || !mwmp::Main::get().getLocalPlayer()->isLoggedIn())
        return;

    const bool isPlayer = (ptr == MWBase::Environment::get().getWorld()->getPlayerPtr());

    // Everything we send has to be anchored to a cell. Items inside a container report
    // isInCell() as false but still carry the cell of whatever is holding them
    const MWWorld::CellStore* cellStore = ptr.mCell;

    if (cellStore == nullptr)
        return;

    const std::string scriptId = ptr.getClass().getScript(ptr);

    GroupKey groupKey;
    groupKey.cellDescription = cellStore->getCell()->getShortDescription();
    groupKey.packetOrigin = packetOrigin;
    groupKey.originScriptName = originScriptName;

    const std::string objectKey = getObjectKey(ptr, scriptId);

    PendingObject& pendingObject = sPending[groupKey][objectKey];
    pendingObject.cell = *cellStore->getCell();
    pendingObject.scriptId = scriptId;

    if (isPlayer)
    {
        pendingObject.isPlayer = true;
        pendingObject.guid = mwmp::Main::get().getLocalPlayer()->guid;
    }
    else
    {
        pendingObject.isPlayer = false;
        pendingObject.refId = ptr.getCellRef().getRefId();
        pendingObject.refNum = ptr.getCellRef().getRefNum().mIndex;
        pendingObject.mpNum = ptr.getCellRef().getMpNum();
    }

    mwmp::ClientVariable clientVariable;
    clientVariable.internalIndex = internalIndex;
    clientVariable.variableType = static_cast<char>(variableType);
    clientVariable.intValue = intValue;
    clientVariable.floatValue = floatValue;

    pendingObject.variables[std::make_pair(clientVariable.variableType, internalIndex)] = clientVariable;
}

void ScriptController::dropQueuedLocalChanges(const MWWorld::Ptr& ptr)
{
    if (ptr.isEmpty() || sPending.empty())
        return;

    const std::string objectKey = getObjectKey(ptr, ptr.getClass().getScript(ptr));

    for (auto groupIterator = sPending.begin(); groupIterator != sPending.end(); )
    {
        groupIterator->second.erase(objectKey);

        if (groupIterator->second.empty())
            groupIterator = sPending.erase(groupIterator);
        else
            ++groupIterator;
    }
}

void ScriptController::flushQueuedLocalChanges(float dt, bool force)
{
    sTimeSinceFlush += dt;

    if (!force && sTimeSinceFlush < sSyncInterval)
        return;

    sTimeSinceFlush = 0.0f;

    if (sPending.empty())
        return;

    if (!mwmp::Main::isInitialized() || !mwmp::Main::get().getLocalPlayer()->isLoggedIn())
    {
        sPending.clear();
        return;
    }

    mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();

    for (const auto& group : sPending)
    {
        if (group.second.empty())
            continue;

        objectList->reset();
        objectList->packetOrigin = static_cast<unsigned char>(group.first.packetOrigin);
        objectList->originClientScript = group.first.originScriptName;
        objectList->cell = group.second.begin()->second.cell;

        for (const auto& entry : group.second)
        {
            const PendingObject& pendingObject = entry.second;

            if (pendingObject.variables.empty())
                continue;

            mwmp::BaseObject baseObject;
            baseObject.isPlayer = pendingObject.isPlayer;
            baseObject.guid = pendingObject.guid;
            baseObject.refId = pendingObject.refId;
            baseObject.refNum = pendingObject.refNum;
            baseObject.mpNum = pendingObject.mpNum;
            baseObject.clientScriptId = pendingObject.scriptId;

            for (const auto& variable : pendingObject.variables)
                baseObject.clientLocals.push_back(variable.second);

            objectList->addBaseObject(baseObject);
        }

        if (!objectList->baseObjects.empty())
            objectList->sendClientScriptLocal();
    }

    sPending.clear();
}

void ScriptController::clearQueuedLocalChanges()
{
    sPending.clear();
    sTimeSinceFlush = 0.0f;
}
/*
    End of AMP addition
*/
