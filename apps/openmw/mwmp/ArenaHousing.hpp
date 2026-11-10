#ifndef OPENMW_ARENAHOUSING_HPP
#define OPENMW_ARENAHOUSING_HPP

#include <cstddef>
#include <map>
#include <set>
#include <string>

#include <components/esm/defs.hpp>

namespace MWWorld
{
    class Ptr;
    class ConstPtr;
}

namespace mwmp
{
    /// Z001: client half of the ArenaMP housing system.
    ///
    /// Why this exists at all
    /// ----------------------
    /// A teleport door is executed entirely on the client: MWClass::Door hands
    /// back an ActionTeleport and the player is in the next cell before the
    /// server hears about it. The archived server therefore had to "correct"
    /// the player afterwards, which is what produced the familiar bug where you
    /// walk into a house and are immediately yanked back out.
    ///
    /// The fix is to ask first. For the small set of doors the server has
    /// declared interesting - the ones whose destination is registered property -
    /// the client suppresses its own teleport, sends the destination to the
    /// server and waits. The server either approves the plain entry (and this
    /// class performs the teleport it was holding back) or teleports the player
    /// into a private instance itself. Either way the player never lands in a
    /// cell that has to be undone.
    ///
    /// Everything here is deliberately inert until the server says otherwise:
    /// with no housing data the set of managed cells is empty, isManaged() is
    /// always false, and every door in the game behaves exactly as it does in
    /// vanilla OpenMW.
    class ArenaHousing
    {
    public:
        enum Status
        {
            Status_None = 0,
            Status_Locked,
            Status_Free,   ///< vacant and for sale
            Status_Owned,  ///< owned by somebody the player may visit
            Status_Mine    ///< owned by the local player
        };

        struct Entry
        {
            Status status = Status_None;
            std::string hint;
            bool titleIsCell = false;
            bool originalCell = false;
            bool trusted = false; // owner, friend or co-owner (server state)
            std::string cell;   ///< original spelling of the cell description
        };

        static ArenaHousing& get();

        /// Handle a payload that arrived as "@@AMP_HOUSE@@<payload>".
        /// Returns true when the payload was understood.
        bool handleControlMessage(const std::string& payload);

        /// Forget everything. Called on disconnect so a second server never
        /// inherits the first one's property list.
        void reset();

        void cancelDoor();
        /// Open the server-authoritative furniture catalog without writing to game chat.
        void requestFurnitureMenu() const;
        void captureFacedFurniture() const;
        void armFurnitureCapture();
        bool consumeFurnitureCapture(const MWWorld::Ptr& target);
        void sendFurnitureAction(const std::string& verb, const std::string& arg="", int page=1,
            const std::string& subcategory="") const;
        bool ownsHome() const { return mOwnsHome; }
        bool hasItemWallet() const { return mHasItemWallet; }
        bool mHasItemWallet = false;
        bool isPlacementStaff() const { return mPlacementStaff; }
        std::string displayName(const std::string& cell) const;
        bool canTake(const MWWorld::Ptr& object) const;
        bool canTakeHere() const;
        /// Y070: house-wide no-take also blocks guest containers/beds. Trusted
        /// owner/co-owner/friend roles retain utility access through ROOM.utility.
        bool canUseHouseUtility(const MWWorld::ConstPtr& object) const;
        bool isManagedItem(const MWWorld::ConstPtr& object) const;
        bool itemLocked(const MWWorld::ConstPtr& object) const;
        void requestHouseItem(const std::string& verb, const MWWorld::ConstPtr& object) const;
        void requestItemWallet() const;
        std::map<std::string, bool> mItemLocks;

        /// True when the local account has reached the server-defined veteran
        /// threshold and this is a container in a home owned/co-owned by them.
        /// Such containers ignore the ESM capacity limit but keep normal contents.
        bool isBottomlessHomeContainer(const MWWorld::ConstPtr& object) const;

        /// Z006: true when this object stands in a room the local player owns
        /// (owner or co-owner).
        ///
        /// The furniture inside a bought house keeps whatever owner the ESM
        /// gave it, so the chest in your own bedroom still counted as somebody
        /// else's property: red "owned" tooltips, a stolen-goods flag on every
        /// item taken out of it, and a bed you were not allowed to sleep in.
        /// Rewriting the ownership of every reference would mean syncing it to
        /// every client and keeping it forever; treating the room as the
        /// player's own at the moment ownership is judged achieves the same
        /// thing and costs nothing.
        bool ownsPlace(const MWWorld::ConstPtr& object) const;
        bool isManaged(const std::string& destinationCell) const;
        const Entry* find(const std::string& destinationCell) const;

        /// Hint line for the door's tooltip, or an empty string.
        std::string hintForDoor(const MWWorld::ConstPtr& door) const;

        /// True when this door leads somewhere the server wants to arbitrate.
        bool isManagedDoor(const MWWorld::ConstPtr& door) const;

        /// Ask the server what to do about this door.
        /// @param hold  the player held the activate key instead of tapping it,
        ///              which the server reads as "I want to buy this".
        /// @return false when the request could not be sent, in which case the
        ///         caller should fall back to the ordinary local teleport.
        bool requestForDoor(const MWWorld::ConstPtr& door, bool hold, bool originalKeyUsed = false);
        bool canBypassDoorLock(const MWWorld::ConstPtr& door) const;

        /// Called every frame from GUIController::update().
        void update(float dt);

        /// The cell the server most recently asked us to strip of NPCs and
        /// loose items, or an empty string.
        const std::string& pendingPurgeCell() const { return mPendingPurgeCell; }

    private:
        ArenaHousing() = default;

        void sendControl(const std::string& payload) const;
        void applyState(const std::string& payload);
        void performHeldTeleport(const std::string& destinationCell);
        void purgeCell(const std::string& cellDescription);
        void unlockPrivateContainers(const std::string& cellDescription);

        /// Managed destinations, keyed by a lowercased cell description.
        std::map<std::string, Entry> mEntries;

        // Assemble large registries before replacing the current door state.
        std::string mPendingState;
        std::size_t mPendingStateSize = 0;
        std::string mInterestCell;
        std::set<std::string> mInterestDestinations;
        bool mInterestAcknowledged = false;
        float mInterestDelay = 0.f;

        /// The door the player is currently waiting on. Kept so that a "GO"
        /// reply can complete the teleport the client refused to do itself.
        struct Room
        {
            std::string title;
            bool take = false;
            bool mine = false;
            // Y069: owner/co-owner/friend may take books even when the house-wide
            // "take items" permission is disabled. Ordinary visitors may read only.
            bool bookTake = false;
            // Y070: beds/containers are trusted-house utilities. Friends retain
            // access even when ordinary item pickup is disabled for visitors.
            bool utility = false;
        };
        std::map<std::string, Room> mRooms;
        bool mOwnsHome = false;
        bool mPlacementStaff = false;
        bool mBottomlessHomeStorage = false;
        bool mCaptureFurniturePending = false;
        bool mHasPendingDoor = false;
        bool mPendingOriginalLocked = false;
        std::string mPendingDoorCell;
        ESM::Position mPendingDoorDest = {};
        float mPendingTimeout = 0.f;

        /// Instance cells the server has asked us to clean out.
        std::string mPendingPurgeCell;
        float mPurgeDelay = 0.f;
        std::string mPendingUnlockCell;
        float mUnlockDelay = 0.f;

        /// Cells already swept in this session, so re-entering a home does not
        /// re-scan it on every visit.
    };
}

#endif // OPENMW_ARENAHOUSING_HPP
