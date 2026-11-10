#ifndef GAME_SOUND_VOLUMESETTINGS_H
#define GAME_SOUND_VOLUMESETTINGS_H

#include "type.hpp"

namespace MWSound
{
    class VolumeSettings
    {
        public:
            VolumeSettings();

            float getVolumeFromType(Type type) const;
            float getVoiceChatDuckingAmount() const { return mVoiceChatVolume > 0.001f ? mVoiceChatDuckingAmount : 0.f; }
            void setRuntimeDuckingFactor(float factor);

            void update();

        private:
            float mMasterVolume;
            float mSFXVolume;
            float mMusicVolume;
            float mVoiceVolume;
            float mVoiceChatVolume;
            float mFootstepsVolume;
            float mVoiceChatDuckingAmount;
            float mRuntimeDuckingFactor = 1.f;
    };
}

#endif
