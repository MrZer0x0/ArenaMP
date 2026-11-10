using namespace mwmp;
static unsigned assertions=0;
static void check(bool ok,const char* message){++assertions;if(!ok)throw std::runtime_error(message);}
static RakNet::Packet event(unsigned id,RakNet::RakNetGUID guid,RakNet::SystemAddress address){
    return RakNet::Packet({static_cast<unsigned char>(id)},guid,address);
}
struct Fixture {
    RakNet::RakPeerInterface clientPeer,serverPeer;
    server::Networking server;
    client::Networking client;
    Player* original=nullptr;
    Fixture():server(&serverPeer),client(&clientPeer){
        Players::clear();Script::connects=Script::disconnects=0;
        PlayerProcessor::chats=ObjectProcessor::activations=0;Utils::hashes=0;
        CellController::get()->cells.clear();
        clientPeer.own={100};clientPeer.address={100};serverPeer.own={900};serverPeer.address={200};
        clientPeer.remote=&serverPeer;serverPeer.remote=&clientPeer;
        clientPeer.pump=[this]{drainServer();};
        client.preInit(client.mReconnectContent,client.mReconnectGroundcover,client.collections);
        original=Players::getPlayer(clientPeer.own);
        check(original!=nullptr&&client.connected,"initial pre-init creates player");
        check(client.mResumeToken.size()==64,"client saves initial server token");
        check(server.resumeSessions.at(clientPeer.own).token==client.mResumeToken,"both sides share token");
        check(Utils::hashes==1,"one hash read across manifest phases");
        while(!clientPeer.queue.empty()){clientPeer.DeallocatePacket(clientPeer.queue.front());clientPeer.queue.pop_front();}
        original->setHandshake();
        send(ID_LOADED);drainServer();
        original->authoritative=original->visible=true;
        check(Script::connects==1&&original->loadState==Player::POSTLOADED,"initial login still works");
    }
    ~Fixture(){CellController::get()->cells.clear();Players::clear();}
    void drainServer(){while(auto* p=serverPeer.Receive()){server.deliver(p);serverPeer.DeallocatePacket(p);}}
    void send(unsigned id){
        TestPacket packet(&clientPeer,id);RakNet::BitStream stream;
        packet.SetSendStream(&stream);packet.setPlayer(&client.local);packet.Send();
    }
    void lose(){
        auto lost=event(ID_CONNECTION_LOST,clientPeer.own,clientPeer.address);server.deliver(&lost);
        client.beginConnectionRestore();
        check(server.suspendedSessions.size()==1&&Players::getPlayer(clientPeer.own)==original,"logical player held");
        check(Script::disconnects==0&&!client.connected,"loss does not log out player");
    }
    bool restore(uint64_t newGuid=100){
        clientPeer.own={newGuid};clientPeer.address={newGuid};
        auto incoming=event(ID_NEW_INCOMING_CONNECTION,clientPeer.own,clientPeer.address);server.deliver(&incoming);
        auto accepted=event(ID_CONNECTION_REQUEST_ACCEPTED,serverPeer.own,serverPeer.address);
        return client.completeConnectionRestore(&accepted);
    }
    void drop(unsigned id){
        for(auto i=clientPeer.queue.begin();i!=clientPeer.queue.end();){
            if((*i)->data[0]==id){clientPeer.DeallocatePacket(*i);i=clientPeer.queue.erase(i);}else ++i;
        }
    }
    void interaction(){
        auto* chat=client.playerPacketController.GetPacket(ID_CHAT_MESSAGE);chat->setPlayer(&client.local);chat->Send();
        auto* activation=client.objectPacketController.GetPacket(ID_OBJECT_ACTIVATE);activation->setPlayer(&client.local);activation->Send();
        drainServer();
    }
};
int main(){
    {
        Fixture f;const auto token=f.client.mResumeToken;
        f.client.local.charGenState.endStage=2;
        f.lose();f.interaction();
        check(PlayerProcessor::chats==0&&ObjectProcessor::activations==0,"gameplay sends blocked during outage");
        auto* packet=f.client.objectPacketController.GetPacket(ID_OBJECT_ACTIVATE);
        const auto count=f.clientPeer.sent.size();
        packet->Send(false);packet->Send(RakNet::RakNetGUID{900});packet->RequestData({900});
        check(f.clientPeer.sent.size()==count,"all BasePacket send entry points obey gate");
        const auto deadline=f.server.suspendedSessions.at({100}).expiresAt;
        auto close=event(ID_DISCONNECTION_NOTIFICATION,{100},{100});f.server.deliver(&close);
        auto lost=event(ID_CONNECTION_LOST,{100},{100});f.server.deliver(&lost);
        check(f.server.suspendedSessions.at({100}).expiresAt==deadline,"stale close/loss preserve grace deadline");
        check(f.restore(),"same-GUID resume accepted");
        check(!f.client.connected&&f.client.mAwaitingRestoreAck,"transport acceptance is not recovery");
        check(f.client.local.isLoginSyncPending(),"resume forces profile gate even after CharGen");
        check(f.client.mResumeToken!=token,"token rotates on resume");
        check(f.server.resumeSessions.at({100}).token==f.client.mResumeToken,"rotated token saved on both ends");
        check(Utils::hashes==1,"resume never rereads loaded plugins");
        check(Players::created==1&&Players::getPlayer({100})==f.original,"same native player retained");
        auto* resumeAck=f.serverPeer.queue.front();f.serverPeer.queue.pop_front();
        f.send(ID_CHAT_MESSAGE);f.send(ID_OBJECT_ACTIVATE);f.drainServer();
        check(PlayerProcessor::chats==0&&ObjectProcessor::activations==0,"server also rejects gameplay before resume ACK");
        f.serverPeer.queue.push_front(resumeAck);
        f.client.update(0);
        check(f.client.connected&&!f.client.mRestoringConnection,"profile plus server ACK completes recovery");
        f.serverPeer.connectionState=IS_CONNECTED;
        f.server.deliver(&lost);f.server.deliver(&close);
        check(f.server.suspendedSessions.empty()&&Players::getPlayer({100})==f.original,"late old transport events cannot break connected replacement");
        f.serverPeer.connectionState=IS_NOT_CONNECTED;
        f.clientPeer.connectionState=IS_CONNECTED;
        f.clientPeer.queue.push_back(new RakNet::Packet({ID_CONNECTION_LOST},f.serverPeer.own,f.serverPeer.address));
        f.clientPeer.queue.push_back(new RakNet::Packet({ID_DISCONNECTION_NOTIFICATION},f.serverPeer.own,f.serverPeer.address));
        f.client.update(0);
        check(f.client.connected&&!f.client.mRestoringConnection,"late client transport events cannot restart recovery");
        f.clientPeer.connectionState=IS_NOT_CONNECTED;
        check(f.client.local.mLoginSyncReceived==31&&f.client.local.baselines==1,"five profile sections applied");
        check(Script::connects==1&&Script::disconnects==0,"resume does not replace Lua login");
        const auto chats=PlayerProcessor::chats,acts=ObjectProcessor::activations;
        f.interaction();
        check(PlayerProcessor::chats==chats+1&&ObjectProcessor::activations==acts+1,"commands and activation dispatch after resume");
        f.send(ID_LOADED);f.drainServer();check(Script::connects==1,"duplicate LOADED cannot restart login");
        f.lose();check(f.restore(),"second outage uses rotated credential");f.client.update(0);
        check(f.client.connected&&Players::created==1,"repeated recovery retains one player slot");
    }
    std::cout<<"PASS same GUID, rotating tokens, repeat outage, send gates, preserved login, commands and activation\n";
    {
        Fixture f;Cell cell;cell.authorityGuid={100};cell.cellActorList.guid={100};
        BaseActor actor;actor.aiTarget={true,{100}};actor.aiCombatTargets.push_back({true,{100}});
        actor.attack.target={true,{100}};actor.cast.target={true,{100}};actor.killer={true,{100}};
        actor.spellsActiveChanges.activeSpells.push_back({{true,{100}}});
        cell.cellActorList.baseActors.push_back(actor);cell.cellActorAIList.baseActors.push_back(actor);
        cell.players.push_back(f.original);f.original->cells.push_back(&cell);CellController::get()->cells.push_back(&cell);
        f.original->alliedPlayers.push_back({100});f.original->pendingCellChangeRecipients.insert({100});
        f.original->attack.target={true,{100}};f.original->spellsActiveChanges=actor.spellsActiveChanges;
        f.lose();check(f.restore(101),"new GUID resume accepted");f.client.update(0);
        check(f.client.connected&&Players::getPlayer({101})==f.original&&!Players::getPlayer({100}),"new GUID keeps original object");
        check(f.original->alliedPlayers[0].g==101&&f.original->pendingCellChangeRecipients.count({101}),"player GUID references rebound");
        check(f.original->attack.target.guid.g==101&&f.original->spellsActiveChanges.activeSpells[0].caster.guid.g==101,"player target references rebound");
        check(cell.authorityGuid.g==101&&cell.cellActorList.guid.g==101,"cell authority rebound");
        for(const auto* list:{&cell.cellActorList,&cell.cellActorAIList}){
            const auto& a=list->baseActors[0];
            check(a.aiTarget.guid.g==101&&a.aiCombatTargets[0].guid.g==101,"AI references rebound");
            check(a.attack.target.guid.g==101&&a.cast.target.guid.g==101&&a.killer.guid.g==101,"actor targets rebound");
            check(a.spellsActiveChanges.activeSpells[0].caster.guid.g==101,"actor spell caster rebound");
        }
        Players::newPlayer({102});
        check(!Players::rebindPlayerGuid({101},{102}),"occupied destination cannot overwrite player");
        check(!Players::rebindPlayerGuid({999},{101}),"missing source cannot bind");
        f.interaction();check(PlayerProcessor::chats==1&&ObjectProcessor::activations==1,"new GUID accepts actions");
    }
    std::cout<<"PASS changed GUID, authority, followers, combat/spell targets and collision protection\n";
    {
        Fixture f;f.lose();f.client.mResumeToken="forged-token";
        check(!f.restore(),"invalid token refused");
        check(!f.client.connected&&!f.client.mRestoreError.empty(),"refusal gives explicit recovery error");
        check(Players::created==1&&Script::connects==1&&f.server.awaitingResumeAcks.empty(),"refusal never starts fresh login");
    }
    {
        Fixture f;f.lose();f.client.mSessionIdentity="wrong-process";
        check(!f.restore(),"wrong identity refused");check(Players::created==1,"wrong identity cannot allocate slot");
    }
    {
        Fixture f;f.lose();f.server.suspendedSessions.at({100}).expiresAt=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        check(!f.restore(),"expired proof refused before sweeper");
        f.server.expireSuspendedPlayers();check(Script::disconnects==1&&Players::deleted==1,"expiry completes one disconnect");
    }
    {
        Fixture f;f.client.beginConnectionRestore();f.serverPeer.connectionState=IS_CONNECTED;
        check(!f.restore(101),"another live transport cannot be taken over");
        check(Players::getPlayer({100})==f.original,"live player remains intact after refused takeover");
    }
    {
        Fixture f;f.client.mResumeToken.clear();f.client.beginConnectionRestore();
        check(!f.client.mRestoringConnection&&!f.client.mRestoreError.empty(),"legacy server without token cannot fake recovery");
    }
    std::cout<<"PASS invalid/expired credentials, active-connection protection and legacy refusal\n";
    {
        Fixture f;f.lose();f.server.capacity=1;
        for(bool enforce:{true,false}){
            server::dataFileEnforcementState=enforce;
            RakNet::RakPeerInterface stranger;stranger.own={101};stranger.address={101};stranger.remote=&f.serverPeer;
            stranger.pump=[&f]{f.drainServer();};f.serverPeer.remote=&stranger;
            client::Networking newcomer(&stranger);newcomer.mSessionIdentity="other-session";
            newcomer.preInit(newcomer.mReconnectContent,newcomer.mReconnectGroundcover,newcomer.collections);
            check(Players::created==1&&!Players::getPlayer({101}),"held session reserves PID slot against fresh login");
            check(!newcomer.connected&&newcomer.mResumeToken.empty(),"fresh login refusal cannot fake online or issue token");
        }
        server::dataFileEnforcementState=true;f.serverPeer.remote=&f.clientPeer;
        check(f.restore(),"original session can reclaim its reserved full-server slot");f.client.update(0);
        check(f.client.connected&&Players::created==1,"full server resume needs no additional slot");
    }
    {
        Fixture f;server::dataFileEnforcementState=false;f.lose();
        check(f.restore(),"one-phase resume without manifest enforcement");f.client.update(0);
        check(f.client.connected&&Players::created==1,"one-phase flow retains player");
        server::dataFileEnforcementState=true;
    }
    {
        Fixture f;f.client.beginConnectionRestore();
        check(f.restore(),"new incoming same GUID handles loss event still queued");f.client.update(0);
        check(f.client.connected&&Script::disconnects==0,"new incoming event suspends old session for verification");
    }
    for(unsigned missing:{unsigned(ID_LOADED),unsigned(ID_PLAYER_LEVEL)}){
        Fixture f;f.lose();check(f.restore(),"resume starts for delayed acknowledgement test");
        f.drainServer();f.drop(missing);f.client.update(0);
        check(!f.client.connected&&f.client.mRestoringConnection,"ACK alone or profile alone cannot report recovery");
        f.client.mRestoreDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);f.client.update(0);
        check(!f.client.connected&&!f.client.mRestoringConnection,"confirmation timeout cannot leave fake online state");
    }
    {
        Fixture f;f.lose();check(f.restore(),"resume starts for server ACK timeout test");
        while(!f.serverPeer.queue.empty()){f.serverPeer.DeallocatePacket(f.serverPeer.queue.front());f.serverPeer.queue.pop_front();}
        f.server.awaitingResumeAcks.at({100})=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        f.server.expireSuspendedPlayers();check(Script::disconnects==1&&Players::deleted==1,"abandoned resume expires on server");
    }
    {
        Fixture f;f.lose();f.client.mRestoreDeadline=std::chrono::steady_clock::now()-std::chrono::seconds(1);
        check(!f.restore(),"pre-init honors existing restore deadline");
    }
    std::cout<<"PASS incomplete profile/ACK, bounded pre-init and server/client deadlines\n";
    std::cout<<"PASS "<<assertions<<" assertions using production resume methods and packet serializers\n";
}
