#ifndef OPENMW_PROCESSORCLIENTSCRIPTLOCAL_HPP
#define OPENMW_PROCESSORCLIENTSCRIPTLOCAL_HPP

#include "BaseObjectProcessor.hpp"

namespace mwmp
{
    class ProcessorClientScriptLocal final: public BaseObjectProcessor
    {
    public:
        ProcessorClientScriptLocal()
        {
            BPP_INIT(ID_CLIENT_SCRIPT_LOCAL)
        }

        virtual void Do(ObjectPacket &packet, ObjectList &objectList)
        {
            BaseObjectProcessor::Do(packet, objectList);

            ptrCellStore = Main::get().getCellController()->getCellStore(objectList.cell);

            /*
                Start of AMP change

                Do not bail out when the cell is not loaded here. This packet can now carry
                locals belonging to scripts running on our own character, which have to be
                applied on login, before we have that cell - or any cell - loaded
            */
            bool hasPlayerObject = false;

            for (const auto &baseObject : objectList.baseObjects)
            {
                if (baseObject.isPlayer)
                {
                    hasPlayerObject = true;
                    break;
                }
            }

            if (!ptrCellStore && !hasPlayerObject) return;
            /*
                End of AMP change
            */

            objectList.setClientLocals(ptrCellStore);
        }
    };
}

#endif //OPENMW_PROCESSORCLIENTSCRIPTLOCAL_HPP
