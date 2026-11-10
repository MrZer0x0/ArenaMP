// ArenaMP Craft and Economic client bridge. GPL-3.0-or-later.
#ifndef ARENA_MP_CRAFT_ECONOMIC_HPP
#define ARENA_MP_CRAFT_ECONOMIC_HPP
#include <components/misc/crafteconomicprotocol.hpp>
#include <map>
namespace MWWorld { class Ptr; }
namespace mwmp
{
    class CraftEconomic
    {
    public:
        using Value = ArenaCraft::Value;
        enum Mode { Unknown, Disabled, Enabled };
        static CraftEconomic& get();
        void reset();
        void update(float dt);
        bool receive(const std::string& message, bool fromLocalPlayer);
        int adjustedPrice(int base, const std::string& service, const std::string& itemId, bool buying) const;
        Mode mode() const { return mMode; }
        const std::string& contentKey() const { return mContentKey; }
        bool busy() const { return mCommitSequence!=0; }
        unsigned quote(Value request); // replaces the unsent draft; debounced to <= 5 Hz
        unsigned commit(const std::string& token);
        void menu(const MWWorld::Ptr& merchant);
        const Value* response(unsigned sequence) const;
        static Value item(const MWWorld::Ptr& ptr, int count);
        static Value merchant(const MWWorld::Ptr& ptr);
        static std::string status(const Value& response);
    private:
        unsigned sequence();
        bool send(const Value& value);
        void accept(Value value);
        Mode mMode=Unknown;
        unsigned mSequence=0,mCommitSequence=0;
        Value mDraft,mCommit,mMenu;
        Value mPricing;
        std::string mContentKey;
        std::map<unsigned,Value> mResponses;
        std::vector<Value> mDeferred;
        float mDraftDelay=0,mRetry=0,mNegotiation=0;
    };
}
#endif
