#ifndef OPENMW_MWMP_VOICECHAT_HPP
#define OPENMW_MWMP_VOICECHAT_HPP

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

#include <RakNetTypes.h>

namespace mwmp
{
    struct VoiceFrame;
    class VoiceStreamDecoder;

    class VoiceChat
    {
    public:
        VoiceChat();
        ~VoiceChat();

        void configure(bool enabled, const std::string& pushToTalkKey, const std::string& captureDevice,
            float rangeMeters, float fullVolumeMeters, float sourceVolume, float micGain, float playbackGain,
            bool toggleMode);
        void init();
        void update(float dt);
        void receive(const RakNet::RakNetGUID& speakerGuid, const VoiceFrame& frame);
        void removeSpeaker(const RakNet::RakNetGUID& speakerGuid);
        void shutdown();

        bool isAvailable() const { return mAvailable; }
        bool isTransmitting() const { return mTransmitting; }
        bool isSpeakerActive(const RakNet::RakNetGUID& guid) const;

        /// U035: true when the key latches the microphone open (radio mode)
        /// instead of having to be held down.
        bool isToggleMode() const { return mToggleMode; }
        /// U035: in radio mode, whether the microphone is currently latched on.
        bool isMicOpen() const;
        /// U035: flips the radio-mode latch. Exposed so a UI button or an
        /// Android control can toggle it without synthesising a key press.
        void toggleMicrophone();

        /// Z005: party voice.
        ///
        /// V remains world voice: it is heard by whoever is standing close
        /// enough, exactly as before. Group voice is a separate latch, opened
        /// from the button in the group window, and it is carried by the server
        /// to every member of the group regardless of distance or cell. While
        /// the group latch is on it takes the microphone, because talking to
        /// your party and shouting across the room at the same time is not a
        /// thing a single microphone can do.
        ///
        /// Z007: the latch is a remembered preference, not a session flag. It
        /// survives leaving a group, opening a text field and restarting the
        /// game (settings.cfg, [Chat] "group voice"), because a player who
        /// talks to his party talks to his party every evening. Membership and
        /// keyboard ownership only decide whether it TRANSMITS right now.
        void setGroupTalk(bool state);
        bool isGroupTalk() const { return mGroupTalk; }
        /// True when the local player is currently in a group, which is what
        /// decides whether the latch actually opens the microphone.
        void setInGroup(bool state);
        bool isInGroup() const { return mInGroup; }

    public:
        struct Impl;

    private:
        void notifyUnavailable(const std::string& reason);
        void publishSpeakerHud();

        std::unique_ptr<Impl> mImpl;
        bool mEnabled = true;
        bool mAvailable = false;
        bool mTransmitting = false;
        bool mTransmitGroup = false;
        std::string mPushToTalkKey = "V";
        std::string mCaptureDevice; // empty = SDL/system default recording device
        float mRangeMeters = 30.f;
        // U035: OpenAL uses the inverse distance model, so past the reference
        // distance gain is ref/dist. The old 2 m reference made a speaker 10 m
        // away five times quieter than one standing next to you; conversation
        // range belongs inside the full-volume radius.
        float mFullVolumeMeters = 12.f;
        // Source gain handed to OpenAL. ArenaMP U035g has a dedicated [Sound]
        // voice-chat volume slider, separate from NPC/dialogue voice volume.
        float mSourceVolume = 2.f;
        float mMicGain = 1.f;        // digital pre-encode boost, soft-limited
        float mPlaybackGain = 1.f;   // digital post-decode boost, soft-limited
        // U035 radio mode: the push-to-talk key latches instead of being held.
        bool mToggleMode = true;
        // Z005 party voice; Z007: mGroupTalk is the remembered latch, and
        // mGroupActive is what the current frame is actually doing with it.
        bool mGroupTalk = false;
        bool mInGroup = false;
        bool mGroupActive = false;
        std::uint16_t mSequence = 0;
        int mAdpcmIndex = 0;
    };
}

#endif
