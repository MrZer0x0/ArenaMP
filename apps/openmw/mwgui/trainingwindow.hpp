#ifndef MWGUI_TRAININGWINDOW_H
#define MWGUI_TRAININGWINDOW_H

#include <string>

#include "windowbase.hpp"
#include "referenceinterface.hpp"
#include "timeadvancer.hpp"
#include "waitdialog.hpp"

namespace MWMechanics
{
    class NpcStats;
}

namespace MWGui
{

    class TrainingWindow : public WindowBase, public ReferenceInterface
    {
    public:
        TrainingWindow();

        void onOpen() override;

        bool exit() override;

        void setPtr(const MWWorld::Ptr& actor) override;

        void onFrame(float dt) override;

        // Y054: hidden server control updates the runtime-only per-NPC training cap.
        static void setServerTrainingState(const std::string& trainerKey, int count, int limit);

        WindowBase* getProgressBar() { return &mProgressBar; }

        void clear() override { resetReference(); }

    protected:
        void onReferenceUnavailable() override;

        void onCancelButtonClicked (MyGUI::Widget* sender);
        void onTrainingSelected(MyGUI::Widget* sender);

        void onTrainingProgressChanged(int cur, int total);
        void onTrainingFinished();

        // Training eligibility always uses permanent/base skill. Temporary
        // Drain/Fortify effects must not change trainer capability.
        float getSkillForTraining(const MWMechanics::NpcStats& stats, int skillId) const;
        void sendTrainingControl(const std::string& action, int skillId = -1);
        void refreshTrainingCounter();
        std::string makeTrainerKey(const MWWorld::Ptr& actor) const;

        MyGUI::Widget* mTrainingOptions;
        MyGUI::Button* mCancelButton;
        MyGUI::TextBox* mPlayerGold;
        MyGUI::TextBox* mTrainingCounter;
        std::string mTrainerKey;
        unsigned int mTrainingStateRevision;

        WaitDialogProgressBar mProgressBar;
        TimeAdvancer mTimeAdvancer;
    };

}

#endif
