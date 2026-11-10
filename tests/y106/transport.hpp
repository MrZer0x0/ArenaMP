#ifndef ARENA_Y106_TEST_TRANSPORT_HPP
#define ARENA_Y106_TEST_TRANSPORT_HPP
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <deque>
#include <string>
#include <vector>

enum { ID_REMOTE_DISCONNECTION_NOTIFICATION=1, ID_REMOTE_CONNECTION_LOST,
    ID_REMOTE_NEW_INCOMING_CONNECTION, ID_CONNECTION_REQUEST_ACCEPTED, ID_NEW_INCOMING_CONNECTION,
    ID_NO_FREE_INCOMING_CONNECTIONS, ID_DISCONNECTION_NOTIFICATION, ID_CONNECTION_LOST,
    ID_CONNECTION_ATTEMPT_FAILED, ID_USER_PACKET_ENUM=128 };
enum { HIGH_PRIORITY, RELIABLE_ORDERED };
namespace RakNet { enum { IS_CONNECTED, IS_NOT_CONNECTED }; }
using RakNet::IS_CONNECTED;
using RakNet::IS_NOT_CONNECTED;
using PacketPriority = int;
using PacketReliability = int;
namespace RakNet {
using MessageID = uint8_t;
using BitSize_t = unsigned;
struct RakNetGUID {
    uint64_t g=0;
    static unsigned size() { return sizeof(uint64_t); }
    bool operator==(RakNetGUID b) const {return g==b.g;}
    bool operator!=(RakNetGUID b) const {return g!=b.g;}
    bool operator<(RakNetGUID b) const {return g<b.g;}
};
inline const RakNetGUID UNASSIGNED_CRABNET_GUID{0};
struct SystemAddress {
    uint64_t id=0;
    bool operator==(SystemAddress b) const {return id==b.id;}
    bool operator!=(SystemAddress b) const {return id!=b.id;}
    const char* ToString(bool=true) const {return "memory";}
    unsigned short GetPort() const {return 8087;}
};
struct AddressOrGUID {
    uint64_t id;
    AddressOrGUID(RakNetGUID g):id(g.g){}
    AddressOrGUID(SystemAddress a):id(a.id){}
};
class RakString;
class BitStream {
public:
    std::vector<unsigned char> bytes;
    std::size_t offset=0;
    BitStream()=default;
    BitStream(unsigned char* data,unsigned size,bool):bytes(data,data+size){}
    template<class T> void Write(const T& value) {
        const auto* p=reinterpret_cast<const unsigned char*>(&value);
        bytes.insert(bytes.end(),p,p+sizeof(T));
    }
    template<class T> bool Read(T& value) {
        if(offset+sizeof(T)>bytes.size()) return false;
        std::memcpy(&value,bytes.data()+offset,sizeof(T));offset+=sizeof(T);return true;
    }
    void Write(const RakString& value);
    bool Read(RakString& value);
    template<class T> void WriteCompressed(const T& value) {Write(value);}
    template<class T> bool ReadCompressed(T& value) {return Read(value);}
    template<class T> void Write(const T* values,uint32_t count) {
        for(uint32_t i=0;i<count;++i) Write(values[i]);
    }
    template<class T> bool Read(T* values,uint32_t count) {
        for(uint32_t i=0;i<count;++i) if(!Read(values[i]))return false;
        return true;
    }
    void IgnoreBytes(unsigned n) {offset=std::min(offset+n,bytes.size());}
    void ResetWritePointer() {bytes.clear();offset=0;}
    unsigned GetNumberOfBytesUsed() const {return bytes.size();}
    unsigned GetNumberOfUnreadBits() const {return (bytes.size()-offset)*8;}
};
class RakString {
public:
    std::string value;
    void AppendBytes(const char* p,std::size_t n) {value.append(p,n);}
    unsigned GetLength() const {return value.size();}
    void Truncate(unsigned n) {value.resize(n);}
    const char* C_String() const {return value.c_str();}
    static void SerializeCompressed(const char* v,BitStream* bs) {RakString s;s.value=v;bs->Write(s);}
    bool DeserializeCompressed(BitStream* bs) {return bs->Read(*this);}
};
inline void BitStream::Write(const RakString& value) {
    uint16_t n=value.value.size();Write(n);
    bytes.insert(bytes.end(),value.value.begin(),value.value.end());
}
inline bool BitStream::Read(RakString& value) {
    uint16_t n=0;if(!Read(n)||offset+n>bytes.size())return false;
    value.value.assign(reinterpret_cast<const char*>(bytes.data()+offset),n);offset+=n;return true;
}
struct Packet {
    std::vector<unsigned char> storage;
    unsigned char* data=nullptr;
    unsigned length=0;
    RakNetGUID guid;
    SystemAddress systemAddress;
    Packet(const std::vector<unsigned char>& b,RakNetGUID g,SystemAddress a)
        :storage(b),data(storage.data()),length(storage.size()),guid(g),systemAddress(a){}
};
class RakPeerInterface {
public:
    RakNetGUID own{100};
    SystemAddress address{100};
    RakPeerInterface* remote=nullptr;
    std::deque<Packet*> queue;
    std::vector<std::vector<unsigned char>> sent;
    std::function<void()> pump;
    unsigned closed=0,attempts=0;
    int connectionState=IS_NOT_CONNECTED;
    bool dropProfile=false,dropAck=false;
    uint32_t Send(BitStream* bs,int,int,int,AddressOrGUID,bool) {
        sent.push_back(bs->bytes);
        if(remote) remote->queue.push_back(new Packet(bs->bytes,own,address));
        return sent.size();
    }
    Packet* Receive() {
        if(queue.empty()&&pump)pump();
        if(queue.empty())return nullptr;
        auto* p=queue.front();queue.pop_front();return p;
    }
    void DeallocatePacket(Packet* p) {delete p;}
    RakNetGUID GetMyGUID() const {return own;}
    void CloseConnection(AddressOrGUID,bool) {++closed;}
    template<class... T> void Connect(T&&...) {++attempts;}
    int GetConnectionState(AddressOrGUID) const {return connectionState;}
    ~RakPeerInterface() {for(auto* p:queue) delete p;}
};
}
#endif
