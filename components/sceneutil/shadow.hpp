#ifndef COMPONENTS_SCENEUTIL_SHADOW_H
#define COMPONENTS_SCENEUTIL_SHADOW_H

#include <osgShadow/ShadowSettings>
#include <osgShadow/ShadowedScene>

#include <components/shader/shadermanager.hpp>

#include "mwshadowtechnique.hpp"

namespace SceneUtil
{
    class ShadowManager
    {
    public:
        static void disableShadowsForStateSet(osg::ref_ptr<osg::StateSet> stateSet);

        static Shader::ShaderManager::DefineMap getShadowsDisabledDefines();

        ShadowManager(osg::ref_ptr<osg::Group> sceneRoot, osg::ref_ptr<osg::Group> rootNode, unsigned int outdoorShadowCastingMask, unsigned int indoorShadowCastingMask, Shader::ShaderManager &shaderManager);

        void setupShadowSettings();
        void setShadowCastingMasks(unsigned int outdoorShadowCastingMask, unsigned int indoorShadowCastingMask);

        void setMaximumShadowMapDistance(float distance);

        /// Select the light used by the view-dependent shadow map. Light 0 is
        /// the sun; light 1 is ArenaMP's nearest-local-light proxy.
        void setActiveLightNum(int lightNum);

        /// Enable the multi-page local-light atlas for proxy lights 1..count.
        /// OpenMW 0.47's compatibility path supports two full point lights
        /// (four hemisphere maps) without colliding with material texture units.
        void setActiveLocalLightCount(unsigned int count);

        Shader::ShaderManager::DefineMap getShadowDefines();

        void enableIndoorMode();

        void enableOutdoorMode();
    protected:
        bool mEnableShadows;

        osg::ref_ptr<osgShadow::ShadowedScene> mShadowedScene;
        osg::ref_ptr<osgShadow::ShadowSettings> mShadowSettings;
        osg::ref_ptr<MWShadowTechnique> mShadowTechnique;

        unsigned int mOutdoorShadowCastingMask;
        unsigned int mIndoorShadowCastingMask;
    };
}

#endif //COMPONENTS_SCENEUTIL_SHADOW_H
