// Headless UI doubles. Production event handlers are compiled by run_checks.py.
#include <algorithm>
#include <any>
#include <cassert>
#include <cmath>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace MyGUI
{
    template<class... Args> struct Event
    {
        std::vector<std::function<void(Args...)>> handlers;
        Event& operator+=(std::function<void(Args...)> handler)
        { handlers.push_back(std::move(handler)); return *this; }
        void operator()(Args... args) { for (auto& handler : handlers) handler(args...); }
    };
    template<class T, class... Args>
    std::function<void(Args...)> newDelegate(T* target, void (T::*method)(Args...))
    { return [target,method](Args... args) { (target->*method)(args...); }; }
    struct IntPoint { int left=0, top=0; };
    enum class MouseButton { Left, Right };
    enum class KeyCode { Return, NumpadEnter, Space, ArrowUp, ArrowDown, ArrowLeft, ArrowRight, Escape };
    struct Widget
    {
        std::any data;
        virtual ~Widget() = default;
        template<class T> T* getUserData() { return std::any_cast<T>(&data); }
        Event<Widget*> eventMouseButtonClick;
    };
    struct TextBox : Widget
    {
        std::string caption;
        void setCaptionWithReplacing(const std::string& text) { caption=text; }
    };
    struct EditBox : TextBox
    {
        Event<EditBox*> eventEditTextChange;
        void setCaption(const std::string& text) { caption=text; }
        const std::string& getCaption() const { return caption; }
    };
    struct Button : Widget {};
    struct ScrollView : Widget { int getHeight() const { return 200; } };
    struct InputManager
    {
        static InputManager& getInstance() { static InputManager input; return input; }
        IntPoint getMousePosition() const { return {}; }
    };
}
namespace MWWorld
{
    struct Ptr;
    struct Class { std::string getDownSoundId(const Ptr&) const { return "item down"; } };
    struct Ptr
    {
        std::string id;
        bool isEmpty() const { return id.empty(); }
        const Class& getClass() const { static Class cls; return cls; }
    };
}
namespace MWMechanics { inline MWWorld::Ptr getPlayer() { return {"player"}; } }
namespace MWBase
{
    struct WindowManager
    {
        int sounds=0, messages=0;
        void playSound(const std::string&) { ++sounds; }
        void messageBox(const std::string&) { ++messages; }
    };
    struct Environment
    {
        WindowManager manager;
        static Environment& get() { static Environment environment; return environment; }
        WindowManager* getWindowManager() { return &manager; }
    };
}
namespace MWGui
{
    struct ItemStack { MWWorld::Ptr mBase; };
    struct ItemModel
    {
        using ModelIndex=int;
        std::vector<ItemStack> items;
        virtual ~ItemModel() = default;
        virtual void update() {}
        std::size_t getItemCount() const { return items.size(); }
        ItemStack getItem(int index) const { return items.at(index); }
    };
    inline std::vector<ItemStack> fixtureInventory={{{"robe"}},{{"sword"}},{{"armor"}},{{"scroll"}},{{"filled_gem"}}};
    struct InventoryItemModel : ItemModel
    {
        explicit InventoryItemModel(const MWWorld::Ptr&) { items=fixtureInventory; }
    };
    struct SortFilterItemModel : ItemModel
    {
        enum { Sort_Name, Filter_OnlyEnchantable=1, Filter_OnlyChargedSoulstones=2 };
        std::unique_ptr<ItemModel> source;
        int filter=0, category=0;
        std::string search;
        explicit SortFilterItemModel(ItemModel* model) : source(model) {}
        void setSortMode(int, bool) {}
        void setCategory(int value) { category=value; }
        void setFilter(int value) { filter=value; }
        void setNameFilter(const std::string& value) { search=value; }
        void update() override { source->update(); items=source->items; }
    };
    struct ItemView : MyGUI::Widget
    {
        enum ViewMode { View_List, View_Grid };
        ItemModel* mModel=nullptr;
        bool mExtendedMode=false, mSingleClickActionEnabled=false;
        ViewMode mViewMode=View_List;
        int mListPressedIndex=-1, mListDragStartX=0, mListDragStartY=0;
        bool mListDragStarted=false;
        int mKeyboardFocusedIndex=-1;
        MyGUI::Event<int> eventItemClicked, eventItemDoubleClicked, eventItemDragStarted;
        MyGUI::Event<> eventBackgroundClicked;
        ~ItemView() override { delete mModel; }
        void setExtendedMode(bool enabled);
        void setSingleClickActionEnabled(bool enabled);
        void setModel(ItemModel* model);
        void update() { if (mModel) mModel->update(); }
        void updateHeaderCaptions() {}
        void updateExtendedGeometry() {}
        void resetScrollBars() {}
        int forceItemFocused(int index) { mKeyboardFocusedIndex=index; return index; }
        MyGUI::ScrollView* getActiveScrollView() { return nullptr; }
        static ItemView* findVisibleItemViewAt(const MyGUI::IntPoint&) { return nullptr; }
        bool handleNavigationKey(MyGUI::KeyCode key);
        void onSelectedItem(MyGUI::Widget* sender);
        void onListItemPressed(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void onListItemDragged(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void onListItemReleased(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void onListItemDoubleClicked(MyGUI::Widget*);
    };
    struct WindowModal
    {
        bool visible=false;
        std::vector<std::unique_ptr<MyGUI::Widget>> widgets;
        explicit WindowModal(const std::string&) {}
        template<class T> void getWidget(T*& out, const std::string&)
        { auto widget=std::make_unique<T>(); out=widget.get(); widgets.push_back(std::move(widget)); }
        void center() {}
        void setVisible(bool value) { visible=value; }
    };
    struct ItemSelectionDialog : WindowModal
    {
        MyGUI::EditBox* mSearch;
        ItemView* mItemView;
        SortFilterItemModel* mSortModel;
        InventoryItemModel* mModel;
        MyGUI::Event<MWWorld::Ptr> eventItemSelected;
        MyGUI::Event<> eventDialogCanceled;
        explicit ItemSelectionDialog(const std::string&);
        bool exit();
        void openContainer(const MWWorld::Ptr&);
        void setCategory(int);
        void setFilter(int);
        void onSearchChanged(MyGUI::EditBox*);
        void onSelectedItem(int);
        void onCancelButtonClicked(MyGUI::Widget*);
    };
    struct EnchantingModel
    {
        MWWorld::Ptr item, gem;
        int castChanges=0;
        MWWorld::Ptr getOldItem() const { return item; }
        MWWorld::Ptr getGem() const { return gem; }
        void nextCastStyle() { ++castChanges; }
        void setSoulGem(const MWWorld::Ptr& value) { gem=value; }
        int getGemCharge() const { return gem.isEmpty() || gem.id=="empty_gem" ? 0 : 100; }
    };
    struct EnchantingDialog
    {
        ItemSelectionDialog* mItemSelectionDialog=nullptr;
        EnchantingModel mEnchanting;
        int labelUpdates=0, itemSelections=0, soulSelections=0;
        ~EnchantingDialog() { delete mItemSelectionDialog; }
        void setItem(const MWWorld::Ptr& value) { mEnchanting.item=value; if (!value.isEmpty()) ++itemSelections; }
        void setSoulGem(const MWWorld::Ptr& value) { mEnchanting.gem=value; if (!value.isEmpty()) ++soulSelections; }
        void updateLabels() { ++labelUpdates; }
        void updateEffectsView() {}
        void onSelectItem(MyGUI::Widget*);
        void onSelectSoul(MyGUI::Widget*);
        void onItemSelected(MWWorld::Ptr);
        void onSoulSelected(MWWorld::Ptr);
        void onItemCancel();
        void onSoulCancel();
    };
}
