#ifndef GRAPHICSPAGE_H
#define GRAPHICSPAGE_H

#include "ui_graphicspage.h"

#include <components/settings/settings.hpp>

#include "sdlinit.hpp"

namespace Config { class LauncherSettings; }
namespace Files { struct ConfigurationManager; }

namespace Launcher
{
    class GraphicsSettings;

    class GraphicsPage : public QWidget, private Ui::GraphicsPage
    {
        Q_OBJECT

    public:
        explicit GraphicsPage(Config::LauncherSettings& launcherSettings, QWidget *parent = nullptr);

        bool saveSettings();
        bool loadSettings();

        // U023: re-apply the currently selected Auto/manual graphics preset to
        // the active build settings.cfg (used after switching build folders).
        void applyCurrentQualityPreset();

        // U020: shared with the Play page status column.
        QString hardwareGpuName() const;
        QString hardwareGpuDetail() const;
        int hardwareLogicalThreads() const;

    signals:
        void hardwareInfoChanged();

    public slots:
        void screenChanged(int screen);

    private slots:
        void slotFullScreenChanged(int state);
        void slotStandardToggled(bool checked);
        void slotFramerateLimitToggled(bool checked);
        void slotShadowDistLimitToggled(bool checked);
        void slotQualityPresetChanged(int index);
        void slotTerrainDetailChanged(int index);
        void slotPbrQualityChanged(int index);
        void slotApplyQualityPreset();
        void slotDetectHardware();
        void slotOcclusionSettingChanged();

    private:
        struct HardwareInfo
        {
            QString vendor;
            QString renderer;
            quint64 dedicatedVramMb = 0;
            int logicalCores = 1;
            int displayMegapixelsTimes100 = 0;
            bool softwareRenderer = false;
            bool integrated = false;

            QString signature() const;
        };

        QVector<QStringList> mResolutionsPerScreen;
        Config::LauncherSettings& mLauncherSettings;
        HardwareInfo mHardwareInfo;
        int mRecommendedQuality;
        bool mInitializingQuality;
        bool mInitializingOcclusion;
        Settings::CategorySettingValueMap mGraphicsBaselineSettings;
        bool mGraphicsBaselineValid;

        static QStringList getAvailableResolutions(int screen);
        static QRect getMaximumResolution();

        bool setupSDL();
        void syncGraphicsControls();
        void updateShadowControls();
        void initializeQualityPage();
        // X041: streaming/occlusion budget, moved here from the removed
        // Advanced -> Arena Settings tab.
        void loadOcclusionSettings();
        void applyOcclusionSettingsToManager();
        HardwareInfo detectHardware() const;
        int recommendQuality(const HardwareInfo& info) const;
        void applyQualityLevel(int level);
        void applyRequiredShaderBaseline();
        void applyTerrainDetail(int index);
        void applyPbrQuality(int index);
        int terrainDetailIndexFromSettings() const;
        int pbrQualityIndexFromSettings() const;
        void applyVendorOptimizations(int level);
        void updateQualityDescription();
        void updateHardwareLabels();
        void syncQualityPresetButtons();
        bool reloadUserSettingsFromDisk();
        bool saveUserSettingsToDisk();
        void storeLauncherValue(const QString& key, const QString& value);
        QString qualityName(int level) const;
        QString qualityDescription(int level) const;
    };
}
#endif
