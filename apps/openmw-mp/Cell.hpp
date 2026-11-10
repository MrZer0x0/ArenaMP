#ifndef OPENMW_SERVERCELL_HPP
#define OPENMW_SERVERCELL_HPP

#include <deque>
#include <string>
#include <components/esm/records.hpp>
#include <components/openmw-mp/Base/BaseActor.hpp>
#include <components/openmw-mp/Base/BaseObject.hpp>
#include <components/openmw-mp/Packets/Actor/ActorPacket.hpp>
#include <components/openmw-mp/Packets/Object/ObjectPacket.hpp>

class Player;
class Cell;

class Cell
{
    friend class CellController;
public:
    Cell(ESM::Cell cell);
    typedef std::deque<Player*> TPlayers;
    typedef TPlayers::const_iterator Iterator;

    Iterator begin() const;
    Iterator end() const;

    void addPlayer(Player *player);
    void removePlayer(Player *player, bool cleanPlayer = true);

    void readActorList(unsigned char packetID, const mwmp::BaseActorList *newActorList);
    void readActorAI(const mwmp::BaseActorList *newActorList);
    bool getActorAI(int refNum, int mpNum, mwmp::BaseActor& actor) const;
    void setActorAI(const mwmp::BaseActor& actor);
    void removeActorAI(int refNum, int mpNum);
    bool containsActor(int refNum, int mpNum);
    mwmp::BaseActor *getActor(int refNum, int mpNum);
    void removeActors(const mwmp::BaseActorList *newActorList);

    RakNet::RakNetGUID *getAuthority();
    void setAuthority(const RakNet::RakNetGUID& guid);
    void rebindPlayerGuid(RakNet::RakNetGUID oldGuid, RakNet::RakNetGUID newGuid);
    mwmp::BaseActorList *getActorList();
    mwmp::BaseActorList *getActorAIList();

    TPlayers getPlayers() const;
    void sendToLoaded(mwmp::ActorPacket *actorPacket, mwmp::BaseActorList *baseActorList) const;
    void sendCachedActorAI(const RakNet::RakNetGUID& authority) const;
    void sendToLoaded(mwmp::ObjectPacket *objectPacket, mwmp::BaseObjectList *baseObjectList) const;

    std::string getShortDescription() const;


private:
    TPlayers players;
    ESM::Cell cell;

    RakNet::RakNetGUID authorityGuid;
    mwmp::BaseActorList cellActorList;
    mwmp::BaseActorList cellActorAIList;
};


#endif //OPENMW_SERVERCELL_HPP
