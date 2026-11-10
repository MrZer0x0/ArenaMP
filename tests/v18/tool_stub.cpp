// The runner inserts the actual consumeToolUse function from security.cpp.
#include <cassert>
#include <memory>
#include <vector>
#include <iostream>
struct Store;
struct Row { int count=1,charge=-1; void setCharge(int c) {charge=c;} };
namespace MWWorld { struct Ptr {
 std::shared_ptr<Row> row;Store* store=nullptr;
 Store* getContainerStore() const {return store;}
 Row& getCellRef() const {return *row;}
 bool operator==(const Ptr& p) const {return row==p.row;}
}; }
MWWorld::Ptr player;
MWWorld::Ptr getPlayer() {return player;}
struct Item {int charge,count;};
namespace mwmp {
namespace InventoryChanges {enum {REMOVE=2,ADD=1};}
struct Delta {Item item;int action;};
struct LocalPlayer {
 bool avoidSendingInventoryPackets=false;
 std::vector<Delta> sent;
 void sendItemChange(Item i,int action) {sent.push_back({i,action});}
 void sendItemChange(const MWWorld::Ptr& p,int count,int action) {sendItemChange({p.row->charge,count},action);}
};
LocalPlayer client;
struct Main {static Main& get(){static Main m;return m;} LocalPlayer* getLocalPlayer(){return &client;}};
}
namespace MechanicsHelper {Item getItem(const MWWorld::Ptr& p,int count) {return {p.row->charge,count};}}
struct Store {
 std::vector<std::shared_ptr<Row>> rows;
 void unstack(const MWWorld::Ptr& p,const MWWorld::Ptr&,int n) {
  if(p.row->count<=n)return;
  auto rest=std::make_shared<Row>(*p.row);rest->count-=n;rows.push_back(rest);
  if(!mwmp::client.avoidSendingInventoryPackets) {
   mwmp::client.sendItemChange(p,rest->count,mwmp::InventoryChanges::ADD);
   mwmp::client.sendItemChange(p,rest->count,mwmp::InventoryChanges::REMOVE);
  }
  p.row->count=n;
 }
 void remove(const MWWorld::Ptr& p,int n,const MWWorld::Ptr&) {
  p.row->count-=n;
  if(!mwmp::client.avoidSendingInventoryPackets) mwmp::client.sendItemChange(p,n,mwmp::InventoryChanges::REMOVE);
 }
};
// INSERT_ACTUAL_HELPER
int main() {
 Store s;auto row=std::make_shared<Row>();row->count=10;s.rows.push_back(row);
 MWWorld::Ptr tool{row,&s};player={std::make_shared<Row>(),nullptr};
 consumeToolUse(player,tool,24);
 assert(row->count==1 && row->charge==24);
 assert(s.rows.size()==2 && s.rows[1]->count==9 && s.rows[1]->charge==-1);
 auto& sent=mwmp::client.sent;
 assert(sent.size()==2 && sent[0].action==2 && sent[0].item.charge==-1 && sent[0].item.count==1);
 assert(sent[1].action==1 && sent[1].item.charge==24 && sent[1].item.count==1);
 assert(!mwmp::client.avoidSendingInventoryPackets);
 sent.clear();consumeToolUse(player,tool,23);
 assert(sent.size()==2 && sent[0].item.charge==24 && sent[1].item.charge==23);
 row->charge=1;sent.clear();consumeToolUse(player,tool,0);
 assert(row->count==0 && s.rows[1]->count==9 && s.rows[1]->charge==-1);
 assert(sent.size()==1 && sent[0].action==2 && sent[0].item.charge==1);
 mwmp::client.avoidSendingInventoryPackets=true;sent.clear();
 auto other=std::make_shared<Row>();other->count=2;s.rows.push_back(other);
 consumeToolUse(player,{other,&s},24);
 assert(sent.empty() && mwmp::client.avoidSendingInventoryPackets);
 std::cout<<"actual tool consumption helper passed\n";
}
