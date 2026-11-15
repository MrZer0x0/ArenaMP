#ifndef COMPONENTS_LOADINGLISTENER_PROGRESS_H
#define COMPONENTS_LOADINGLISTENER_PROGRESS_H

#include "loadinglistener.hpp"
#include <algorithm>
#include <cstddef>

namespace Loading
{
    // Weighted work progress, not a time estimate. Local range changes never
    // rewind the visible bar; only the owner completes the outer operation.
    class Progress
    {
    public:
        void reset(bool startup = false)
        {
            mStartup = startup;
            mRange = 1;
            mLocal = 0;
            mBegin = mShown = 0.;
            mEnd = .98;
        }
        void stage(Stage stage, std::size_t index = 0, std::size_t count = 1)
        {
            count = std::max(std::size_t(1), count);
            index = std::min(index, count - 1);
            switch (stage)
            {
                case Stage::Content:
                    mBegin = .55 * static_cast<double>(index) / count;
                    mEnd = .55 * static_cast<double>(index + 1) / count;
                    break;
                case Stage::World: mBegin = .55; mEnd = .65; break;
                case Stage::Terrain: mBegin = mStartup ? .65 : 0.; mEnd = mStartup ? .80 : .35; break;
                case Stage::Objects: mBegin = mStartup ? .80 : .35; mEnd = .98; break;
            }
            mLocal = 0;
            mRange = 1;
            mShown = std::max(mShown, mBegin);
        }
        void range(std::size_t range) { mRange = std::max(std::size_t(1), range); mLocal = 0; }
        void set(std::size_t value)
        {
            mLocal = std::min(value, mRange);
            mShown = std::max(mShown, mBegin + (mEnd - mBegin) * static_cast<double>(mLocal) / mRange);
        }
        void increase(std::size_t value) { set(mLocal + std::min(value, mRange - mLocal)); }
        void complete() { mShown = 1.; }
        double fraction() const { return mShown; }
    private:
        bool mStartup = false;
        std::size_t mRange = 1, mLocal = 0;
        double mBegin = 0., mEnd = .98, mShown = 0.;
    };
}
#endif
