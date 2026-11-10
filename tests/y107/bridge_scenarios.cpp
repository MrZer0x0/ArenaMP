int main()
{
    using namespace mwmp;using ArenaCraft::Value;
    auto& ce=CraftEconomic::get();auto& main=Main::get();
    for(int i=0;i<11;++i)ce.update(1);
    assert(ce.mode()==CraftEconomic::Disabled && main.net.packet.sent.empty());
    ce.reset();
    const auto advertise=[](bool enabled){auto v=Value::dict();v["v"]=1;v["seq"]=0;v["ok"]=true;v["enabled"]=enabled;v["contentKey"]="set-one";return std::string(ArenaCraft::Prefix)+ArenaCraft::encode(v);};
    assert(ce.receive(advertise(true),false) && ce.mode()==CraftEconomic::Unknown);
    assert(ce.receive(advertise(true),true) && ce.mode()==CraftEconomic::Enabled && ce.contentKey()=="set-one");
    auto q=Value::dict();q["kind"]="trade";q["test"]=1;
    const auto old=ce.quote(q);q["test"]=2;const auto current=ce.quote(q);assert(old!=current);
    ce.update(.19f);assert(main.net.packet.sent.empty());ce.update(.02f);assert(main.net.packet.sent.size()==1);
    auto wire=ArenaCraft::Parser(main.net.packet.sent.back().substr(std::string(ArenaCraft::Prefix).size())).parse();
    assert(wire.at("test").integer()==2 && wire.at("seq").integer()==int(current));
    assert(main.player.chatMessage=="previous chat");
    ce.update(1);ce.update(1);assert(main.net.packet.sent.size()==2 && main.net.packet.sent[0]==main.net.packet.sent[1]);
    auto response=Value::dict();response["v"]=1;response["seq"]=current;response["ok"]=true;response["quote"]=Value::dict();response["quote"]["token"]=std::string(32,'a');
    ce.receive(std::string(ArenaCraft::Prefix)+ArenaCraft::encode(response),true);
    assert(ce.response(current)->at("ok").yes());
    const auto commit=ce.commit(std::string(32,'a'));assert(commit && ce.busy());
    assert(ce.commit(std::string(32,'b'))==0 && ce.commit(std::string(32,'a'))==commit);
    main.net.connected=false;for(int i=0;i<30;++i)ce.update(1);assert(main.net.packet.sent.size()==2 && ce.busy());
    main.net.connected=true;ce.update(.01f);assert(main.net.packet.sent.size()==3);
    wire=ArenaCraft::Parser(main.net.packet.sent.back().substr(std::string(ArenaCraft::Prefix).size())).parse();
    assert(wire.at("token").text()==std::string(32,'a') && wire.at("seq").integer()==int(commit));
    ce.update(1);ce.update(1);assert(main.net.packet.sent.back()==main.net.packet.sent[2]);
    auto pending=Value::dict();pending["v"]=1;pending["seq"]=commit;pending["ok"]=false;pending["error"]="delivery_pending";
    ce.receive(std::string(ArenaCraft::Prefix)+ArenaCraft::encode(pending),true);assert(ce.busy());
    response=Value::dict();response["v"]=1;response["seq"]=commit;response["ok"]=true;response["kind"]="enchant";response["created"]=true;
    response["xp"]=Value::dict();response["xp"]["skill"]="Enchant";response["xp"]["factor"]=4;
    const auto message=std::string(ArenaCraft::Prefix)+ArenaCraft::encode(response);
    main.net.connected=false;ce.receive(message,true);assert(ce.busy() && MWWorld::skillRewards==0);
    main.net.connected=true;ce.update(.01f);assert(!ce.busy() && ce.response(commit)->at("created").yes());
    ce.receive(message,true);assert(MWBase::Environment::get().window.messages.size()==1); assert(MWWorld::skillRewards==1); // retry cannot repeat XP or feedback
    assert(ce.receive(std::string(ArenaCraft::Prefix)+"{",true));assert(!ce.receive("ordinary chat",true));
    // Menu waits for the latest basket quote; no consignment of a previous draft.
    const auto before=main.net.packet.sent.size();const auto menuQuote=ce.quote(q);ce.menu(MWWorld::Ptr());
    ce.update(.3f);assert(main.net.packet.sent.size()==before+1);
    response["seq"]=menuQuote;ce.receive(std::string(ArenaCraft::Prefix)+ArenaCraft::encode(response),true);
    ce.update(.1f);assert(main.net.packet.sent.size()==before+2);
    wire=ArenaCraft::Parser(main.net.packet.sent.back().substr(std::string(ArenaCraft::Prefix).size())).parse();assert(wire.at("op").text()=="menu");
    ce.reset();assert(ce.mode()==CraftEconomic::Unknown && !ce.busy() && ce.contentKey().empty());
    std::cout<<"PASS production CraftEconomic bridge methods: debounce, negotiation, retry, reconnect, feedback and ordered menu\n";
}
