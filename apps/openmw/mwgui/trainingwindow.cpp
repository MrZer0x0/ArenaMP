#include "trainingwindow.hpp"
#include "../mwmechanics/xpleveling.hpp"

#include <MyGUI_Gui.h>
#include <MyGUI_LanguageManager.h>

#include <algorithm>
#include <cstdlib>
#include <map>
#include <sstream>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/LocalPlayer.hpp"
#include "../mwmp/ObjectList.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/windowmanager.hpp"
#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/mechanicsmanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include <components/settings/settings.hpp>

#include "tooltips.hpp"

namespace
{
// Sorts a container descending by skill value. If skill value is equal, sorts ascending by skill ID.
// pair <skill ID, skill value>
bool sortSkills (const std::pair<int, int>& left, const std::pair<int, int>& right)
{
    if (left == right)
        return false;

    if (left.second > right.second)
        return true;
    else if (left.second < right.second)
        return false;

    return left.first < right.first;
}

struct ServerTrainingState
{
    int count = 0;
    int limit = 3;
    unsigned int revision = 0;
    bool ready = false;
};

std::map<std::string, ServerTrainingState> sServerTrainingStates;
unsigned int sServerTrainingRevision = 0;

std::string arenaTrainingText(const std::string& key)
{
    return MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=" + key + "}");
}
}

namespace MWGui
{

    TrainingWindow::TrainingWindow()
        : WindowBase("openmw_trainingwindow.layout")
        , mTrainingStateRevision(0)
        , mTimeAdvancer(0.05f)
    {
        getWidget(mTrainingOptions, "TrainingOptions");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mPlayerGold, "PlayerGold");
        getWidget(mTrainingCounter, "TrainingCounter");

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TrainingWindow::onCancelButtonClicked);

        mTimeAdvancer.eventProgressChanged += MyGUI::newDelegate(this, &TrainingWindow::onTrainingProgressChanged);
        mTimeAdvancer.eventFinished += MyGUI::newDelegate(this, &TrainingWindow::onTrainingFinished);
    }

    void TrainingWindow::onOpen()
    {
        if (mTimeAdvancer.isRunning())
        {
            mProgressBar.setVisible(true);
            setVisible(false);
        }
        else
            mProgressBar.setVisible(false);

        center();
    }

    void TrainingWindow::setServerTrainingState(const std::string& trainerKey, int count, int limit)
    {
        if (trainerKey.empty())
            return;
        ServerTrainingState& state = sServerTrainingStates[trainerKey];
        state.count = std::max(0, count);
        state.limit = std::max(1, limit);
        state.ready = true;
        state.revision = ++sServerTrainingRevision;
    }

    std::string TrainingWindow::makeTrainerKey(const MWWorld::Ptr& actor) const
    {
        if (actor.isEmpty())
            return std::string();
        std::ostringstream stream;
        stream << actor.getCellRef().getRefId() << '|'
               << actor.getCellRef().getRefNum().mIndex << '|'
               << actor.getCellRef().getMpNum();
        return stream.str();
    }

    void TrainingWindow::sendTrainingControl(const std::string& action, int skillId)
    {
        if (mTrainerKey.empty() || !mwmp::Main::isInitialized())
            return;
        mwmp::LocalPlayer* localPlayer = mwmp::Main::get().getLocalPlayer();
        mwmp::Networking* networking = mwmp::Main::get().getNetworking();
        if (!localPlayer || !networking)
            return;

        // Y092-SP: successful trainer USE includes the exact trained skill so the
        // server anti-cheat can exempt that legitimate +1 from the SP budget.
        localPlayer->chatMessage = "@@AMP_TRAIN@@" + action + "\t" + mTrainerKey;
        if (skillId >= 0)
            localPlayer->chatMessage += "\t" + std::to_string(skillId);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(localPlayer);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
    }

    void TrainingWindow::refreshTrainingCounter()
    {
        if (!mTrainingCounter)
            return;
        auto it = sServerTrainingStates.find(mTrainerKey);
        const bool ready = it != sServerTrainingStates.end() && it->second.ready;
        if (!ready)
        {
            mTrainingCounter->setCaption(arenaTrainingText("training.limit.counter") + ": "
                + arenaTrainingText("training.limit.loading") + " / 3");
            mTrainingCounter->setTextColour(MyGUI::Colour::White);
            return;
        }
        const ServerTrainingState& state = it->second;
        mTrainingCounter->setCaption(arenaTrainingText("training.limit.counter") + ": "
            + MyGUI::utility::toString(state.count) + " / " + MyGUI::utility::toString(state.limit));
        mTrainingCounter->setTextColour(state.count >= state.limit
            ? MyGUI::Colour(1.f, 0.25f, 0.25f) : MyGUI::Colour::White);
        mTrainingStateRevision = state.revision;
    }

    void TrainingWindow::setPtr (const MWWorld::Ptr& actor)
    {
        mPtr = actor;
        mTrainerKey = makeTrainerKey(actor);
        mTrainingStateRevision = 0;
        if (!mTrainerKey.empty())
        {
            ServerTrainingState& pending = sServerTrainingStates[mTrainerKey];
            pending.ready = false;
            pending.revision = ++sServerTrainingRevision;
        }
        refreshTrainingCounter();
        sendTrainingControl("QUERY");

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);

        mPlayerGold->setCaptionWithReplacing("#{sGold}: " + MyGUI::utility::toString(playerGold));

        // NPC can train you in his best 3 skills
        std::vector< std::pair<int, float> > skills;

        MWMechanics::NpcStats const& actorStats(actor.getClass().getNpcStats(actor));
        for (int i=0; i<ESM::Skill::Length; ++i)
        {
            float value = getSkillForTraining(actorStats, i);

            skills.emplace_back(i, value);
        }

        std::sort(skills.begin(), skills.end(), sortSkills);

        MyGUI::EnumeratorWidgetPtr widgets = mTrainingOptions->getEnumerator ();
        MyGUI::Gui::getInstance ().destroyWidgets (widgets);

        MWMechanics::NpcStats& pcStats = player.getClass().getNpcStats (player);

        const MWWorld::Store<ESM::GameSetting> &gmst =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();

        for (int i=0; i<3; ++i)
        {
            // EncoreMP non linear pricing position two

            int price = static_cast<int>(pcStats.getSkill(skills[i].first).getBase());

            int baseskillforprice = static_cast<int>(pcStats.getSkill(skills[i].first).getBase());

            int priceaddition = 10;

            if (baseskillforprice > 20)
            {
                int baseover20 = (baseskillforprice - 20);
                priceaddition += baseover20;
            }

            if (baseskillforprice > 30)
            {
                int baseover30 = (baseskillforprice - 30);
                baseover30 *= 3;
                priceaddition += baseover30;
            }

            if (baseskillforprice > 50)
            {
                int baseover50 = (baseskillforprice - 50);
                baseover50 *= 10;
                priceaddition += baseover50;
            }

            if (baseskillforprice > 70)
            {
                int baseover70 = (baseskillforprice - 70);
                baseover70 *= 5;
                priceaddition += baseover70;
            }

            if (baseskillforprice > 80)
            {
                int baseover80 = (baseskillforprice - 80);
                baseover80 *= 5;
                priceaddition += baseover80;
            }

            if (baseskillforprice > 90)
            {
                int baseover90 = (baseskillforprice - 80);
                baseover90 *= 35;
                priceaddition += baseover90;
            }

            price += priceaddition;

            price *= gmst.find("iTrainingMod")->mValue.getInteger();

            price = std::max(1, price);

            price = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, price, true, true, "training");

            MyGUI::Button* button = mTrainingOptions->createWidget<MyGUI::Button>(price <= playerGold ? "SandTextButton" : "SandTextButtonDisabled", // can't use setEnabled since that removes tooltip
                MyGUI::IntCoord(5, 5+i*18, mTrainingOptions->getWidth()-10, 18), MyGUI::Align::Default);

            button->setUserData(skills[i].first);
            button->eventMouseButtonClick += MyGUI::newDelegate(this, &TrainingWindow::onTrainingSelected);

            button->setCaptionWithReplacing("#{" + ESM::Skill::sSkillNameIds[skills[i].first] + "} - " + MyGUI::utility::toString(price));

            button->setSize(button->getTextSize ().width+12, button->getSize().height);

            ToolTips::createSkillToolTip (button, skills[i].first);
        }

        center();
    }

    void TrainingWindow::onReferenceUnavailable ()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Training);
    }

    void TrainingWindow::onCancelButtonClicked (MyGUI::Widget *sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Training);
    }

    void TrainingWindow::onTrainingSelected (MyGUI::Widget *sender)
    {
        auto trainingState = sServerTrainingStates.find(mTrainerKey);
        if (trainingState == sServerTrainingStates.end() || !trainingState->second.ready)
        {
            MWBase::Environment::get().getWindowManager()->messageBox(
                arenaTrainingText("training.limit.loading"));
            sendTrainingControl("QUERY");
            return;
        }
        if (trainingState->second.count >= trainingState->second.limit)
        {
            MWBase::Environment::get().getWindowManager()->messageBox(
                arenaTrainingText("training.limit.reached"));
            return;
        }

        int skillId = *sender->getUserData<int>();

        MWWorld::Ptr player = MWBase::Environment::get().getWorld ()->getPlayerPtr();
        MWMechanics::NpcStats& pcStats = player.getClass().getNpcStats (player);

        const MWWorld::ESMStore &store =
            MWBase::Environment::get().getWorld()->getStore();

        // EncoreMP non linear pricing position one

        int price = pcStats.getSkill(skillId).getBase();

        int baseskillforprice = pcStats.getSkill(skillId).getBase();

        int priceaddition = 10;

        if (baseskillforprice > 20)
        {
            int baseover20 = (baseskillforprice - 20);
            priceaddition += baseover20;
        }

        if (baseskillforprice > 30)
        {
            int baseover30 = (baseskillforprice - 30);
            baseover30 *= 3;
            priceaddition += baseover30;
        }

        if (baseskillforprice > 50)
        {
            int baseover50 = (baseskillforprice - 50);
            baseover50 *= 10;
            priceaddition += baseover50;
        }

        if (baseskillforprice > 70)
        {
            int baseover70 = (baseskillforprice - 70);
            baseover70 *= 5;
            priceaddition += baseover70;
        }

        if (baseskillforprice > 80)
        {
            int baseover80 = (baseskillforprice - 80);
            baseover80 *= 5;
            priceaddition += baseover80;
        }

        if (baseskillforprice > 90)
        {
            int baseover90 = (baseskillforprice - 80);
            baseover90 *= 35;
            priceaddition += baseover90;
        }

        price += priceaddition;

        
        price *= store.get<ESM::GameSetting>().find("iTrainingMod")->mValue.getInteger();
     


        price = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, price, true, true, "training");

        if (price > player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId))
            return;

        if (getSkillForTraining(mPtr.getClass().getNpcStats(mPtr), skillId) <= pcStats.getSkill(skillId).getBase())
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sServiceTrainingWords}");
            return;
        }

        // You can not train a skill above its governing attribute
        const ESM::Skill* skill = MWBase::Environment::get().getWorld()->getStore().get<ESM::Skill>().find(skillId);
        if (pcStats.getSkill(skillId).getBase() >= pcStats.getAttribute(skill->mData.mAttribute).getBase())
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage17}");
            return;
        }

        // increase skill
        MWWorld::LiveCellRef<ESM::NPC> *playerRef = player.get<ESM::NPC>();

        const ESM::Class *class_ =
            store.get<ESM::Class>().find(playerRef->mBase->mClass);
        const float skillBefore = pcStats.getSkill(skillId).getBase();
        pcStats.increaseSkill (skillId, *class_, true);
        if (pcStats.getSkill(skillId).getBase() <= skillBefore)
            return;
        MWMechanics::XPLeveling::applySkillAttributeProgress(player, skillId);

        // remove gold
        player.getClass().getContainerStore(player).remove(MWWorld::ContainerStore::sGoldId, price, player);

        // add gold to NPC trading gold pool
        MWMechanics::NpcStats& npcStats = mPtr.getClass().getNpcStats(mPtr);

        /*
            Start of tes3mp change (major)

            Don't unilaterally change the merchant's gold pool on our client and instead let the server do it
        */
        //npcStats.setGoldPool(npcStats.getGoldPool() + price);

        mwmp::ObjectList* objectList = mwmp::Main::get().getNetworking()->getObjectList();
        objectList->reset();
        objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
        objectList->addObjectMiscellaneous(mPtr, npcStats.getGoldPool() + price, npcStats.getLastRestockTime().getHour(),
            npcStats.getLastRestockTime().getDay());
        objectList->sendObjectMiscellaneous();
        /*
            End of tes3mp change (major)
        */

        // Y054: one successful training consumes one runtime slot for this exact
        // NPC. Update locally first to close the reopen-before-reply race, then
        // let the server return its authoritative counter.
        setServerTrainingState(mTrainerKey, trainingState->second.count + 1, trainingState->second.limit);
        refreshTrainingCounter();
        sendTrainingControl("USE", skillId);

        // advance time
        MWBase::Environment::get().getMechanicsManager()->rest(2, false);

        /*
            Start of tes3mp change (major)

            Multiplayer requires that time not get advanced here
        */
        //MWBase::Environment::get().getWorld ()->advanceTime (2);
        /*
            End of tes3mp change (major)
        */

        setVisible(false);
        mProgressBar.setVisible(true);
        mProgressBar.setProgress(0, 2);
        mTimeAdvancer.run(2);

        MWBase::Environment::get().getWindowManager()->fadeScreenOut(0.25);
        MWBase::Environment::get().getWindowManager()->fadeScreenIn(0.25, false, 0.25);
    }

    void TrainingWindow::onTrainingProgressChanged(int cur, int total)
    {
        mProgressBar.setProgress(cur, total);
    }

    void TrainingWindow::onTrainingFinished()
    {
        mProgressBar.setVisible(false);

        // go back to game mode
        MWBase::Environment::get().getWindowManager()->removeGuiMode (GM_Training);
        MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
    }

    float TrainingWindow::getSkillForTraining(const MWMechanics::NpcStats& stats, int skillId) const
    {
        // Refined Alchemy/anti-exploit compatibility: Fortify/Drain Skill must
        // never alter trainer capacity or the trainer's top-three skill list.
        return stats.getSkill(skillId).getBase();
    }

    void TrainingWindow::onFrame(float dt)
    {
        checkReferenceAvailable();
        auto state = sServerTrainingStates.find(mTrainerKey);
        if (state != sServerTrainingStates.end() && state->second.revision != mTrainingStateRevision)
            refreshTrainingCounter();
        mTimeAdvancer.onFrame(dt);
    }

    bool TrainingWindow::exit()
    {
        return !mTimeAdvancer.isRunning();
    }

}
