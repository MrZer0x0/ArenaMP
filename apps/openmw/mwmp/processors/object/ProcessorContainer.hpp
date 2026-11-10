#ifndef OPENMW_PROCESSORCONTAINER_HPP
#define OPENMW_PROCESSORCONTAINER_HPP

#include "BaseObjectProcessor.hpp"

namespace mwmp
{
    class ProcessorContainer final: public BaseObjectProcessor
    {
    public:
        ProcessorContainer()
        {
            BPP_INIT(ID_CONTAINER)
        }

        virtual void Do(ObjectPacket &packet, ObjectList &objectList)
        {
            BaseObjectProcessor::Do(packet, objectList);

            ptrCellStore = Main::get().getCellController()->getCellStore(objectList.cell);

            if (!ptrCellStore) return;

            std::string debugMessage = "- action ";
            unsigned char action = objectList.action;
            unsigned char containerSubAction = objectList.containerSubAction;

            if (action == mwmp::BaseObjectList::SET)
                debugMessage += "SET";
            else if (action == mwmp::BaseObjectList::ADD)
                debugMessage += "ADD";
            else if (action == mwmp::BaseObjectList::REMOVE)
                debugMessage += "REMOVE";
            else if (action == mwmp::BaseObjectList::REQUEST)
                debugMessage += "REQUEST";

            debugMessage += " and subaction ";

            if (containerSubAction == mwmp::BaseObjectList::NONE)
                debugMessage += "NONE";
            else if (containerSubAction == mwmp::BaseObjectList::DRAG)
                debugMessage += "DRAG";
            else if (containerSubAction == mwmp::BaseObjectList::DROP)
                debugMessage += "DROP";
            else if (containerSubAction == mwmp::BaseObjectList::TAKE_ALL)
                debugMessage += "TAKE_ALL";
            else if (containerSubAction == mwmp::BaseObjectList::REPLY_TO_REQUEST)
                debugMessage += "REPLY_TO_REQUEST";

            LOG_APPEND(TimedLog::LOG_VERBOSE, "%s", debugMessage.c_str());

            // If we've received a request for information, comply with it
            if (objectList.action == mwmp::BaseObjectList::REQUEST)
            {
                if (objectList.baseObjectCount == 0)
                {
                    /*
                        Start of AMP change (X048)

                        Serializing every container of a cell is expensive, and an exterior
                        transition requests up to nine cells at once. Queue the reply and let
                        Main::updateWorld() send one cell per frame instead of stalling here.
                    */
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Request had no objects attached, so we are queueing all containers in the cell %s",
                        objectList.cell.getShortDescription().c_str());
                    ObjectList::queueCellContainerRequest(*ptrCellStore->getCell());
                    /*
                        End of AMP change (X048)
                    */
                }
                else
                {
                    LOG_APPEND(TimedLog::LOG_VERBOSE, "- Request was for %i %s", objectList.baseObjectCount, objectList.baseObjectCount == 1 ? "object" : "objects");
                    std::vector<BaseObject> requestObjects = objectList.baseObjects;
                    objectList.reset();
                    objectList.cell = *ptrCellStore->getCell();
                    objectList.action = mwmp::BaseObjectList::SET;
                    objectList.containerSubAction = mwmp::BaseObjectList::REPLY_TO_REQUEST;
                    objectList.addRequestedContainers(ptrCellStore, requestObjects);

                    if (objectList.baseObjects.size() > 0)
                        objectList.sendContainer();
                }
            }
            // Otherwise, edit containers based on the information received
            else
            {
                LOG_APPEND(TimedLog::LOG_VERBOSE, "- Editing container contents to match those of packet", objectList.baseObjectCount);
                objectList.editContainers(ptrCellStore);
            }
        }

    };
}

#endif //OPENMW_PROCESSORCONTAINER_HPP
