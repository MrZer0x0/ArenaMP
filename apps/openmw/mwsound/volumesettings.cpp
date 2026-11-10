#include "volumesettings.hpp"

#include <components/settings/settings.hpp>

#include <algorithm>

namespace MWSound
{
    namespace
    {
        float clamp(float value)
        {
            return std::max(0.0f, std::min(1.0f, value));
        }
    }

    VolumeSettings::VolumeSettings()
        : mMasterVolume(clamp(Settings::Manager::getFloat("master volume", "Sound"))),
          mSFXVolume(clamp(Settings::Manager::getFloat("sfx volume", "Sound"))),
          mMusicVolume(clamp(Settings::Manager::getFloat("music volume", "Sound"))),
          mVoiceVolume(clamp(Settings::Manager::getFloat("voice volume", "Sound"))),
          mVoiceChatVolume(clamp(Settings::Manager::getFloat("voice chat volume", "Sound"))),
          mFootstepsVolume(clamp(Settings::Manager::getFloat("footsteps volume", "Sound"))),
          mVoiceChatDuckingAmount(clamp(Settings::Manager::getFloat("voice chat ducking", "Sound")))
    {
    }

    float VolumeSettings::getVolumeFromType(Type type) const
    {
        float volume = mMasterVolume;

        switch(type)
        {
            case Type::Sfx:
                volume *= mSFXVolume;
                break;
            case Type::Voice:
                volume *= mVoiceVolume;
                break;
            case Type::VoiceChat:
                volume *= mVoiceChatVolume;
                break;
            case Type::Foot:
                volume *= mFootstepsVolume;
                break;
            case Type::Music:
                volume *= mMusicVolume;
                break;
            case Type::Movie:
            case Type::Mask:
                break;
        }

        // Smart voice-chat ducking only affects game audio. Realtime player
        // voice must stay clear and movie playback is left untouched.
        if (type != Type::VoiceChat && type != Type::Movie)
            volume *= mRuntimeDuckingFactor;

        return volume;
    }

    void VolumeSettings::setRuntimeDuckingFactor(float factor)
    {
        mRuntimeDuckingFactor = clamp(factor);
    }

    void VolumeSettings::update()
    {
        const float runtimeDuckingFactor = mRuntimeDuckingFactor;
        *this = VolumeSettings();
        mRuntimeDuckingFactor = runtimeDuckingFactor;
    }
}
