#include <algorithm>
#include <cassert>
#include <iostream>
#include <string>
#include <vector>
namespace MWWorld {struct Ptr {int id=-1;bool isEmpty()const{return id<0;}bool operator==(const Ptr& p)const{return id==p.id;}bool operator!=(const Ptr& p)const{return id!=p.id;}};}
namespace MWMechanics {inline MWWorld::Ptr getPlayer(){return {0};}}
namespace mwmp {struct CraftEconomic {enum {Enabled=2};static CraftEconomic& get(){static CraftEconomic c;return c;}int mode(){return Enabled;}};}
namespace MWGui
{
    struct Widget {int value=0;bool shown=true;void setVisible(bool b){shown=b;}void setEnabled(bool){}std::string getCaption(){return "filter";}};
    struct ContainerItemModel {ContainerItemModel(const std::vector<MWWorld::Ptr>&,const std::vector<MWWorld::Ptr>&){}};
    struct TradeItemModel
    {
        ContainerItemModel* source;int aborted=0;static inline int live=0;
        TradeItemModel(ContainerItemModel* s,MWWorld::Ptr):source(s){++live;}
        ~TradeItemModel(){delete source;--live;}void abort(){++aborted;}
    };
    struct SortFilterItemModel {TradeItemModel* source;explicit SortFilterItemModel(TradeItemModel* s):source(s){}~SortFilterItemModel(){delete source;}void setNameFilter(const std::string&){}};
    struct ItemView {SortFilterItemModel* source=nullptr;void setModel(SortFilterItemModel* s){delete source;source=s;}void update(){}};
    struct InventoryWindow {TradeItemModel model{nullptr,{0}};int updates=0;TradeItemModel* getTradeModel(){return &model;}void updateItemView(){++updates;}};
}
namespace MWBase
{
    struct WindowManager {MWGui::InventoryWindow inventory;MWGui::Widget count;int drags=0;void finishDragDrop(){++drags;}MWGui::InventoryWindow* getInventoryWindow(){return &inventory;}MWGui::Widget* getCountDialog(){return &count;}};
    struct Environment {WindowManager wm;static Environment& get(){static Environment e;return e;}WindowManager* getWindowManager(){return &wm;}};
}
namespace MWGui
{
    struct TradeWindow
    {
        MWWorld::Ptr mPtr{10};std::vector<MWWorld::Ptr> mEconomicSources{{10},{11}};
        TradeItemModel* mTradeModel=nullptr;SortFilterItemModel* mSortModel=nullptr;ItemView view;ItemView* mItemView=&view;
        Widget filter,offer;Widget* mFilterEdit=&filter;Widget* mOfferButton=&offer;Widget* mFilterAll=&filter;
        unsigned mServerQuote=3,mServerCommit=0;float mQuoteAge=20;int mItemToSell=5,requests=0;
        void requestServerQuote(){++requests;}void onFilterChanged(Widget*){}
        bool prepareInventoryReplace(const MWWorld::Ptr&);
        void finishInventoryReplace(bool);
    };
}
