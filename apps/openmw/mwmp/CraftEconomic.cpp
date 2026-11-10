#include "CraftEconomic.hpp"
#include <components/misc/economypricing.hpp>
#include "Main.hpp"
#include "Networking.hpp"
#include "LocalPlayer.hpp"
#include "../mwworld/ptr.hpp"
#include "../mwworld/class.hpp"
#include "../mwmechanics/actorutil.hpp"
#include "../mwmechanics/xpleveling.hpp"
#include <components/esm/loadskil.hpp>
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include <components/openmw-mp/NetworkMessages.hpp>
#include <components/openmw-mp/Packets/Player/PlayerPacket.hpp>
#include <MyGUI_LanguageManager.h>
#include <algorithm>

namespace mwmp
{
    CraftEconomic& CraftEconomic::get() { static CraftEconomic instance;return instance; }
    void CraftEconomic::reset() { *this=CraftEconomic(); }
    unsigned CraftEconomic::sequence() { if(++mSequence>1000000000) {mSequence=1;mResponses.clear();}return mSequence; }
    bool CraftEconomic::send(const Value& value)
    {
        auto* net=Main::get().getNetworking();
        auto* player=Main::get().getLocalPlayer();
        if(!net || !net->isConnected() || !player || !player->isLoggedIn())return false;
        std::string raw;
        try {raw=std::string(ArenaCraft::Prefix)+ArenaCraft::encode(value);}catch(...){return false;}
        if(raw.size()>49152)return false;
        const auto previous=player->chatMessage;
        player->chatMessage=std::move(raw);
        auto* packet=net->getPlayerPacket(ID_CHAT_MESSAGE);
        packet->setPlayer(player);packet->Send();
        player->chatMessage=previous;
        return true;
    }
    unsigned CraftEconomic::quote(Value request)
    {
        if(mMode!=Enabled || busy())return 0;
        request["v"]=1;request["op"]="quote";request["seq"]=sequence();
        mDraft=std::move(request);mDraftDelay=0.2f;
        return static_cast<unsigned>(mDraft.at("seq").integer());
    }
    unsigned CraftEconomic::commit(const std::string& token)
    {
        if(mMode!=Enabled || token.size()!=32)return 0;
        if(busy())return mCommit.at("token").text()==token ? mCommitSequence : 0;
        mDraft=Value();mCommit=Value::dict();mCommit["v"]=1;mCommit["op"]="commit";
        mCommitSequence=sequence();mCommit["seq"]=mCommitSequence;mCommit["token"]=token;mRetry=0;
        return mCommitSequence;
    }
    void CraftEconomic::menu(const MWWorld::Ptr& ptr)
    {
        if(mMode!=Enabled || busy())return;
        auto request=Value::dict();request["v"]=1;request["seq"]=sequence();request["op"]="menu";
        if(!ptr.isEmpty())request["merchant"]=merchant(ptr);
        mMenu=std::move(request);
    }
    void CraftEconomic::update(float dt)
    {
        if(!Main::get().getNetworking()->isConnected() || !Main::get().getLocalPlayer()->isLoggedIn())return;
        auto deferred=std::move(mDeferred);mDeferred.clear();
        for(auto& value:deferred)accept(std::move(value));
        dt=std::max(0.f,std::min(1.f,dt));
        // New servers advertise privately at authentication. Old servers receive
        // no machine traffic and keep their original trading path.
        if(mMode==Unknown && (mNegotiation+=dt)>10.f)mMode=Disabled;
        if(busy())
        {
            mRetry-=dt;
            if(mRetry<=0 && send(mCommit))mRetry=2.f;
        }
        else if(mDraft.type==Value::Object)
        {
            mDraftDelay-=dt;
            if(mDraftDelay<=0 && send(mDraft))mDraftDelay=2.f;
        }
        else if(mMenu.type==Value::Object && send(mMenu))mMenu=Value();
    }
    void CraftEconomic::accept(Value value)
    {
        if(value.at("v").integer()!=1)return;
        const int seq=value.at("seq").integer(-1);if(seq<0)return;
        if(value.at("enabled").type==Value::Boolean)
        {mMode=value.at("enabled").yes() ? Enabled : Disabled;mContentKey=value.at("contentKey").text();return;}
        if(seq==0)return;
        if(seq==mDraft.at("seq").integer(-1))mDraft=Value();
        if(static_cast<unsigned>(seq)==mCommitSequence)
        {
            const auto error=value.at("error").text();
            if(!value.at("ok").yes() && (error=="delivery_pending" || error=="materialize_failed" || error=="journal_clear_failed"))
            {mRetry=2.f;return;} // preserve the same commit while its persisted result is being recovered
            mCommitSequence=0;mCommit=Value();
            // Report the authoritative result even if the user closed its window
            // while the transport was restoring the session.
            if(value.at("ok").yes())
            {
                const auto player=MWMechanics::getPlayer();
                const auto& xp=value.at("xp");
                const double factor=xp.at("factor").number;
                if(xp.at("skill").text()=="Enchant" && factor>0 && factor<=4)
                    player.getClass().skillUsageSucceeded(player,ESM::Skill::Enchant,2,static_cast<float>(factor));
                if(value.at("traded").yes())
                {
                    MWMechanics::XPLeveling::awardSuccessfulTrade(player);
                    if(xp.at("skill").text()=="Mercantile" && factor>0 && factor<=2000000)
                        player.getClass().skillUsageSucceeded(player,ESM::Skill::Mercantile,0,static_cast<float>(factor));
                }
            }
            std::string message;
            if(!value.at("ok").yes())message=status(value);
            else if(value.at("kind").text()=="enchant")
                message=value.at("created").yes() ? "#{sEnchantmentMenu12}" : "#{sNotifyMessage34}";
            if(!message.empty())MWBase::Environment::get().getWindowManager()->messageBox(message);
        }
        mResponses[static_cast<unsigned>(seq)]=std::move(value);
        while(mResponses.size()>32)mResponses.erase(mResponses.begin());
    }
    int CraftEconomic::adjustedPrice(int base,const std::string& service,const std::string& itemId,bool buying) const
    { return ArenaEconomy::price(base,mPricing,service,itemId,buying); }
    bool CraftEconomic::receive(const std::string& message,bool fromLocalPlayer)
    {
        const std::string coefficients="@@ARENA_ECO2@@";
        if (message.compare(0,coefficients.size(),coefficients)==0)
        {
            if (fromLocalPlayer)
                try {
                    auto value=ArenaCraft::Parser(message.substr(coefficients.size())).parse();
                    if (value.at("v").integer()==1 && value.at("items").object.size()<=256)
                        mPricing=std::move(value);
                } catch (...) {}
            return true;
        }
        const std::string prefix=ArenaCraft::Prefix;
        if(message.compare(0,prefix.size(),prefix)!=0)return false;
        // Consume malformed/reserved messages without displaying JSON in chat.
        if(fromLocalPlayer)
            try
            {
                auto value=ArenaCraft::Parser(message.substr(prefix.size())).parse();
                if(value.at("seq").integer()>0 && (!Main::get().getNetworking()->isConnected() || !Main::get().getLocalPlayer()->isLoggedIn()))
                {if(mDeferred.size()<32)mDeferred.push_back(std::move(value));}
                else accept(std::move(value));
            }catch(...){}
        return true;
    }
    const CraftEconomic::Value* CraftEconomic::response(unsigned seq) const
    {
        const auto i=mResponses.find(seq);return i==mResponses.end() ? nullptr : &i->second;
    }
    CraftEconomic::Value CraftEconomic::item(const MWWorld::Ptr& ptr,int count)
    {
        auto out=Value::dict();const auto& ref=ptr.getCellRef();
        out["refId"]=ref.getRefId();out["count"]=count;out["charge"]=ref.getCharge();
        out["enchantmentCharge"]=double(ref.getEnchantmentCharge());out["soul"]=ref.getSoul();
        out["poisonId"]=ptr.getRefData().getPoisonId();out["poisonCharges"]=ptr.getRefData().getPoisonCharges();
        return out;
    }
    CraftEconomic::Value CraftEconomic::merchant(const MWWorld::Ptr& ptr)
    {
        auto out=Value::dict();const auto& ref=ptr.getCellRef();
        out["uid"]=std::to_string(ref.getRefNum().mIndex)+"-"+std::to_string(ref.getMpNum());out["refId"]=ref.getRefId();return out;
    }
    std::string CraftEconomic::status(const Value& value)
    {
        std::string key=value.at("error").text(value.at("quote").at("blocked").text());
        static const std::map<std::string,std::string> messages {
            {"insufficient_gold","#{sNotifyMessage18}"},{"merchant_gold","#{sBarterDialog2}"},
            {"capacity_exceeded","#{sNotifyMessage29}"},{"invalid_name","#{sNotifyMessage10}"},
            {"stolen_goods","#{arenamp=ce.stolen}"},{"inventory_pending","#{arenamp=ce.inventory_pending}"},
            {"weak_soul","#{arenamp=ce.weak_soul}"},{"effect_not_known","#{arenamp=ce.effect_not_known}"}
        };
        const auto found=messages.find(key);
        return found!=messages.end() ? found->second : "#{arenamp=ce.refresh}";
    }
}
