#include "itemselection.hpp"

#include <MyGUI_TextBox.h>
#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>

#include "itemview.hpp"
#include "inventoryitemmodel.hpp"
#include "sortfilteritemmodel.hpp"

namespace MWGui
{

    ItemSelectionDialog::ItemSelectionDialog(const std::string &label)
        : WindowModal("openmw_itemselection_dialog.layout")
        , mSortModel(nullptr)
        , mModel(nullptr)
    {
        getWidget(mSearch, "Search");
        mSearch->eventEditTextChange += MyGUI::newDelegate(this, &ItemSelectionDialog::onSearchChanged);
        getWidget(mItemView, "ItemView");
        mItemView->setExtendedMode(true);
        // Y108: picker rows must activate on release, including touch taps and Enter.
        mItemView->setSingleClickActionEnabled(true);
        mItemView->eventItemClicked += MyGUI::newDelegate(this, &ItemSelectionDialog::onSelectedItem);

        MyGUI::TextBox* l;
        getWidget(l, "Label");
        l->setCaptionWithReplacing (label);

        MyGUI::Button* cancelButton;
        getWidget(cancelButton, "CancelButton");
        cancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &ItemSelectionDialog::onCancelButtonClicked);

        center();
    }

    bool ItemSelectionDialog::exit()
    {
        eventDialogCanceled();
        return true;
    }

    void ItemSelectionDialog::openContainer(const MWWorld::Ptr& container)
    {
        mModel = new InventoryItemModel(container);
        mSortModel = new SortFilterItemModel(mModel);
        mSortModel->setSortMode(SortFilterItemModel::Sort_Name,true);
        mSearch->setCaption("");
        mItemView->setModel(mSortModel);
        mItemView->resetScrollBars();
    }

    void ItemSelectionDialog::setCategory(int category)
    {
        mSortModel->setCategory(category);
        mItemView->update();
    }

    void ItemSelectionDialog::setFilter(int filter)
    {
        mSortModel->setFilter(filter);
        mItemView->update();
    }

    void ItemSelectionDialog::onSearchChanged(MyGUI::EditBox* sender)
    {
        if(!mSortModel)return;
        mSortModel->setNameFilter(sender->getCaption());mItemView->update();
    }

    void ItemSelectionDialog::onSelectedItem(int index)
    {
        ItemStack item = mSortModel->getItem(index);
        eventItemSelected(item.mBase);
    }

    void ItemSelectionDialog::onCancelButtonClicked(MyGUI::Widget* sender)
    {
        exit();
    }

}
