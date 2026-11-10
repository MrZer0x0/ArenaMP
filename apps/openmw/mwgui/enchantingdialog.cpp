#include "enchantingdialog.hpp"

#include <iomanip>
#include <algorithm>
#include <cmath>
#include <MyGUI_ComboBox.h>

#include <MyGUI_Button.h>
#include <MyGUI_ScrollView.h>
#include <MyGUI_EditBox.h>

#include <components/widgets/list.hpp>
#include <components/settings/settings.hpp>

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
#include "../mwbase/mechanicsmanager.hpp"
#include "../mwbase/world.hpp"

#include "../mwworld/class.hpp"
#include "../mwworld/containerstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwmechanics/actorutil.hpp"

#include "itemselection.hpp"
#include "itemwidget.hpp"

#include "sortfilteritemmodel.hpp"

namespace MWGui
{


    EnchantingDialog::EnchantingDialog()
        : WindowBase("openmw_enchanting_dialog.layout")
        , EffectEditorBase(EffectEditorBase::Enchanting)
        , mItemSelectionDialog(nullptr)
    {
        getWidget(mName, "NameEdit");
        getWidget(mEffectSearch,"EffectSearch");getWidget(mEconomicStatus,"EconomicStatus");
        getWidget(mUses,"Uses");getWidget(mFit,"FitButton");
        getWidget(mTemplates,"Templates");getWidget(mSaveTemplate,"SaveTemplate");getWidget(mLoadTemplate,"LoadTemplate");
        mEffectSearch->eventEditTextChange += MyGUI::newDelegate(this,&EnchantingDialog::onSearchChanged);
        mName->eventEditTextChange += MyGUI::newDelegate(this,&EnchantingDialog::onNameChanged);
        mFit->eventMouseButtonClick += MyGUI::newDelegate(this,&EnchantingDialog::onFitClicked);
        mSaveTemplate->eventMouseButtonClick += MyGUI::newDelegate(this,&EnchantingDialog::onSaveTemplate);
        mLoadTemplate->eventMouseButtonClick += MyGUI::newDelegate(this,&EnchantingDialog::onLoadTemplate);
        getWidget(mCancelButton, "CancelButton");
        getWidget(mAvailableEffectsList, "AvailableEffects");
        getWidget(mUsedEffectsView, "UsedEffects");
        getWidget(mItemBox, "ItemBox");
        getWidget(mSoulBox, "SoulBox");
        getWidget(mEnchantmentPoints, "Enchantment");
        getWidget(mCastCost, "CastCost");
        getWidget(mCharge, "Charge");
        getWidget(mSuccessChance, "SuccessChance");
        getWidget(mChanceLayout, "ChanceLayout");
        getWidget(mTypeButton, "TypeButton");
        getWidget(mBuyButton, "BuyButton");
        getWidget(mPrice, "PriceLabel");
        getWidget(mPriceText, "PriceTextLabel");

        setWidgets(mAvailableEffectsList, mUsedEffectsView);

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onCancelButtonClicked);
        mItemBox->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onSelectItem);
        mSoulBox->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onSelectSoul);
        mBuyButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onBuyButtonClicked);
        mTypeButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EnchantingDialog::onTypeButtonClicked);
        mName->eventEditSelectAccept += MyGUI::newDelegate(this, &EnchantingDialog::onAccept);
    }

    void EnchantingDialog::onNameChanged(MyGUI::EditBox*)
    {
        requestServerQuote();
    }
    void EnchantingDialog::onSearchChanged(MyGUI::EditBox* sender)
    {
        setEffectFilter(sender->getCaption());
    }
    void EnchantingDialog::requestServerQuote()
    {
        auto& ce=mwmp::CraftEconomic::get();
        mServerQuote=0;mQuoteAge=0;
        if(ce.mode()!=mwmp::CraftEconomic::Enabled || ce.busy() || mEffects.empty()
            || mEnchanting.itemEmpty() || mEnchanting.soulEmpty() || mName->getCaption().empty())return;
        auto request=ArenaCraft::Value::dict();request["kind"]="enchant";
        request["name"]=mName->getCaption().asUTF8();request["castType"]=mEnchanting.getCastStyle();
        request["item"]=mwmp::CraftEconomic::item(mEnchanting.getOldItem(),1);
        request["gem"]=mwmp::CraftEconomic::item(mEnchanting.getGem(),1);
        if(!mPtr.isEmpty() && mPtr!=MWMechanics::getPlayer())request["merchant"]=mwmp::CraftEconomic::merchant(mPtr);
        auto effects=ArenaCraft::Value::list();
        for(const auto& e:mEffects)
        {
            auto effect=ArenaCraft::Value::dict();effect["id"]=int(e.mEffectID);effect["skill"]=int(e.mSkill);
            effect["attribute"]=int(e.mAttribute);effect["range"]=int(e.mRange);effect["area"]=int(e.mArea);
            effect["duration"]=int(e.mDuration);effect["magnitudeMin"]=int(e.mMagnMin);effect["magnitudeMax"]=int(e.mMagnMax);
            effects.array.push_back(std::move(effect));
        }
        request["effects"]=std::move(effects);mServerQuote=ce.quote(std::move(request));
    }
    void EnchantingDialog::onFrame(float dt)
    {
        checkReferenceAvailable();if(mPtr.isEmpty())return;
        auto& ce=mwmp::CraftEconomic::get();
        if(mServerCommit)
        {
            if(const auto* result=ce.response(mServerCommit))
            {
                mServerCommit=0;
                if(result->at("ok").yes())
                {
                    MWBase::Environment::get().getWindowManager()->playSound(result->at("created").yes() ? "enchant success" : "enchant fail");
                    MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);return;
                }
                requestServerQuote();
            }
        }
        const bool busy=ce.busy();
        mItemBox->setEnabled(!busy);mSoulBox->setEnabled(!busy);mName->setEnabled(!busy);
        mTypeButton->setEnabled(!busy);mUsedEffectsView->setEnabled(!busy);mAvailableEffectsList->setEnabled(!busy);
        mFit->setEnabled(!busy);mLoadTemplate->setEnabled(!busy);
        if(ce.mode()==mwmp::CraftEconomic::Disabled){mBuyButton->setEnabled(!busy);mEconomicStatus->setCaption("");return;}
        if(!busy && ce.mode()==mwmp::CraftEconomic::Enabled && (!mServerQuote || (mQuoteAge+=dt)>=10.f))requestServerQuote();
        const auto* response=ce.response(mServerQuote);
        mBuyButton->setEnabled(!busy && response && response->at("ok").yes());
        if(response && response->at("ok").yes())
        {
            const auto& q=response->at("quote");
            mEnchantmentPoints->setCaption(std::to_string(int(q.at("points").number))+" / "+std::to_string(q.at("capacity").integer()));
            mCharge->setCaption(std::to_string(q.at("charge").integer()));mPrice->setCaption(std::to_string(-q.at("net").integer()));
            mSuccessChance->setCaption(std::to_string(q.at("chance").integer())+"%");
            mEconomicStatus->setCaption(q.at("region").text()+" | x"+std::to_string(q.at("count").integer()));
        }
        else mEconomicStatus->setCaptionWithReplacing(response ? mwmp::CraftEconomic::status(*response) : "#{arenamp=ce.wait}");
        if(busy)mEconomicStatus->setCaptionWithReplacing("#{arenamp=ce.committing}");
    }
    void EnchantingDialog::onFitClicked(MyGUI::Widget*)
    {
        if(mEffects.empty() || mEnchanting.itemEmpty() || mwmp::CraftEconomic::get().busy())return;
        const auto original=mEffects;
        const auto apply=[&](double scale)
        {
            mEffects=original;
            for(auto& e:mEffects)
            {
                const auto* effect=MWBase::Environment::get().getWorld()->getStore().get<ESM::MagicEffect>().find(e.mEffectID);
                if(!(effect->mData.mFlags & ESM::MagicEffect::NoMagnitude))
                {e.mMagnMin=std::max(1,int(std::floor(e.mMagnMin*scale)));e.mMagnMax=std::max(int(e.mMagnMin),int(std::floor(e.mMagnMax*scale)));}
                if(!(effect->mData.mFlags & ESM::MagicEffect::NoDuration))e.mDuration=std::max(1,int(std::floor(e.mDuration*scale)));
                e.mArea=int(std::floor(e.mArea*scale));
            }
            mEffectList.mList=mEffects;mEnchanting.setEffect(mEffectList);
            return mEnchanting.getEnchantPoints(false)<=mEnchanting.getMaxEnchantValue();
        };
        if(mEnchanting.getEnchantPoints(false)<=mEnchanting.getMaxEnchantValue())return;
        if(!apply(0.0))
        {
            mEffects=original;mEffectList.mList=mEffects;mEnchanting.setEffect(mEffectList);
            MWBase::Environment::get().getWindowManager()->messageBox("#{sNotifyMessage29}");return;
        }
        double low=0,high=1;
        for(int i=0;i<24;++i){const double mid=(low+high)/2;if(apply(mid))low=mid;else high=mid;}
        apply(low);updateEffectsView();
    }
    void EnchantingDialog::refreshTemplates()
    {
        const auto selected=mTemplates->getIndexSelected();mTemplates->removeAllItems();
        for(int i=0;i<8;++i)
        {
            std::string name=std::to_string(i+1);
            try
            {
                const auto text=Settings::Manager::getString("enchant template "+std::to_string(i+1),"Arena Craft");
                if(!text.empty())name+=". "+ArenaCraft::Parser(text).parse().at("name").text();
            }catch(...){}
            mTemplates->addItem(name);
        }
        mTemplates->setIndexSelected(selected<8 ? selected : 0);
    }
    void EnchantingDialog::onSaveTemplate(MyGUI::Widget*)
    {
        if(mEffects.empty() || mTemplates->getIndexSelected()>=8)return;
        auto data=ArenaCraft::Value::dict();data["name"]=mName->getCaption().asUTF8();data["castType"]=mEnchanting.getCastStyle();
        data["contentKey"]=mwmp::CraftEconomic::get().contentKey();data["effects"]=ArenaCraft::Value::list();
        for(const auto& e:mEffects)
        {
            auto v=ArenaCraft::Value::dict();v["id"]=int(e.mEffectID);v["skill"]=int(e.mSkill);v["attribute"]=int(e.mAttribute);
            v["range"]=int(e.mRange);v["area"]=int(e.mArea);v["duration"]=int(e.mDuration);v["magnitudeMin"]=int(e.mMagnMin);v["magnitudeMax"]=int(e.mMagnMax);
            data["effects"].array.push_back(std::move(v));
        }
        Settings::Manager::setString("enchant template "+std::to_string(mTemplates->getIndexSelected()+1),"Arena Craft",ArenaCraft::encode(data));
        Settings::Manager::saveUser();refreshTemplates();
    }
    void EnchantingDialog::onLoadTemplate(MyGUI::Widget*)
    {
        if(mTemplates->getIndexSelected()>=8 || mwmp::CraftEconomic::get().busy())return;
        try
        {
            const auto text=Settings::Manager::getString("enchant template "+std::to_string(mTemplates->getIndexSelected()+1),"Arena Craft");
            if(text.empty())return;
            const auto data=ArenaCraft::Parser(text).parse();
            if(data.at("contentKey").text()!=mwmp::CraftEconomic::get().contentKey())throw std::runtime_error("different plugin catalog");
            std::vector<ESM::ENAMstruct> effects;
            for(const auto& v:data.at("effects").array)
            {
                ESM::ENAMstruct e{};e.mEffectID=v.at("id").integer(-1);
                if(std::find(mKnownEffects.begin(),mKnownEffects.end(),e.mEffectID)==mKnownEffects.end())throw std::runtime_error("unknown effect");
                e.mSkill=v.at("skill").integer(-1);e.mAttribute=v.at("attribute").integer(-1);e.mRange=v.at("range").integer();
                e.mArea=std::max(0,std::min(50,v.at("area").integer()));e.mDuration=std::max(0,std::min(1440,v.at("duration").integer()));
                e.mMagnMin=std::max(0,std::min(100,v.at("magnitudeMin").integer()));e.mMagnMax=std::max(int(e.mMagnMin),std::min(100,v.at("magnitudeMax").integer()));
                if(e.mRange<0 || e.mRange>2 || effects.size()>=8)throw std::runtime_error("invalid template");
                effects.push_back(e);
            }
            const int wanted=data.at("castType").integer(-1);
            for(int i=0;i<4 && mEnchanting.getCastStyle()!=wanted;++i)mEnchanting.nextCastStyle();
            mName->setCaption(data.at("name").text());mEffects=std::move(effects);updateEffectsView();
        }
        catch(...){MWBase::Environment::get().getWindowManager()->messageBox("#{arenamp=ce.template_mismatch}");}
    }

    EnchantingDialog::~EnchantingDialog()
    {
        delete mItemSelectionDialog;
    }

    void EnchantingDialog::onOpen()
    {
        center();
        refreshTemplates();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mName);
    }

    void EnchantingDialog::onClose()
    {
        if(!MWBase::Environment::get().getWindowManager()->containsMode(GM_Enchanting))resetReference();
    }

    void EnchantingDialog::setSoulGem(const MWWorld::Ptr &gem)
    {
        if (gem.isEmpty())
        {
            mSoulBox->setItem(MWWorld::Ptr());
            mSoulBox->clearUserStrings();
            mSoulBox->setUserData(MWWorld::Ptr());
            mEnchanting.setSoulGem(MWWorld::Ptr());
        }
        else
        {
            mSoulBox->setItem(gem);
            mSoulBox->setUserString ("ToolTipType", "ItemPtr");
            mSoulBox->setUserData(MWWorld::Ptr(gem));
            mEnchanting.setSoulGem(gem);
        }
    }

    void EnchantingDialog::setItem(const MWWorld::Ptr &item)
    {
        if (item.isEmpty())
        {
            mItemBox->setItem(MWWorld::Ptr());
            mItemBox->clearUserStrings();
            mItemBox->setUserData(MWWorld::Ptr());
            mEnchanting.setOldItem(MWWorld::Ptr());
        }
        else
        {
            mName->setCaption(item.getClass().getName(item));
            mItemBox->setItem(item);
            mItemBox->setUserString ("ToolTipType", "ItemPtr");
            mItemBox->setUserData(MWWorld::Ptr(item));
            mEnchanting.setOldItem(item);
        }
    }

    void EnchantingDialog::updateLabels()
    {
        if(mServerCommit)return;
        mEnchantmentPoints->setCaption(std::to_string(static_cast<int>(mEnchanting.getEnchantPoints(false))) + " / " + std::to_string(mEnchanting.getMaxEnchantValue()));
        mCharge->setCaption(std::to_string(mEnchanting.getGemCharge()));
        mSuccessChance->setCaption(std::to_string(std::max(0, std::min(100, mEnchanting.getEnchantChance()))));
        mCastCost->setCaption(std::to_string(mEnchanting.getEffectiveCastCost()));
        mPrice->setCaption(std::to_string(mEnchanting.getEnchantPrice()));
        const int castCost=mEnchanting.getEffectiveCastCost();
        mUses->setCaption(castCost>0 ? std::to_string(mEnchanting.getGemCharge()/castCost) : "-");

        switch(mEnchanting.getCastStyle())
        {
            case ESM::Enchantment::CastOnce:
                mTypeButton->setCaption(MWBase::Environment::get().getWindowManager()->getGameSettingString("sItemCastOnce","Cast Once"));
                setConstantEffect(false);
                break;
            case ESM::Enchantment::WhenStrikes:
                mTypeButton->setCaption(MWBase::Environment::get().getWindowManager()->getGameSettingString("sItemCastWhenStrikes", "When Strikes"));
                setConstantEffect(false);
                break;
            case ESM::Enchantment::WhenUsed:
                mTypeButton->setCaption(MWBase::Environment::get().getWindowManager()->getGameSettingString("sItemCastWhenUsed", "When Used"));
                setConstantEffect(false);
                break;
            case ESM::Enchantment::ConstantEffect:
                mTypeButton->setCaption(MWBase::Environment::get().getWindowManager()->getGameSettingString("sItemCastConstant", "Cast Constant"));
                setConstantEffect(true);
                break;
        }
        requestServerQuote();
    }

    void EnchantingDialog::setPtr (const MWWorld::Ptr& ptr)
    {
        mServerQuote=0;mServerCommit=0;mQuoteAge=0;
        mName->setCaption("");

        if (ptr.getClass().isActor())
        {
            mEnchanting.setSelfEnchanting(false);
            mEnchanting.setEnchanter(ptr);
            mBuyButton->setCaptionWithReplacing("#{sBuy}");
            mChanceLayout->setVisible(false);
            mPtr = ptr;
            setSoulGem(MWWorld::Ptr());
            mPrice->setVisible(true);
            mPriceText->setVisible(true);
        }
        else
        {
            mEnchanting.setSelfEnchanting(true);
            mEnchanting.setEnchanter(MWMechanics::getPlayer());
            mBuyButton->setCaptionWithReplacing("#{sCreate}");
            bool enabled = Settings::Manager::getBool("show enchant chance","Game");
            mChanceLayout->setVisible(enabled);
            mPtr = MWMechanics::getPlayer();
            setSoulGem(ptr);
            mPrice->setVisible(false);
            mPriceText->setVisible(false);
        }

        setItem(MWWorld::Ptr());
        startEditing ();
        mEffectSearch->setCaption("");
        updateLabels();
    }

    void EnchantingDialog::onReferenceUnavailable ()
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode (GM_Enchanting);
        resetReference();
    }

    void EnchantingDialog::resetReference()
    {
        if(mItemSelectionDialog)mItemSelectionDialog->setVisible(false);
        ReferenceInterface::resetReference();
        setItem(MWWorld::Ptr());
        setSoulGem(MWWorld::Ptr());
        mPtr = MWWorld::Ptr();
        mEnchanting.setEnchanter(MWWorld::Ptr());
    }

    void EnchantingDialog::onCancelButtonClicked(MyGUI::Widget* sender)
    {
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Enchanting);
    }

    void EnchantingDialog::onSelectItem(MyGUI::Widget *sender)
    {
        if (mEnchanting.getOldItem().isEmpty())
        {
            delete mItemSelectionDialog;
            mItemSelectionDialog = new ItemSelectionDialog("#{sEnchantItems}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &EnchantingDialog::onItemSelected);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &EnchantingDialog::onItemCancel);
            mItemSelectionDialog->setVisible(true);
            mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
            mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyEnchantable);
        }
        else
        {
            setItem(MWWorld::Ptr());
            updateLabels();
        }
    }

    void EnchantingDialog::onItemSelected(MWWorld::Ptr item)
    {
        mItemSelectionDialog->setVisible(false);

        setItem(item);
        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        mEnchanting.nextCastStyle();
        updateLabels();
    }

    void EnchantingDialog::onItemCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onSoulSelected(MWWorld::Ptr item)
    {
        mItemSelectionDialog->setVisible(false);

        mEnchanting.setSoulGem(item);
        if(mEnchanting.getGemCharge()==0)
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage32}");
            return;
        }

        setSoulGem(item);
        MWBase::Environment::get().getWindowManager()->playSound(item.getClass().getDownSoundId(item));
        updateLabels();
    }

    void EnchantingDialog::onSoulCancel()
    {
        mItemSelectionDialog->setVisible(false);
    }

    void EnchantingDialog::onSelectSoul(MyGUI::Widget *sender)
    {
        if (mEnchanting.getGem().isEmpty())
        {
            delete mItemSelectionDialog;
            mItemSelectionDialog = new ItemSelectionDialog("#{sSoulGemsWithSouls}");
            mItemSelectionDialog->eventItemSelected += MyGUI::newDelegate(this, &EnchantingDialog::onSoulSelected);
            mItemSelectionDialog->eventDialogCanceled += MyGUI::newDelegate(this, &EnchantingDialog::onSoulCancel);
            mItemSelectionDialog->setVisible(true);
            mItemSelectionDialog->openContainer(MWMechanics::getPlayer());
            mItemSelectionDialog->setFilter(SortFilterItemModel::Filter_OnlyChargedSoulstones);

            //MWBase::Environment::get().getWindowManager()->messageBox("#{sInventorySelectNoSoul}");
        }
        else
        {
            setSoulGem(MWWorld::Ptr());
            mEnchanting.nextCastStyle();
            updateLabels();
            updateEffectsView();
        }
    }

    void EnchantingDialog::notifyEffectsChanged ()
    {
        mEffectList.mList = mEffects;
        mEnchanting.setEffect(mEffectList);
        updateLabels();
    }

    void EnchantingDialog::onTypeButtonClicked(MyGUI::Widget* sender)
    {
        mEnchanting.nextCastStyle();
        updateLabels();
        updateEffectsView();
    }

    void EnchantingDialog::onAccept(MyGUI::EditBox *sender)
    {
        onBuyButtonClicked(sender);

        // To do not spam onAccept() again and again
        MWBase::Environment::get().getWindowManager()->injectKeyRelease(MyGUI::KeyCode::None);
    }

    void EnchantingDialog::onBuyButtonClicked(MyGUI::Widget* sender)
    {
        auto& ce=mwmp::CraftEconomic::get();
        if(ce.mode()==mwmp::CraftEconomic::Unknown || ce.busy())return;
        if(ce.mode()==mwmp::CraftEconomic::Enabled)
        {
            const auto* response=ce.response(mServerQuote);
            if(!response || !response->at("ok").yes())return;
            mServerCommit=ce.commit(response->at("quote").at("token").text());
            mBuyButton->setEnabled(false);
            if(mServerCommit){setItem(MWWorld::Ptr());setSoulGem(MWWorld::Ptr());}
            return; // all item removal, RNG, records and payment happen on the server
        }
        if (mEffects.size() <= 0)
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sEnchantmentMenu11}");
            return;
        }

        if (mName->getCaption ().empty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage10}");
            return;
        }

        if (mEnchanting.soulEmpty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage52}");
            return;
        }

        if (mEnchanting.itemEmpty())
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage11}");
            return;
        }

        if (static_cast<int>(mEnchanting.getEnchantPoints(false)) > mEnchanting.getMaxEnchantValue())
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage29}");
            return;
        }

        mEnchanting.setNewItemName(mName->getCaption());
        mEnchanting.setEffect(mEffectList);

        MWWorld::Ptr player = MWMechanics::getPlayer();
        int playerGold = player.getClass().getContainerStore(player).count(MWWorld::ContainerStore::sGoldId);
        if (mPtr != player && mEnchanting.getEnchantPrice() > playerGold)
        {
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage18}");
            return;
        }

        // check if the player is attempting to use a soulstone or item that was stolen from this actor
        if (mPtr != player)
        {
            for (int i=0; i<2; ++i)
            {
                MWWorld::Ptr item = (i == 0) ? mEnchanting.getOldItem() : mEnchanting.getGem();
                if (MWBase::Environment::get().getMechanicsManager()->isItemStolenFrom(item.getCellRef().getRefId(), mPtr))
                {
                    std::string msg = MWBase::Environment::get().getWorld()->getStore().get<ESM::GameSetting>().find("sNotifyMessage49")->mValue.getString();
                    msg = Misc::StringUtils::format(msg, item.getClass().getName(item));
                    MWBase::Environment::get().getWindowManager()->messageBox(msg);

                    MWBase::Environment::get().getMechanicsManager()->confiscateStolenItemToOwner(player, item, mPtr, 1);

                    MWBase::Environment::get().getWindowManager()->removeGuiMode (GM_Enchanting);
                    MWBase::Environment::get().getWindowManager()->exitCurrentGuiMode();
                    return;
                }
            }
        }

        int result = mEnchanting.create();

        if(result==1)
        {
            MWBase::Environment::get().getWindowManager()->playSound("enchant success");
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sEnchantmentMenu12}");
            MWBase::Environment::get().getWindowManager()->removeGuiMode (GM_Enchanting);

            /*
                Start of tes3mp addition

                Send an ID_OBJECT_SOUND packet every time the player makes a sound here
            */
            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectSound(MWMechanics::getPlayer(), "enchant success", 1.0, 1.0);
            objectList->sendObjectSound();
            /*
                End of tes3mp addition
            */
        }
        else
        {
            MWBase::Environment::get().getWindowManager()->playSound("enchant fail");
            MWBase::Environment::get().getWindowManager()->messageBox ("#{sNotifyMessage34}");

            /*
                Start of tes3mp addition

                Send an ID_OBJECT_SOUND packet every time the player makes a sound here
            */
            mwmp::ObjectList *objectList = mwmp::Main::get().getNetworking()->getObjectList();
            objectList->reset();
            objectList->packetOrigin = mwmp::CLIENT_GAMEPLAY;
            objectList->addObjectSound(MWMechanics::getPlayer(), "enchant fail", 1.0, 1.0);
            objectList->sendObjectSound();
            /*
                End of tes3mp addition
            */

            if (!mEnchanting.getGem().isEmpty() && !mEnchanting.getGem().getRefData().getCount())
            {
                setSoulGem(MWWorld::Ptr());
                mEnchanting.nextCastStyle();
                updateLabels();
                updateEffectsView();
            }
        }
    }
}
