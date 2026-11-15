#ifndef OPENMW_MWGUI_ARENAICONBUTTON_H
#define OPENMW_MWGUI_ARENAICONBUTTON_H

// UI3: icon buttons shared by ArenaMP windows (furniture store, map, character
// creation). Header-only so no build file has to change.
//
// decorate() gives a button a gold ArenaMP icon (textures/ui/arenamw/<name>.png)
// and either keeps the caption beside it or shows the icon alone with the
// caption as a hover tooltip. It is idempotent: call it again whenever the
// caption or the button size changes (localisation, rescaling, state).

#include <algorithm>
#include <string>

#include <MyGUI_Button.h>
#include <MyGUI_ImageBox.h>

namespace ArenaUi
{
    enum class IconMode
    {
        IconOnly,        ///< icon in the middle, caption as tooltip
        IconAndCaption,  ///< icon left of the centred caption; icon alone if it does not fit
    };

    inline MyGUI::ImageBox* findButtonIcon(MyGUI::Button* button)
    {
        if (button == nullptr)
            return nullptr;
        for (std::size_t i = 0; i < button->getChildCount(); ++i)
        {
            MyGUI::Widget* child = button->getChildAt(i);
            if (child != nullptr && child->getUserString("ArenaIcon") == "1")
                return child->castType<MyGUI::ImageBox>(false);
        }
        return nullptr;
    }

    inline void setTooltip(MyGUI::Widget* widget, const std::string& text)
    {
        if (widget == nullptr)
            return;
        if (text.empty())
        {
            widget->clearUserString("ToolTipType");
            return;
        }
        widget->setUserString("ToolTipType", "Layout");
        widget->setUserString("ToolTipLayout", "TextToolTipOneLine");
        widget->setUserString("Caption_TextOneLine", text);
    }

    /// `icon` is the file name without extension, e.g. "btn_add" or "map_flag".
    inline void decorate(MyGUI::Button* button, const std::string& icon, const std::string& caption, IconMode mode)
    {
        if (button == nullptr)
            return;

        MyGUI::ImageBox* image = findButtonIcon(button);
        if (image == nullptr)
        {
            image = button->createWidget<MyGUI::ImageBox>("ImageBox", MyGUI::IntCoord(0, 0, 16, 16), MyGUI::Align::Default);
            image->setUserString("ArenaIcon", "1");
            image->setNeedMouseFocus(false);
        }
        image->setImageTexture("textures/ui/arenamw/" + icon + ".png");
        button->setUserString("ArenaIconName", icon);
        button->setUserString("ArenaIconCaption", caption);
        button->setUserString("ArenaIconMode", mode == IconMode::IconOnly ? "icon" : "both");
        setTooltip(button, caption);

        // UI8: frameless icon buttons (ArenaUI_IconButton) have no frame to
        // clear, so their icon may fill more of the button.
        const bool frameless = button->getUserString("ArenaFrameless") == "1";
        const int size = frameless ? std::max(12, std::min(24, button->getHeight() - 6))
                                   : std::max(12, std::min(20, button->getHeight() - 8));
        const int top = (button->getHeight() - size) / 2;
        if (mode == IconMode::IconAndCaption && !caption.empty())
        {
            // Measure the actual font: five fixed spaces can be narrower than
            // the icon. Reserve the icon plus at least one space (8 px minimum).
            button->setCaption(caption);
            const int captionWidth = button->getTextSize().width;
            button->setCaption(" " + caption);
            const int gap = std::max(8, button->getTextSize().width - captionWidth);
            std::string padded = " " + caption;
            for (int n = 0; n < 64 && button->getTextSize().width - captionWidth < size + gap; ++n)
            {
                padded.insert(padded.begin(), ' ');
                button->setCaption(padded);
            }
            const int textWidth = button->getTextSize().width;
            if (textWidth <= button->getWidth() - 6)
            {
                image->setCoord(std::max(3, (button->getWidth() - textWidth) / 2), top, size, size);
                return;
            }
        }
        // Icon only (or the caption does not fit): the name stays in the tooltip.
        button->setCaption("");
        image->setCoord((button->getWidth() - size) / 2, top, size, size);
    }

    /// Re-run decorate() with the stored icon/caption, e.g. after a resize.
    inline void refresh(MyGUI::Button* button)
    {
        if (findButtonIcon(button) == nullptr)
            return;
        decorate(button, button->getUserString("ArenaIconName"), button->getUserString("ArenaIconCaption"),
            button->getUserString("ArenaIconMode") == "icon" ? IconMode::IconOnly : IconMode::IconAndCaption);
    }
}

#endif
