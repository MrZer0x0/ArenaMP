#include "footprintmanager.hpp"
#include "footprintpolicy.hpp"
#include "renderingmanager.hpp"
#include "terrainstorage.hpp"
#include "vismask.hpp"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <set>
#include <unordered_map>

#include <osg/BlendFunc>
#include <osg/Depth>
#include <osg/Geode>
#include <osg/Material>
#include <osg/PositionAttitudeTransform>
#include <osg/Uniform>

#include <components/debug/debuglog.hpp>
#include <components/esm/records.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/statesetupdater.hpp>
#include <components/settings/settings.hpp>
#include <components/vfs/manager.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/world.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwmechanics/npcstats.hpp"
#include "../mwworld/cellstore.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/esmstore.hpp"
#include "../mwworld/inventorystore.hpp"
#include "../mwworld/ptr.hpp"

namespace MWRender
{
namespace
{
    using namespace FootprintPolicy;
    #include "footprintdata.inc"

    // Use the renderer's double-buffered state mechanism, as actor transparency does.
    // Each mark has its own callback/material; shared NIF state is never modified.
    class FindMaterial : public osg::NodeVisitor
    {
    public:
        osg::ref_ptr<osg::Material> material;
        FindMaterial() : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN) {}
        void inspect(osg::StateSet* state)
        {
            if (!material && state)
                material = dynamic_cast<osg::Material*>(state->getAttribute(osg::StateAttribute::MATERIAL));
        }
        void apply(osg::Node& node) override { inspect(node.getStateSet()); traverse(node); }
        void apply(osg::Geode& geode) override
        {
            inspect(geode.getStateSet());
            for (unsigned int i = 0; i < geode.getNumDrawables(); ++i)
                inspect(geode.getDrawable(i)->getStateSet());
            traverse(geode);
        }
    };

    class Fade : public SceneUtil::StateSetUpdater
    {
    public:
        float mAlpha = 1.f;
        osg::ref_ptr<osg::Material> mOriginal;
        explicit Fade(osg::Material* original)
            : mOriginal(original ? new osg::Material(*original) : new osg::Material) {}
        void setDefaults(osg::StateSet* state) override
        {
            const unsigned int flags = osg::StateAttribute::ON | osg::StateAttribute::OVERRIDE;
            state->setAttributeAndModes(new osg::BlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA), flags);
            state->setAttributeAndModes(new osg::Depth(osg::Depth::LEQUAL, 0, 1, false), flags);
            osg::ref_ptr<osg::Material> material = new osg::Material(*mOriginal);
            material->setColorMode(osg::Material::OFF);
            state->setAttributeAndModes(material, flags);
            state->addUniform(new osg::Uniform("colorMode", 0), flags);
            state->setRenderingHint(osg::StateSet::TRANSPARENT_BIN);
            state->setRenderBinMode(osg::StateSet::OVERRIDE_RENDERBIN_DETAILS);
        }
        void apply(osg::StateSet* state, osg::NodeVisitor*) override
        {
            static_cast<osg::Material*>(state->getAttribute(osg::StateAttribute::MATERIAL))
                ->setAlpha(osg::Material::FRONT_AND_BACK, mAlpha * mOriginal->getDiffuse(osg::Material::FRONT).a());
        }
    };

    template<class T> T setting(const std::string& name, T fallback);
    template<> bool setting(const std::string& name, bool fallback)
    { try { return Settings::Manager::getBool(name, "Footprints"); } catch (...) { return fallback; } }
    template<> int setting(const std::string& name, int fallback)
    { try { return Settings::Manager::getInt(name, "Footprints"); } catch (...) { return fallback; } }
    template<> float setting(const std::string& name, float fallback)
    { try { const float v = Settings::Manager::getFloat(name, "Footprints"); return std::isfinite(v) ? v : fallback; } catch (...) { return fallback; } }

    int surfaceClass(std::string model, const std::string& texture, bool terrain, bool skipStairs)
    {
        model = normalize(model);
        const std::string tex = baseName(texture);
        if (forbidden(model, skipStairs) || forbidden(tex, false)) return -1;
        if (!terrain)
        {
            if (!model.empty() && model.compare(0, 7, "meshes/") != 0) model = "meshes/" + model;
            auto it = modelSurfaces.find(model);
            if (it != modelSurfaces.end()) return it->second;
        }
        auto it = landSurfaces.find(tex);
        if (it != landSurfaces.end()) return it->second;
        const int byTexture = classify(tex);
        return byTexture ? byTexture : classify(baseName(model));
    }
}

struct FootprintManager::Impl
{
    struct Job
    {
        MWWorld::Ptr actor;
        const MWWorld::CellStore* cell;
        osg::Vec3f position;
        float yaw;
        double time;
        bool left;
    };
    struct ActorState
    {
        osg::Vec3f position;
        double lastTime = -1000;
        double dirtyUntil = 0;
    };
    struct Mark
    {
        osg::ref_ptr<osg::PositionAttitudeTransform> node;
        osg::ref_ptr<Fade> fade;
        const MWWorld::CellStore* cell;
        double born;
    };
    osg::ref_ptr<osg::Group> parent;
    Resource::ResourceSystem* resources;
    RenderingManager& rendering;
    std::deque<Job> pending;
    std::deque<Mark> marks;
    std::map<MWWorld::Ptr, ActorState> actors;
    std::set<std::string> failedModels;
    double time = 0;
    double nextDust = 0;
    bool enabled = setting("enabled", true);
    bool playerEnabled = setting("player", true);
    bool actorsEnabled = setting("actors", true);
    bool creaturesEnabled = setting("creatures", true);
    bool skipStairs = setting("skip stairs", true);
    bool dirtyFeet = setting("dirty feet", true);
    bool dust = setting("dust", false);
    int maxCount = std::max(20, std::min(1000, setting("max count", 400)));
    int budget = std::max(1, std::min(8, setting("steps per frame", 3)));
    float duration = std::max(5.f, std::min(600.f, setting("duration", 60.f)));
    float maxDistance = std::max(256.f, std::min(4096.f, setting("max distance", 1600.f)));
    float dirtyDuration = std::max(1.f, std::min(60.f, setting("dirty duration", 20.f)));

    Impl(osg::Group* root, Resource::ResourceSystem* res, RenderingManager& renderer)
        : parent(root), resources(res), rendering(renderer)
    {
#ifdef __ANDROID__
        maxCount = std::min(maxCount, 160);
        maxDistance = std::min(maxDistance, 1024.f);
        budget = 1;
        dust = false;
#endif
    }
    void clear()
    {
        for (auto& mark : marks) parent->removeChild(mark.node);
        marks.clear(); pending.clear(); actors.clear();
        time = nextDust = 0;
    }
    std::string type(const MWWorld::Ptr& actor, float& scale)
    {
        const auto& cls = actor.getClass();
        scale = actor.getCellRef().getScale();
        if (cls.isNpc())
        {
            const auto& npc = *actor.get<ESM::NPC>()->mBase;
            if (cls.getNpcStats(actor).isWerewolf()) return "paw";
            const auto& store = MWBase::Environment::get().getWorld()->getStore();
            const ESM::Race* race = store.get<ESM::Race>().search(npc.mRace);
            if (race)
            {
                scale *= npc.isMale() ? race->mData.mWeight.mMale : race->mData.mWeight.mFemale;
                if (race->mData.mFlags & ESM::Race::Beast)
                    return normalize(npc.mRace) == "argonian" ? "rept" : "paw";
            }
            auto& inventory = cls.getInventoryStore(actor);
            const auto boots = inventory.getSlot(MWWorld::InventoryStore::Slot_Boots);
            if (boots == inventory.end()) return "foot";
            if (boots->getTypeName() == typeid(ESM::Armor).name())
            {
                const float lightLimit = store.get<ESM::GameSetting>().find("iBootsWeight")->mValue.getInteger()
                    * store.get<ESM::GameSetting>().find("fLightMaxMod")->mValue.getFloat();
                if (boots->get<ESM::Armor>()->mBase->mData.mWeight > lightLimit) return "boot2";
            }
            return "boot";
        }
        const auto it = creatureTypes.find(baseName(cls.getModel(actor)));
        if (it == creatureTypes.end()) return {};
        const osg::Vec3f half = MWBase::Environment::get().getWorld()->getHalfExtents(actor);
        const float width = 2.f * std::min(half.x(), half.y());
        if (width > 1.f) scale = std::max(0.3f, std::min(5.f, width / 44.f));
        return it->second;
    }
    void enqueue(const MWWorld::Ptr& actor, bool left, bool landing)
    {
        if (!enabled || actor.isEmpty() || !actor.isInCell() || !actor.getClass().isActor()
            || actor.getRefData().isDeleted() || !actor.getRefData().isEnabled()) return;
        auto* world = MWBase::Environment::get().getWorld();
        const auto& player = world->getPlayerPtr();
        if (player.isEmpty() || !player.isInCell()) return;
        if (actor == player ? !playerEnabled : !actorsEnabled) return;
        if (!actor.getClass().isNpc() && !creaturesEnabled) return;
        if (actor.getClass().getCreatureStats(actor).isDead()) return;
        const osg::Vec3f pos = actor.getRefData().getPosition().asVec3();
        if (!std::isfinite(pos.x()) || !std::isfinite(pos.y()) || !std::isfinite(pos.z())) return;
        const osg::Vec3f playerPos = player.getRefData().getPosition().asVec3();
        if ((pos - playerPos).length2() > maxDistance * maxDistance) return;
        const std::string identity = normalize(actor.getCellRef().getRefId());
        for (const char* word : {"dummy", "mannequin", "manequin", "target", "invis"})
            if (contains(identity, word)) return;
        if (actors.size() >= 256 && actors.find(actor) == actors.end()) return;
        if (world->isSwimming(actor) || world->isWalkingOnWater(actor))
        {
            if (dirtyFeet) actors[actor].dirtyUntil = time + dirtyDuration;
            return;
        }
        if (world->isFlying(actor)) return;
        // Remote actors need not have a locally authoritative onGround flag.
        if (actor == player && !landing && !world->isOnGround(actor)) return;
        ActorState& state = actors[actor];
        // Establish a baseline, avoiding a phantom mark from initialization/idle keys.
        if (state.lastTime < -999)
        { state.position = pos; state.lastTime = time; return; }
        const float distance = (pos - state.position).length2();
        if (!acceptStep(time, state.lastTime, distance, landing)) return;
        state.position = pos;
        state.lastTime = time;
        const float yaw = actor.getRefData().getPosition().rot[2];
        if (!std::isfinite(yaw)) return;
        const auto push = [&](bool side) {
            Job job{actor, actor.getCell(), pos, yaw, time, side};
            if (pending.size() >= 60) pending.pop_back();
            if (actor == player) pending.push_front(job);
            else pending.push_back(job);
        };
        push(left);
        if (landing) push(!left);
    }
    void process(const Job& job)
    {
        const auto& actor = job.actor;
        if (!actor.isInCell() || actor.getCell() != job.cell || actor.getRefData().isDeleted()
            || !actor.getRefData().isEnabled() || time - job.time > 0.5) return;
        auto* world = MWBase::Environment::get().getWorld();
        const auto& player = world->getPlayerPtr();
        if (player.isEmpty() || !player.isInCell()) return;
        if ((job.position - player.getRefData().getPosition().asVec3()).length2() > maxDistance * maxDistance
            || (job.position - actor.getRefData().getPosition().asVec3()).length2() > 256.f * 256.f) return;
        float scale = 1;
        const std::string fpType = type(actor, scale);
        if (fpType.empty() || !std::isfinite(scale)) return;
        scale = std::max(0.3f, std::min(5.f, scale));
        osg::Vec3f from = job.position + osg::Vec3f(0, 0, 40.f * scale);
        const osg::Vec3f to = job.position - osg::Vec3f(0, 0, 32.f);
        RenderingManager::RayResult hit;
        bool found = false;
        for (int pass = 0; pass < 3; ++pass)
        {
            hit = rendering.castRay(from, to, true, true, true);
            if (!hit.mHit) return;
            if (hit.mHitObject.isEmpty() || hit.mHitObject.getTypeName() == typeid(ESM::Static).name())
            { found = true; break; }
            from = hit.mHitPointWorld - osg::Vec3f(0, 0, 0.5f);
            if (from.z() <= to.z()) return;
        }
        if (!found) return;
        osg::Vec3f normal = hit.mHitNormalWorld;
        if (normal.normalize() == 0 || !validGround(normal.z(), hit.mHitPointWorld.z() - job.position.z())) return;
        if (world->isUnderwater(job.cell, hit.mHitPointWorld + osg::Vec3f(0, 0, 0.5f)))
        {
            if (dirtyFeet) actors[actor].dirtyUntil = time + dirtyDuration;
            return;
        }
        std::string model;
        if (!hit.mHitObject.isEmpty()) model = hit.mHitObject.getClass().getModel(hit.mHitObject);
        const int surface = surfaceClass(model, hit.mHitTexture, hit.mHitTerrain, skipStairs);
        if (surface < 0) return;
        ActorState& state = actors[actor];
        if (dirtyFeet && surface >= 2) state.dirtyUntil = time + dirtyDuration;
        const bool dirty = dirtyFeet && time < state.dirtyUntil;
        if (!surface && !dirty) return;
        const std::string path = "meshes/abot/fp/" + fpType + (job.left ? "_l.nif" : "_r.nif");
        if (failedModels.count(path)) return;
        if (!resources->getVFS()->exists(path))
        {
            failedModels.insert(path);
            Log(Debug::Warning) << "Footprints: missing built-in model " << path;
            return;
        }
        try
        {
            osg::ref_ptr<osg::Node> mesh = resources->getSceneManager()->getInstance(path);
            osg::ref_ptr<osg::PositionAttitudeTransform> node = new osg::PositionAttitudeTransform;
            node->setNodeMask(Mask_Effect);
            node->setPosition(hit.mHitPointWorld + normal * 0.5f);
            osg::Quat slope; slope.makeRotate(osg::Vec3f(0, 0, 1), normal);
            // Original meshes already contain the left/right lateral foot offsets.
            node->setAttitude(osg::Quat(-job.yaw, osg::Vec3f(0, 0, 1)) * slope);
            node->setScale(osg::Vec3f(scale, scale, scale));
            node->addChild(mesh);
            FindMaterial material; mesh->accept(material);
            osg::ref_ptr<Fade> fade = new Fade(material.material);
            node->addUpdateCallback(fade);
            parent->addChild(node);
            marks.push_back({node, fade, job.cell, time});
            while (marks.size() > static_cast<std::size_t>(maxCount))
            { parent->removeChild(marks.front().node); marks.pop_front(); }
            if (dust && surface >= 2 && time >= nextDust)
            {
                nextDust = time + 0.2;
                const std::string puff = std::string("meshes/abot/fp/puff_") + (job.left ? "l.nif" : "r.nif");
                if (resources->getVFS()->exists(puff))
                    rendering.spawnEffect(puff, "", hit.mHitPointWorld, scale, false);
            }
        }
        catch (const std::exception& error)
        {
            failedModels.insert(path);
            Log(Debug::Warning) << "Footprints: " << path << ": " << error.what();
        }
    }
    void update(float dt)
    {
        if (!enabled || !std::isfinite(dt) || dt < 0) return;
        time += dt;
        for (auto it = marks.begin(); it != marks.end();)
        {
            const float age = static_cast<float>(time - it->born);
            if (age >= duration)
            { parent->removeChild(it->node); it = marks.erase(it); }
            else { it->fade->mAlpha = alpha(age, duration); ++it; }
        }
        for (int i = 0; i < budget && !pending.empty(); ++i)
        {
            const Job job = pending.front(); pending.pop_front();
            process(job);
        }
        for (auto it = actors.begin(); it != actors.end();)
            if (time - it->second.lastTime > 60 && time >= it->second.dirtyUntil) it = actors.erase(it);
            else ++it;
    }
};

FootprintManager::FootprintManager(osg::Group* parent, Resource::ResourceSystem* resources, RenderingManager& renderer)
    : mImpl(new Impl(parent, resources, renderer)) {}
FootprintManager::~FootprintManager() { mImpl->clear(); }
void FootprintManager::enqueue(const MWWorld::Ptr& actor, bool left, bool landing) { mImpl->enqueue(actor, left, landing); }
void FootprintManager::update(float dt) { mImpl->update(dt); }
void FootprintManager::clear() { mImpl->clear(); }
void FootprintManager::forgetActor(const MWWorld::Ptr& actor)
{
    mImpl->actors.erase(actor);
    auto& jobs = mImpl->pending;
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const Impl::Job& job) { return job.actor == actor; }), jobs.end());
}
void FootprintManager::removeCell(const MWWorld::CellStore* cell)
{
    auto& marks = mImpl->marks;
    for (auto it = marks.begin(); it != marks.end();)
        if (it->cell == cell) { mImpl->parent->removeChild(it->node); it = marks.erase(it); }
        else ++it;
    auto& jobs = mImpl->pending;
    jobs.erase(std::remove_if(jobs.begin(), jobs.end(), [&](const Impl::Job& job) { return job.cell == cell; }), jobs.end());
    for (auto it = mImpl->actors.begin(); it != mImpl->actors.end();)
        if (it->first.isInCell() && it->first.getCell() == cell) it = mImpl->actors.erase(it);
        else ++it;
}
}
