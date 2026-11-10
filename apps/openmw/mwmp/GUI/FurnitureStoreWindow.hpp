#ifndef OPENMW_MWMP_FURNITURESTOREWINDOW_HPP
#define OPENMW_MWMP_FURNITURESTOREWINDOW_HPP

#include <memory>
#include <map>
#include <string>
#include <vector>
#include <utility>

#include <MyGUI_Button.h>
#include <MyGUI_EditBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_ListBox.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_RenderManager.h>

#include "apps/openmw/mwgui/windowbase.hpp"

namespace mwmp
{
    class FurniturePreview;
    class FurnitureStoreWindow : public MWGui::WindowModal
    {
    public:
        FurnitureStoreWindow();
        ~FurnitureStoreWindow() override;
        // Keep retired RTT graphs alive for a few main-loop frames. This mirrors
        // CharacterPreview and avoids Android/GL draw-thread use-after-free.
        static void collectRetiredPreviews();
        void receive(const std::string& payload);
        void onOpen() override;
        void onClose() override;
        bool exit() override;
        void reset();
        // Use the existing Decorations page as the catalogue host: no second
        // GUI mode is pushed when the shop is requested from the player menu.
        void showInPlayerMenu(MyGUI::Widget* pane);
        void syncPlayerMenu(bool visible);
    private:
        struct Item { std::string id, name, nameRu, nameEn, category, subcategory; int price=0; };
        enum Tab { Shop, Inventory, Placed };
        void send(const std::string& verb, const std::string& arg="", int page=1,
                  const std::string& subcategory="") const;
        void requestPage();
        std::string categoryCaption(const std::string& category) const;
        std::map<std::string, std::string> mCategoryKeys;
        void rebuildCategories();
        void updateStyle();
        void rebuildItems();
        void updateSelection();
        void resizeToViewport();
        void localize();
        void cacheGeometry(MyGUI::Widget* widget);
        void hideEmbeddedSiblings();
        void onTabShop(MyGUI::Widget*);
        void onTabInventory(MyGUI::Widget*);
        void onTabPlaced(MyGUI::Widget*);
        void onClosePressed(MyGUI::Widget*);
        void onCategory(MyGUI::ListBox*, std::size_t);
        void onCategoryClick(MyGUI::Widget*);
        void selectCategory(std::size_t);
        void onStyle(MyGUI::Widget*);
        void onAdminEdit(MyGUI::Widget*);
        void onAdminCapture(MyGUI::Widget*);
        void onAdminRefresh(MyGUI::Widget*);
        void onAdminCategory(MyGUI::Widget*);
        void onAdminSave(MyGUI::Widget*);
        void onAdminCancel(MyGUI::Widget*);
        void onItem(MyGUI::ListBox*, std::size_t);
        void onSearch(MyGUI::EditBox*);
        void onPrevious(MyGUI::Widget*);
        void onNext(MyGUI::Widget*);
        void onPrimary(MyGUI::Widget*);
        void onSecondary(MyGUI::Widget*);
        void onPreviewPressed(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void onPreviewDrag(MyGUI::Widget*, int, int, MyGUI::MouseButton);
        void onPreviewWheel(MyGUI::Widget*, int);

        MyGUI::ListBox* mCategoryList=nullptr;
        MyGUI::Button* mStyleButton=nullptr;
        MyGUI::ListBox* mItemList=nullptr;
        MyGUI::EditBox* mSearch=nullptr;
        MyGUI::ImageBox* mPreviewImage=nullptr;
        MyGUI::TextBox* mPreviewCaption=nullptr;
        MyGUI::TextBox* mTitle=nullptr;
        MyGUI::TextBox* mPrice=nullptr;
        MyGUI::TextBox* mNotice=nullptr;
        MyGUI::TextBox* mPageLabel=nullptr;
        MyGUI::TextBox* mCategoryLabel=nullptr;

        MyGUI::Button* mPrimary=nullptr;
        MyGUI::Button* mSecondary=nullptr;
        MyGUI::Button* mPrevious=nullptr;
        MyGUI::Button* mNext=nullptr;
        std::vector<std::pair<MyGUI::Widget*,MyGUI::IntCoord>> mGeometry;
        std::vector<std::string> mCategories, mVisibleIds;
        MyGUI::Widget* mAdminPanel=nullptr;
        std::vector<std::pair<MyGUI::Widget*,MyGUI::IntCoord>> mAdminGeometry;
        bool mEditorModalActive=false;
        MyGUI::Widget* mEmbeddedPane=nullptr;
        MyGUI::IntCoord mLastEmbeddedCoord{-1,-1,-1,-1};
        std::vector<std::pair<MyGUI::Widget*,bool>> mEmbeddedSiblings;
        MyGUI::Button* mAdminEdit=nullptr;
        MyGUI::Button* mAdminCapture=nullptr;
        MyGUI::Button* mAdminRefresh=nullptr;
        MyGUI::Button* mAdminCategory=nullptr;
        std::vector<std::string> mEditorCategories;
        std::size_t mEditorCategoryIndex=0;
        MyGUI::EditBox* mAdminNameRu=nullptr;
        MyGUI::EditBox* mAdminNameEn=nullptr;
        MyGUI::EditBox* mAdminPrice=nullptr;
        bool mIsAdmin=false;
        int mStyleIndex=0;
        std::vector<Item> mPageItems;
        std::map<std::string,int> mStock;
        std::map<std::string,std::string> mItemNames;
        std::map<std::string,std::string> mPlaced;
        std::unique_ptr<FurniturePreview> mPreview;
        int mPreviousDragX=0;
        int mPreviousDragY=0;
        Tab mTab=Shop;
        std::string mCategory, mSubcategory, mSelected;
        int mPage=1, mPages=1;
    };
}
#endif
