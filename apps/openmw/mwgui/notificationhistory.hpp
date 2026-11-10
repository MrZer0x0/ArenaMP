#ifndef MWGUI_NOTIFICATIONHISTORY_H
#define MWGUI_NOTIFICATIONHISTORY_H

#include <algorithm>
#include <cctype>
#include <string>
#include <unordered_map>

namespace MWGui
{
    // Transient messages only. Interactive choices never enter this cache.
    class NotificationHistory
    {
        std::unordered_map<std::string, float> mRecent;
    public:
        static std::string key(const std::string& text)
        {
            std::string result;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                // MyGUI colour markup is presentation, not message identity.
                if (text[i] == '#' && i + 1 < text.size() && text[i + 1] == '#')
                {
                    result += '#';
                    ++i;
                    continue;
                }
                if (text[i] == '#' && i + 6 < text.size()
                    && std::all_of(text.begin() + i + 1, text.begin() + i + 7,
                        [](unsigned char c) { return std::isxdigit(c) != 0; }))
                {
                    i += 6;
                    continue;
                }
                const unsigned char c = static_cast<unsigned char>(text[i]);
                if (std::isspace(c))
                {
                    if (!result.empty() && result.back() != ' ') result += ' ';
                }
                else result += text[i];
            }
            if (!result.empty() && result.back() == ' ') result.pop_back();
            return result;
        }

        bool accept(const std::string& text)
        {
            const std::string id = key(text);
            if (id.empty()) return false;
            const bool duplicate = mRecent.find(id) != mRecent.end();
            keepVisible(text);
            return !duplicate;
        }

        void keepVisible(const std::string& text)
        {
            const std::string id = key(text);
            if (id.empty()) return;
            if (mRecent.size() >= 512 && mRecent.find(id) == mRecent.end())
            {
                const auto oldest = std::min_element(mRecent.begin(), mRecent.end(),
                    [](const auto& a, const auto& b) { return a.second < b.second; });
                mRecent.erase(oldest);
            }
            mRecent[id] = 5.f;
        }

        void advance(float seconds)
        {
            for (auto it = mRecent.begin(); it != mRecent.end();)
            {
                it->second -= std::max(0.f, seconds);
                if (it->second <= 0.f) it = mRecent.erase(it);
                else ++it;
            }
        }
        void clear() { mRecent.clear(); }
    };
}
#endif
