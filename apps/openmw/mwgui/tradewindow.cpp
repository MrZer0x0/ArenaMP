#include "tradewindow.hpp"

#include <algorithm>
#include <MyGUI_Button.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_ControllerManager.h>
#include <MyGUI_ControllerRepeatClick.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_LanguageManager.h>

#include <components/widgets/numericeditbox.hpp>
#include <components/settings/settings.hpp>
#include <components/esm/loadmisc.hpp>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/ObjectList.hpp"
#include "../mwmp/CraftEconomic.hpp"
/*
    End of tes3mp addition
*/

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/dialoguemanager.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/interactionanimation.hpp"

#include "../mwrender/animation.hpp"

#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/xpleveling.hpp"

#include "inventorywindow.hpp"
#include "draganddrop.hpp"
#include "itemview.hpp"
#include "sortfilteritemmodel.hpp"
#include "containeritemmodel.hpp"
#include "tradeitemmodel.hpp"
#include "countdialog.hpp"
#include "tooltips.hpp"

namespace
{

    int getEffectiveValue (MWWorld::Ptr item, int count)
    {
        float price = static_cast<float>(item.getClass().getValue(item));
        if (item.getClass().hasItemHealth(item))
        {
            price *= item.getClass().getItemNormalizedHealth(item);
        }
        return static_cast<int>(price * count);
    }

}

namespace MWGui
{
    TradeWindow::TradeWindow(DragAndDrop* dragAndDrop)
        : WindowBase("openmw_trade_window.layout")
        , mSortModel(nullptr)
        , mTradeModel(nullptr)
        , mDragAndDrop(dragAndDrop)
        , mItemToSell(-1)
        , mCurrentBalance(0)
        , mCurrentMerchantOffer(0)
        , mViewModeButton(nullptr)
        , mViewModeIcon(nullptr)
        , mSingleTransferButton(nullptr)
        , mFilterKeys(nullptr)
        , mMerchantGoldIcon(nullptr)
    {
        getWidget(mEconomicStatus, "EconomicStatus");
        getWidget(mMarketButton, "MarketButton");
        mMarketButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onMarketClicked);
        getWidget(mFilterAll, "AllButton");
        getWidget(mFilterWeapon, "WeaponButton");
        getWidget(mFilterApparel, "ApparelButton");
        getWidget(mFilterMagic, "MagicButton");
        getWidget(mFilterMisc, "MiscButton");
        getWidget(mFilterKeys, "KeysButton");

        getWidget(mMaxSaleButton, "MaxSaleButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mOfferButton, "OfferButton");
        getWidget(mMerchantGoldIcon, "MerchantGoldIcon");
        getWidget(mMerchantGold, "MerchantGold");
        mMerchantGoldIcon->setNeedMouseFocus(false);
        mMerchantGold->setNeedMouseFocus(false);
        mMerchantGold->setTextAlign(MyGUI::Align::Right | MyGUI::Align::Bottom);
        mMerchantGold->setTextShadow(true);
        mMerchantGold->setTextShadowColour(MyGUI::Colour::Black);
        getWidget(mIncreaseButton, "IncreaseButton");
        getWidget(mDecreaseButton, "DecreaseButton");
        getWidget(mTotalBalance, "TotalBalance");
        getWidget(mTotalBalanceLabel, "TotalBalanceLabel");
        getWidget(mBottomPane, "BottomPane");
        getWidget(mFilterEdit, "FilterEdit");
        getWidget(mViewModeButton, "ViewModeButton");
        getWidget(mSingleTransferButton, "SingleTransferButton");

        getWidget(mItemView, "ItemView");
        mItemView->setExtendedMode(true);
        mItemView->setSingleClickActionEnabled(true);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &TradeWindow::onItemSelected);
        mItemView->eventItemDragStarted += MyGUI::newDelegate(this, &TradeWindow::onItemDragStarted);
        mItemView->eventItemDoubleClicked += MyGUI::newDelegate(this, &TradeWindow::onItemDoubleClicked);
        mItemView->eventBackgroundClicked += MyGUI::newDelegate(this, &TradeWindow::onBackgroundSelected);

        mViewModeIcon = mViewModeButton->createWidget<MyGUI::ImageBox>("ImageBox",
            MyGUI::IntCoord(5, 4, 18, 18), MyGUI::Align::Center, "ArenaTradeViewToggleIcon");
        mViewModeIcon->setNeedMouseFocus(false);
        mViewModeIcon->setColour(MyGUI::Colour(0.93f, 0.82f, 0.58f));

        mFilterAll->setStateSelected(true);
        mItemView->setInternalViewModeButtonVisible(false);
        mItemView->setExtendedMode(true);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture("icons/inventoryextender/Base/view_grid.dds");

        mFilterAll->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterWeapon->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterApparel->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterMagic->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterMisc->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterKeys->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onFilterChanged);
        mFilterEdit->eventEditTextChange += MyGUI::newDelegate(this, &TradeWindow::onNameFilterChanged);
        mViewModeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onViewModeClicked);
        mSingleTransferButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onSingleTransferClicked);

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onCancelButtonClicked);
        mOfferButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onOfferButtonClicked);
        mMaxSaleButton->eventMouseButtonClick += MyGUI::newDelegate(this, &TradeWindow::onMaxSaleButtonClicked);
        mIncreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &TradeWindow::onIncreaseButtonPressed);
        mIncreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &TradeWindow::onBalanceButtonReleased);
        mDecreaseButton->eventMouseButtonPressed += MyGUI::newDelegate(this, &TradeWindow::onDecreaseButtonPressed);
        mDecreaseButton->eventMouseButtonReleased += MyGUI::newDelegate(this, &TradeWindow::onBalanceButtonReleased);

        mTotalBalance->eventValueChanged += MyGUI::newDelegate(this, &TradeWindow::onBalanceValueChanged);
        mTotalBalance->eventEditSelectAccept += MyGUI::newDelegate(this, &TradeWindow::onAccept);
        mTotalBalance->setMinValue(std::numeric_limits<int>::min()+1); // disallow INT_MIN since abs(INT_MIN) is undefined

        updateSingleTransferButton();

        // Keep the merchant panel a little roomier than the old layout so the
        // ItemView scrollbar and both bottom action rows remain fully inside
        // the outer frame at UI scaling factors above 1.0.
        setCoord(320, 20, 700, 460);
    }

    void TradeWindow::setPtr(const MWWorld::Ptr& actor)
    {
        mPtr = actor;
        mServerQuote=0;mServerCommit=0;mQuoteAge=0;
        mItemView->setEnabled(true);
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->mMainWidget->setEnabled(true);
        updateSingleTransferButton();

        mCurrentBalance = 0;
        mCurrentMerchantOffer = 0;

        std::vector<MWWorld::Ptr> itemSources;
        // Important: actor goes first, so purchased items come out of the actor's pocket first
        itemSources.push_back(actor);
        MWBase::Environment::get().getWorld()->getContainersOwnedBy(actor, itemSources);

        mEconomicSources=itemSources;
        std::vector<MWWorld::Ptr> worldItems;
        if(mwmp::CraftEconomic::get().mode()!=mwmp::CraftEconomic::Enabled)
            MWBase::Environment::get().getWorld()->getItemsOwnedBy(actor, worldItems);

        mTradeModel = new TradeItemModel(new ContainerItemModel(itemSources, worldItems), mPtr);
        mSortModel = new SortFilterItemModel(mTradeModel);
        mItemView->setModel (mSortModel);
        mItemView->resetScrollBars();

        updateLabels();

        setTitle(actor.getClass().getName(actor));

        onFilterChanged(mFilterAll);
        mFilterEdit->setCaption("");
        updateOffer();
    }

    void TradeWindow::onFrame(float dt)
    {
        checkReferenceAvailable();
        if(mPtr.isEmpty())return;
        auto& ce=mwmp::CraftEconomic::get();
        const bool active=ce.mode()==mwmp::CraftEconomic::Enabled;
        mEconomicStatus->setVisible(active || ce.mode()==mwmp::CraftEconomic::Unknown);
        mMarketButton->setVisible(active);
        mIncreaseButton->setEnabled(!active);mDecreaseButton->setEnabled(!active);
        mTotalBalance->setEnabled(!active);mMaxSaleButton->setEnabled(!active);
        if(!active)
        {
            mOfferButton->setEnabled(ce.mode()==mwmp::CraftEconomic::Disabled && !ce.busy());
            return;
        }
        if(mServerCommit)
        {
            if(const auto* result=ce.response(mServerCommit))
            {
                mServerCommit=0;
                mItemView->setEnabled(true);
                MWBase::Environment::get().getWindowManager()->getInventoryWindow()->mMainWidget->setEnabled(true);
                if(result->at("ok").yes()){finishServerTrade();return;}
                mServerQuote=0;
            }
        }
        if(!mServerCommit)
        {
            mQuoteAge+=dt;
            if(!mServerQuote || mQuoteAge>=10.f)requestServerQuote();
        }
        const auto* response=ce.response(mServerQuote);
        mOfferButton->setEnabled(!ce.busy() && response && response->at("ok").yes()
            && response->at("quote").at("blocked").type==ArenaCraft::Value::Null);
        if(response && response->at("ok").yes())
        {
            const auto& q=response->at("quote");
            mCurrentBalance=q.at("net").integer();mCurrentMerchantOffer=mCurrentBalance;
            mTotalBalance->setValue(std::abs(mCurrentBalance));
            mMerchantGold->setCaption(std::to_string(q.at("wallet").integer()));
            const std::string taxLabel=MyGUI::LanguageManager::getInstance().replaceTags("#{arenamp=ce.tax}");
            mEconomicStatus->setCaption(q.at("region").text()+" | "+taxLabel+": "+std::to_string(q.at("tax").integer())
                +" | "+(mCurrentBalance>=0 ? "+" : "-")+std::to_string(std::abs(mCurrentBalance)));
            if(q.at("blocked").type==ArenaCraft::Value::String)
                mEconomicStatus->setCaptionWithReplacing(mwmp::CraftEconomic::status(*response));
        }
        else mEconomicStatus->setCaptionWithReplacing(response ? mwmp::CraftEconomic::status(*response) : "#{arenamp=ce.wait}");
        if(ce.busy())mEconomicStatus->setCaptionWithReplacing("#{arenamp=ce.committing}");
    }

    void TradeWindow::requestServerQuote()
    {
        auto& ce=mwmp::CraftEconomic::get();
        if(ce.mode()!=mwmp::CraftEconomic::Enabled || ce.busy() || !mTradeModel || mPtr.isEmpty())return;
        auto request=ArenaCraft::Value::dict();request["kind"]="trade";
        request["merchant"]=mwmp::CraftEconomic::merchant(mPtr);
        auto buys=ArenaCraft::Value::list();auto sells=ArenaCraft::Value::list();
        auto* model=MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        if(!model)return;
        for(const auto& stack:model->getItemsBorrowedToUs())buys.array.push_back(mwmp::CraftEconomic::item(stack.mBase,stack.mCount));
        for(const auto& stack:mTradeModel->getItemsBorrowedToUs())sells.array.push_back(mwmp::CraftEconomic::item(stack.mBase,stack.mCount));
        request["buy"]=std::move(buys);request["sell"]=std::move(sells);
        mServerQuote=ce.quote(std::move(request));mQuoteAge=0;mOfferButton->setEnabled(false);
    }
    void TradeWindow::onMarketClicked(MyGUI::Widget*)
    {
        if(mwmp::CraftEconomic::get().busy())return;
        requestServerQuote(); // flush this basket before opening the server menu
        mwmp::CraftEconomic::get().menu(mPtr);
        onCancelButtonClicked(nullptr);
    }
    bool TradeWindow::prepareInventoryReplace(const MWWorld::Ptr& container)
    {
        if(mwmp::CraftEconomic::get().mode()!=mwmp::CraftEconomic::Enabled || mPtr.isEmpty() || !mTradeModel)return false;
        if(container!=MWMechanics::getPlayer() && std::find(mEconomicSources.begin(),mEconomicSources.end(),container)==mEconomicSources.end())return false;
        auto* wm=MWBase::Environment::get().getWindowManager();
        wm->finishDragDrop();wm->getCountDialog()->setVisible(false);
        if(auto* playerModel=wm->getInventoryWindow()->getTradeModel())playerModel->abort();
        mTradeModel->abort();mItemView->setModel(nullptr);mTradeModel=nullptr;mSortModel=nullptr;
        mServerQuote=0;mQuoteAge=0;mItemToSell=-1;mOfferButton->setEnabled(false);
        wm->getInventoryWindow()->updateItemView();
        return true;
    }
    void TradeWindow::finishInventoryReplace(bool prepared)
    {
        if(!prepared || mPtr.isEmpty())return;
        std::vector<MWWorld::Ptr> worldItems;
        mTradeModel=new TradeItemModel(new ContainerItemModel(mEconomicSources,worldItems),mPtr);
        mSortModel=new SortFilterItemModel(mTradeModel);mItemView->setModel(mSortModel);
        onFilterChanged(mFilterAll);mSortModel->setNameFilter(mFilterEdit->getCaption());mItemView->update();
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
        if(!mServerCommit)requestServerQuote();
    }

    void TradeWindow::finishServerTrade()
    {
        eventTradeDone();
        MWBase::Environment::get().getWindowManager()->playSound("Item Gold Up");
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    void TradeWindow::onNameFilterChanged(MyGUI::EditBox* _sender)
    {
        mSortModel->setNameFilter(_sender->getCaption());
        mItemView->update();
    }

    void TradeWindow::onFilterChanged(MyGUI::Widget* _sender)
    {
        if (_sender == mFilterAll)
            mSortModel->setCategory(SortFilterItemModel::Category_All);
        else if (_sender == mFilterWeapon)
            mSortModel->setCategory(SortFilterItemModel::Category_Weapon);
        else if (_sender == mFilterApparel)
            mSortModel->setCategory(SortFilterItemModel::Category_Apparel);
        else if (_sender == mFilterMagic)
            mSortModel->setCategory(SortFilterItemModel::Category_Magic);
        else if (_sender == mFilterMisc)
            mSortModel->setCategory(SortFilterItemModel::Category_Misc);
        else if (_sender == mFilterKeys)
            mSortModel->setCategory(SortFilterItemModel::Category_Keys);

        mFilterAll->setStateSelected(false);
        mFilterWeapon->setStateSelected(false);
        mFilterApparel->setStateSelected(false);
        mFilterMagic->setStateSelected(false);
        mFilterMisc->setStateSelected(false);
        mFilterKeys->setStateSelected(false);

        _sender->castType<MyGUI::Button>()->setStateSelected(true);

        mItemView->update();
    }

    int TradeWindow::getMerchantServices()
    {
        return mPtr.getClass().getServices(mPtr);
    }

    bool TradeWindow::exit()
    {
        mTradeModel->abort();
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel()->abort();
        return true;
    }

    void TradeWindow::onItemSelected (int index)
    {
        if (!mSortModel || !mTradeModel || index < 0 || index >= static_cast<int>(mSortModel->getItemCount()))
            return;

        const ItemStack item = mSortModel->getItem(index);
        const bool control = MyGUI::InputManager::getInstance().isControlPressed()
            || Settings::Manager::getBool("single item transfer", "GUI");
        const bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
        const int count = control ? 1 : item.mCount;

        // Merchant pane mirrors the player pane: click a stack to choose an
        // amount, Ctrl+click transfers one, Shift+click transfers all. ItemView
        // still guards a stale second release after the list has been rebuilt.
        mItemToSell = mSortModel->mapToSource(index);
        if (mItemToSell < 0 || mItemToSell >= static_cast<int>(mTradeModel->getItemCount()))
            return;

        if (item.mCount > 1 && !control && !shift)
        {
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            const std::string name = item.mBase.getClass().getName(item.mBase)
                + MWGui::ToolTips::getSoulString(item.mBase.getCellRef());
            dialog->openCountDialog(name, "#{sQuanityMenuMessage01}", item.mCount);
            dialog->eventOkClicked.clear();
            dialog->eventOkClicked += MyGUI::newDelegate(this, &TradeWindow::sellItem);
        }
        else
            sellItem(nullptr, count);
    }

    void TradeWindow::onItemDragStarted(int index)
    {
        if (!mSortModel || !mTradeModel || mDragAndDrop->mIsOnDragAndDrop)
            return;

        const int sourceIndex = mSortModel->mapToSource(index);
        if (sourceIndex < 0 || sourceIndex >= static_cast<int>(mTradeModel->getItemCount()))
            return;

        mItemToSell = sourceIndex;
        const ItemStack item = mTradeModel->getItem(sourceIndex);
        const bool control = MyGUI::InputManager::getInstance().isControlPressed()
            || Settings::Manager::getBool("single item transfer", "GUI");
        const bool shift = MyGUI::InputManager::getInstance().isShiftPressed();
        const int count = control ? 1 : item.mCount;

        if (item.mCount > 1 && !control && !shift)
        {
            CountDialog* dialog = MWBase::Environment::get().getWindowManager()->getCountDialog();
            const std::string name = item.mBase.getClass().getName(item.mBase)
                + MWGui::ToolTips::getSoulString(item.mBase.getCellRef());
            dialog->openCountDialog(name, "#{sQuanityMenuMessage01}", item.mCount);
            dialog->eventOkClicked.clear();
            dialog->eventOkClicked += MyGUI::newDelegate(this, &TradeWindow::startBarterDrag);
            return;
        }

        startBarterDrag(nullptr, count);
    }

    void TradeWindow::startBarterDrag(MyGUI::Widget* sender, int count)
    {
        (void)sender;
        if (!mTradeModel || !mSortModel || mItemToSell < 0
            || mItemToSell >= static_cast<int>(mTradeModel->getItemCount()))
            return;

        const ItemStack item = mTradeModel->getItem(mItemToSell);
        count = std::max(1, std::min(count, static_cast<int>(item.mCount)));
        mDragAndDrop->startBarterDrag(mItemToSell, mSortModel, mTradeModel, mItemView, count);
    }

    void TradeWindow::onItemDoubleClicked(int index)
    {
        // Barter rows already transfer on the first click. A second activation
        // would operate on the item that slid into the old row.
        (void)index;
    }

    void TradeWindow::onBackgroundSelected()
    {
        if (!mDragAndDrop->mIsOnDragAndDrop || !mDragAndDrop->isBarterDrag())
            return;

        if (mDragAndDrop->mSourceView == mItemView)
        {
            mDragAndDrop->finish();
            return;
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->completeBarterDragToMerchant(
            mDragAndDrop->mSourceIndex, mDragAndDrop->mDraggedCount);
        mDragAndDrop->finish();
    }

    void TradeWindow::completeBarterDragToPlayer(int sourceIndex, int count)
    {
        if (!mTradeModel)
            return;

        const MWWorld::Ptr draggedItem = mDragAndDrop->mItem.mBase;
        int resolvedIndex = sourceIndex;
        if (resolvedIndex < 0 || resolvedIndex >= static_cast<int>(mTradeModel->getItemCount())
            || mTradeModel->getItem(resolvedIndex).mBase != draggedItem)
        {
            resolvedIndex = -1;
            for (size_t i = 0; i < mTradeModel->getItemCount(); ++i)
            {
                if (mTradeModel->getItem(i).mBase == draggedItem)
                {
                    resolvedIndex = static_cast<int>(i);
                    break;
                }
            }
        }
        if (resolvedIndex < 0)
            return;

        mItemToSell = resolvedIndex;
        const ItemStack item = mTradeModel->getItem(resolvedIndex);
        count = std::max(1, std::min(count, static_cast<int>(item.mCount)));
        sellItem(nullptr, count);
    }

    void TradeWindow::sellItem(MyGUI::Widget* sender, int count)
    {
        (void)sender;
        if (!mTradeModel || mItemToSell < 0 || mItemToSell >= static_cast<int>(mTradeModel->getItemCount()))
            return;

        count = std::max(1, std::min(count, static_cast<int>(mTradeModel->getItem(mItemToSell).mCount)));
        // borrow/return mutates TradeItemModel, so keep a value copy for the
        // later balance update instead of a potentially invalidated reference.
        const ItemStack item = mTradeModel->getItem(mItemToSell);
        std::string sound = item.mBase.getClass().getUpSoundId(item.mBase);
        MWBase::Environment::get().getWindowManager()->playSound(sound);

        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        if (item.mType == ItemStack::Type_Barter)
        {
            // this was an item borrowed to us by the player
            mTradeModel->returnItemBorrowedToUs(mItemToSell, count);
            playerTradeModel->returnItemBorrowedFromUs(mItemToSell, mTradeModel, count);
            buyFromNpc(item.mBase, count, true);
        }
        else
        {
            // borrow item to player
            playerTradeModel->borrowItemToUs(mItemToSell, mTradeModel, count);
            mTradeModel->borrowItemFromUs(mItemToSell, count);
            buyFromNpc(item.mBase, count, false);
        }

        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->updateItemView();
        mItemView->update();
    }

    void TradeWindow::borrowItem (int index, size_t count)
    {
        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        mTradeModel->borrowItemToUs(index, playerTradeModel, count);
        mItemView->update();
        sellToNpc(playerTradeModel->getItem(index).mBase, count, false);
    }

    void TradeWindow::returnItem (int index, size_t count)
    {
        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
        const ItemStack& item = playerTradeModel->getItem(index);
        mTradeModel->returnItemBorrowedFromUs(index, playerTradeModel, count);
        mItemView->update();
        sellToNpc(item.mBase, count, true);
    }

    void TradeWindow::addOrRemoveGold(int amount, const MWWorld::Ptr& actor)
    {
        MWWorld::ContainerStore& store = actor.getClass().getContainerStore(actor);

        if (amount > 0)
        {
            store.add(MWWorld::ContainerStore::sGoldId, amount, actor);
        }
        else
        {
            store.remove(MWWorld::ContainerStore::sGoldId, - amount, actor);
        }
    }

    void TradeWindow::onOfferButtonClicked(MyGUI::Widget* _sender)
    {
        auto& ce=mwmp::CraftEconomic::get();
        if(ce.mode()==mwmp::CraftEconomic::Unknown || ce.busy())return;
        if(ce.mode()==mwmp::CraftEconomic::Enabled)
        {
            const auto* response=ce.response(mServerQuote);
            if(!response || !response->at("ok").yes())return;
            const auto& q=response->at("quote");
            if(q.at("blocked").type!=ArenaCraft::Value::Null)return;
            auto* model=MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();
            if(!model || (model->getItemsBorrowedToUs().empty() && mTradeModel->getItemsBorrowedToUs().empty()))return;
            mServerCommit=ce.commit(q.at("token").text());
            if(mServerCommit)
            {
                model->abort();mTradeModel->abort(); // no local transfer or gold mutation
                mItemView->setEnabled(false);
                MWBase::Environment::get().getWindowManager()->getInventoryWindow()->mMainWidget->setEnabled(false);
                mOfferButton->setEnabled(false);
            }
            return;
        }
        TradeItemModel* playerItemModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        const MWWorld::Store<ESM::GameSetting> &gmst =
            MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>();

        // were there any items traded at all?
        const std::vector<ItemStack>& playerBought = playerItemModel->getItemsBorrowedToUs();
        const std::vector<ItemStack>& merchantBought = mTradeModel->getItemsBorrowedToUs();
        if (playerBought.empty() && merchantBought.empty())
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sBarterDialog11}");
            return;
        }

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);

        // check if the player can afford this
        if (mCurrentBalance < 0 && playerGold < std::abs(mCurrentBalance))
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sBarterDialog1}");
            return;
        }

        // check if the merchant can afford this
        if (mCurrentBalance > 0 && getMerchantGold() < mCurrentBalance)
        {
            // user notification
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sBarterDialog2}");
            return;
        }

        // check if the player is attempting to sell back an item stolen from this actor
        for (const ItemStack& itemStack : merchantBought)
        {
            if (MWBase::Environment::get().getMechanicsManager()->isItemStolenFrom(itemStack.mBase.getCellRef().getRefId(), mPtr))
            {
                std::string msg = gmst.find("sNotifyMessage49")->mValue.getString();
                msg = Misc::StringUtils::format(msg, itemStack.mBase.getClass().getName(itemStack.mBase));
                MWBase::Environment::get().getWindowManager()->messageBox(msg);

                MWBase::Environment::get().getMechanicsManager()->confiscateStolenItemToOwner(player, itemStack.mBase, mPtr, itemStack.mCount);

                onCancelButtonClicked(mCancelButton);
                MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
                return;
            }
        }

        bool offerAccepted = mTrading.haggle(player, mPtr, mCurrentBalance, mCurrentMerchantOffer);

        // Apply disposition and play a voiced reaction if the merchant is an NPC.
        // Persuasion voice topics provide race/gender-specific positive and angry lines,
        // while DialogueManager handles subtitles and TES3MP actor-sound synchronization.
        if (mPtr.getClass().isNpc()) {
            int dispositionDelta = offerAccepted
                ? gmst.find("iBarterSuccessDisposition")->mValue.getInteger()
                : gmst.find("iBarterFailDisposition")->mValue.getInteger();

            MWBase::Environment::get().getDialogueManager()->applyBarterDispositionChange(dispositionDelta);
            MWBase::Environment::get().getDialogueManager()->say(mPtr,
                offerAccepted ? "Admire Success" : "Taunt Success");
        }

        // display message on haggle failure
        if (!offerAccepted) {
            MWBase::Environment::get().getWindowManager()->
                messageBox("#{sNotifyMessage9}");
            return;
        }

        // make the item transfer
        mTradeModel->transferItems();
        playerItemModel->transferItems();

        // C30/C23: a completed barter can grant an additional random XP bonus.
        MWMechanics::XPLeveling::awardSuccessfulTrade(player);

        ///snapshot the gold you recieve, if any, for EncoreMP XP calculations further down
        int goldRecieved = 0;
        if (mCurrentBalance > 0)
        {
            goldRecieved = mCurrentBalance;
        }


        // transfer the gold
        if (mCurrentBalance != 0)
        {
            addOrRemoveGold(mCurrentBalance, player);

            /*
                Start of tes3mp change (major)

                Don't unilaterally change the merchant's gold pool on our client and instead let the server do it
            */
            //mPtr.getClass().getCreatureStats(mPtr).setGoldPool(
            //    mPtr.getClass().getCreatureStats(mPtr).getGoldPool() - mCurrentBalance);

            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            MWMechanics::CreatureStats& merchantCreatureStats = mPtr.getClass().getCreatureStats(mPtr);
            objectList->addObjectMiscellaneous(mPtr, merchantCreatureStats.getGoldPool() - mCurrentBalance, merchantCreatureStats.getLastRestockTime().getHour(),
                merchantCreatureStats.getLastRestockTime().getDay());
            objectList->sendObjectMiscellaneous();
            /*
                End of tes3mp change (major)
            */
        }

        /// EncoreMP system for calcualting XP gained from sales

        int sumBaseSold = 0;
        int sumBasePurchased = 0;

        if (!merchantBought.empty())
        {
            ///sum the base gold value of all items you are selling
            for (const ItemStack& stack : merchantBought)
            {
                MWWorld::Ptr objectPtr = stack.mBase;
                int singleValue = objectPtr.getClass().getValue(objectPtr);

                for (int i = 0; i < stack.mCount; ++i)
                {
                    if (singleValue > 0)
                    {
                        sumBaseSold += singleValue;
                    }
                }
            }

            ///sum the base gold value of all items you are recieving
            if (!playerBought.empty())
            {
                for (const ItemStack& stack : playerBought)
                {
                    MWWorld::Ptr objectPtr = stack.mBase;
                    int singleValue = objectPtr.getClass().getValue(objectPtr);

                    for (int i = 0; i < stack.mCount; ++i)
                    {
                        if (singleValue > 0)
                        {
                            sumBasePurchased += singleValue;
                        }
                    }
                }

            }

        }

        MWWorld::Ptr playerPtr = MWBase::Environment::get().getWorld()->getPlayerPtr();

        /// calculate the barter value of everything you sold
        const int barterItemsSold = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(playerPtr, sumBaseSold, false);

        ///calculate the barter value of everything you purchased
        const int barterItemsPurchased = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(playerPtr, sumBasePurchased, true);

        ///sum the gold you recieved and the barter value of the items you recieved
        int totalValuePurchased = (barterItemsPurchased + goldRecieved);

        ///cap the total value of what you have gained to be no more than the barter value of what you sold
        totalValuePurchased = std::min(barterItemsSold, totalValuePurchased);

        ///calculate the ratio of what your items were worth in this transaction to what you got
        float xpMultiplier = 1.0f;
        if (barterItemsSold > 0)
        {
            xpMultiplier = ((float)totalValuePurchased / (float)barterItemsSold);
        }

        /// use the barter value of items sold to determine XP gain
        float xpAwardIncrements = 0.0f;
        if (barterItemsSold > 0)
        {
            xpAwardIncrements = (float(barterItemsSold) / 50.0f);
        }

        ///apply the multiplier for potentially underselling items
        xpAwardIncrements *= xpMultiplier;

        ///award experience if items were sold for a non zero value
        if (barterItemsSold > 0)
        {
            if (xpAwardIncrements > 0.0f)
            {
                player.getClass().skillUsageSucceeded(player, ESM::Skill::Mercantile, 0, xpAwardIncrements);
            }
        }


        /// end of EncoreMP xp gain system


        if (mCurrentBalance != 0
            && Settings::Manager::getBool("animated interactions", "GUI")
            && Settings::Manager::getBool("animated barter handoff", "GUI"))
        {
            if (mCurrentBalance < 0)
            {
                MWWorld::InteractionAnimation::playOneShot("give-to-player",
                    MWRender::Animation::BlendMask_UpperBody, 2.f, 0.8325f, 1,
                    MWWorld::InteractionAnimation::Prop_Gold);
            }
            else
            {
                MWWorld::InteractionAnimation::playOneShot("loot1",
                    MWRender::Animation::BlendMask_Torso
                        | MWRender::Animation::BlendMask_RightArm,
                    0.7f, 1.f);
            }
        }

        eventTradeDone();

        MWBase::Environment::get().getWindowManager()->playSound("Item Gold Up");
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    void TradeWindow::onAccept(MyGUI::EditBox *sender)
    {
        onOfferButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void TradeWindow::onCancelButtonClicked(MyGUI::Widget* _sender)
    {
        exit();
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
    }

    void TradeWindow::onMaxSaleButtonClicked(MyGUI::Widget* _sender)
    {
        if(mwmp::CraftEconomic::get().mode()!=mwmp::CraftEconomic::Disabled)return;
        mCurrentBalance = getMerchantGold();
        updateLabels();
    }

    void TradeWindow::addRepeatController(MyGUI::Widget *widget)
    {
        MyGUI::ControllerItem* item = MyGUI::ControllerManager::getInstance().createItem(MyGUI::ControllerRepeatClick::getClassTypeName());
        MyGUI::ControllerRepeatClick* controller = static_cast<MyGUI::ControllerRepeatClick*>(item);
        controller->eventRepeatClick += newDelegate(this, &TradeWindow::onRepeatClick);
        MyGUI::ControllerManager::getInstance().addItem(widget, controller);
    }

    void TradeWindow::onIncreaseButtonPressed(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        addRepeatController(_sender);
        onIncreaseButtonTriggered();
    }

    void TradeWindow::onDecreaseButtonPressed(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        addRepeatController(_sender);
        onDecreaseButtonTriggered();
    }

    void TradeWindow::onRepeatClick(MyGUI::Widget* widget, MyGUI::ControllerItem* controller)
    {
        if (widget == mIncreaseButton)
            onIncreaseButtonTriggered();
        else if (widget == mDecreaseButton)
            onDecreaseButtonTriggered();
    }

    void TradeWindow::onBalanceButtonReleased(MyGUI::Widget *_sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        MyGUI::ControllerManager::getInstance().removeItem(_sender);
    }

    void TradeWindow::onBalanceValueChanged(int value)
    {
        // Entering a "-" sign inverts the buying/selling state
        mCurrentBalance = (mCurrentBalance >= 0 ? 1 : -1) * value;
        updateLabels();

        if (value != std::abs(value))
            mTotalBalance->setValue(std::abs(value));
    }

    void TradeWindow::onIncreaseButtonTriggered()
    {
        // prevent overflows, and prevent entering INT_MIN since abs(INT_MIN) is undefined
        if (mCurrentBalance == std::numeric_limits<int>::max() || mCurrentBalance == std::numeric_limits<int>::min()+1)
            return;
        if (mCurrentBalance < 0) mCurrentBalance -= 1;
        else mCurrentBalance += 1;
        updateLabels();
    }

    void TradeWindow::onDecreaseButtonTriggered()
    {
        if (mCurrentBalance < 0) mCurrentBalance += 1;
        else mCurrentBalance -= 1;
        updateLabels();
    }

    void TradeWindow::updateLabels()
    {
        mTotalBalanceLabel->setCaption("");
        mTotalBalanceLabel->setVisible(false);

        mTotalBalance->setValue(std::abs(mCurrentBalance));

        mMerchantGold->setCaption(MyGUI::utility::toString(getMerchantGold()));
        if (mMerchantGoldIcon)
        {
            const std::string icon = resolveGoldIcon(mPtr);
            if (!icon.empty())
            {
                mMerchantGoldIcon->setVisible(true);
                mMerchantGoldIcon->setImageTexture(icon);
            }
            else
                mMerchantGoldIcon->setVisible(false);
        }
    }

    void TradeWindow::updateOffer()
    {
        if(mwmp::CraftEconomic::get().mode()==mwmp::CraftEconomic::Enabled)
        { requestServerQuote();return; }
        TradeItemModel* playerTradeModel = MWBase::Environment::get().getWindowManager()->getInventoryWindow()->getTradeModel();

        long long merchantOffer = 0;

        // The offered price must be capped at 75% of the base price to avoid exploits
        // connected to buying and selling the same item.
        // This value has been determined by researching the limitations of the vanilla formula
        // and may not be sufficient if getBarterOffer behavior has been changed

        // EncoreMP: I changed it, the above comments were from openMW, it wasn't sufficient when you considered haggling as well
        const std::vector<ItemStack>& playerBorrowed = playerTradeModel->getItemsBorrowedToUs();
        for (const ItemStack& itemStack : playerBorrowed)
        {
            const int basePrice = getEffectiveValue(itemStack.mBase, itemStack.mCount);
            const int cap = static_cast<int>(std::max(1.f, 0.90f * basePrice)); // Minimum buying price -- 90% of the base
            const int lowcap = static_cast<int>(1.50f * basePrice); // maximum buying price -- 150% of the base
            const int buyingPrice = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, true);

            int localbuyprice = std::max(cap, buyingPrice);
            localbuyprice = std::min(lowcap, localbuyprice);

            localbuyprice = mwmp::CraftEconomic::get().adjustedPrice(localbuyprice,"trade",itemStack.mBase.getCellRef().getRefId(),true);
            merchantOffer -= localbuyprice;
        }

        const std::vector<ItemStack>& merchantBorrowed = mTradeModel->getItemsBorrowedToUs();
        for (const ItemStack& itemStack : merchantBorrowed)
        {
            const int basePrice = getEffectiveValue(itemStack.mBase, itemStack.mCount);
            const int cap = static_cast<int>(std::max(1.f, 0.60f * basePrice)); // Maximum selling price -- 60% of the base
            const int lowcap = static_cast<int>(0.20f * basePrice); // minimum selling price -- 20% of the base
            const int sellingPrice = MWBase::Environment::get().getMechanicsManager()->getBarterOffer(mPtr, basePrice, false);

            int localsellprice = std::max(lowcap, sellingPrice);
            localsellprice = std::min(cap, localsellprice);


            merchantOffer += mwmp::CraftEconomic::get().adjustedPrice(mPtr.getClass().isNpc() ? localsellprice : sellingPrice,
                "trade",itemStack.mBase.getCellRef().getRefId(),false);
        }

        const int boundedOffer = static_cast<int>(std::clamp(merchantOffer, -1000000000LL, 1000000000LL));
        const long long diff = static_cast<long long>(boundedOffer) - mCurrentMerchantOffer;
        mCurrentMerchantOffer = boundedOffer;
        mCurrentBalance = static_cast<int>(std::clamp(static_cast<long long>(mCurrentBalance) + diff,
            -1000000000LL, 1000000000LL));
        updateLabels();
    }

    void TradeWindow::sellToNpc(const MWWorld::Ptr& item, int count, bool boughtItem)
    {
        updateOffer();
    }

    void TradeWindow::buyFromNpc(const MWWorld::Ptr& item, int count, bool soldItem)
    {
        updateOffer();
    }

    void TradeWindow::onViewModeClicked(MyGUI::Widget*)
    {
        if (!mItemView)
            return;

        ItemView::ViewMode mode = mItemView->getViewMode();
        mItemView->setViewMode(mode == ItemView::View_List ? ItemView::View_Grid : ItemView::View_List);
        if (mViewModeIcon)
            mViewModeIcon->setImageTexture(std::string("icons/inventoryextender/Base/")
                + (mItemView->getViewMode() == ItemView::View_List ? "view_grid.dds" : "view_table.dds"));
        mViewModeButton->setStateSelected(mItemView->getViewMode() == ItemView::View_List);
    }

    void TradeWindow::onSingleTransferClicked(MyGUI::Widget*)
    {
        const bool enabled = Settings::Manager::getBool("single item transfer", "GUI");
        Settings::Manager::setBool("single item transfer", "GUI", !enabled);
        Settings::Manager::saveUser();
        updateSingleTransferButton();
    }

    void TradeWindow::updateSingleTransferButton()
    {
        if (!mSingleTransferButton)
            return;

        const bool enabled = Settings::Manager::getBool("single item transfer", "GUI");
        mSingleTransferButton->setStateSelected(enabled);
        mSingleTransferButton->setCaption(MyGUI::LanguageManager::getInstance().replaceTags(
            enabled ? "#{arenamp=inventory.single_transfer_on}" : "#{arenamp=inventory.single_transfer_off}"));
    }

    void TradeWindow::onReferenceUnavailable()
    {
        // remove both Trade and Dialogue (since you always trade with the NPC/creature that you have previously talked to)
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Barter);
        MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
    }

    std::string TradeWindow::resolveGoldIcon(const MWWorld::Ptr&) const
    {
        const ESM::Miscellaneous* gold = MWBase::Environment::get().getWorld()->getStore()
            .get<ESM::Miscellaneous>().search(MWWorld::ContainerStore::sGoldId);
        if (!gold || gold->mIcon.empty())
            return std::string();

        // Match ItemWidget's icon-path normalization so the merchant's coin
        // icon is the real inventory icon rather than a missing-texture tile.
        return MWBase::Environment::get().getWindowManager()->correctIconPath(gold->mIcon);
    }

    int TradeWindow::getMerchantGold()
    {
        int merchantGold = mPtr.getClass().getCreatureStats(mPtr).getGoldPool();
        return merchantGold;
    }

    void TradeWindow::resetReference()
    {
        MWBase::Environment::get().getWindowManager()->getInventoryWindow()->mMainWidget->setEnabled(true);
        mItemView->setEnabled(true);mServerQuote=0;mServerCommit=0;mEconomicSources.clear();
        ReferenceInterface::resetReference();
        mItemView->setModel(nullptr);
        mTradeModel = nullptr;
        mSortModel = nullptr;
    }

    void TradeWindow::onClose()
    {
        // Make sure the window was actually closed and not temporarily hidden.
        if (MWBase::Environment::get().getWindowManager()->containsMode(GM_Barter))
            return;
        resetReference();
    }
}
