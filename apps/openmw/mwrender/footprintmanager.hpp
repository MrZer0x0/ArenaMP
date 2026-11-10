#ifndef OPENMW_MWRENDER_FOOTPRINTMANAGER_H
#define OPENMW_MWRENDER_FOOTPRINTMANAGER_H
#include <memory>
namespace osg { class Group; }
namespace Resource { class ResourceSystem; }
namespace MWWorld { class Ptr; class CellStore; }
namespace MWRender
{
    class RenderingManager;
    // Client-only scene nodes: never records, physics objects or network packets.
    class FootprintManager
    {
    public:
        FootprintManager(osg::Group* parent, Resource::ResourceSystem* resources, RenderingManager& rendering);
        ~FootprintManager();
        void enqueue(const MWWorld::Ptr& actor, bool left, bool landing);
        void update(float dt);
        void forgetActor(const MWWorld::Ptr& actor);
        void removeCell(const MWWorld::CellStore* cell);
        void clear();
    private:
        struct Impl;
        std::unique_ptr<Impl> mImpl;
    };
}
#endif
