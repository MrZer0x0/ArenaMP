#ifndef MWGUI_LOADINGSCREEN_H
#define MWGUI_LOADINGSCREEN_H

#include <memory>
#include <vector>
#include <osg/Node>
#include <osg/Timer>
#include <osg/ref_ptr>
#include "windowbase.hpp"
#include <components/loadinglistener/loadinglistener.hpp>

namespace osgViewer { class Viewer; }
namespace Resource { class ResourceSystem; }
namespace osg { class Texture2D; }

namespace MWGui
{
    class BackgroundImage;
    class CopyFramebufferToTextureCallback;

    class LoadingScreen : public WindowBase, public Loading::Listener
    {
    public:
        LoadingScreen(Resource::ResourceSystem* resourceSystem, osgViewer::Viewer* viewer);
        ~LoadingScreen() override;
        void setLabel(const std::string& label, bool important) override;
        void loadingOn(bool visible = true) override;
        void loadingOff() override;
        void setProgressRange(size_t range) override;
        void setProgress(size_t value) override;
        void increaseProgress(size_t increase = 1) override;
        void setVisible(bool visible) override;
        double getTargetFrameRate() const;

    private:
        void findSplashScreens();
        void changeWallpaper();
        void setupCopyFramebufferToTextureCallback();
        BackgroundImage* mSceneImage;
        osg::ref_ptr<osg::Texture2D> mTexture;
        osg::ref_ptr<CopyFramebufferToTextureCallback> mCopyFramebufferToTextureCallback;
        std::unique_ptr<MyGUI::ITexture> mGuiTexture;
        BackgroundImage* mBackgroundImage;
        MyGUI::Widget* mBlackBackground;
        std::vector<std::string> mSplashScreens;
        enum class DestinationKind
        {
            Unknown,
            Interior,
            Exterior
        };

        bool mShowWallpaper = false;
        bool mSceneSnapshotReady = false;
        bool mHasCompletedOuterLoad = false;
        DestinationKind mDestinationKind = DestinationKind::Unknown;
        void updateLayout();
        void changeTip();
        bool needToDrawLoadingScreen();
        void draw();
        Resource::ResourceSystem* mResourceSystem;
        osg::ref_ptr<osgViewer::Viewer> mViewer;
        osg::ref_ptr<osg::Node::ComputeBoundingSphereCallback> mPreviousBoundCallback;
        osg::Timer mTimer;
        double mLastRenderTime = -1000.0;
        int mNestedLoadingCount = 0;
        size_t mProgress = 0;
        size_t mProgressRange = 1;
        float mOldIcoMin = 0.f;
        unsigned int mOldIcoMax = 0;
        int mTipIndex = -1;
        MyGUI::Widget* mLoadingBox;
        MyGUI::Widget* mTipStrip;
        MyGUI::Widget* mStatusBackground;
        MyGUI::TextBox* mLoadingText;
        MyGUI::TextBox* mCenterLoadingText;
        MyGUI::TextBox* mTipText;
        MyGUI::ScrollBar* mProgressBar;
    };
}
#endif
