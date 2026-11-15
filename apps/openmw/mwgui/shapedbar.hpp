#ifndef MWGUI_SHAPEDBAR_H
#define MWGUI_SHAPEDBAR_H

#include <MyGUI_ProgressBar.h>
#include <MyGUI_Widget.h>

namespace MWGui
{
    /// ArenaMP UI9.3: HUD bars whose fill has the backplate's pointed shape.
    ///
    /// The track skins of the ArenaHUD bars (openmw_hud_energybar.skin.xml) hold
    /// the shaped fill as a skin child named "Client". By default it stretches
    /// with the track like an ordinary fill. This pins it to the full width of the
    /// bar instead; the track, which grows with the value, then only crops it.
    /// The left point stays a point at every value, the right point appears only
    /// when the bar is full, and the fill never covers the bronze rim.
    ///
    /// Cheap enough to call every frame; it only touches widgets whose size is off.
    inline void fitShapedProgressBar(MyGUI::ProgressBar* bar)
    {
        if (bar == nullptr)
            return;
        MyGUI::Widget* client = bar->getClientWidget();
        if (client == nullptr)
            client = bar;
        const int width = client->getWidth();
        const MyGUI::Align align = MyGUI::Align::Left | MyGUI::Align::VStretch;
        for (std::size_t i = 0; i < client->getChildCount(); ++i)
        {
            MyGUI::Widget* track = client->getChildAt(i);
            // Widget exposes the skin child named Client through its public API.
            // Plain track skins have no client and need no shape adjustment.
            if (MyGUI::Widget* fill = track->getClientWidget())
            {
                if (fill->getAlign() != align)
                    fill->setAlign(align);
                const MyGUI::IntCoord wanted(0, 0, width, track->getHeight());
                if (fill->getCoord() != wanted)
                    fill->setCoord(wanted);
            }
        }
    }
}

#endif
