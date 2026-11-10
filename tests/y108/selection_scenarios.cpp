namespace
{
    MyGUI::Widget row(MWGui::ItemView& view, int index)
    {
        MyGUI::Widget widget;
        widget.data=std::make_pair(index,view.mModel);
        return widget;
    }
    void tap(MWGui::ItemView& view, int index)
    {
        auto widget=row(view,index);
        view.onListItemPressed(&widget,20,30,MyGUI::MouseButton::Left);
        view.onListItemReleased(&widget,20,30,MyGUI::MouseButton::Left);
    }
}
int main(int argc, char** argv)
{
    using namespace MWGui;
    EnchantingDialog dialog;
    dialog.onSelectItem(nullptr);
    auto* picker=dialog.mItemSelectionDialog;
    assert(picker->visible && picker->mItemView->mExtendedMode);
    assert(picker->mSortModel->filter==SortFilterItemModel::Filter_OnlyEnchantable);
    if (argc>1 && std::string(argv[1])=="--expect-blocked")
    {
        tap(*picker->mItemView,1);
        assert(dialog.mEnchanting.item.isEmpty() && picker->visible);
        assert(picker->mItemView->handleNavigationKey(MyGUI::KeyCode::Return));
        assert(dialog.mEnchanting.item.isEmpty() && picker->visible);
        // The old classic grid worked: this isolates the regression to list activation.
        picker->mItemView->mViewMode=ItemView::View_Grid;
        auto widget=row(*picker->mItemView,1);
        picker->mItemView->onSelectedItem(&widget);
        assert(dialog.mEnchanting.item.id=="sword" && !picker->visible);
        std::cout<<"PASS reproduced Y107: list release and Enter cannot select; classic grid works\n";
        return 0;
    }

    tap(*picker->mItemView,1);
    assert(dialog.mEnchanting.item.id=="sword" && !picker->visible);
    assert(dialog.itemSelections==1 && dialog.mEnchanting.castChanges==1);
    auto widget=row(*picker->mItemView,1);
    picker->mItemView->onListItemDoubleClicked(&widget);
    assert(dialog.itemSelections==1); // No duplicate selection after the first release.

    for (auto key:{MyGUI::KeyCode::Return,MyGUI::KeyCode::NumpadEnter,MyGUI::KeyCode::Space})
    {
        dialog.setItem({});
        dialog.onSelectItem(nullptr);
        picker=dialog.mItemSelectionDialog;
        assert(picker->mItemView->handleNavigationKey(MyGUI::KeyCode::ArrowDown));
        assert(picker->mItemView->handleNavigationKey(key));
        assert(dialog.mEnchanting.item.id=="sword" && !picker->visible);
    }
    for (int index:{0,2,3}) // Clothing, armor, scroll; retain exact selected Ptr.
    {
        dialog.setItem({});
        dialog.onSelectItem(nullptr);
        picker=dialog.mItemSelectionDialog;
        tap(*picker->mItemView,index);
        assert(dialog.mEnchanting.item.id==fixtureInventory[index].mBase.id);
        assert(!picker->visible);
    }

    dialog.setItem({});
    dialog.onSelectItem(nullptr);
    picker=dialog.mItemSelectionDialog;
    auto& view=*picker->mItemView;
    auto first=row(view,0), second=row(view,1);
    const int before=dialog.itemSelections;
    view.onListItemPressed(&first,10,10,MyGUI::MouseButton::Right);
    view.onListItemReleased(&first,10,10,MyGUI::MouseButton::Right);
    assert(dialog.itemSelections==before && picker->visible);
    view.onListItemPressed(&first,10,10,MyGUI::MouseButton::Left);
    view.onListItemReleased(&second,10,10,MyGUI::MouseButton::Left);
    assert(dialog.itemSelections==before && picker->visible);
    view.onListItemPressed(&first,10,10,MyGUI::MouseButton::Left);
    view.onListItemDragged(&first,10,35,MyGUI::MouseButton::Left);
    view.onListItemReleased(&first,10,35,MyGUI::MouseButton::Left);
    assert(dialog.itemSelections==before && picker->visible); // Scroll/drag isn't a tap.
    view.onListItemPressed(&first,10,10,MyGUI::MouseButton::Left);
    view.onListItemDragged(&first,12,12,MyGUI::MouseButton::Left);
    view.onListItemReleased(&first,12,12,MyGUI::MouseButton::Left);
    assert(dialog.mEnchanting.item.id=="robe" && !picker->visible);

    dialog.setItem({});
    dialog.onSelectItem(nullptr);
    picker=dialog.mItemSelectionDialog;
    picker->exit();
    assert(!picker->visible && dialog.mEnchanting.item.isEmpty());
    dialog.onSelectItem(nullptr);
    picker=dialog.mItemSelectionDialog;
    assert(picker->mSearch->getCaption().empty());
    picker->mItemView->mViewMode=ItemView::View_Grid;
    auto grid=row(*picker->mItemView,2);
    picker->mItemView->onSelectedItem(&grid);
    assert(dialog.mEnchanting.item.id=="armor" && !picker->visible);

    dialog.onSelectSoul(nullptr);
    picker=dialog.mItemSelectionDialog;
    assert(picker->mSortModel->filter==SortFilterItemModel::Filter_OnlyChargedSoulstones);
    tap(*picker->mItemView,4);
    assert(dialog.mEnchanting.gem.id=="filled_gem" && !picker->visible);
    assert(dialog.soulSelections==1);
    dialog.setSoulGem({});
    dialog.onSelectSoul(nullptr);
    picker=dialog.mItemSelectionDialog;
    for (int i=0;i<4;++i) picker->mItemView->handleNavigationKey(MyGUI::KeyCode::ArrowDown);
    picker->mItemView->handleNavigationKey(MyGUI::KeyCode::Return);
    assert(dialog.mEnchanting.gem.id=="filled_gem" && !picker->visible);
    // The model double controls rows; real production filtering isn't under test here.
    std::cout<<"PASS production picker/enchanting handlers: item/gem callbacks, mouse/tap release, Enter/Space, grid, cancel, reopen, drag suppression and no double selection\n";
}
