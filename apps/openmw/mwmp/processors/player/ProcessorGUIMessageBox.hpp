#ifndef OPENMW_PROCESSORGUIMESSAGEBOX_HPP
#define OPENMW_PROCESSORGUIMESSAGEBOX_HPP


#include "../PlayerProcessor.hpp"

#include "../../ServerQuestRegistry.hpp"
#include "../../GUIController.hpp"
#include "../../../mwbase/environment.hpp"
#include "../../../mwbase/windowmanager.hpp"
#include "../../../mwgui/dialogue.hpp"
#include "../../../mwworld/cellstore.hpp"
#include <components/misc/stringops.hpp>

#include <utility>
#include <vector>

namespace mwmp
{
    class ProcessorGUIMessageBox final: public PlayerProcessor
    {
    public:
        ProcessorGUIMessageBox()
        {
            BPP_INIT(ID_GUI_MESSAGEBOX)
        }

        virtual void Do(PlayerPacket &packet, BasePlayer *player)
        {
            if (isLocal())
            {
                // X036: reuse the already reliable GUI packet as a hidden server-quest
                // transport. This avoids another wire-format change while keeping quest
                // definitions and dialogue responses server-authoritative.
                if (player->guiMessageBox.id == ServerQuestRegistry::TransportGuiId)
                {
                    ServerQuestResponse response;
                    const ServerQuestRegistry::TransportEvent event
                        = ServerQuestRegistry::get().handleTransport(player->guiMessageBox.label, &response);

                    MWGui::DialogueWindow* dialogueWindow
                        = MWBase::Environment::get().getWindowManager()->getDialogueWindow();
                    if (dialogueWindow != nullptr)
                    {
                        if (event == ServerQuestRegistry::TransportEvent::Response && response.valid)
                        {
                            const MWWorld::Ptr actor = dialogueWindow->getPtr();
                            if (!actor.isEmpty() && actor.getCell() != nullptr
                                && Misc::StringUtils::ciEqual(actor.getCellRef().getRefId(), response.giverRefId)
                                && ServerQuestRegistry::cellsMatch(
                                    response.cell, actor.getCell()->getCell()->getDescription()))
                            {
                                std::vector<std::pair<std::string, std::string>> choices;
                                choices.reserve(response.choices.size());
                                for (const ServerQuestChoice& choice : response.choices)
                                    choices.push_back(std::make_pair(choice.id, choice.text));
                                dialogueWindow->addServerQuestResponse(
                                    response.questId, response.topicId, response.text, choices);
                            }
                        }
                        else if (event == ServerQuestRegistry::TransportEvent::SyncComplete)
                            dialogueWindow->refreshServerQuestTopics();
                    }
                    if (event == ServerQuestRegistry::TransportEvent::EditorSyncComplete)
                        Main::get().getGUIController()->showServerQuestEditor();
                    return;
                }

                LOG_MESSAGE_SIMPLE(TimedLog::LOG_INFO, "ID_GUI_MESSAGEBOX, Type %d, MSG %s", player->guiMessageBox.type,
                                   player->guiMessageBox.label.c_str());

                switch(player->guiMessageBox.type)
                {
                    case BasePlayer::GUIMessageBox::MessageBox:
                        Main::get().getGUIController()->showMessageBox(player->guiMessageBox);
                        break;
                    case BasePlayer::GUIMessageBox::CustomMessageBox:
                        Main::get().getGUIController()->showCustomMessageBox(player->guiMessageBox);
                        break;
                    case BasePlayer::GUIMessageBox::InputDialog:
                    case BasePlayer::GUIMessageBox::PasswordDialog:
                        Main::get().getGUIController()->showInputBox(player->guiMessageBox);
                        break;
                    case BasePlayer::GUIMessageBox::ListBox:
                        Main::get().getGUIController()->showDialogList(player->guiMessageBox);
                        break;
                }
            }
        }
    };
}


#endif //OPENMW_PROCESSORGUIMESSAGEBOX_HPP
