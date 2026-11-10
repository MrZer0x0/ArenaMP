#ifndef ARENAMP_VOICE_UI_STATE_HPP
#define ARENAMP_VOICE_UI_STATE_HPP

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <algorithm>

namespace mwmp
{
    // The Android UI reads only this snapshot. It never touches VoiceChat,
    // LocalPlayer, SDL capture or the player list from the Java thread.
    class VoiceUiState
    {
    public:
        enum { Enabled = 1, Microphone = 2, LoggedIn = 4, Transmitting = 8,
               Speaking = 16, Ready = 32, Muted = 64 };
        static long long now()
        {
            return std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()).count();
        }
        void foreground(bool active)
        {
            mForeground.store(active);
            if (!active) { press(false); mUpdated.store(0); }
        }
        bool foreground() const { return mForeground.load(); }
        void setMuted(bool muted)
        {
            mMuted.store(muted);
            if (muted) press(false);
        }
        bool muted() const { return mMuted.load(); }
        void press(bool held)
        {
            // A short renewable lease also releases capture if Android stops
            // delivering touch/lifecycle events. Polling renews only a held touch.
            mTouchUntil.store(held && foreground() ? now() + 500 : 0);
        }
        bool pressed() const { return foreground() && now() < mTouchUntil.load(); }
        void requestToggle()
        {
            if (foreground()) mToggleRequests.fetch_add(1, std::memory_order_relaxed);
        }
        bool takeToggleRequest()
        {
            // Preserve button semantics even if two very quick taps arrive
            // before the next game frame: only odd parity changes the latch.
            return (mToggleRequests.exchange(0, std::memory_order_acq_rel) & 1u) != 0u;
        }
        void publish(int flags, float level, const std::string& speakers)
        {
            mState.store(flags | (static_cast<int>(std::clamp(level, 0.f, 1.f) * 255.f) << 8));
            { std::lock_guard<std::mutex> guard(mMutex); mSpeakers = speakers; }
            mUpdated.store(foreground() ? now() : 0);
        }
        int state() const { return fresh() ? (mState.load() | (muted() ? Muted : 0)) : 0; }
        std::string speakers() const
        {
            if (!fresh()) return {};
            std::lock_guard<std::mutex> guard(mMutex);
            return mSpeakers;
        }
        void reset() { press(false); mToggleRequests.store(0); publish(0, 0.f, {}); }
    private:
        bool fresh() const { return foreground() && now() - mUpdated.load() < 750; }
        std::atomic<bool> mForeground{false}, mMuted{false};
        std::atomic<long long> mTouchUntil{0}, mUpdated{0};
        std::atomic<unsigned> mToggleRequests{0};
        std::atomic<int> mState{0};
        mutable std::mutex mMutex;
        std::string mSpeakers;
    };
}
#endif
