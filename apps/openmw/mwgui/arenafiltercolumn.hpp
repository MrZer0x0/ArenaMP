#ifndef OPENMW_MWGUI_ARENAFILTERCOLUMN_H
#define OPENMW_MWGUI_ARENAFILTERCOLUMN_H

// UI8: the item-category filter column of the player inventory (IV1), shared
// by the container and merchant windows so all three item panes look and
// behave the same: six square icon buttons in a column left of the list,
// category names in the tooltips, the selected category at full opacity.
// Header-only so no build file has to change.

#include <algorithm>
#include <array>
#include <string>

#include <MyGUI_Button.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_LanguageManager.h>

namespace ArenaUi
{
    /// Width of the column and the gap to the list, as in the inventory.
    constexpr int sFilterColumnWidth = 34;
    constexpr int sFilterColumnGap = 4;

    struct FilterColumn
    {
        MyGUI::Widget* mColumn = nullptr;
        std::array<MyGUI::Button*, 6> mButtons{};
        std::array<MyGUI::ImageBox*, 6> mIcons{};
        int mLaidOutHeight = -1;
    };

    /// buttons: All, Weapon, Apparel, Magic, Misc, Keys (any may be null).
    inline void setupFilterColumn(FilterColumn& column, MyGUI::Widget* parent,
        const std::array<MyGUI::Button*, 6>& buttons)
    {
        static const char* const textures[] = {
            "textures/ui/arenamw/inv_all.png", "textures/ui/arenamw/inv_weapon.png",
            "textures/ui/arenamw/inv_apparel.png", "textures/ui/arenamw/inv_magic.png",
            "textures/ui/arenamw/inv_misc.png", "textures/ui/arenamw/inv_keys.png" };
        static const char* const names[] = { "#{sAllTab}", "#{sWeaponTab}", "#{sApparelTab}",
            "#{sMagicTab}", "#{sMiscTab}", "#{arenamp=inventory.keys}" };

        column.mColumn = parent;
        column.mButtons = buttons;
        column.mLaidOutHeight = -1;
        for (std::size_t i = 0; i < buttons.size(); ++i)
        {
            MyGUI::Button* button = buttons[i];
            if (button == nullptr)
                continue;
            button->setCaption("");
            button->setUserString("ToolTipType", "Layout");
            button->setUserString("ToolTipLayout", "TextToolTipOneLine");
            button->setUserString("Caption_TextOneLine",
                MyGUI::LanguageManager::getInstance().replaceTags(names[i]));
            if (column.mIcons[i] == nullptr)
            {
                column.mIcons[i] = button->createWidget<MyGUI::ImageBox>("ImageBox",
                    MyGUI::IntCoord(5, 2, 24, 24), MyGUI::Align::Default);
                column.mIcons[i]->setNeedMouseFocus(false);
            }
            column.mIcons[i]->setImageTexture(textures[i]);
        }
    }

    /// Lays the six buttons out inside the column widget: 32 px squares with
    /// 4 px gaps; a short pane shrinks the gaps first, then the buttons.
    /// Cheap to call every frame: it only works when the height changed.
    inline void layoutFilterColumn(FilterColumn& column, bool force = false)
    {
        if (column.mColumn == nullptr)
            return;
        const int columnHeight = std::max(40, column.mColumn->getHeight());
        if (!force && columnHeight == column.mLaidOutHeight)
            return;
        column.mLaidOutHeight = columnHeight;

        int gap = sFilterColumnGap;
        int height = (columnHeight - gap * 5) / 6;
        if (height < 26)
        {
            gap = 2;
            height = (columnHeight - gap * 5) / 6;
        }
        height = std::max(16, std::min(32, height));
        const int width = column.mColumn->getWidth();
        const int iconSize = std::max(12, std::min(24, std::min(width, height) - 6));
        int y = 0;
        for (std::size_t i = 0; i < column.mButtons.size(); ++i)
        {
            if (column.mButtons[i] == nullptr)
                continue;
            column.mButtons[i]->setCoord(0, y, width, height);
            if (column.mIcons[i] != nullptr)
                column.mIcons[i]->setCoord((width - iconSize) / 2, (height - iconSize) / 2, iconSize, iconSize);
            y += height + gap;
        }
    }

    /// The selected category is drawn at full opacity, the others dimmed.
    inline void refreshFilterColumn(const FilterColumn& column)
    {
        for (std::size_t i = 0; i < column.mButtons.size(); ++i)
        {
            if (column.mButtons[i] != nullptr && column.mIcons[i] != nullptr)
                column.mIcons[i]->setAlpha(column.mButtons[i]->getStateSelected() ? 1.f : 0.6f);
        }
    }
}

#endif
