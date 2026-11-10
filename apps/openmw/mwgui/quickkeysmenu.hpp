#ifndef MWGUI_QUICKKEYS_H
#define MWGUI_QUICKKEYS_H

#include "windowbase.hpp"

#include "spellmodel.hpp"
#include "quickbarrules.hpp"
#include <array>

namespace MWGui
{

    class QuickKeysMenuAssign;
    class ItemSelectionDialog;
    class MagicSelectionDialog;
    class ItemWidget;
    class SpellView;

    class QuickKeysMenu : public WindowBase
    {
    public:
        QuickKeysMenu();
        ~QuickKeysMenu();

        void onResChange(int, int) override;

        void onItemButtonClicked(MyGUI::Widget* sender);
        void onMagicButtonClicked(MyGUI::Widget* sender);
        void onUnassignButtonClicked(MyGUI::Widget* sender);
        void onCancelButtonClicked(MyGUI::Widget* sender);

        void onAssignItem (MWWorld::Ptr item);
        void onAssignItemCancel ();
        void onAssignMagicItem (MWWorld::Ptr item);
        void onAssignMagic (const std::string& spellId);
        void onAssignMagicCancel ();
        void onOpen() override;

        void activateQuickKey(int index);
        void editQuickKey(int index);
        void updateBar(float dt, bool hudVisible);
        void updateActivatedQuickKey();

        /*
            Start of tes3mp addition

            Allow the setting of the selected index from elsewhere in the code
        */
        void setSelectedIndex(int index);
        /*
            End of tes3mp addition
        */

        /// @note This enum is serialized, so don't move the items around!
        enum QuickKeyType
        {
            Type_Item,
            Type_Magic,
            Type_MagicItem,
            Type_Unassigned,
            Type_HandToHand
        };

        void write (ESM::ESMWriter& writer);
        void readRecord (ESM::ESMReader& reader, uint32_t type);
        void clear() override;

        /*
            Start of tes3mp addition

            Allow unassigning an index directly from elsewhere in the code
        */
        void unassignIndex(int index);
        /*
            End of tes3mp addition
        */


    private:

        struct keyData {
            int index;
            ItemWidget* button;
            QuickKeysMenu::QuickKeyType type;
            std::string id;
            std::string name;
            keyData(): index(-1), button(nullptr), type(Type_Unassigned), id(""), name("") {}
        };

        void activateStoredKey(int index);
        int unlockedRows() const;
        void refreshEditor();
        void refreshBar();
        std::array<int, 10> mCurrentRow{};
        int mLastColumn = -1;
        float mBarTime = 0.f, mBarAlpha = 0.f, mRefreshTimer = 0.f;
        MyGUI::Widget* mBar = nullptr;
        std::array<ItemWidget*, 10> mBarItems{};
        std::array<MyGUI::TextBox*, 10> mBarLabels{};
        std::array<MyGUI::TextBox*, 6> mRowLabels{};
        std::vector<keyData> mKey;
        keyData* mSelected;
        keyData* mActivated;

        MyGUI::EditBox* mInstructionLabel;
        MyGUI::Button* mOkButton;

        QuickKeysMenuAssign* mAssignDialog;
        ItemSelectionDialog* mItemSelectionDialog;
        MagicSelectionDialog* mMagicSelectionDialog;

        void onQuickKeyButtonClicked(MyGUI::Widget* sender);
        void onOkButtonClicked(MyGUI::Widget* sender);

        void unassign(keyData* key);
    };

    class QuickKeysMenuAssign : public WindowModal
    {
    public:
        QuickKeysMenuAssign(QuickKeysMenu* parent);

    private:
        MyGUI::TextBox* mLabel;
        MyGUI::Button* mItemButton;
        MyGUI::Button* mMagicButton;
        MyGUI::Button* mUnassignButton;
        MyGUI::Button* mCancelButton;

        QuickKeysMenu* mParent;
    };

    class MagicSelectionDialog : public WindowModal
    {
    public:
        MagicSelectionDialog(QuickKeysMenu* parent);

        void onOpen() override;
        bool exit() override;

    private:
        MyGUI::Button* mCancelButton;
        SpellView* mMagicList;

        QuickKeysMenu* mParent;

        void onCancelButtonClicked (MyGUI::Widget* sender);
        void onModelIndexSelected(SpellModel::ModelIndex index);
    };
}


#endif
