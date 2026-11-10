#ifndef ARENAMP_STATICINSTANCING_H
#define ARENAMP_STATICINSTANCING_H

#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <tuple>
#include <typeinfo>
#include <vector>
#include <osg/Geode>
#include <osg/Geometry>
#include <osg/GLExtensions>
#include <osg/MatrixTransform>
#include <osg/VertexAttribDivisor>
#include <osg/StateSet>
#include <osg/Uniform>
#include <components/esm/cellref.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/optimizer.hpp>

namespace MWRender
{
namespace StaticInstancing
{
    // Called on the setup thread, not from the paging worker. Missing context
    // or incomplete support always selects the original paging path.
    inline bool supported()
    {
#if defined(__ANDROID__)
        return false; // GL4ES drivers need separate on-device validation.
#else
        const osg::GLExtensions* gl = osg::GLExtensions::Get(0, false);
        return gl && gl->glVersion >= 3.3f && gl->isGlslSupported && gl->glVertexAttribDivisor
            && gl->glDrawArraysInstanced && gl->glDrawElementsInstanced;
#endif
    }

    class Validate : public osg::NodeVisitor
    {
    public:
        bool valid = true;
        bool flattened = false;
        unsigned geometries = 0;
        explicit Validate(bool flat = false) : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN), flattened(flat) {}
        bool inspect(osg::Node& node)
        {
            if (node.getUpdateCallback() || node.getCullCallback() || node.getEventCallback()
                || node.getDataVariance() == osg::Object::DYNAMIC || !node.getCullingActive())
                return valid = false;
            std::string prefix;
            if (node.getUserValue("shaderPrefix", prefix) && prefix != "objects") return valid = false;
            const osg::StateSet* state = node.getStateSet();
            if (state && (state->getUpdateCallback() || state->getEventCallback()
                || (state->getMode(GL_BLEND) & osg::StateAttribute::ON)
                || state->getAttribute(osg::StateAttribute::BLENDFUNC)
                || state->getRenderingHint() == osg::StateSet::TRANSPARENT_BIN))
                return valid = false;
            return true;
        }
        void apply(osg::Node& node) override
        {
            if (!inspect(node)) return;
            // Reject switches, LODs, billboards, particles, custom callbacks and
            // custom node types. No substitution of their runtime semantics.
            if (typeid(node) != typeid(osg::Group) && typeid(node) != typeid(osg::Geode)
                && typeid(node) != typeid(osg::MatrixTransform)) { valid=false;return; }
            if (auto* transform = dynamic_cast<osg::MatrixTransform*>(&node))
                if (transform->getReferenceFrame() != osg::Transform::RELATIVE_RF
                    || (flattened && !transform->getMatrix().isIdentity())) { valid=false;return; }
            traverse(node);
        }
        void apply(osg::Drawable&) override { valid=false; }
        void apply(osg::Geometry& geom) override
        {
            if (!inspect(geom) || typeid(geom) != typeid(osg::Geometry)
                || geom.getDrawCallback() || !geom.getVertexArray() || !geom.getNormalArray()
                || geom.getVertexAttribArray(6) || geom.getVertexAttribArray(7)
                || geom.getSecondaryColorArray() || geom.getFogCoordArray()) { valid=false;return; }
            if (!geom.getNumPrimitiveSets() || !geom.getBoundingBox().valid()) { valid=false;return; }
            for (unsigned i=0;i<geom.getNumPrimitiveSets();++i)
            {
                const auto* primitive=geom.getPrimitiveSet(i);
                if (primitive->getNumInstances() != 0
                    || (primitive->getType()!=osg::PrimitiveSet::DrawArraysPrimitiveType
                        && primitive->getType()!=osg::PrimitiveSet::DrawElementsUBytePrimitiveType
                        && primitive->getType()!=osg::PrimitiveSet::DrawElementsUShortPrimitiveType
                        && primitive->getType()!=osg::PrimitiveSet::DrawElementsUIntPrimitiveType))
                    { valid=false;return; }
            }
            ++geometries;
        }
    };

    struct ExactBounds : osg::Drawable::ComputeBoundingBoxCallback
    {
        osg::BoundingBox box;
        explicit ExactBounds(const osg::BoundingBox& value) : box(value) {}
        osg::BoundingBox computeBound(const osg::Drawable&) const override { return box; }
    };

    class Apply : public osg::NodeVisitor
    {
        osg::ref_ptr<osg::Vec4Array> mOffsets, mRotations;
        const std::vector<osg::Matrixf>& mMatrices;
    public:
        Apply(osg::Vec4Array* offsets, osg::Vec4Array* rotations, const std::vector<osg::Matrixf>& matrices)
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN), mOffsets(offsets), mRotations(rotations), mMatrices(matrices) {}
        void apply(osg::Geometry& geom) override
        {
            const osg::BoundingBox local = geom.getBoundingBox();
            osg::BoundingBox bounds;
            for (const auto& matrix : mMatrices)
                for (unsigned i=0;i<8;++i) bounds.expandBy(local.corner(i) * matrix);
            geom.setComputeBoundingBoxCallback(new ExactBounds(bounds));
            geom.dirtyBound();
            geom.setUseDisplayList(false);
            geom.setUseVertexBufferObjects(true);
            for (unsigned i=0;i<geom.getNumPrimitiveSets();++i)
                geom.getPrimitiveSet(i)->setNumInstances(mMatrices.size());
            geom.setVertexAttribArray(6,mOffsets,osg::Array::BIND_PER_VERTEX);
            geom.setVertexAttribArray(7,mRotations,osg::Array::BIND_PER_VERTEX);
            auto* state=geom.getOrCreateStateSet();
            state->setAttribute(new osg::VertexAttribDivisor(6,1));
            state->setAttribute(new osg::VertexAttribDivisor(7,1));
            state->addUniform(new osg::Uniform("arenaStaticInstancing",true));
        }
    };

    // Append batches and report precisely which references were consumed. Small
    // spatial buckets/tails stay on the ordinary path; never span a whole city.
    inline unsigned build(Resource::SceneManager* sceneManager, const osg::Node* source,
        const std::vector<const ESM::CellRef*>& references, const osg::Vec3f& worldCenter,
        unsigned minimum, unsigned maximum, osg::Group* output,
        std::set<const ESM::CellRef*>& consumed)
    {
        if (references.size()<minimum) return 0;
        Validate initial;
        const_cast<osg::Node*>(source)->accept(initial);
        if (!initial.valid || !initial.geometries) return 0;
        osg::ref_ptr<osg::Group> prepared = new osg::Group;
        prepared->addChild(static_cast<osg::Node*>(source->clone(osg::CopyOp::DEEP_COPY_NODES
            | osg::CopyOp::DEEP_COPY_DRAWABLES | osg::CopyOp::DEEP_COPY_ARRAYS
            | osg::CopyOp::DEEP_COPY_PRIMITIVES | osg::CopyOp::DEEP_COPY_STATESETS)));
        SceneUtil::Optimizer optimizer;
        optimizer.optimize(prepared, SceneUtil::Optimizer::FLATTEN_STATIC_TRANSFORMS
            | SceneUtil::Optimizer::REMOVE_REDUNDANT_NODES);
        Validate flattened(true);
        prepared->accept(flattened);
        if (!flattened.valid || !flattened.geometries) return 0;

        using Bucket = std::tuple<int,int,int>;
        std::map<Bucket,std::vector<const ESM::CellRef*>> buckets;
        for (auto* ref:references)
        {
            const osg::Vec3f p=ref->mPos.asVec3()-worldCenter;
            if (!std::isfinite(ref->mScale) || ref->mScale<=0.f
                || !std::isfinite(p.x()) || !std::isfinite(p.y()) || !std::isfinite(p.z())
                || !std::isfinite(ref->mPos.rot[0]) || !std::isfinite(ref->mPos.rot[1])
                || !std::isfinite(ref->mPos.rot[2])) continue;
            buckets[Bucket(int(std::floor(p.x()/4096.f)),int(std::floor(p.y()/4096.f)),
                int(std::floor(p.z()/4096.f)))].push_back(ref);
        }
        unsigned batches=0;
        for (const auto& bucket:buckets)
        {
            const auto& refs=bucket.second;
            for (std::size_t start=0;start<refs.size();start+=maximum)
            {
                const std::size_t count=std::min<std::size_t>(maximum,refs.size()-start);
                if (count<minimum) continue;
                osg::ref_ptr<osg::Node> batch=static_cast<osg::Node*>(prepared->clone(
                    osg::CopyOp::DEEP_COPY_NODES | osg::CopyOp::DEEP_COPY_DRAWABLES
                    | osg::CopyOp::DEEP_COPY_PRIMITIVES | osg::CopyOp::DEEP_COPY_STATESETS));
                osg::ref_ptr<osg::Vec4Array> offsets=new osg::Vec4Array;
                osg::ref_ptr<osg::Vec4Array> rotations=new osg::Vec4Array;
                std::vector<osg::Matrixf> matrices;
                for (std::size_t i=start;i<start+count;++i)
                {
                    const auto& ref=*refs[i];
                    const osg::Vec3f p=ref.mPos.asVec3()-worldCenter;
                    const osg::Quat rotation=osg::Quat(ref.mPos.rot[2],osg::Vec3f(0,0,-1))
                        * osg::Quat(ref.mPos.rot[1],osg::Vec3f(0,-1,0))
                        * osg::Quat(ref.mPos.rot[0],osg::Vec3f(-1,0,0));
                    offsets->push_back(osg::Vec4f(p,ref.mScale));
                    rotations->push_back(osg::Vec4f(rotation.x(),rotation.y(),rotation.z(),rotation.w()));
                    osg::Matrixf matrix;
                    matrix.preMultTranslate(p);matrix.preMultRotate(rotation);
                    matrix.preMultScale(osg::Vec3f(ref.mScale,ref.mScale,ref.mScale));
                    matrices.push_back(matrix);
                }
                Apply apply(offsets,rotations,matrices);batch->accept(apply);
                sceneManager->recreateShaders(batch, "objects", false, true, true);
                output->addChild(batch);
                for (std::size_t i=start;i<start+count;++i) consumed.insert(refs[i]);
                ++batches;
            }
        }
        return batches;
    }
}
}
#endif
