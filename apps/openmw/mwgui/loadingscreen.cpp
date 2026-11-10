#include "loadingscreen.hpp"

#include <algorithm>
#include <array>
#include <MyGUI_Gui.h>
#include <components/debug/debuglog.hpp>
#include <components/settings/settings.hpp>
#include <components/vfs/manager.hpp>
#include "backgroundimage.hpp"
#include "../mwbase/statemanager.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <osg/Texture2D>
#include <osg/Version>
#include <components/myguiplatform/myguitexture.hpp>
#include <osgViewer/Viewer>
#include <MyGUI_RenderManager.h>
#include <MyGUI_ScrollBar.h>
#include <MyGUI_TextBox.h>
#include <components/misc/rng.hpp>
#include <components/resource/resourcesystem.hpp>
#include "../mwbase/environment.hpp"
#include "../mwbase/windowmanager.hpp"
#include "../mwbase/world.hpp"
#include "../mwbase/inputmanager.hpp"

namespace MWGui
{
    class LoadingBoundCallback : public osg::Node::ComputeBoundingSphereCallback
    {
    public:
        osg::BoundingSphere computeBound(const osg::Node&) const override { return osg::BoundingSphere(); }
    };

    class CopyFramebufferToTextureCallback : public osg::Camera::DrawCallback
    {
    public:
        explicit CopyFramebufferToTextureCallback(osg::Texture2D* texture)
            : mTexture(texture) {}

        void operator () (osg::RenderInfo& renderInfo) const override
        {
            std::lock_guard<std::mutex> lock(mMutex);
            if (mDone) return;
            const osg::Viewport* viewport = renderInfo.getCurrentCamera()->getViewport();
            if (viewport && viewport->width() > 0 && viewport->height() > 0)
                mTexture->copyTexImage2D(*renderInfo.getState(),
                    static_cast<int>(viewport->x()), static_cast<int>(viewport->y()),
                    static_cast<int>(viewport->width()), static_cast<int>(viewport->height()));
            mDone = true;
            mCondition.notify_one();
        }

        void reset()
        {
            std::lock_guard<std::mutex> lock(mMutex);
            mDone = false;
        }

        bool waitTillDone()
        {
            std::unique_lock<std::mutex> lock(mMutex);
            // A lost graphics context must not leave the main thread
            // waiting forever for a draw callback that cannot run.
            return mCondition.wait_for(lock, std::chrono::seconds(2), [this] { return mDone; });
        }

    private:
        mutable std::mutex mMutex;
        mutable std::condition_variable mCondition;
        mutable bool mDone = false;
        osg::ref_ptr<osg::Texture2D> mTexture;
    };

    LoadingScreen::LoadingScreen(Resource::ResourceSystem* resourceSystem, osgViewer::Viewer* viewer)
        : WindowBase("openmw_loading_screen.layout")
        , mResourceSystem(resourceSystem), mViewer(viewer)
    {
        getWidget(mBlackBackground, "BlackBackground");
        mBackgroundImage = MyGUI::Gui::getInstance().createWidgetReal<BackgroundImage>("ImageBox", 0, 0, 1, 1,
            MyGUI::Align::Stretch, "Menu");
        mBackgroundImage->setVisible(false);
        mSceneImage = MyGUI::Gui::getInstance().createWidgetReal<BackgroundImage>("ImageBox", 0, 0, 1, 1,
            MyGUI::Align::Stretch, "Scene");
        mSceneImage->setVisible(false);
        findSplashScreens();
        getWidget(mLoadingText, "LoadingText");
        getWidget(mCenterLoadingText, "CenterLoadingText");
        mCenterLoadingText->setFontHeight(20);
        getWidget(mTipText, "LoadingTip");
        getWidget(mProgressBar, "ProgressBar");
        getWidget(mLoadingBox, "LoadingBox");
        getWidget(mTipStrip, "LoadingTipStrip");
        getWidget(mStatusBackground, "LoadingStatusBackground");
        mLoadingText->setFontHeight(20);
        mTipText->setFontHeight(18);
        mProgressBar->setScrollViewPage(1);
        updateLayout();
    }


    LoadingScreen::~LoadingScreen()
    {
        MyGUI::Gui::getInstance().destroyWidget(mSceneImage);
        MyGUI::Gui::getInstance().destroyWidget(mBackgroundImage);
    }

    void LoadingScreen::findSplashScreens()
    {
        const std::map<std::string, VFS::File*>& index = mResourceSystem->getVFS()->getIndex();
        std::string pattern = "Splash/";
        mResourceSystem->getVFS()->normalizeFilename(pattern);

        /* priority given to the left */
        const std::array<std::string, 7> supported_extensions {{".tga", ".dds", ".ktx", ".png", ".bmp", ".jpeg", ".jpg"}};

        auto found = index.lower_bound(pattern);
        while (found != index.end())
        {
            const std::string& name = found->first;
            if (name.size() >= pattern.size() && name.substr(0, pattern.size()) == pattern)
            {
                size_t pos = name.find_last_of('.');
                if (pos != std::string::npos)
                {
                    for(auto const& extension: supported_extensions)
                    {
                        if (name.compare(pos, name.size() - pos, extension) == 0)
                        {
                            mSplashScreens.push_back(found->first);
                            break;  /* based on priority */
                        }
                    }
                }
            }
            else
                break;
            ++found;
        }
        if (mSplashScreens.empty())
            Log(Debug::Warning) << "Warning: no splash screens found!";
    }

    void LoadingScreen::changeWallpaper()
    {
        if (mSplashScreens.empty()) return;
        const std::string& splash = mSplashScreens[Misc::Rng::rollDice(mSplashScreens.size())];
        mBackgroundImage->setBackgroundImage(splash, true,
            Settings::Manager::getBool("stretch menu background", "GUI"));
    }


    void LoadingScreen::setLabel(const std::string& label, bool /*important*/)
    {
        // ArenaMP Y090: remember what kind of cell the active load is targeting.
        // sLoadingMessage2 is used for interior loads, sLoadingMessage3 for exterior/grid loads.
        if (label == "#{sLoadingMessage2}")
            mDestinationKind = DestinationKind::Interior;
        else if (label == "#{sLoadingMessage3}")
            mDestinationKind = DestinationKind::Exterior;

        mLoadingText->setCaptionWithReplacing(label);
        mCenterLoadingText->setCaptionWithReplacing(label);
        updateLayout();
    }

    void LoadingScreen::setVisible(bool visible)
    {
        WindowBase::setVisible(visible);
        mBackgroundImage->setVisible(visible && mShowWallpaper && !mSplashScreens.empty());
        // For exterior -> exterior travel keep the old scene visible even if a framebuffer
        // snapshot is unavailable. Black is only a fallback for splash-style loads.
        mBlackBackground->setVisible(visible && mShowWallpaper && mSplashScreens.empty());
        mSceneImage->setVisible(visible && !mShowWallpaper && mSceneSnapshotReady);
    }

    double LoadingScreen::getTargetFrameRate() const
    {
        const double limit = MWBase::Environment::get().getFrameRateLimit();
        return limit > 0 ? std::min(limit, 30.0) : 30.0;
    }

    void LoadingScreen::loadingOn(bool visible)
    {
        if (mNestedLoadingCount++ != 0)
            return;

        // ArenaMP Y090 loading policy:
        //  * game startup -> splash;
        //  * interior <-> exterior -> splash;
        //  * exterior -> exterior (and same-type cell loads) -> frozen current frame, no darkening.
        bool crossCellTypeBoundary = false;

        // Do not ask World for a current cell during the first/startup load: there may not
        // be one yet. Subsequent cell loads always have the old cell available here.
        if (mHasCompletedOuterLoad && mDestinationKind != DestinationKind::Unknown)
        {
            try
            {
                const bool currentExterior = MWBase::Environment::get().getWorld()->isCellExterior();
                const bool targetExterior = mDestinationKind == DestinationKind::Exterior;
                crossCellTypeBoundary = currentExterior != targetExterior;
            }
            catch (...)
            {
                // Non-cell loads fall back to the unobscured/freeze-frame presentation.
            }
        }

#ifdef __ANDROID__
        // ArenaMP Y091 Android policy: use splash wallpaper for every cell transition.
        // Android framebuffer capture can briefly expose the cleared backbuffer as a white
        // frame, so never use the PC exterior freeze-frame path for interior/exterior loads.
        mShowWallpaper = !mHasCompletedOuterLoad || mDestinationKind != DestinationKind::Unknown;
#else
        // Desktop keeps the lighter freeze-frame presentation for exterior -> exterior travel.
        mShowWallpaper = !mHasCompletedOuterLoad || crossCellTypeBoundary;
#endif
        mSceneSnapshotReady = false;

        if (mShowWallpaper)
        {
            changeWallpaper();
            changeTip();
        }
        else
        {
            // Capture BEFORE the old exterior cells are unloaded. If capture is not available,
            // the transparent loading UI simply leaves the last rendered scene underneath.
            setupCopyFramebufferToTextureCallback();
        }

        mTipStrip->setVisible(mShowWallpaper);
        mStatusBackground->setVisible(mShowWallpaper);
        mProgressBar->setVisible(mShowWallpaper);
        updateLayout();
        mLoadingBox->setVisible(visible);
        if (osg::Node* scene = mViewer->getSceneData())
        {
            mPreviousBoundCallback = scene->getComputeBoundingSphereCallback();
            scene->setComputeBoundingSphereCallback(new LoadingBoundCallback);
        }
        if (const osgUtil::IncrementalCompileOperation* ico = mViewer->getIncrementalCompileOperation())
        {
            mOldIcoMin = ico->getMinimumTimeAvailableForGLCompileAndDeletePerFrame();
            mOldIcoMax = ico->getMaximumNumOfObjectsToCompilePerFrame();
        }
        MWBase::Environment::get().getWindowManager()->pushGuiMode(
            mShowWallpaper ? GM_LoadingWallpaper : GM_Loading);
        setVisible(true);
        mLastRenderTime = -1000.0;
        draw();
    }

    void LoadingScreen::loadingOff()
    {
        if (mNestedLoadingCount <= 0 || --mNestedLoadingCount != 0)
            return;
        setVisible(false);
        mLoadingBox->setVisible(true);
        if (osg::Node* scene = mViewer->getSceneData())
        {
            scene->setComputeBoundingSphereCallback(mPreviousBoundCallback);
            scene->dirtyBound();
        }
        mPreviousBoundCallback = nullptr;
        if (osgUtil::IncrementalCompileOperation* ico = mViewer->getIncrementalCompileOperation())
        {
            ico->setMinimumTimeAvailableForGLCompileAndDeletePerFrame(mOldIcoMin);
            ico->setMaximumNumOfObjectsToCompilePerFrame(mOldIcoMax);
        }
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_Loading);
        MWBase::Environment::get().getWindowManager()->removeGuiMode(GM_LoadingWallpaper);
        mHasCompletedOuterLoad = true;
        mDestinationKind = DestinationKind::Unknown;
    }

    void LoadingScreen::updateLayout()
    {
        const MyGUI::IntSize view = MyGUI::RenderManager::getInstance().getViewSize();
        if (mMainWidget->getSize() != view) mMainWidget->setSize(view);
        const int margin = std::min(32, view.width / 20);
        mLoadingBox->setCoord(0, 0, view.width, view.height);
        mBlackBackground->setCoord(0, 0, view.width, view.height);
        mBackgroundImage->setCoord(MyGUI::IntCoord(0, 0, view.width, view.height));
        mSceneImage->setCoord(MyGUI::IntCoord(0, 0, view.width, view.height));
        // Measure wrapped UTF-8 hints after setting their width. Long Russian
        // entries need more than two lines on narrow/mobile screens.
        mTipText->setCoord(margin, 6, std::max(1, view.width - 2 * margin), 52);
        const int tipHeight = std::max(52, mTipText->getTextSize().height + 8);
        mTipStrip->setCoord(0, 0, view.width, tipHeight + 12);
        mTipText->setCoord(margin, 6, std::max(1, view.width - 2 * margin), tipHeight);

        // Y092: only desktop exterior streaming uses the crosshair label.
        bool centerLabel = !mShowWallpaper && mDestinationKind == DestinationKind::Exterior;
#ifdef __ANDROID__
        centerLabel = false;
#endif
        mCenterLoadingText->setCoord(margin, (view.height - 44) / 2,
            std::max(1, view.width - 2 * margin), 44);
        mCenterLoadingText->setVisible(centerLabel);
        mLoadingText->setVisible(!centerLabel);
        // LoadingText and ProgressBar are relative to the fixed-height bottom strip.
        mLoadingText->setCoord(margin, 4, std::max(1, view.width - 2 * margin), 44);
        mProgressBar->setCoord(0, 53, view.width, 5);
        mProgressBar->setTrackSize(static_cast<int>(static_cast<double>(mProgress)
            / mProgressRange * mProgressBar->getLineSize()));
    }

    void LoadingScreen::changeTip()
    {
        constexpr int count = 120;
        // Uniformly choose a different entry without an unbounded retry loop.
        const int offset = Misc::Rng::rollDice(mTipIndex < 0 ? count : count - 1);
        mTipIndex = mTipIndex < 0 ? offset : (mTipIndex + 1 + offset) % count;
        mTipText->setCaptionWithReplacing("#{arenamp=loading.did_you_know}\n#{arenamp=loading.tip."
            + std::to_string(mTipIndex + 1) + "}");
    }

    void LoadingScreen::setProgressRange(size_t range)
    {
        mProgressRange = std::max(size_t(1), range);
        // The scrollbar is only a drawing surface, never an input control.
        mProgressBar->setScrollRange(2);
        mProgressBar->setScrollPosition(0);
        mProgressBar->setTrackSize(0);
        mProgress = 0;
    }

    void LoadingScreen::setProgress(size_t value)
    {
        mProgress = std::min(value, mProgressRange);
        // Keep pumping events during long stages, with a capped render rate.
        draw();
    }

    void LoadingScreen::increaseProgress(size_t increase)
    {
        setProgress(mProgress + std::min(increase, mProgressRange - mProgress));
    }

    bool LoadingScreen::needToDrawLoadingScreen()
    {
        return mTimer.time_m() >= mLastRenderTime + 1000.0 / getTargetFrameRate();
    }

    void LoadingScreen::setupCopyFramebufferToTextureCallback()
    {
        if (!mViewer->isRealized() || mViewer->done() || !mViewer->getCamera()->getGraphicsContext()) return;
        const osg::Viewport* viewport = mViewer->getCamera()->getViewport();
        if (!viewport || viewport->width() <= 0 || viewport->height() <= 0) return;
        // GPU copy at final draw, before swap; no per-frame capture cost in gameplay.

        if (!mTexture)
        {
            mTexture = new osg::Texture2D;
            mTexture->setInternalFormat(GL_RGB);
            mTexture->setSourceFormat(GL_RGB);
            mTexture->setSourceType(GL_UNSIGNED_BYTE);
            mTexture->setFilter(osg::Texture::MIN_FILTER, osg::Texture::LINEAR);
            mTexture->setFilter(osg::Texture::MAG_FILTER, osg::Texture::LINEAR);
            mTexture->setWrap(osg::Texture::WRAP_S, osg::Texture::CLAMP_TO_EDGE);
            mTexture->setWrap(osg::Texture::WRAP_T, osg::Texture::CLAMP_TO_EDGE);
            mTexture->setResizeNonPowerOfTwoHint(false);
        }

        if (!mCopyFramebufferToTextureCallback)
        {
            mCopyFramebufferToTextureCallback = new CopyFramebufferToTextureCallback(mTexture);
        }

        mCopyFramebufferToTextureCallback->reset();
        osg::Camera* camera = mViewer->getCamera();
#if OSG_VERSION_GREATER_OR_EQUAL(3, 5, 10)
        camera->addFinalDrawCallback(mCopyFramebufferToTextureCallback);
#else
        osg::ref_ptr<osg::Camera::DrawCallback> previous = camera->getFinalDrawCallback();
        camera->setFinalDrawCallback(mCopyFramebufferToTextureCallback);
#endif
        // Cull/draw the existing scene only: do not recursively run simulation,
        // input, scripts or an update traversal from within a cell load.
        mViewer->renderingTraversals();
        const bool captured = mCopyFramebufferToTextureCallback->waitTillDone();
#if OSG_VERSION_GREATER_OR_EQUAL(3, 5, 10)
        camera->removeFinalDrawCallback(mCopyFramebufferToTextureCallback);
#else
        camera->setFinalDrawCallback(previous);
#endif
        if (!captured) return;
        mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());
        // OSGTexture caches width/height in its constructor. Construct it only
        // AFTER the GPU copy allocated the texture (also handles screen rotation).
        mSceneImage->setRenderItemTexture(nullptr);
        mGuiTexture.reset(new osgMyGUI::OSGTexture(mTexture));
        mSceneSnapshotReady = true;
        mSceneImage->setRenderItemTexture(mGuiTexture.get());
        mSceneImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f, 0.f, 1.f, 1.f));
        mSceneImage->setAlpha(1.f);
    }

    void LoadingScreen::draw()
    {
        if (mNestedLoadingCount == 0 || !needToDrawLoadingScreen()
            || !mViewer->isRealized() || mViewer->done())
            return;

        updateLayout();
        MWBase::Environment::get().getInputManager()->update(0, true, true);

        mResourceSystem->reportStats(mViewer->getFrameStamp()->getFrameNumber(), mViewer->getViewerStats());
        if (osgUtil::IncrementalCompileOperation* ico = mViewer->getIncrementalCompileOperation())
        {
            ico->setMinimumTimeAvailableForGLCompileAndDeletePerFrame(1.f/getTargetFrameRate());
            ico->setMaximumNumOfObjectsToCompilePerFrame(1000);
        }

        // at the time this function is called we are in the middle of a frame,
        // so out of order calls are necessary to get a correct frameNumber for the next frame.
        // refer to the advance() and frame() order in Engine::go()
        mViewer->eventTraversal();
        mViewer->updateTraversal();
        mViewer->renderingTraversals();
        mViewer->advance(mViewer->getFrameStamp()->getSimulationTime());

        mLastRenderTime = mTimer.time_m();
    }

}
