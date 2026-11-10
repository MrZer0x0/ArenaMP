#include "mapwindow.hpp"

#include <osg/Texture2D>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <iomanip>
#include <sstream>

#include <MyGUI_ScrollView.h>
#include <MyGUI_ComboBox.h>
#include <MyGUI_TextBox.h>
#include <MyGUI_ImageBox.h>
#include <MyGUI_RenderManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_LanguageManager.h>
#include <MyGUI_InputManager.h>
#include <MyGUI_RotatingSkin.h>
#include <MyGUI_FactoryManager.h>

/*
    Start of tes3mp addition

    Include additional headers for multiplayer purposes
*/
#include "../mwmp/Main.hpp"
#include "../mwmp/GUIController.hpp"
#include "../mwmp/Networking.hpp"
#include "../mwmp/LocalPlayer.hpp"
/*
    End of tes3mp addition
*/

#include <components/esm/globalmap.hpp>
#include <components/esm/esmwriter.hpp>
#include <components/settings/settings.hpp>
#include <components/myguiplatform/myguitexture.hpp>

#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/environment.hpp"

#include "../mwworld/player.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/esmstore.hpp"

#include "../mwrender/globalmap.hpp"
#include "../mwrender/localmap.hpp"

#include "confirmationdialog.hpp"
#include "exposedwindow.hpp"
#include "tooltips.hpp"
#include "mapmarkerstyle.hpp"

namespace
{

    /*
        Start of ArenaMP X026 addition

        Multiplayer teardown order. Since X022 the engine destroys mwmp::Main
        (so that DedicatedPlayer destructors still see a live World) and only
        afterwards runs MWBase::Environment::cleanup(), which deletes the
        WindowManager and with it every window. LocalMapBase lives in two of
        those windows, and its destructor used to call mwmp::Main::get()
        unconditionally - on a null pMain that reads GUIController through a
        null this and takes the process down on exit.

        Every access to the multiplayer GUI controller from this file goes
        through this helper, which simply reports "no multiplayer right now".
    */
    mwmp::GUIController* multiplayerGuiController()
    {
        if (!mwmp::Main::isInitialized())
            return nullptr;

        return mwmp::Main::get().getGUIController();
    }
    /*
        End of ArenaMP X026 addition
    */

    std::string arenaMapUrlEncode(const std::string& value)
    {
        std::ostringstream out;
        out << std::uppercase << std::hex;
        for (unsigned char c : value)
        {
            if (std::isalnum(c) || c == '_' || c == '-' || c == '.')
                out << static_cast<char>(c);
            else
                out << '%' << std::setw(2) << std::setfill('0') << static_cast<int>(c);
        }
        return out.str();
    }

    void sendArenaGroupMarkerCommand(const std::string& command)
    {
        if (!mwmp::Main::isInitialized())
            return;
        mwmp::LocalPlayer* player = mwmp::Main::get().getLocalPlayer();
        mwmp::Networking* networking = mwmp::Main::get().getNetworking();
        if (!player || !networking || !player->isLoggedIn())
            return;
        player->chatMessage = command;
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->setPlayer(player);
        networking->getPlayerPacket(ID_CHAT_MESSAGE)->Send();
    }

    const int cellSize = Constants::CellSizeInUnits;

    enum LocalMapWidgetDepth
    {
        Local_MarkerAboveFogLayer = 0,
        Local_CompassLayer = 1,
        Local_FogLayer = 2,
        Local_MarkerLayer = 3,
        Local_MapLayer = 4
    };

    enum GlobalMapWidgetDepth
    {
        Global_CompassLayer = 0,
        Global_MarkerLayer = 1,
        Global_ExploreOverlayLayer = 2,
        Global_MapLayer = 3
    };


    /// @brief A widget that changes its color when hovered.
    class MarkerWidget final : public MyGUI::Widget
    {
        MYGUI_RTTI_DERIVED(MarkerWidget)

    public:
        void setNormalColour(const MyGUI::Colour& colour)
        {
            mNormalColour = colour;
            setColour(colour);
        }

        void setHoverColour(const MyGUI::Colour& colour)
        {
            mHoverColour = colour;
        }

    private:
        MyGUI::Colour mNormalColour;
        MyGUI::Colour mHoverColour;

        void onMouseLostFocus(MyGUI::Widget* _new) override
        {
            setColour(mNormalColour);
        }

        void onMouseSetFocus(MyGUI::Widget* _old) override
        {
            setColour(mHoverColour);
        }
    };
}

namespace MWGui
{

    void CustomMarkerCollection::addMarker(const ESM::CustomMarker &marker, bool triggerEvent)
    {
        mMarkers.insert(std::make_pair(marker.mCell, marker));
        if (triggerEvent)
            eventMarkersChanged();
    }

    void CustomMarkerCollection::deleteMarker(const ESM::CustomMarker &marker)
    {
        std::pair<ContainerType::iterator, ContainerType::iterator> range = mMarkers.equal_range(marker.mCell);

        for (ContainerType::iterator it = range.first; it != range.second; ++it)
        {
            if (it->second == marker)
            {
                mMarkers.erase(it);
                eventMarkersChanged();
                return;
            }
        }
        throw std::runtime_error("can't find marker to delete");
    }

    void CustomMarkerCollection::updateMarker(const ESM::CustomMarker &marker, const std::string &newNote)
    {
        std::pair<ContainerType::iterator, ContainerType::iterator> range = mMarkers.equal_range(marker.mCell);

        for (ContainerType::iterator it = range.first; it != range.second; ++it)
        {
            if (it->second == marker)
            {
                it->second.mNote = newNote;
                eventMarkersChanged();
                return;
            }
        }
        throw std::runtime_error("can't find marker to update");
    }

    void CustomMarkerCollection::clear()
    {
        mMarkers.clear();
        eventMarkersChanged();
    }

    CustomMarkerCollection::ContainerType::const_iterator CustomMarkerCollection::begin() const
    {
        return mMarkers.begin();
    }

    CustomMarkerCollection::ContainerType::const_iterator CustomMarkerCollection::end() const
    {
        return mMarkers.end();
    }

    CustomMarkerCollection::RangeType CustomMarkerCollection::getMarkers(const ESM::CellId &cellId) const
    {
        return mMarkers.equal_range(cellId);
    }

    size_t CustomMarkerCollection::size() const
    {
        return mMarkers.size();
    }

    // ------------------------------------------------------

    LocalMapBase::LocalMapBase(CustomMarkerCollection &markers, MWRender::LocalMap* localMapRender, bool fogOfWarEnabled)
        : mLocalMapRender(localMapRender)
        , mCurX(0)
        , mCurY(0)
        , mInterior(false)
        , mLocalMap(nullptr)
        , mCompass(nullptr)
        , mChanged(true)
        , mFogOfWarToggled(true)
        , mFogOfWarEnabled(fogOfWarEnabled)
        , mMapWidgetSize(0)
        , mNumCells(0)
        , mCellDistance(0)
        , mCustomMarkers(markers)
        , mMarkerUpdateTimer(0.0f)
        , mLastDirectionX(0.0f)
        , mLastDirectionY(0.0f)
        , mNeedDoorMarkersUpdate(false)
    {
        mCustomMarkers.eventMarkersChanged += MyGUI::newDelegate(this, &LocalMapBase::updateCustomMarkers);
        
        /*
            Start of tes3mp addition

            Add a MyGUI delegate for updating player markers
        */
        if (mwmp::GUIController* guiController = multiplayerGuiController())
            guiController->mPlayerMarkers.eventMarkersChanged += MyGUI::newDelegate(this, &LocalMapBase::updatePlayerMarkers);
        /*
            End of tes3mp addition
        */
    }

    LocalMapBase::~LocalMapBase()
    {
        mCustomMarkers.eventMarkersChanged -= MyGUI::newDelegate(this, &LocalMapBase::updateCustomMarkers);

        /*
            Start of tes3mp addition

            Remove a MyGUI delegate for updating player markers
        */
        /*
            X026: if multiplayer is already gone, so is the marker collection this
            delegate was registered on. There is nothing left to unregister from,
            and reaching for it is exactly what crashed the client on exit.
        */
        if (mwmp::GUIController* guiController = multiplayerGuiController())
            guiController->mPlayerMarkers.eventMarkersChanged -= MyGUI::newDelegate(this, &LocalMapBase::updatePlayerMarkers);
        /*
            End of tes3mp addition
        */
    }

    void LocalMapBase::init(MyGUI::ScrollView* widget, MyGUI::ImageBox* compass)
    {
        mLocalMap = widget;
        mCompass = compass;
        mMapWidgetSize = std::max(1, Settings::Manager::getInt("local map widget size", "Map"));
        mCellDistance = Constants::CellGridRadius;
        mNumCells = mCellDistance * 2 + 1;

        mLocalMap->setCanvasSize(mMapWidgetSize*mNumCells, mMapWidgetSize*mNumCells);

        mCompass->setDepth(Local_CompassLayer);
        mCompass->setNeedMouseFocus(false);

        for (int mx=0; mx<mNumCells; ++mx)
        {
            for (int my=0; my<mNumCells; ++my)
            {
                MyGUI::ImageBox* map = mLocalMap->createWidget<MyGUI::ImageBox>("ImageBox",
                    MyGUI::IntCoord(mx*mMapWidgetSize, my*mMapWidgetSize, mMapWidgetSize, mMapWidgetSize),
                    MyGUI::Align::Top | MyGUI::Align::Left);
                map->setDepth(Local_MapLayer);

                MyGUI::ImageBox* fog = mLocalMap->createWidget<MyGUI::ImageBox>("ImageBox",
                    MyGUI::IntCoord(mx*mMapWidgetSize, my*mMapWidgetSize, mMapWidgetSize, mMapWidgetSize),
                    MyGUI::Align::Top | MyGUI::Align::Left);
                fog->setDepth(Local_FogLayer);
                fog->setColour(MyGUI::Colour(0, 0, 0));

                map->setNeedMouseFocus(false);
                fog->setNeedMouseFocus(false);

                mMaps.emplace_back(map, fog);
            }
        }
    }

    void LocalMapBase::setMapWidgetSize(int size)
    {
        size = std::clamp(size, 48, 1024);
        if (!mLocalMap || size == mMapWidgetSize || mMapWidgetSize <= 0)
            return;

        const int oldSize = mMapWidgetSize;
        const float ratio = static_cast<float>(size) / static_cast<float>(oldSize);
        const MyGUI::IntPoint oldCompass = mCompass->getPosition();

        mMapWidgetSize = size;
        mLocalMap->setCanvasSize(mMapWidgetSize * mNumCells, mMapWidgetSize * mNumCells);

        for (int mx = 0; mx < mNumCells; ++mx)
        {
            for (int my = 0; my < mNumCells; ++my)
            {
                MapEntry& entry = mMaps[my + mNumCells * mx];
                const MyGUI::IntCoord coord(mx * mMapWidgetSize, my * mMapWidgetSize, mMapWidgetSize, mMapWidgetSize);
                entry.mMapWidget->setCoord(coord);
                entry.mFogWidget->setCoord(coord);
            }
        }

        // Keep the current player position centred while the map scale changes.
        const float oldCenterX = static_cast<float>(oldCompass.left + 16);
        const float oldCenterY = static_cast<float>(oldCompass.top + 16);
        const MyGUI::IntPoint compassPos(
            static_cast<int>(std::lround(oldCenterX * ratio)) - 16,
            static_cast<int>(std::lround(oldCenterY * ratio)) - 16);
        mCompass->setPosition(compassPos);
        const MyGUI::IntCoord viewSize = mLocalMap->getCoord();
        mLocalMap->setViewOffset(MyGUI::IntPoint(
            viewSize.width / 2 - (compassPos.left + 16),
            viewSize.height / 2 - (compassPos.top + 16)));

        mNeedDoorMarkersUpdate = true;
        updateMagicMarkers();
        updateCustomMarkers();
        updatePlayerMarkers();
        redraw();
    }

    void LocalMapBase::setCellPrefix(const std::string& prefix)
    {
        mPrefix = prefix;
        mChanged = true;
    }

    bool LocalMapBase::toggleFogOfWar()
    {
        mFogOfWarToggled = !mFogOfWarToggled;
        applyFogOfWar();
        return mFogOfWarToggled;
    }

    void LocalMapBase::applyFogOfWar()
    {
        for (int mx=0; mx<mNumCells; ++mx)
        {
            for (int my=0; my<mNumCells; ++my)
            {
                MapEntry& entry = mMaps[my + mNumCells*mx];
                MyGUI::ImageBox* fog = entry.mFogWidget;

                if (!mFogOfWarToggled || !mFogOfWarEnabled)
                {
                    fog->setImageTexture("");
                    entry.mFogTexture.reset();
                    continue;
                }
            }
        }

        redraw();
    }

    MyGUI::IntPoint LocalMapBase::getMarkerPosition(float worldX, float worldY, MarkerUserData& markerPos)
    {
        MyGUI::IntPoint widgetPos;
        // normalized cell coordinates
        float nX,nY;

        if (!mInterior)
        {
            int cellX, cellY;
            MWBase::Environment::get().getWorld()->positionToIndex(worldX, worldY, cellX, cellY);
            nX = (worldX - cellSize * cellX) / cellSize;
            // Image space is -Y up, cells are Y up
            nY = 1 - (worldY - cellSize * cellY) / cellSize;

            float cellDx = static_cast<float>(cellX - mCurX);
            float cellDy = static_cast<float>(cellY - mCurY);

            markerPos.cellX = cellX;
            markerPos.cellY = cellY;

            widgetPos = MyGUI::IntPoint(static_cast<int>(nX * mMapWidgetSize + (mCellDistance + cellDx) * mMapWidgetSize),
                                        static_cast<int>(nY * mMapWidgetSize + (mCellDistance - cellDy) * mMapWidgetSize));
        }
        else
        {
            int cellX, cellY;
            osg::Vec2f worldPos (worldX, worldY);
            mLocalMapRender->worldToInteriorMapPosition(worldPos, nX, nY, cellX, cellY);

            markerPos.cellX = cellX;
            markerPos.cellY = cellY;

            // Image space is -Y up, cells are Y up
            widgetPos = MyGUI::IntPoint(static_cast<int>(nX * mMapWidgetSize + (mCellDistance + (cellX - mCurX)) * mMapWidgetSize),
                                        static_cast<int>(nY * mMapWidgetSize + (mCellDistance - (cellY - mCurY)) * mMapWidgetSize));
        }

        markerPos.nX = nX;
        markerPos.nY = nY;
        return widgetPos;
    }

    void LocalMapBase::updateCustomMarkers()
    {
        for (MyGUI::Widget* widget : mCustomMarkerWidgets)
            MyGUI::Gui::getInstance().destroyWidget(widget);
        mCustomMarkerWidgets.clear();

        for (int dX = -mCellDistance; dX <= mCellDistance; ++dX)
        {
            for (int dY =-mCellDistance; dY <= mCellDistance; ++dY)
            {
                ESM::CellId cellId;
                cellId.mPaged = !mInterior;
                cellId.mWorldspace = (mInterior ? mPrefix : ESM::CellId::sDefaultWorldspace);
                cellId.mIndex.mX = mCurX+dX;
                cellId.mIndex.mY = mCurY+dY;

                CustomMarkerCollection::RangeType markers = mCustomMarkers.getMarkers(cellId);
                for (CustomMarkerCollection::ContainerType::const_iterator it = markers.first; it != markers.second; ++it)
                {
                    const ESM::CustomMarker& marker = it->second;

                    MarkerUserData markerPos (mLocalMapRender);
                    MyGUI::IntPoint widgetPos = getMarkerPosition(marker.mWorldX, marker.mWorldY, markerPos);

                    MyGUI::IntCoord widgetCoord(widgetPos.left - 8,
                                                widgetPos.top - 8,
                                                16, 16);
                    MarkerWidget* markerWidget = mLocalMap->createWidget<MarkerWidget>("CustomMarkerButton",
                        widgetCoord, MyGUI::Align::Default);
                    markerWidget->setDepth(Local_MarkerAboveFogLayer);
                    const ArenaMapMarkerStyle style = parseArenaMapMarker(marker.mNote);
                    markerWidget->setUserString("ToolTipType", "Layout");
                    markerWidget->setUserString("ToolTipLayout", "TextToolTipOneLine");
                    markerWidget->setUserString("Caption_TextOneLine", MyGUI::TextIterator::toTagsString(style.text));
                    markerWidget->setNormalColour(style.styled ? arenaMarkerColour(style.color) : MyGUI::Colour(0.6f, 0.6f, 0.6f));
                    markerWidget->setHoverColour(MyGUI::Colour(1.0f, 1.0f, 1.0f));
                    markerWidget->setUserData(marker);
                    markerWidget->setNeedMouseFocus(true);
                    if (style.styled)
                    {
                        MyGUI::TextBox* glyph = markerWidget->createWidget<MyGUI::TextBox>(
                            "SandBrightText", MyGUI::IntCoord(0, -1, 16, 16), MyGUI::Align::Stretch);
                        glyph->setCaption(style.kind);
                        glyph->setTextAlign(MyGUI::Align::Center);
                        glyph->setNeedMouseFocus(false);
                    }
                    customMarkerCreated(markerWidget);
                    mCustomMarkerWidgets.push_back(markerWidget);
                }
            }
        }

        redraw();
    }

    /*
        Start of tes3mp addition

        Send the LocalMapBase to our GUIController when updating player markers
    */
    void LocalMapBase::updatePlayerMarkers()
    {
        if (mwmp::GUIController* guiController = multiplayerGuiController())
            guiController->updatePlayersMarkers(this);
    }
    /*
        End of tes3mp addition
    */

    /*
        Start of tes3mp addition

        Send the MapWindow to our GUIController when updating player markers
    */
    void MapWindow::updatePlayerMarkers()
    {
        LocalMapBase::updatePlayerMarkers();

        if (mwmp::GUIController* guiController = multiplayerGuiController())
            guiController->updateGlobalMapMarkerTooltips(this);
    }
    /*
        End of tes3mp addition
    */

    void LocalMapBase::setActiveCell(const int x, const int y, bool interior)
    {
        if (x==mCurX && y==mCurY && mInterior==interior && !mChanged)
            return; // don't do anything if we're still in the same cell

        mCurX = x;
        mCurY = y;
        mInterior = interior;
        mChanged = false;

        for (int mx=0; mx<mNumCells; ++mx)
        {
            for (int my=0; my<mNumCells; ++my)
            {
                MapEntry& entry = mMaps[my + mNumCells*mx];
                entry.mMapWidget->setRenderItemTexture(nullptr);
                entry.mFogWidget->setRenderItemTexture(nullptr);
                entry.mMapTexture.reset();
                entry.mFogTexture.reset();

                entry.mCellX = x + (mx - mCellDistance);
                entry.mCellY = y - (my - mCellDistance);
            }
        }

        // Delay the door markers update until scripts have been given a chance to run.
        // If we don't do this, door markers that should be disabled will still appear on the map.
        mNeedDoorMarkersUpdate = true;

        updateMagicMarkers();
        updateCustomMarkers();
    }

    void LocalMapBase::requestMapRender(const MWWorld::CellStore *cell)
    {
        mLocalMapRender->requestMap(cell);
    }

    void LocalMapBase::redraw()
    {
        // Redraw children in proper order
        mLocalMap->getParent()->_updateChilds();
    }

    void LocalMapBase::setPlayerPos(int cellX, int cellY, const float nx, const float ny)
    {
        MyGUI::IntPoint pos(static_cast<int>(mMapWidgetSize * mCellDistance + nx*mMapWidgetSize - 16), static_cast<int>(mMapWidgetSize * mCellDistance + ny*mMapWidgetSize - 16));
        pos.left += (cellX - mCurX) * mMapWidgetSize;
        pos.top -= (cellY - mCurY) * mMapWidgetSize;

        if (pos != mCompass->getPosition())
        {
            notifyPlayerUpdate ();

            mCompass->setPosition(pos);
            MyGUI::IntPoint middle (pos.left+16, pos.top+16);
                    MyGUI::IntCoord viewsize = mLocalMap->getCoord();
            MyGUI::IntPoint viewOffset((viewsize.width / 2) - middle.left, (viewsize.height / 2) - middle.top);
            mLocalMap->setViewOffset(viewOffset);
        }
    }

    void LocalMapBase::setPlayerDir(const float x, const float y)
    {
        if (x == mLastDirectionX && y == mLastDirectionY)
            return;

        notifyPlayerUpdate ();

        MyGUI::ISubWidget* main = mCompass->getSubWidgetMain();
        MyGUI::RotatingSkin* rotatingSubskin = main->castType<MyGUI::RotatingSkin>();
        rotatingSubskin->setCenter(MyGUI::IntPoint(16,16));
        float angle = std::atan2(x,y);
        rotatingSubskin->setAngle(angle);

        mLastDirectionX = x;
        mLastDirectionY = y;
    }

    void LocalMapBase::addDetectionMarkers(int type)
    {
        std::vector<MWWorld::Ptr> markers;
        MWBase::World* world = MWBase::Environment::get().getWorld();
        world->listDetectedReferences(
                    world->getPlayerPtr(),
                    markers, MWBase::World::DetectionType(type));
        if (markers.empty())
            return;

        std::string markerTexture;
        if (type == MWBase::World::Detect_Creature)
        {
            markerTexture = "textures\\detect_animal_icon.dds";
        }
        if (type == MWBase::World::Detect_Key)
        {
            markerTexture = "textures\\detect_key_icon.dds";
        }
        if (type == MWBase::World::Detect_Enchantment)
        {
            markerTexture = "textures\\detect_enchantment_icon.dds";
        }

        int counter = 0;
        for (const MWWorld::Ptr& ptr : markers)
        {
            const ESM::Position& worldPos = ptr.getRefData().getPosition();
            MarkerUserData markerPos (mLocalMapRender);
            MyGUI::IntPoint widgetPos = getMarkerPosition(worldPos.pos[0], worldPos.pos[1], markerPos);
            MyGUI::IntCoord widgetCoord(widgetPos.left - 4,
                                        widgetPos.top - 4,
                                        8, 8);
            ++counter;
            MyGUI::ImageBox* markerWidget = mLocalMap->createWidget<MyGUI::ImageBox>("ImageBox",
                widgetCoord, MyGUI::Align::Default);
            markerWidget->setDepth(Local_MarkerAboveFogLayer);
            markerWidget->setImageTexture(markerTexture);
            markerWidget->setImageCoord(MyGUI::IntCoord(0,0,8,8));
            markerWidget->setNeedMouseFocus(false);
            mMagicMarkerWidgets.push_back(markerWidget);
        }
    }

    void LocalMapBase::onFrame(float dt)
    {
        if (mNeedDoorMarkersUpdate)
        {
            updateDoorMarkers();
            mNeedDoorMarkersUpdate = false;
        }

        mMarkerUpdateTimer += dt;

        if (mMarkerUpdateTimer >= 0.25)
        {
            mMarkerUpdateTimer = 0;
            updateMagicMarkers();
        }

        updateRequiredMaps();
    }

    bool widgetCropped(MyGUI::Widget* widget, MyGUI::Widget* cropTo)
    {
        MyGUI::IntRect coord = widget->getAbsoluteRect();
        MyGUI::IntRect croppedCoord = cropTo->getAbsoluteRect();
        if (coord.left < croppedCoord.left && coord.right < croppedCoord.left)
            return true;
        if (coord.left > croppedCoord.right && coord.right > croppedCoord.right)
            return true;
        if (coord.top < croppedCoord.top && coord.bottom < croppedCoord.top)
            return true;
        if (coord.top > croppedCoord.bottom && coord.bottom > croppedCoord.bottom)
            return true;
        return false;
    }

    void LocalMapBase::updateRequiredMaps()
    {
        bool needRedraw = false;
        for (MapEntry& entry : mMaps)
        {
            if (widgetCropped(entry.mMapWidget, mLocalMap))
                continue;

            if (!entry.mMapTexture)
            {
                if (!mInterior)
                    requestMapRender(MWBase::Environment::get().getWorld()->getExterior (entry.mCellX, entry.mCellY));

                osg::ref_ptr<osg::Texture2D> texture = mLocalMapRender->getMapTexture(entry.mCellX, entry.mCellY);
                if (texture)
                {
                    entry.mMapTexture.reset(new osgMyGUI::OSGTexture(texture));
                    entry.mMapWidget->setRenderItemTexture(entry.mMapTexture.get());
                    entry.mMapWidget->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
                    needRedraw = true;
                }
                else
                    entry.mMapTexture.reset(new osgMyGUI::OSGTexture("", nullptr));
            }
            if (!entry.mFogTexture && mFogOfWarToggled && mFogOfWarEnabled)
            {
                osg::ref_ptr<osg::Texture2D> tex = mLocalMapRender->getFogOfWarTexture(entry.mCellX, entry.mCellY);
                if (tex)
                {
                    entry.mFogTexture.reset(new osgMyGUI::OSGTexture(tex));
                    entry.mFogWidget->setRenderItemTexture(entry.mFogTexture.get());
                    entry.mFogWidget->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 1.f, 1.f, 0.f));
                }
                else
                {
                    entry.mFogWidget->setImageTexture("black");
                    entry.mFogTexture.reset(new osgMyGUI::OSGTexture("", nullptr));
                }
                needRedraw = true;
            }
        }
        if (needRedraw)
            redraw();
    }

    void LocalMapBase::updateDoorMarkers()
    {
        // clear all previous door markers
        for (MyGUI::Widget* widget : mDoorMarkerWidgets)
            MyGUI::Gui::getInstance().destroyWidget(widget);
        mDoorMarkerWidgets.clear();

        MWBase::World* world = MWBase::Environment::get().getWorld();

        // Retrieve the door markers we want to show
        std::vector<MWBase::World::DoorMarker> doors;
        if (mInterior)
        {
            MWWorld::CellStore* cell = world->getInterior (mPrefix);
            world->getDoorMarkers(cell, doors);
        }
        else
        {
            for (int dX=-mCellDistance; dX<=mCellDistance; ++dX)
            {
                for (int dY=-mCellDistance; dY<=mCellDistance; ++dY)
                {
                    MWWorld::CellStore* cell = world->getExterior (mCurX+dX, mCurY+dY);
                    world->getDoorMarkers(cell, doors);
                }
            }
        }

        // Create a widget for each marker
        int counter = 0;
        for (MWBase::World::DoorMarker& marker : doors)
        {
            std::vector<std::string> destNotes;
            CustomMarkerCollection::RangeType markers = mCustomMarkers.getMarkers(marker.dest);
            for (CustomMarkerCollection::ContainerType::const_iterator iter = markers.first; iter != markers.second; ++iter)
                destNotes.push_back(iter->second.mNote);

            MarkerUserData data (mLocalMapRender);
            data.notes = destNotes;
            data.caption = marker.name;
            MyGUI::IntPoint widgetPos = getMarkerPosition(marker.x, marker.y, data);
            MyGUI::IntCoord widgetCoord(widgetPos.left - 4,
                                        widgetPos.top - 4,
                                        8, 8);
            ++counter;
            MarkerWidget* markerWidget = mLocalMap->createWidget<MarkerWidget>("MarkerButton",
                widgetCoord, MyGUI::Align::Default);
            markerWidget->setNormalColour(MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}")));
            markerWidget->setHoverColour(MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal_over}")));
            markerWidget->setDepth(Local_MarkerLayer);
            markerWidget->setNeedMouseFocus(true);
            // Used by tooltips to not show the tooltip if marker is hidden by fog of war
            markerWidget->setUserString("ToolTipType", "MapMarker");

            markerWidget->setUserData(data);
            doorMarkerCreated(markerWidget);

            mDoorMarkerWidgets.push_back(markerWidget);
        }
    }

    void LocalMapBase::updateMagicMarkers()
    {
        // clear all previous markers
        for (MyGUI::Widget* widget : mMagicMarkerWidgets)
            MyGUI::Gui::getInstance().destroyWidget(widget);
        mMagicMarkerWidgets.clear();

        addDetectionMarkers(MWBase::World::Detect_Creature);
        addDetectionMarkers(MWBase::World::Detect_Key);
        addDetectionMarkers(MWBase::World::Detect_Enchantment);

        // Add marker for the spot marked with Mark magic effect
        MWWorld::CellStore* markedCell = nullptr;
        ESM::Position markedPosition;
        MWBase::Environment::get().getWorld()->getPlayer().getMarkedPosition(markedCell, markedPosition);
        if (markedCell && markedCell->isExterior() == !mInterior
                && (!mInterior || Misc::StringUtils::ciEqual(markedCell->getCell()->mName, mPrefix)))
        {
            MarkerUserData markerPos (mLocalMapRender);
            MyGUI::IntPoint widgetPos = getMarkerPosition(markedPosition.pos[0], markedPosition.pos[1], markerPos);
            MyGUI::IntCoord widgetCoord(widgetPos.left - 4,
                                        widgetPos.top - 4,
                                        8, 8);
            MyGUI::ImageBox* markerWidget = mLocalMap->createWidget<MyGUI::ImageBox>("ImageBox",
                widgetCoord, MyGUI::Align::Default);
            markerWidget->setDepth(Local_MarkerAboveFogLayer);
            markerWidget->setImageTexture("textures\\menu_map_smark.dds");
            markerWidget->setNeedMouseFocus(false);
            mMagicMarkerWidgets.push_back(markerWidget);
        }

        redraw();
    }

    // ------------------------------------------------------------------------------------------

    MapWindow::MapWindow(CustomMarkerCollection &customMarkers, DragAndDrop* drag, MWRender::LocalMap* localMapRender, SceneUtil::WorkQueue* workQueue)
        : WindowPinnableBase("openmw_map_window.layout")
        , LocalMapBase(customMarkers, localMapRender)
        , NoDrop(drag, mMainWidget)
        , mGlobalMap(nullptr)
        , mGlobalMapImage(nullptr)
        , mGlobalMapOverlay(nullptr)
        , mZoomInButton(nullptr)
        , mZoomOutButton(nullptr)
        , mLocalZoomStep(std::clamp(Settings::Manager::getInt("local map zoom step", "Map"), -2, 3))
        , mGlobalZoomStep(std::clamp(Settings::Manager::getInt("global map zoom step", "Map"), -2, 3))
        , mLastGlobalPlayerX(0.f)
        , mLastGlobalPlayerY(0.f)
        , mHasGlobalPlayerPosition(false)
        , mGlobal(Settings::Manager::getBool("global", "Map"))
        , mEventBoxGlobal(nullptr)
        , mEventBoxLocal(nullptr)
        , mGlobalMapRender(new MWRender::GlobalMap(localMapRender->getRoot(), workQueue))
        , mEditNoteDialog()
    {
        static bool registered = false;
        if (!registered)
        {
            MyGUI::FactoryManager::getInstance().registerFactory<MarkerWidget>("Widget");
            registered = true;
        }

        mEditNoteDialog.setVisible(false);
        mEditNoteDialog.eventOkClicked += MyGUI::newDelegate(this, &MapWindow::onNoteEditOk);
        mEditNoteDialog.eventDeleteClicked += MyGUI::newDelegate(this, &MapWindow::onNoteEditDelete);

        setCoord(500,0,320,300);

        getWidget(mLocalMap, "LocalMap");
        getWidget(mGlobalMap, "GlobalMap");
        getWidget(mGlobalMapImage, "GlobalMapImage");
        getWidget(mGlobalMapOverlay, "GlobalMapOverlay");
        getWidget(mPlayerArrowLocal, "CompassLocal");
        getWidget(mPlayerArrowGlobal, "CompassGlobal");

        mPlayerArrowGlobal->setDepth(Global_CompassLayer);
        mPlayerArrowGlobal->setNeedMouseFocus(false);
        mGlobalMapImage->setDepth(Global_MapLayer);
        mGlobalMapOverlay->setDepth(Global_ExploreOverlayLayer);

        mLastScrollWindowCoordinates = mLocalMap->getCoord();
        mLocalMap->eventChangeCoord += MyGUI::newDelegate(this, &MapWindow::onChangeScrollWindowCoord);

        mGlobalMap->setVisible (false);

        getWidget(mButton, "WorldButton");
        mButton->eventMouseButtonClick += MyGUI::newDelegate(this, &MapWindow::onWorldButtonClicked);
        mButton->setCaptionWithReplacing( mGlobal ? "#{sLocal}" : "#{sWorld}");

        getWidget(mZoomOutButton, "ZoomOutButton");
        getWidget(mZoomInButton, "ZoomInButton");
        mZoomOutButton->eventMouseButtonClick += MyGUI::newDelegate(this, &MapWindow::onZoomOutClicked);
        mZoomInButton->eventMouseButtonClick += MyGUI::newDelegate(this, &MapWindow::onZoomInClicked);

        getWidget(mEventBoxGlobal, "EventBoxGlobal");
        mEventBoxGlobal->eventMouseDrag += MyGUI::newDelegate(this, &MapWindow::onMouseDrag);
        mEventBoxGlobal->eventMouseButtonPressed += MyGUI::newDelegate(this, &MapWindow::onDragStart);
        mEventBoxGlobal->setDepth(Global_ExploreOverlayLayer);

        getWidget(mEventBoxLocal, "EventBoxLocal");
        mEventBoxLocal->eventMouseDrag += MyGUI::newDelegate(this, &MapWindow::onMouseDrag);
        mEventBoxLocal->eventMouseButtonPressed += MyGUI::newDelegate(this, &MapWindow::onDragStart);
        mEventBoxLocal->eventMouseButtonDoubleClick += MyGUI::newDelegate(this, &MapWindow::onMapDoubleClicked);

        LocalMapBase::init(mLocalMap, mPlayerArrowLocal);

        mGlobalMap->setVisible(mGlobal);
        mLocalMap->setVisible(!mGlobal);
        applyLocalZoom();
        updatePinnedPresentation();
    }

    void MapWindow::onNoteEditOk()
    {
        const bool editing = mEditNoteDialog.getDeleteButtonShown();
        const ArenaMapMarkerStyle previousStyle = editing ? parseArenaMapMarker(mEditingMarker.mNote) : ArenaMapMarkerStyle();
        const std::string kind = mEditNoteDialog.getMarkerKind();
        const std::string color = mEditNoteDialog.getMarkerColor();
        const bool groupShare = mEditNoteDialog.getGroupShare();
        const std::string text = mEditNoteDialog.getText();
        const std::string encoded = makeArenaPersonalMarker(kind, color, groupShare, text);

        if (editing)
            mCustomMarkers.updateMarker(mEditingMarker, encoded);
        else
        {
            mEditingMarker.mNote = encoded;
            mCustomMarkers.addMarker(mEditingMarker);
        }

        const std::string cell = mEditingMarker.mCell.mPaged ? std::string("world") : mEditingMarker.mCell.mWorldspace;

        // Server-authoritative personal persistence. Cell-local map indices are
        // transmitted as well so markers in large interiors survive reconnects.
        {
            std::ostringstream cmd;
            cmd << "/mapmark add " << (mEditingMarker.mCell.mPaged ? "1" : "0") << ' '
                << arenaMapUrlEncode(cell) << ' ' << mEditingMarker.mCell.mIndex.mX << ' ' << mEditingMarker.mCell.mIndex.mY << ' '
                << mEditingMarker.mWorldX << ' ' << mEditingMarker.mWorldY << ' '
                << kind << ' ' << color << ' ' << (groupShare ? "1" : "0") << ' '
                << arenaMapUrlEncode(text.empty() ? std::string(" ") : text);
            sendArenaGroupMarkerCommand(cmd.str());
        }

        if (groupShare)
        {
            std::ostringstream cmd;
            cmd << "/groupmark add " << (mEditingMarker.mCell.mPaged ? "1" : "0") << ' '
                << arenaMapUrlEncode(cell) << ' ' << mEditingMarker.mCell.mIndex.mX << ' ' << mEditingMarker.mCell.mIndex.mY << ' '
                << mEditingMarker.mWorldX << ' ' << mEditingMarker.mWorldY << ' '
                << kind << ' ' << color << ' ' << arenaMapUrlEncode(text.empty() ? std::string(" ") : text);
            sendArenaGroupMarkerCommand(cmd.str());
        }
        else if (editing && previousStyle.styled && previousStyle.group)
        {
            // Turning group sharing off must remove the old server group copy.
            std::ostringstream cmd;
            cmd << "/groupmark deleteat " << (mEditingMarker.mCell.mPaged ? "1" : "0") << ' '
                << arenaMapUrlEncode(cell) << ' ' << mEditingMarker.mWorldX << ' ' << mEditingMarker.mWorldY;
            sendArenaGroupMarkerCommand(cmd.str());
        }

        mEditNoteDialog.setVisible(false);
    }

    void MapWindow::onNoteEditDelete()
    {
        ConfirmationDialog* confirmation = MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        confirmation->askForConfirmation("#{sDeleteNote}");
        confirmation->eventCancelClicked.clear();
        confirmation->eventOkClicked.clear();
        confirmation->eventOkClicked += MyGUI::newDelegate(this, &MapWindow::onNoteEditDeleteConfirm);
    }

    void MapWindow::onNoteEditDeleteConfirm()
    {
        const ArenaMapMarkerStyle style = parseArenaMapMarker(mEditingMarker.mNote);
        const std::string cell = mEditingMarker.mCell.mPaged ? std::string("world") : mEditingMarker.mCell.mWorldspace;

        {
            std::ostringstream cmd;
            cmd << "/mapmark deleteat " << (mEditingMarker.mCell.mPaged ? "1" : "0") << ' '
                << arenaMapUrlEncode(cell) << ' ' << mEditingMarker.mWorldX << ' ' << mEditingMarker.mWorldY;
            sendArenaGroupMarkerCommand(cmd.str());
        }
        if (style.styled && style.group)
        {
            std::ostringstream cmd;
            cmd << "/groupmark deleteat " << (mEditingMarker.mCell.mPaged ? "1" : "0") << ' '
                << arenaMapUrlEncode(cell) << ' ' << mEditingMarker.mWorldX << ' ' << mEditingMarker.mWorldY;
            sendArenaGroupMarkerCommand(cmd.str());
        }
        mCustomMarkers.deleteMarker(mEditingMarker);
        mEditNoteDialog.setVisible(false);
    }

    void MapWindow::onCustomMarkerDoubleClicked(MyGUI::Widget *sender)
    {
        mEditingMarker = *sender->getUserData<ESM::CustomMarker>();
        const ArenaMapMarkerStyle style = parseArenaMapMarker(mEditingMarker.mNote);
        mEditNoteDialog.setText(style.text);
        mEditNoteDialog.setMarkerStyle(style.kind, style.color, style.group);
        mEditNoteDialog.showDeleteButton(true);
        mEditNoteDialog.setVisible(true);
    }

    void MapWindow::onMapDoubleClicked(MyGUI::Widget *sender)
    {
        MyGUI::IntPoint clickedPos = MyGUI::InputManager::getInstance().getMousePosition();

        MyGUI::IntPoint widgetPos = clickedPos - mEventBoxLocal->getAbsolutePosition();
        int x = int(widgetPos.left/float(mMapWidgetSize))-mCellDistance;
        int y = (int(widgetPos.top/float(mMapWidgetSize))-mCellDistance)*-1;
        float nX = widgetPos.left/float(mMapWidgetSize) - int(widgetPos.left/float(mMapWidgetSize));
        float nY = widgetPos.top/float(mMapWidgetSize) - int(widgetPos.top/float(mMapWidgetSize));
        x += mCurX;
        y += mCurY;

        osg::Vec2f worldPos;
        if (mInterior)
        {
            worldPos = mLocalMapRender->interiorMapToWorldPosition(nX, nY, x, y);
        }
        else
        {
            worldPos.x() = (x + nX) * cellSize;
            worldPos.y() = (y + (1.0f-nY)) * cellSize;
        }

        mEditingMarker.mWorldX = worldPos.x();
        mEditingMarker.mWorldY = worldPos.y();

        mEditingMarker.mCell.mPaged = !mInterior;
        if (mInterior)
            mEditingMarker.mCell.mWorldspace = LocalMapBase::mPrefix;
        else
        {
            mEditingMarker.mCell.mWorldspace = ESM::CellId::sDefaultWorldspace;
            mEditingMarker.mCell.mIndex.mX = x;
            mEditingMarker.mCell.mIndex.mY = y;
        }

        mEditNoteDialog.setMarkerStyle("?", "yellow", false);
        mEditNoteDialog.setVisible(true);
        mEditNoteDialog.showDeleteButton(false);
        mEditNoteDialog.setText("");
    }

    void MapWindow::onChangeScrollWindowCoord(MyGUI::Widget* sender)
    {
        MyGUI::IntCoord currentCoordinates = sender->getCoord();

        MyGUI::IntPoint currentViewPortCenter = MyGUI::IntPoint(currentCoordinates.width / 2, currentCoordinates.height / 2);
        MyGUI::IntPoint lastViewPortCenter = MyGUI::IntPoint(mLastScrollWindowCoordinates.width / 2, mLastScrollWindowCoordinates.height / 2);
        MyGUI::IntPoint viewPortCenterDiff = currentViewPortCenter - lastViewPortCenter;

        mLocalMap->setViewOffset(mLocalMap->getViewOffset() + viewPortCenterDiff);
        mGlobalMap->setViewOffset(mGlobalMap->getViewOffset() + viewPortCenterDiff);

        mLastScrollWindowCoordinates = currentCoordinates;
    }

    void MapWindow::setVisible(bool visible)
    {
        WindowBase::setVisible(visible);
        updatePinnedPresentation();
    }

    void MapWindow::renderGlobalMap()
    {
        mGlobalMapRender->render();
        mGlobalMap->setCanvasSize(mGlobalMapRender->getWidth(), mGlobalMapRender->getHeight());
        mGlobalMapImage->setSize(mGlobalMapRender->getWidth(), mGlobalMapRender->getHeight());
        applyGlobalZoom();
    }

    MapWindow::~MapWindow()
    {
        delete mGlobalMapRender;
    }

    void MapWindow::setCellName(const std::string& cellName)
    {
        setTitle("#{sCell=" + cellName + "}");
    }

    void MapWindow::addVisitedLocation(const std::string& name, int x, int y)
    {
        CellId cell;
        cell.first = x;
        cell.second = y;
        if (mMarkers.insert(cell).second)
        {
            float worldX, worldY;
            mGlobalMapRender->cellTopLeftCornerToImageSpace (x, y, worldX, worldY);

            const int markerSize = 12;
            const float zoom = getGlobalZoomFactor();
            const float centerX = worldX * mGlobalMapRender->getWidth() + mGlobalMapRender->getCellSize() * 0.5f;
            const float centerY = worldY * mGlobalMapRender->getHeight() + mGlobalMapRender->getCellSize() * 0.5f;
            MyGUI::IntCoord widgetCoord(
                        static_cast<int>(std::lround(centerX * zoom)) - markerSize / 2,
                        static_cast<int>(std::lround(centerY * zoom)) - markerSize / 2,
                        markerSize, markerSize);

            MyGUI::Widget* markerWidget = mGlobalMap->createWidget<MyGUI::Widget>("MarkerButton",
                widgetCoord, MyGUI::Align::Default);

            markerWidget->setUserString("Caption_TextOneLine", "#{sCell=" + name + "}");

            setGlobalMapMarkerTooltip(markerWidget, x, y);

            markerWidget->setUserString("ToolTipLayout", "TextToolTipOneLine");

            markerWidget->setNeedMouseFocus(true);
            markerWidget->setColour(MyGUI::Colour::parse(MyGUI::LanguageManager::getInstance().replaceTags("#{fontcolour=normal}")));
            markerWidget->setDepth(Global_MarkerLayer);
            markerWidget->eventMouseDrag += MyGUI::newDelegate(this, &MapWindow::onMouseDrag);
            markerWidget->eventMouseButtonPressed += MyGUI::newDelegate(this, &MapWindow::onDragStart);
            mGlobalMapMarkers[std::make_pair(x,y)] = markerWidget;
        }
    }

    void MapWindow::cellExplored(int x, int y)
    {
        mGlobalMapRender->cleanupCameras();
        mGlobalMapRender->exploreCell(x, y, mLocalMapRender->getMapTexture(x, y));
    }

    void MapWindow::onFrame(float dt)
    {
        LocalMapBase::onFrame(dt);
        NoDrop::onFrame(dt);

        // The GUI mode can change while a pinned map remains visible, so keep
        // its HUD/menu presentation in sync with inventory/menu transitions.
        updatePinnedPresentation();
    }

    void MapWindow::setGlobalMapMarkerTooltip(MyGUI::Widget* markerWidget, int x, int y)
    {
        ESM::CellId cellId;
        cellId.mIndex.mX = x;
        cellId.mIndex.mY = y;
        cellId.mWorldspace = ESM::CellId::sDefaultWorldspace;
        cellId.mPaged = true;
        CustomMarkerCollection::RangeType markers = mCustomMarkers.getMarkers(cellId);
        std::vector<std::string> destNotes;
        for (CustomMarkerCollection::ContainerType::const_iterator it = markers.first; it != markers.second; ++it)
        {
            const ArenaMapMarkerStyle style = parseArenaMapMarker(it->second.mNote);
            destNotes.push_back(style.styled ? style.kind + "  " + style.text : it->second.mNote);
        }

        if (!destNotes.empty())
        {
            MarkerUserData data (nullptr);
            data.notes = destNotes;
            data.caption = markerWidget->getUserString("Caption_TextOneLine");
            markerWidget->setUserData(data);
            markerWidget->setUserString("ToolTipType", "MapMarker");
        }
        else
        {
            markerWidget->setUserString("ToolTipType", "Layout");
        }
    }

    /*
        Start of tes3mp addition

        Allow the setting of the image data for a global map tile from elsewhere
        in the code
    */
    void MapWindow::setGlobalMapImage(int cellX, int cellY, const std::vector<char>& imageData)
    {
        mGlobalMapRender->setImage(cellX, cellY, imageData);
    }
    /*
        End of tes3mp addition
    */

    void MapWindow::updateCustomMarkers()
    {
        LocalMapBase::updateCustomMarkers();

        for (auto& widgetPair : mGlobalMapMarkers)
        {
            int x = widgetPair.first.first;
            int y = widgetPair.first.second;
            MyGUI::Widget* markerWidget = widgetPair.second;
            setGlobalMapMarkerTooltip(markerWidget, x, y);
        }
    }

    void MapWindow::onDragStart(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        if (_id!=MyGUI::MouseButton::Left) return;
        mLastDragPos = MyGUI::IntPoint(_left, _top);
    }

    void MapWindow::onMouseDrag(MyGUI::Widget* _sender, int _left, int _top, MyGUI::MouseButton _id)
    {
        if (_id!=MyGUI::MouseButton::Left) return;

        MyGUI::IntPoint diff = MyGUI::IntPoint(_left, _top) - mLastDragPos;

        if (!mGlobal)
            mLocalMap->setViewOffset( mLocalMap->getViewOffset() + diff );
        else
            mGlobalMap->setViewOffset( mGlobalMap->getViewOffset() + diff );

        mLastDragPos = MyGUI::IntPoint(_left, _top);
    }

    void MapWindow::onWorldButtonClicked(MyGUI::Widget* _sender)
    {
        mGlobal = !mGlobal;
        mGlobalMap->setVisible(mGlobal);
        mLocalMap->setVisible(!mGlobal);

        Settings::Manager::setBool("global", "Map", mGlobal);

        mButton->setCaptionWithReplacing( mGlobal ? "#{sLocal}" :
                "#{sWorld}");

        if (mGlobal)
            globalMapUpdatePlayer ();

        updatePinnedPresentation();
    }

    void MapWindow::onZoomInClicked(MyGUI::Widget*)
    {
        if (mGlobal)
        {
            mGlobalZoomStep = std::min(3, mGlobalZoomStep + 1);
            applyGlobalZoom();
            return;
        }

        mLocalZoomStep = std::min(3, mLocalZoomStep + 1);
        applyLocalZoom();
    }

    void MapWindow::onZoomOutClicked(MyGUI::Widget*)
    {
        if (mGlobal)
        {
            mGlobalZoomStep = std::max(-2, mGlobalZoomStep - 1);
            applyGlobalZoom();
            return;
        }

        mLocalZoomStep = std::max(-2, mLocalZoomStep - 1);
        applyLocalZoom();
    }

    void MapWindow::applyLocalZoom()
    {
        static const float zoomFactors[] = {0.60f, 0.78f, 1.0f, 1.30f, 1.65f, 2.05f};
        const int index = std::clamp(mLocalZoomStep + 2, 0, 5);
        const int baseSize = std::max(1, Settings::Manager::getInt("local map widget size", "Map"));
        setMapWidgetSize(static_cast<int>(std::lround(baseSize * zoomFactors[index])));

        // Y052: remember the chosen scale across sessions instead of snapping
        // back to 1.0 on every launch.
        if (Settings::Manager::getInt("local map zoom step", "Map") != mLocalZoomStep)
            Settings::Manager::setInt("local map zoom step", "Map", mLocalZoomStep);
    }

    float MapWindow::getGlobalZoomFactor() const
    {
        static const float zoomFactors[] = {0.60f, 0.78f, 1.0f, 1.30f, 1.65f, 2.05f};
        const int index = std::clamp(mGlobalZoomStep + 2, 0, 5);
        return zoomFactors[index];
    }

    void MapWindow::applyGlobalZoom()
    {
        if (!mGlobalMap || !mGlobalMapImage || !mGlobalMapRender)
            return;

        const int baseWidth = mGlobalMapRender->getWidth();
        const int baseHeight = mGlobalMapRender->getHeight();
        if (baseWidth <= 0 || baseHeight <= 0)
            return;

        const float zoom = getGlobalZoomFactor();
        const int newWidth = std::max(1, static_cast<int>(std::lround(baseWidth * zoom)));
        const int newHeight = std::max(1, static_cast<int>(std::lround(baseHeight * zoom)));

        // Preserve the point currently under the centre of the viewport. This keeps
        // manual panning intact instead of snapping back to the player on every zoom.
        const MyGUI::IntSize oldCanvas = mGlobalMap->getCanvasSize();
        const MyGUI::IntSize viewSize = mGlobalMap->getSize();
        const MyGUI::IntPoint oldOffset = mGlobalMap->getViewOffset();
        const bool canPreserveCenter = oldCanvas.width > 0 && oldCanvas.height > 0;
        float oldCenterX = 0.f;
        float oldCenterY = 0.f;
        if (canPreserveCenter)
        {
            oldCenterX = static_cast<float>(viewSize.width) * 0.5f - oldOffset.left;
            oldCenterY = static_cast<float>(viewSize.height) * 0.5f - oldOffset.top;
        }

        mGlobalMap->setCanvasSize(newWidth, newHeight);
        mGlobalMapImage->setSize(newWidth, newHeight);
        if (mGlobalMapOverlay)
            mGlobalMapOverlay->setSize(newWidth, newHeight);
        if (mEventBoxGlobal)
            mEventBoxGlobal->setCoord(0, 0, newWidth, newHeight);

        // Reposition visited-location markers from their cell coordinates so repeated
        // zooming does not accumulate rounding error. Marker icons themselves stay
        // a constant screen size, matching the local-map behaviour.
        for (const auto& markerPair : mGlobalMapMarkers)
        {
            float worldX = 0.f;
            float worldY = 0.f;
            mGlobalMapRender->cellTopLeftCornerToImageSpace(
                markerPair.first.first, markerPair.first.second, worldX, worldY);

            constexpr int markerSize = 12;
            const float centerX = worldX * baseWidth + mGlobalMapRender->getCellSize() * 0.5f;
            const float centerY = worldY * baseHeight + mGlobalMapRender->getCellSize() * 0.5f;
            markerPair.second->setPosition(MyGUI::IntPoint(
                static_cast<int>(std::lround(centerX * zoom)) - markerSize / 2,
                static_cast<int>(std::lround(centerY * zoom)) - markerSize / 2));
        }

        if (mHasGlobalPlayerPosition && mPlayerArrowGlobal)
        {
            float x = 0.f;
            float y = 0.f;
            mGlobalMapRender->worldPosToImageSpace(mLastGlobalPlayerX, mLastGlobalPlayerY, x, y);
            x *= baseWidth * zoom;
            y *= baseHeight * zoom;
            mPlayerArrowGlobal->setPosition(MyGUI::IntPoint(
                static_cast<int>(std::lround(x)) - 16,
                static_cast<int>(std::lround(y)) - 16));
        }

        if (canPreserveCenter)
        {
            const float ratioX = static_cast<float>(newWidth) / oldCanvas.width;
            const float ratioY = static_cast<float>(newHeight) / oldCanvas.height;
            mGlobalMap->setViewOffset(MyGUI::IntPoint(
                static_cast<int>(std::lround(viewSize.width * 0.5f - oldCenterX * ratioX)),
                static_cast<int>(std::lround(viewSize.height * 0.5f - oldCenterY * ratioY))));
        }

        if (Settings::Manager::getInt("global map zoom step", "Map") != mGlobalZoomStep)
            Settings::Manager::setInt("global map zoom step", "Map", mGlobalZoomStep);
    }

    void MapWindow::updatePinnedPresentation()
    {
        if (!mMainWidget)
            return;

        const bool visible = isVisible();
        const bool guiOpen = MWBase::Environment::get().getWindowManager()->getMode() != MWGui::GM_None;

        // ArenaMP Y051: pinning only removes the frame while the map is acting as
        // an in-game HUD/minimap. Opening inventory or another GUI mode restores
        // the normal window frame and controls without changing the saved pin state.
        const bool framelessHud = mPinned && !guiOpen;
        const bool normalControls = visible && guiOpen;

        if (mButton)
            mButton->setVisible(normalControls);
        if (mZoomInButton)
            mZoomInButton->setVisible(normalControls);
        if (mZoomOutButton)
            mZoomOutButton->setVisible(normalControls);

        Window* window = mMainWidget->castType<Window>(false);
        if (!window)
            return;

        for (MyGUI::Widget* widget : window->getSkinWidgetsByName("Action"))
            widget->setVisible(!framelessHud);
        for (MyGUI::Widget* widget : window->getSkinWidgetsByName("Caption"))
            widget->setVisible(!framelessHud);

        if (mPinButton)
            mPinButton->setVisible(visible);

        if (MyGUI::Widget* client = window->getSkinWidget("Client", false))
        {
            const MyGUI::IntSize size = mMainWidget->getSize();
            if (framelessHud)
                client->setCoord(0, 0, size.width, size.height);
            else
                client->setCoord(8, 28, std::max(1, size.width - 16), std::max(1, size.height - 36));
        }
    }

    void MapWindow::onPinToggled()
    {
        Settings::Manager::setBool("map pin", "Windows", mPinned);

        MWBase::Environment::get().getWindowManager()->setMinimapVisibility(!mPinned);
        updatePinnedPresentation();
    }

    void MapWindow::onTitleDoubleClicked()
    {
        if (MyGUI::InputManager::getInstance().isShiftPressed())
            MWBase::Environment::get().getWindowManager()->toggleMaximized(this);
        else if (!mPinned)
            MWBase::Environment::get().getWindowManager()->toggleVisible(GW_Map);
    }

    void MapWindow::onOpen()
    {
        ensureGlobalMapLoaded();

        globalMapUpdatePlayer();
    }

    void MapWindow::globalMapUpdatePlayer ()
    {
        // For interiors, position is set by WindowManager via setGlobalMapPlayerPosition
        if (MWBase::Environment::get().getWorld ()->isCellExterior ())
        {
            osg::Vec3f pos = MWBase::Environment::get().getWorld ()->getPlayerPtr().getRefData().getPosition().asVec3();
            setGlobalMapPlayerPosition(pos.x(), pos.y());
        }
    }

    void MapWindow::notifyPlayerUpdate ()
    {
        globalMapUpdatePlayer ();

        setGlobalMapPlayerDir(mLastDirectionX, mLastDirectionY);
    }

    void MapWindow::setGlobalMapPlayerPosition(float worldX, float worldY)
    {
        mLastGlobalPlayerX = worldX;
        mLastGlobalPlayerY = worldY;
        mHasGlobalPlayerPosition = true;

        float x, y;
        mGlobalMapRender->worldPosToImageSpace(worldX, worldY, x, y);
        const float zoom = getGlobalZoomFactor();
        x *= mGlobalMapRender->getWidth() * zoom;
        y *= mGlobalMapRender->getHeight() * zoom;

        mPlayerArrowGlobal->setPosition(MyGUI::IntPoint(
            static_cast<int>(std::lround(x)) - 16,
            static_cast<int>(std::lround(y)) - 16));

        // Set the view offset so that player is in the center.
        MyGUI::IntSize viewsize = mGlobalMap->getSize();
        MyGUI::IntPoint viewoffs(
            static_cast<int>(std::lround(viewsize.width * 0.5f - x)),
            static_cast<int>(std::lround(viewsize.height * 0.5f - y)));
        mGlobalMap->setViewOffset(viewoffs);
    }

    void MapWindow::setGlobalMapPlayerDir(const float x, const float y)
    {
        MyGUI::ISubWidget* main = mPlayerArrowGlobal->getSubWidgetMain();
        MyGUI::RotatingSkin* rotatingSubskin = main->castType<MyGUI::RotatingSkin>();
        rotatingSubskin->setCenter(MyGUI::IntPoint(16,16));
        float angle = std::atan2(x,y);
        rotatingSubskin->setAngle(angle);
    }

    void MapWindow::ensureGlobalMapLoaded()
    {
        if (!mGlobalMapTexture.get())
        {
            mGlobalMapTexture.reset(new osgMyGUI::OSGTexture(mGlobalMapRender->getBaseTexture()));
            mGlobalMapImage->setRenderItemTexture(mGlobalMapTexture.get());
            mGlobalMapImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));

            mGlobalMapOverlayTexture.reset(new osgMyGUI::OSGTexture(mGlobalMapRender->getOverlayTexture()));
            mGlobalMapOverlay->setRenderItemTexture(mGlobalMapOverlayTexture.get());
            mGlobalMapOverlay->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));

            // Redraw children in proper order
            mGlobalMap->getParent()->_updateChilds();
        }
    }

    void MapWindow::clear()
    {
        mMarkers.clear();

        mGlobalMapRender->clear();
        mChanged = true;

        for (auto& widgetPair : mGlobalMapMarkers)
            MyGUI::Gui::getInstance().destroyWidget(widgetPair.second);
        mGlobalMapMarkers.clear();
    }

    void MapWindow::write(ESM::ESMWriter &writer, Loading::Listener& progress)
    {
        ESM::GlobalMap map;
        mGlobalMapRender->write(map);

        map.mMarkers = mMarkers;

        writer.startRecord(ESM::REC_GMAP);
        map.save(writer);
        writer.endRecord(ESM::REC_GMAP);
    }

    void MapWindow::readRecord(ESM::ESMReader &reader, uint32_t type)
    {
        if (type == ESM::REC_GMAP)
        {
            ESM::GlobalMap map;
            map.load(reader);

            mGlobalMapRender->read(map);

            for (const ESM::GlobalMap::CellId& cellId : map.mMarkers)
            {
                const ESM::Cell* cell = MWBase::Environment::get().getWorld()->getStore().get<ESM::Cell>().search(cellId.first, cellId.second);
                if (cell && !cell->mName.empty())
                    addVisitedLocation(cell->mName, cellId.first, cellId.second);
            }
        }
    }

    void MapWindow::setAlpha(float alpha)
    {
        NoDrop::setAlpha(alpha);
        // can't allow showing map with partial transparency, as the fog of war will also go transparent
        // and reveal parts of the map you shouldn't be able to see
        for (MapEntry& entry : mMaps)
            entry.mMapWidget->setVisible(alpha == 1);
    }

    void MapWindow::customMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseDrag += MyGUI::newDelegate(this, &MapWindow::onMouseDrag);
        marker->eventMouseButtonPressed += MyGUI::newDelegate(this, &MapWindow::onDragStart);
        marker->eventMouseButtonDoubleClick += MyGUI::newDelegate(this, &MapWindow::onCustomMarkerDoubleClicked);
    }

    void MapWindow::doorMarkerCreated(MyGUI::Widget *marker)
    {
        marker->eventMouseDrag += MyGUI::newDelegate(this, &MapWindow::onMouseDrag);
        marker->eventMouseButtonPressed += MyGUI::newDelegate(this, &MapWindow::onDragStart);
    }

    // -------------------------------------------------------------------

    EditNoteDialog::EditNoteDialog()
        : WindowModal("openmw_edit_note.layout")
        , mGroupShare(false)
    {
        getWidget(mOkButton, "OkButton");
        getWidget(mCancelButton, "CancelButton");
        getWidget(mDeleteButton, "DeleteButton");
        getWidget(mTextEdit, "TextEdit");
        getWidget(mTypeSelect, "MarkerType");
        getWidget(mColorSelect, "MarkerColor");
        getWidget(mGroupButton, "GroupShareButton");

        for (const char* value : {"?", "!", "A", "B", "C"})
            mTypeSelect->addItem(value);
        for (const char* value : {"yellow", "red", "green", "blue", "orange", "purple", "white"})
            mColorSelect->addItem(MyGUI::LanguageManager::getInstance().replaceTags(
                std::string("#{arenamp=map.marker.color.") + value + "}"));
        mTypeSelect->setIndexSelected(0);
        mColorSelect->setIndexSelected(0);
        refreshGroupCaption();

        mCancelButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EditNoteDialog::onCancelButtonClicked);
        mOkButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EditNoteDialog::onOkButtonClicked);
        mDeleteButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EditNoteDialog::onDeleteButtonClicked);
        mGroupButton->eventMouseButtonClick += MyGUI::newDelegate(this, &EditNoteDialog::onGroupButtonClicked);
    }

    void EditNoteDialog::showDeleteButton(bool show)
    {
        mDeleteButton->setVisible(show);
    }

    bool EditNoteDialog::getDeleteButtonShown()
    {
        return mDeleteButton->getVisible();
    }

    void EditNoteDialog::setText(const std::string &text)
    {
        mTextEdit->setCaption(MyGUI::TextIterator::toTagsString(text));
    }

    std::string EditNoteDialog::getText()
    {
        return MyGUI::TextIterator::getOnlyText(mTextEdit->getCaption());
    }

    void EditNoteDialog::setMarkerStyle(const std::string& kind, const std::string& color, bool groupShare)
    {
        const std::vector<std::string> kinds={"?","!","A","B","C"};
        const std::vector<std::string> colors={"yellow","red","green","blue","orange","purple","white"};
        auto ki=std::find(kinds.begin(),kinds.end(),kind);
        auto ci=std::find(colors.begin(),colors.end(),color);
        mTypeSelect->setIndexSelected(ki==kinds.end()?0:static_cast<std::size_t>(std::distance(kinds.begin(),ki)));
        mColorSelect->setIndexSelected(ci==colors.end()?0:static_cast<std::size_t>(std::distance(colors.begin(),ci)));
        mGroupShare=groupShare;
        refreshGroupCaption();
    }

    std::string EditNoteDialog::getMarkerKind() const
    {
        const std::vector<std::string> v={"?","!","A","B","C"};
        const std::size_t i=mTypeSelect->getIndexSelected();
        return i<v.size()?v[i]:"?";
    }

    std::string EditNoteDialog::getMarkerColor() const
    {
        const std::vector<std::string> v={"yellow","red","green","blue","orange","purple","white"};
        const std::size_t i=mColorSelect->getIndexSelected();
        return i<v.size()?v[i]:"yellow";
    }

    bool EditNoteDialog::getGroupShare() const { return mGroupShare; }

    void EditNoteDialog::refreshGroupCaption()
    {
        mGroupButton->setCaption(MyGUI::LanguageManager::getInstance().replaceTags(
            mGroupShare ? "#{arenamp=map.marker.group_on}" : "#{arenamp=map.marker.group_off}"));
    }

    void EditNoteDialog::onGroupButtonClicked(MyGUI::Widget*)
    {
        mGroupShare=!mGroupShare;
        refreshGroupCaption();
    }

    void EditNoteDialog::onOpen()
    {
        WindowModal::onOpen();
        center();
        MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mTextEdit);
    }

    void EditNoteDialog::onCancelButtonClicked(MyGUI::Widget *sender)
    {
        setVisible(false);
    }

    void EditNoteDialog::onOkButtonClicked(MyGUI::Widget *sender)
    {
        eventOkClicked();
    }

    void EditNoteDialog::onDeleteButtonClicked(MyGUI::Widget *sender)
    {
        eventDeleteClicked();
    }

    bool LocalMapBase::MarkerUserData::isPositionExplored() const
    {
        if (!mLocalMapRender)
            return true;
        return mLocalMapRender->isPositionExplored(nX, nY, cellX, cellY);
    }

}
