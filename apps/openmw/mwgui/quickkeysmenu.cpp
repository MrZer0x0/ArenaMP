#include "quickkeysmenu.hpp"

#include <MyGUI_EditBox.h>
#include <MyGUI_Button.h>
#include <MyGUI_Gui.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_LanguageManager.h>
#include <algorithm>
#include <cmath>
#include <mutex>
#include <string>
#include <utility>
#include <components/esm/loadmgef.hpp>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include <components/esm/esmwriter.hpp>
#include <components/esm/loadbook.hpp>
#include <components/esm/quickkeys.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include <components/openmw-mp/TimedLog.hpp>
#include "../mwmp/Main.hpp"
#include "../mwmp/LocalPlayer.hpp"
/*
    End of tes3mp addition
*/

#include "../mwworld/inventorystore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/player.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwbase/environment.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"

#include "../mwmechanics/spellutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/actorutil.hpp"

#include "itemselection.hpp"
#include "spellview.hpp"
#include "itemwidget.hpp"
#include "sortfilteritemmodel.hpp"


namespace {
    std::mutex quickLabelMutex;
    std::array<std::string, MWGui::QuickBarRules::Columns> quickLabels{};
}
#ifdef __ANDROID__
extern "C" JNIEXPORT jobjectArray JNICALL Java_ui_controls_NativeQuickBar_nativeLabels(JNIEnv* env, jobject)
{
    const jclass stringClass = env->FindClass("java/lang/String");
    if (!stringClass) return nullptr;
    jobjectArray result = env->NewObjectArray(MWGui::QuickBarRules::Columns, stringClass, nullptr);
    env->DeleteLocalRef(stringClass);
    if (!result) return nullptr;
    std::lock_guard<std::mutex> lock(quickLabelMutex);
    for (int i=0; i<MWGui::QuickBarRules::Columns; ++i) {
        // Name is UTF-8; convert through UTF-16 for JNI (not modified UTF-8).
        const MyGUI::UString value(quickLabels[i]);
        std::u16string utf16;
        const auto codepoints=value.asUTF32();
        for (auto point : codepoints) {
            unsigned int cp=point;
            if (cp<=0xffff) utf16.push_back(static_cast<char16_t>(cp));
            else { cp-=0x10000; utf16.push_back(0xd800+(cp>>10)); utf16.push_back(0xdc00+(cp&1023)); }
        }
        jstring label = env->NewString(reinterpret_cast<const jchar*>(utf16.data()), static_cast<jsize>(utf16.size()));
        env->SetObjectArrayElement(result, i, label);
        env->DeleteLocalRef(label);
    }
    return result;
}
#endif

namespace MWGui
{

    QuickKeysMenu::QuickKeysMenu()
        : WindowBase("openmw_quickkeys_menu.layout")
        , mKey(std::vector<keyData>(QuickBarRules::Count))
        , mSelected(nullptr)
        , mActivated(nullptr)
        , mAssignDialog(nullptr)
        , mItemSelectionDialog(nullptr)
        , mMagicSelectionDialog(nullptr)

    {
        getWidget(mOkButton, "OKButton");
        getWidget(mInstructionLabel, "InstructionLabel");

        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &QuickKeysMenu::onOkButtonClicked);
        center();

        MyGUI::Widget* grid;
        getWidget(grid, "QuickKeyGrid");
        for (int row=0; row<QuickBarRules::Rows; ++row)
        {
            mRowLabels[row] = grid->createWidget<MyGUI::TextBox>("SandText",
                MyGUI::IntCoord(0, row*61+22, 66, 24), MyGUI::Align::Default);
            mRowLabels[row]->setNeedMouseFocus(false);
            for (int col=0; col<10; ++col) {
                const int i=row*10+col;
                mKey[i].index = i+1;
                mKey[i].button = grid->createWidget<ItemWidget>("MW_ItemIconBox",
                    MyGUI::IntCoord(68+col*55, row*61, 52, 52), MyGUI::Align::Default);
                mKey[i].button->eventMouseButtonClick += MyGUI::newDelegate(this, &QuickKeysMenu::onQuickKeyButtonClicked);
                unassign(&mKey[i]);
            }
        }
#ifndef __ANDROID__
        mBar = MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>("", MyGUI::IntCoord(0,0,560,82),
            MyGUI::Align::Default, "HUD");
        mBar->setNeedMouseFocus(false);
        for (int col=0; col<10; ++col) {
            mBarItems[col] = mBar->createWidget<ItemWidget>("MW_ItemIconBox",
                MyGUI::IntCoord(col*56,0,52,52), MyGUI::Align::Default);
            mBarItems[col]->setNeedMouseFocus(false);
            mBarLabels[col] = mBar->createWidget<MyGUI::TextBox>("SandText",
                MyGUI::IntCoord(col*56,53,54,24), MyGUI::Align::Default);
            mBarLabels[col]->setTextAlign(MyGUI::Align::Center);
            mBarLabels[col]->setNeedMouseFocus(false);
        }
        mBar->setVisible(false);
#endif
        const auto view = MyGUI::RenderManager::getInstance().getViewSize();
        onResChange(view.width,view.height);

    }

    void QuickKeysMenu::clear()
    {
        mActivated = nullptr;
        mLastColumn = -1; mCurrentRow.fill(0); mBarTime = mBarAlpha = 0.f;
        if (mBar) mBar->setVisible(false);
        { std::lock_guard<std::mutex> lock(quickLabelMutex); quickLabels.fill(""); }

        for (int i=0; i<QuickBarRules::Count; ++i)
        {
            unassign(&mKey[i]);
        }
    }

    QuickKeysMenu::~QuickKeysMenu()
    {
        if (mBar) MyGUI::Gui::getInstance().destroyWidget(mBar);
        delete mAssignDialog;
        delete mItemSelectionDialog;
        delete mMagicSelectionDialog;
    }

    void QuickKeysMenu::onOpen()
    {
        WindowBase::onOpen();
        const auto view = MyGUI::RenderManager::getInstance().getViewSize();
        onResChange(view.width,view.height);
        refreshEditor();

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);

        // Check if quick keys are still valid
        for (int i=0; i<QuickBarRules::Count; ++i)
        {
            switch (mKey[i].type)
            {
                case Type_Unassigned:
                case Type_HandToHand:
                case Type_Magic:
                    break;
                case Type_Item:
                case Type_MagicItem:
                {
                    MWWorld::Ptr item = *mKey[i].button->getUserData<MWWorld::Ptr>();
                    // Make sure the item is available and is not broken
                    if (!item || item.getRefData().getCount() < 1 ||
                        (item.getClass().hasItemHealth(item) &&
                        item.getClass().getItemHealth(item) <= 0))
                    {
                        // Try searching for a compatible replacement
                        item = store.findReplacement(mKey[i].id);

                        if (item)
                            mKey[i].button->setUserData(MWWorld::Ptr(item));

                        break;
                    }
                }
            }
        }
    }

    void QuickKeysMenu::unassign(keyData* key)
    {
        mRefreshTimer = 0.f;
        key->id.clear(); key->name.clear();
        key->button->clearUserStrings();
        key->button->setItem(MWWorld::Ptr());

        while (key->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(key->button->getChildAt(0));

        if (key->index == 10)
        {
            key->type = Type_HandToHand;
            key->name = "#{sSkillHandtohand}";

            MyGUI::ImageBox* image = key->button->createWidget<MyGUI::ImageBox>("ImageBox",
                MyGUI::IntCoord(8, 8, 28, 28), MyGUI::Align::Default);

            image->setImageTexture("icons\\k\\stealth_handtohand.dds");
            image->setNeedMouseFocus(false);
        }
        else
        {
            key->type = Type_Unassigned;
            key->id = "";
            key->name = "";

            MyGUI::TextBox* textBox = key->button->createWidgetReal<MyGUI::TextBox>("SandText",
                MyGUI::FloatCoord(0,0,1,1), MyGUI::Align::Default);

            textBox->setTextAlign(MyGUI::Align::Center);
            const int col=(key->index-1)%10;
            textBox->setCaption(MyGUI::utility::toString(col==9 ? 0 : col+1));
            textBox->setNeedMouseFocus(false);
        }

        /*
            Start of tes3mp addition

            Send a PLAYER_QUICKKEYS packet whenever a key is unassigned, but only if the player
            is logged in on the server, so as to avoid doing anything doing at startup when all
            quick keys get unassigned by default
        */
        if (mwmp::Main::get().getLocalPlayer()->isLoggedIn() && !mwmp::Main::get().getLocalPlayer()->isReceivingQuickKeys)
        {
            mwmp::Main::get().getLocalPlayer()->sendQuickKey(key->index, Type_Unassigned);
        }
        /*
            End of tes3mp addition
        */
    }

    /*
        Start of tes3mp addition

        Allow unassigning an index directly from elsewhere in the code
    */
    void QuickKeysMenu::unassignIndex(int index)
    {
        if (index>=0 && index<QuickBarRules::Count) unassign(&mKey[index]);
    }
    /*
        End of tes3mp addition
    */

    void QuickKeysMenu::onQuickKeyButtonClicked(MyGUI::Widget* sender)
    {
        int index = -1;
        for (int i = 0; i < QuickBarRules::Count; ++i)
        {
            if (sender == mKey[i].button || sender->getParent() == mKey[i].button)
            {
                index = i;
                break;
            }
        }
        assert(index != -1);
        if (index < 0)
        {
            mSelected = nullptr;
            return;
        }

        mSelected = &mKey[index];

        if (index/10 >= unlockedRows()) return;
        mCurrentRow[index%10] = index/10;
        mLastColumn = index%10;
        mBarTime = 3.f;

        // open assign dialog
        if (!mAssignDialog)
            mAssignDialog = new QuickKeysMenuAssign(this);

        mAssignDialog->setVisible(true);
    }

    void QuickKeysMenu::onOkButtonClicked (MyGUI::Widget *sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_QuickKeysMenu);
    }

    void QuickKeysMenu::onItemButtonClicked(MyGUI::Widget* sender)
    {
        if (!mItemSelectionDialog)
        {
            mItemSelectionDialog = new ItemSelectionDialog("#{sQuickMenu6}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &QuickKeysMenu::onAssignItem);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &QuickKeysMenu::onAssignItemCancel);
        }
        mItemSelectionDialog->setVisible(true);
        mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
        mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyUsableItems);

        mAssignDialog->setVisible(false);
    }

    void QuickKeysMenu::onMagicButtonClicked(MyGUI::Widget* sender)
    {
        if (!mMagicSelectionDialog)
        {
            mMagicSelectionDialog = new MagicSelectionDialog(this);
        }
        mMagicSelectionDialog->setVisible(true);

        mAssignDialog->setVisible(false);
    }

    void QuickKeysMenu::onUnassignButtonClicked(MyGUI::Widget* sender)
    {
        unassign(mSelected);
        mAssignDialog->setVisible(false);
    }

    void QuickKeysMenu::onCancelButtonClicked(MyGUI::Widget* sender)
    {
        mAssignDialog->setVisible(false);
    }

    void QuickKeysMenu::onAssignItem(MWWorld::Ptr item)
    {
        if (!mSelected) return;
        mRefreshTimer = 0.f;
        mLastColumn = -1;
        mLastColumn = -1;

        // Enchanted scrolls are castable magic items. Assigning one through the
        // generic item picker must not make its quick key open the reading GUI.
        // Treat it exactly like an item selected from the magic picker.
        if (item.getTypeName() == typeid(ESM::Book).name())
        {
            const MWWorld::LiveCellRef<ESM::Book>* book = item.get<ESM::Book>();
            if (book->mBase->mData.mIsScroll && !book->mBase->mEnchant.empty())
            {
                onAssignMagicItem(item);
                return;
            }
        }

        while (mSelected->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(mSelected->button->getChildAt(0));

        mSelected->type = Type_Item;

        mSelected->id = item.getCellRef().getRefId();
        mSelected->name = item.getClass().getName(item);

        mSelected->button->setItem(item, ItemWidget::Barter);
        mSelected->button->setUserString("ToolTipType", "ItemPtr");
        mSelected->button->setUserData(item);

        if (mItemSelectionDialog)
            mItemSelectionDialog->setVisible(false);

        /*
            Start of tes3mp addition

            Send a PlayerQuickKeys packet whenever a key is assigned to an item
            by a player, not by a packet received from the server
        */
        if (!mwmp::Main::get().getLocalPlayer()->isReceivingQuickKeys)
            mwmp::Main::get().getLocalPlayer()->sendQuickKey(mSelected->index, Type_Item, item.getCellRef().getRefId());
        /*
            End of tes3mp addition
        */
    }

    void QuickKeysMenu::onAssignItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::onAssignMagicItem(MWWorld::Ptr item)
    {
        if (!mSelected) return;
        mRefreshTimer = 0.f;
        mLastColumn = -1;
        mLastColumn = -1;

        while (mSelected->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(mSelected->button->getChildAt(0));

        mSelected->type = Type_MagicItem;
        mSelected->id = item.getCellRef().getRefId();
        mSelected->name = item.getClass().getName(item);

        float scale = 1.f;
        MyGUI::ITexture* texture = MyGUI::RenderManager::getInstance().getTexture("textures\\menu_icon_select_magic_magic.dds");
        if (texture)
            scale = texture->getHeight() / 64.f;

        mSelected->button->setFrame("textures\\menu_icon_select_magic_magic.dds", MyGUI::IntCoord(0, 0, 44*scale, 44*scale));
        mSelected->button->setIcon(item);

        mSelected->button->setUserString("ToolTipType", "ItemPtr");
        mSelected->button->setUserData(MWWorld::Ptr(item));

        if (mMagicSelectionDialog)
            mMagicSelectionDialog->setVisible(false);

        /*
            Start of tes3mp addition

            Send a PLAYER_QUICKKEYS packet whenever a key is assigned to an item's magic
        */
        if (!mwmp::Main::get().getLocalPlayer()->isReceivingQuickKeys)
            mwmp::Main::get().getLocalPlayer()->sendQuickKey(mSelected->index, Type_MagicItem, item.getCellRef().getRefId());
        /*
            End of tes3mp addition
        */
    }

    void QuickKeysMenu::onAssignMagic(const std::string& spellId)
    {
        if (!mSelected) return;
        mRefreshTimer = 0.f;
        mLastColumn = -1;
        while (mSelected->button->getChildCount()) // Destroy number label
            MyGUI::Gui::getInstance().destroyWidget(mSelected->button->getChildAt(0));

        const MWWorld::ESMStore &esmStore = MWBase::Environment::get().getWorld()->getStore();
        const ESM::Spell* spell = esmStore.get<ESM::Spell>().find(spellId);

        mSelected->type = Type_Magic;
        mSelected->id = spellId;
        mSelected->name = spell->mName;

        mSelected->button->setItem(MWWorld::Ptr());
        mSelected->button->setUserString("ToolTipType", "Spell");
        mSelected->button->setUserString("Spell", spellId);

        // use the icon of the first effect
        const ESM::MagicEffect* effect = esmStore.get<ESM::MagicEffect>().find(spell->mEffects.mList.empty() ? ESM::MagicEffect::RestoreHealth : spell->mEffects.mList.front().mEffectID);

        std::string path = effect->mIcon;
        int slashPos = path.rfind('\\');
        path.insert(slashPos+1, "b_");
        path = MWBase::Environment::get().getWindowManager()->correctIconPath(path);

        float scale = 1.f;
        MyGUI::ITexture* texture = MyGUI::RenderManager::getInstance().getTexture("textures\\menu_icon_select_magic.dds");
        if (texture)
            scale = texture->getHeight() / 64.f;

        mSelected->button->setFrame("textures\\menu_icon_select_magic.dds", MyGUI::IntCoord(0, 0, 44*scale, 44*scale));
        mSelected->button->setIcon(path);

        if (mMagicSelectionDialog)
            mMagicSelectionDialog->setVisible(false);

        /*
            Start of tes3mp addition

            Send a PLAYER_QUICKKEYS packet whenever a key is assigned to a spell
        */
        if (!mwmp::Main::get().getLocalPlayer()->isReceivingQuickKeys)
            mwmp::Main::get().getLocalPlayer()->sendQuickKey(mSelected->index, Type_Magic, spellId);
        /*
            End of tes3mp addition
        */
    }

    void QuickKeysMenu::onAssignMagicCancel()
    {
        mMagicSelectionDialog->setVisible(false);
    }

    void QuickKeysMenu::updateActivatedQuickKey()
    {
        // there is no delayed action, nothing to do.
        if (!mActivated)
            return;

        activateStoredKey(mActivated->index);
    }

    void QuickKeysMenu::activateStoredKey(int index)
    {
        if (!QuickBarRules::valid(index) || (index-1)/10 >= unlockedRows()) {
            mActivated = nullptr;
            return;
        }

        keyData *key = &mKey[index-1];

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);
        const MWMechanics::CreatureStats &playerStats = player.getClass().getCreatureStats(player);

        // Delay action executing,
        // if player is busy for now (casting a spell, attacking someone, etc.)
        bool isDelayNeeded = MWBase::Environment::get().getMechanicsManager()->isAttackingOrSpell(player)
                || playerStats.getKnockedDown()
                || playerStats.getHitRecovery();

        bool godmode = MWBase::Environment::get().getWorld()->getGodModeState();
        bool isReturnNeeded = (!godmode && playerStats.isParalyzed()) || playerStats.isDead();

        if (isReturnNeeded && key->type != Type_Item)
        {
            return;
        }
        else if (isDelayNeeded && key->type != Type_Item)
        {
            mActivated = key;
            return;
        }
        else
        {
            mActivated = nullptr;
        }

        if (key->type == Type_Item || key->type == Type_MagicItem)
        {
            MWWorld::Ptr item = *key->button->getUserData<MWWorld::Ptr>();

            MWWorld::ContainerStoreIterator it = store.begin();
            for (; it != store.end(); ++it)
            {
                if (*it == item)
                    break;
            }
            if (it == store.end())
                item = nullptr;

            // check the item is available and not broken
            if (!item || item.getRefData().getCount() < 1 ||
               (item.getClass().hasItemHealth(item) && item.getClass().getItemHealth(item) <= 0))
            {
                item = store.findReplacement(key->id);

                if (!item || item.getRefData().getCount() < 1)
                {
                    MWBase::Environment::get().getWindowManager()->messageBox(
                        "#{sQuickMenu5} " + key->name);

                    return;
                }
            }

            if (key->type == Type_Item)
            {
                // Backward compatibility for saves where an enchanted scroll was
                // already stored as a normal item quick key. Such a scroll must be
                // prepared for casting, not opened for reading.
                if (item.getTypeName() == typeid(ESM::Book).name())
                {
                    const MWWorld::LiveCellRef<ESM::Book>* book = item.get<ESM::Book>();
                    if (book->mBase->mData.mIsScroll && !book->mBase->mEnchant.empty())
                    {
                        mwmp::Main::get().getLocalPlayer()->sendItemUse(
                            item, true, MWMechanics::DrawState_Spell);
                        return;
                    }
                }

                bool isWeapon = item.getTypeName() == typeid(ESM::Weapon).name();
                bool isTool = item.getTypeName() == typeid(ESM::Probe).name() ||
                    item.getTypeName() == typeid(ESM::Lockpick).name();

                // delay weapon switching if player is busy
                if (isDelayNeeded && (isWeapon || isTool))
                {
                    mActivated = key;
                    return;
                }
                else if (isReturnNeeded && (isWeapon || isTool))
                {
                    return;
                }

                /*
                    Start of tes3mp change (major)

                    Instead of unilaterally using an item, send an ID_PLAYER_ITEM_USE packet and let the server
                    decide if the item actually gets used
                */

                /*
                if (!store.isEquipped(item))
                    MWBase::Environment::get().getWindowManager()->useItem(item);
                MWWorld::ConstContainerStoreIterator rightHand = store.getSlot(MWWorld::InventoryStore::Slot_CarriedRight);
                // change draw state only if the item is in player's right hand
                if (rightHand != store.end() && item == *rightHand)
                {
                    MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState_Weapon);
                }
                */

                bool shouldDraw = isWeapon || isTool;
                
                if (!store.isEquipped(item))
                {
                    mwmp::Main::get().getLocalPlayer()->sendItemUse(item, false, shouldDraw ? MWMechanics::DrawState_Weapon : MWMechanics::DrawState_Nothing);
                }
                /*
                    End of tes3mp change (major)
                */
            }
            else if (key->type == Type_MagicItem)
            {
                // equip, if it can be equipped and isn't yet equipped

                /*
                    Start of tes3mp change (major)

                    Instead of unilaterally using an item, send an ID_PLAYER_ITEM_USE packet and let the server
                    decide if the item actually gets used
                */
                /*
                if (!item.getClass().getEquipmentSlots(item).first.empty() && !store.isEquipped(item))
                {
                    MWBase::Environment::get().getWindowManager()->useItem(item);

                    // make sure that item was successfully equipped
                    if (!store.isEquipped(item))
                        return;
                }
                
                store.setSelectedEnchantItem(it);
                MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState_Spell);
                */

                mwmp::Main::get().getLocalPlayer()->sendItemUse(item, true, MWMechanics::DrawState_Spell);
                /*
                    End of tes3mp change (major)
                */
            }
        }
        else if (key->type == Type_Magic)
        {
            std::string spellId = key->id;

            // Make sure the player still has this spell
            MWMechanics::CreatureStats& stats = player.getClass().getCreatureStats(player);
            MWMechanics::Spells& spells = stats.getSpells();

            if (!spells.hasSpell(spellId))
            {
                MWBase::Environment::get().getWindowManager()->messageBox("#{sQuickMenu5} " + key->name);
                return;
            }

            store.setSelectedEnchantItem(store.end());
            MWBase::Environment::get().getWindowManager()
                ->setSelectedSpell(spellId, int(MWMechanics::getSpellSuccessChance(spellId, player)));
            MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState_Spell);

            /*
                Start of tes3mp addition

                Send a PlayerMiscellaneous packet with the player's new selected spell
            */
            mwmp::Main::get().getLocalPlayer()->sendSelectedSpell(spellId);
            /*
                End of tes3mp addition
            */
        }
        else if (key->type == Type_HandToHand)
        {
            store.unequipSlot(MWWorld::InventoryStore::Slot_CarriedRight, player);
            MWBase::Environment::get().getWorld()->getPlayer().setDrawState(MWMechanics::DrawState_Weapon);
        }
    }

    /*
        Start of tes3mp addition

        Make it possible to add quickKeys from elsewhere in the code
    */
    void QuickKeysMenu::setSelectedIndex(int index)
    {
        mSelected = index>=0 && index<QuickBarRules::Count ? &mKey[index] : nullptr;
    }
    /*
        End of tes3mp addition
    */

    void QuickKeysMenu::onResChange(int width, int height)
    {
        const int cell=std::clamp(std::min((width-146)/10,(height-203)/6),20,52);
        const int stride=cell+3, rowStride=cell+9;
        const int panelWidth=68+stride*10+26;
        mMainWidget->setSize(panelWidth,146+rowStride*6);
        mInstructionLabel->setCoord(12,36,panelWidth-24,66);
        mInstructionLabel->setFontHeight(cell<35 ? 12 : 16);
        MyGUI::TextBox* title; getWidget(title,"QuickKeyTitle");
        title->setCoord(12,8,panelWidth-24,24);
        title->setFontHeight(cell<35 ? 12 : 16);
        MyGUI::Widget* grid; getWidget(grid,"QuickKeyGrid");
        grid->setCoord(12,108,panelWidth-24,rowStride*6);
        for (int row=0; row<6; ++row) {
            if (mRowLabels[row]) mRowLabels[row]->setCoord(0,row*rowStride+std::max(0,(cell-18)/2),66,24);
            for (int col=0; col<10; ++col)
                if (mKey[row*10+col].button) mKey[row*10+col].button->setCoord(68+col*stride,row*rowStride,cell,cell);
        }
        mOkButton->setPosition(panelWidth-mOkButton->getWidth()-20,mMainWidget->getHeight()-32);
        center();
    }

    int QuickKeysMenu::unlockedRows() const
    {
        const auto player=MWMechanics::getPlayer();
        return player.isEmpty() ? 1 : QuickBarRules::unlocked(player.getClass().getCreatureStats(player).getLevel());
    }

    void QuickKeysMenu::activateQuickKey(int index)
    {
        if (index<1 || index>10) return;
        const int col=index-1;
        const int slot=QuickBarRules::select(col,mCurrentRow[col],unlockedRows(),mLastColumn==col,
            [this](int i){return mKey[i].type!=Type_Unassigned;});
        mLastColumn=col; mBarTime=3.f; mRefreshTimer=0.f;
        if (slot>=0) { mCurrentRow[col]=slot/10; activateStoredKey(slot+1); }
    }

    void QuickKeysMenu::editQuickKey(int index)
    {
        if (index<1 || index>10) return;
        const int col=index-1;
        mActivated = nullptr;
        auto* wm=MWBase::Environment::get().getWindowManager();
        if (!wm->containsMode(GM_QuickKeysMenu)) wm->pushGuiMode(GM_QuickKeysMenu);
        const int slot=std::clamp(mCurrentRow[col],0,unlockedRows()-1)*10+col;
        onQuickKeyButtonClicked(mKey[slot].button);
    }

    void QuickKeysMenu::refreshEditor()
    {
        const int rows=unlockedRows();
        for (int row=0; row<6; ++row) {
            mRowLabels[row]->setCaption(row==0 ? "1+" : MyGUI::utility::toString(row*30)+"+");
            mRowLabels[row]->setAlpha(row<rows ? 1.f : .3f);
            for (int col=0; col<10; ++col) {
                mKey[row*10+col].button->setEnabled(row<rows);
                mKey[row*10+col].button->setAlpha(row<rows ? 1.f : .22f);
            }
        }
    }

    void QuickKeysMenu::refreshBar()
    {
        const int rows=unlockedRows();
        std::array<std::string,10> labels;
        const auto player=MWMechanics::getPlayer();
        for (int col=0; col<10; ++col) {
            const int row=std::clamp(mCurrentRow[col],0,rows-1);
            const auto& key=mKey[row*10+col];
            const std::string number=MyGUI::utility::toString(col==9 ? 0 : col+1);
            const std::string page=MyGUI::utility::toString(row+1)+"/"+MyGUI::utility::toString(rows);
            labels[col]=number+" "+page+"\n"+MyGUI::LanguageManager::getInstance().replaceTags(key.name).asUTF8();
            if (!mBarItems[col]) continue;
            auto* icon=mBarItems[col];
            icon->setItem(MWWorld::Ptr());
            if ((key.type==Type_Item || key.type==Type_MagicItem) && !player.isEmpty()) {
                const auto item=player.getClass().getInventoryStore(player).findReplacement(key.id);
                icon->setItem(item);
            } else if (key.type==Type_Magic) {
                const auto* spell=MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(key.id);
                if (spell && !spell->mEffects.mList.empty()) {
                    const auto* effect=MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(spell->mEffects.mList.front().mEffectID);
                    std::string path=effect->mIcon;
                    path.insert(path.find_last_of("\\")+1,"b_");
                    icon->setIcon(MWBase::Environment::get().getWindowManager()->correctIconPath(path));
                }
            } else if (key.type==Type_HandToHand) icon->setIcon("icons\\k\\stealth_handtohand.dds");
            mBarLabels[col]->setCaption(number+"  "+page);
            mBarLabels[col]->setTextColour(col==mLastColumn ? MyGUI::Colour(1.f,.82f,.38f) : MyGUI::Colour(.72f,.68f,.58f));
            icon->setAlpha(col==mLastColumn ? 1.f : .65f);
        }
        std::lock_guard<std::mutex> lock(quickLabelMutex);
        quickLabels=std::move(labels);
    }

    void QuickKeysMenu::updateBar(float dt, bool hudVisible)
    {
        auto* wm=MWBase::Environment::get().getWindowManager();
        const bool editor=wm->containsMode(GM_QuickKeysMenu);
        if (editor) refreshEditor();
        mBarTime=std::max(0.f,mBarTime-std::max(0.f,dt));
        const float target=!wm->isGuiMode() && mBarTime>0.f ? 1.f : 0.f;
        mBarAlpha+=(target-mBarAlpha)*std::min(1.f,std::max(0.f,dt)*10.f);
        mRefreshTimer-=std::max(0.f,dt);
        if (mRefreshTimer<=0.f) { refreshBar(); mRefreshTimer=.15f; }
        if (mBar) {
            const auto view=MyGUI::RenderManager::getInstance().getViewSize();
            mBar->setPosition(std::max(0,(view.width-560)/2),std::max(0,view.height-150));
            mBar->setAlpha(mBarAlpha);
            mBar->setVisible(mBarAlpha>.01f && hudVisible);
        }
    }

    // ---------------------------------------------------------------------------------------------------------

    QuickKeysMenuAssign::QuickKeysMenuAssign (QuickKeysMenu* parent)
        : WindowModal("openmw_quickkeys_menu_assign.layout")
        , mParent(parent)
    {
        getWidget(mLabel, "Label");
        getWidget(mItemButton, "ItemButton");
        getWidget(mMagicButton, "MagicButton");
        getWidget(mUnassignButton, "UnassignButton");
        getWidget(mCancelButton, "CancelButton");

        mItemButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onItemButtonClicked);
        mMagicButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onMagicButtonClicked);
        mUnassignButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onUnassignButtonClicked);
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(mParent, &QuickKeysMenu::onCancelButtonClicked);


        int maxWidth = mLabel->getTextSize ().width + 24;
        maxWidth = std::max(maxWidth, mItemButton->getTextSize ().width + 24);
        maxWidth = std::max(maxWidth, mMagicButton->getTextSize ().width + 24);
        maxWidth = std::max(maxWidth, mUnassignButton->getTextSize ().width + 24);
        maxWidth = std::max(maxWidth, mCancelButton->getTextSize ().width + 24);

        mMainWidget->setSize(maxWidth + 24, mMainWidget->getHeight());
        mLabel->setSize(maxWidth, mLabel->getHeight());

        mItemButton->setCoord((maxWidth - mItemButton->getTextSize().width-24)/2 + 8,
                              mItemButton->getTop(),
                              mItemButton->getTextSize().width + 24,
                              mItemButton->getHeight());
        mMagicButton->setCoord((maxWidth - mMagicButton->getTextSize().width-24)/2 + 8,
                              mMagicButton->getTop(),
                              mMagicButton->getTextSize().width + 24,
                              mMagicButton->getHeight());
        mUnassignButton->setCoord((maxWidth - mUnassignButton->getTextSize().width-24)/2 + 8,
                              mUnassignButton->getTop(),
                              mUnassignButton->getTextSize().width + 24,
                              mUnassignButton->getHeight());
        mCancelButton->setCoord((maxWidth - mCancelButton->getTextSize().width-24)/2 + 8,
                              mCancelButton->getTop(),
                              mCancelButton->getTextSize().width + 24,
                              mCancelButton->getHeight());

        center();
    }

    void QuickKeysMenu::write(ESM::ESMWriter &writer)
    {
        writer.startRecord(ESM::REC_KEYS);

        ESM::QuickKeys keys;

        // Preserve all rows; level gates affect use, not saved assignments.
        for (int i=0; i<QuickBarRules::Count; ++i)
        {
            ItemWidget* button = mKey[i].button;

            int type = mKey[i].type;

            ESM::QuickKeys::QuickKey key;
            key.mType = type;

            switch (type)
            {
                case Type_Unassigned:
                case Type_HandToHand:
                    break;
                case Type_Item:
                case Type_MagicItem:
                {
                    MWWorld::Ptr item = *button->getUserData<MWWorld::Ptr>();
                    key.mId = item.getCellRef().getRefId();
                    break;
                }
                case Type_Magic:
                    std::string spellId = button->getUserString("Spell");
                    key.mId = spellId;
                    break;
            }

            keys.mKeys.push_back(key);
        }

        keys.save(writer);

        writer.endRecord(ESM::REC_KEYS);
    }

    void QuickKeysMenu::readRecord(ESM::ESMReader &reader, uint32_t type)
    {
        if (type != ESM::REC_KEYS)
            return;

        ESM::QuickKeys keys;
        keys.load(reader);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        MWWorld::InventoryStore& store = player.getClass().getInventoryStore(player);

        int i=0;
        for (ESM::QuickKeys::QuickKey& quickKey : keys.mKeys)
        {
            if (i >= QuickBarRules::Count)
                return;

            mSelected = &mKey[i];

            switch (quickKey.mType)
            {
            case Type_Magic:
                if (MWBase::Environment::get().getWorld()->getStore().get<ESM::Spell>().search(quickKey.mId))
                    onAssignMagic(quickKey.mId);
                break;
            case Type_Item:
            case Type_MagicItem:
            {
                // Find the item by id
                MWWorld::Ptr item = store.findReplacement(quickKey.mId);

                if (item.isEmpty())
                    unassign(mSelected);
                else
                {
                    if (quickKey.mType == Type_Item)
                        onAssignItem(item);
                    else // if (quickKey.mType == Type_MagicItem)
                        onAssignMagicItem(item);
                }

                break;
            }
            case Type_Unassigned:
            case Type_HandToHand:
                unassign(mSelected);
                break;
            }

            ++i;
        }
    }

    // ---------------------------------------------------------------------------------------------------------

    MagicSelectionDialog::MagicSelectionDialog(QuickKeysMenu* parent)
        : WindowModal("openmw_magicselection_dialog.layout")
        , mParent(parent)
    {
        getWidget(mCancelButton, "CancelButton");
        getWidget(mMagicList, "MagicList");
        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &MagicSelectionDialog::onCancelButtonClicked);

        mMagicList->setShowCostColumn(false);
        mMagicList->setHighlightSelected(false);
        mMagicList->eventSpellClicked += MyGUI::newDelegate(this, &MagicSelectionDialog::onModelIndexSelected);

        center();
    }

    void MagicSelectionDialog::onCancelButtonClicked (MyGUI::Widget *sender)
    {
        exit();
    }

    bool MagicSelectionDialog::exit()
    {
        mParent->onAssignMagicCancel();
        return true;
    }

    void MagicSelectionDialog::onOpen ()
    {
        WindowModal::onOpen();

        mMagicList->setModel(new SpellModel(MWMechanics::getPlayer()));
        mMagicList->resetScrollbars();
    }

    void MagicSelectionDialog::onModelIndexSelected(SpellModel::ModelIndex index)
    {
        const Spell& spell = mMagicList->getModel()->getItem(index);
        if (spell.mType == Spell::Type_EnchantedItem)
            mParent->onAssignMagicItem(spell.mItem);
        else
            mParent->onAssignMagic(spell.mId);
    }

}
