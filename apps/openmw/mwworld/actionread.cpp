#include "actionread.hpp"

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwmechanics/npcstats.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/xpleveling.hpp"

#include "player.hpp"
#include "class.hpp"
#include "esmstore.hpp"

namespace MWWorld
{
    ActionRead::ActionRead (const MWWorld::Ptr& object) : Action (false, object)
    {
    }

    void ActionRead::executeImp (const MWWorld::Ptr& actor) {

        if (actor != MWMechanics::getPlayer())
            return;

        //Ensure we're not in combat
        if(MWMechanics::isPlayerInCombat()
                // Reading in combat is still allowed if the scroll/book is not in the player inventory yet
                // (since otherwise, there would be no way to pick it up)
                && getTarget().getContainerStore() == &actor.getClass().getContainerStore(actor)
                ) {
            MWBase::Environment::get().getWindowManager()->messageBox("#{sInventoryMessage4}");
            return;
        }

        LiveCellRef<ESM::Book> *ref = getTarget().get<ESM::Book>();

        if (ref->mBase->mData.mIsScroll)
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Scroll, getTarget());
            //Encore, when the new flag is added to the quick keys chain, here fork the logic to if flag is true, return
            //return;
        else
            MWBase::Environment::get().getWindowManager()->pushGuiMode(MWGui::GM_Book, getTarget());

        MWMechanics::NpcStats& npcStats = actor.getClass().getNpcStats(actor);
        const bool firstRead = !npcStats.hasBeenUsed(ref->mBase->mId);
        const bool skillBook = ref->mBase->mData.mSkillId >= 0
            && ref->mBase->mData.mSkillId < ESM::Skill::Length;

        if (firstRead && MWMechanics::XPLeveling::isEnabled())
            MWMechanics::XPLeveling::awardBookRead(actor, *ref->mBase);

        // Skill gain from books remains a one-time effect.
        if (skillBook && firstRead)
        {
            MWWorld::LiveCellRef<ESM::NPC>* playerRef = actor.get<ESM::NPC>();
            const ESM::Class* class_ =
                MWBase::Environment::get().getWorld()->getStore().get<ESM::Class>().find(
                    playerRef->mBase->mClass);

            npcStats.increaseSkill(ref->mBase->mData.mSkillId, *class_, true, true);
        }

        // Bookworm: ID_PLAYER_BOOK already has server-profile persistence in
        // BasePlayer.data.books. Extend it from skill books to every ordinary
        // first-read book; no new packet or protocol revision is required.
        const bool trackRead = firstRead && (!ref->mBase->mData.mIsScroll || skillBook);
        if (trackRead)
        {
            npcStats.flagAsUsed(ref->mBase->mId);
            mwmp::Main::get().getLocalPlayer()->sendBook(ref->mBase->mId);
        }

    }
}
