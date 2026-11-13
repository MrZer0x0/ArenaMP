#include "FurnitureStoreWindow.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

#include <cmath>
#include <exception>
#include <map>

#include <osg/Camera>
#include <osg/BlendFunc>
#include <osg/NodeVisitor>
#include <osg/Group>
#include <osg/Fog>
#include <osg/Light>
#include <osg/LightModel>
#include <osg/Material>
#include <osg/LightSource>
#include <osg/MatrixTransform>
#include <osg/PositionAttitudeTransform>
#include <osg/Texture2D>
#include <osg/TexEnvCombine>
#include <osg/Viewport>
#include <osg/Uniform>
#include <osg/StateSet>
#include <osg/BoundingSphere>
#include <osg/GL>

#include <components/esm/loadstat.hpp>
#include <components/esm/loadcont.hpp>
#include <components/esm/loadacti.hpp>
#include <components/esm/loadligh.hpp>
#include <components/esm/loadmisc.hpp>
#include <components/fallback/fallback.hpp>
#include <components/myguiplatform/myguitexture.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/resource/scenemanager.hpp>
#include <components/sceneutil/lightmanager.hpp>
#include <components/sceneutil/shadow.hpp>
#include <components/debug/debuglog.hpp>

#include "apps/openmw/mwbase/world.hpp"
#include "apps/openmw/mwgui/windowmanagerimp.hpp"
#include "apps/openmw/mwgui/confirmationdialog.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwrender/vismask.hpp"

#include <MyGUI_InputManager.h>
#include <MyGUI_Gui.h>
#include <MyGUI_UString.h>
#include <MyGUI_LanguageManager.h>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwbase/windowmanager.hpp"

#include "../ArenaHousing.hpp"
#include "../GUIController.hpp"
#include "../Main.hpp"

namespace
{

    struct RetiredFurniturePreview
    {
        osg::ref_ptr<osg::Group> root;
        osg::ref_ptr<osg::Camera> camera;
        std::unique_ptr<MyGUI::ITexture> guiTexture;
        unsigned int age = 0;
    };

    // OSG may still be drawing frame N while the main thread is already
    // destroying UI from frame N+1. Android drivers are particularly
    // unforgiving here. CharacterPreview already uses the same delayed-retire
    // strategy; furniture RTTs must not detach/free their graph immediately.
    std::vector<RetiredFurniturePreview> gRetiredFurniturePreviews;

    void retireFurniturePreview(osg::ref_ptr<osg::Group> root, osg::ref_ptr<osg::Camera> camera,
        std::unique_ptr<MyGUI::ITexture> guiTexture)
    {
        if (!camera)
            return;
        camera->setNodeMask(0);
        RetiredFurniturePreview retired;
        retired.root = root;
        retired.camera = camera;
        retired.guiTexture = std::move(guiTexture);
        gRetiredFurniturePreviews.push_back(std::move(retired));
    }

    void collectFurniturePreviewGarbage()
    {
        // Four main-loop ticks cover cull/draw overlap plus one GL object-cache
        // turn, matching the safety margin used by CharacterPreview.
        for (std::size_t i = gRetiredFurniturePreviews.size(); i > 0; --i)
        {
            RetiredFurniturePreview& retired = gRetiredFurniturePreviews[i - 1];
            if (++retired.age < 4)
                continue;
            if (retired.root && retired.camera)
                retired.root->removeChild(retired.camera.get());
            if (retired.camera)
                retired.camera->removeChildren(0, retired.camera->getNumChildren());
            retired.guiTexture.reset();
            gRetiredFurniturePreviews.erase(gRetiredFurniturePreviews.begin() + (i - 1));
        }
    }

    std::vector<std::string> fields(const std::string& text)
    {
        std::vector<std::string> values;
        std::string value;
        std::istringstream stream(text);
        while (std::getline(stream, value, '\t')) values.push_back(value);
        return values;
    }

    // ASCII + UTF-8 Russian case folding for catalogue search, independent
    // of the OS locale (Windows/Linux/Android). Preserve other UTF-8 bytes.
    std::string lower(const std::string& value)
    {
        std::string out;
        out.reserve(value.size());
        for (std::size_t i=0;i<value.size();++i)
        {
            const unsigned char c=static_cast<unsigned char>(value[i]);
            if (c==0xD0 && i+1<value.size())
            {
                const unsigned char second=static_cast<unsigned char>(value[i+1]);
                if (second==0x81) {out += "\xD1\x91"; ++i; continue;} // Ё -> ё
                if (second>=0x90 && second<=0x9F)
                {out += static_cast<char>(0xD0);out += static_cast<char>(second+0x20);++i;continue;}
                if (second>=0xA0 && second<=0xAF)
                {out += static_cast<char>(0xD1);out += static_cast<char>(second-0x20);++i;continue;}
            }
            out+=static_cast<char>(c<0x80?std::tolower(c):c);
        }
        return out;
    }

    // ArenaMP's existing language manager reads l10n/arenamp/{en,ru}.ini.
    // Catalog identifiers stay unchanged on the wire, regardless of UI language.
    std::string furnitureText(const std::string& key)
    {
        const std::string tag = "#{arenamp=furniture." + key + "}";
        const std::string translated = MyGUI::LanguageManager::getInstance().replaceTags(tag);
        if (!translated.empty() && translated != tag && translated != "furniture." + key && translated.find("#{arenamp=") == std::string::npos)
            return translated;

        // Never leave a control blank when a language pack is incomplete or has
        // not finished loading yet. The normal l10n files still take priority.
        static const std::map<std::string, std::pair<std::string, std::string>> fallback = {
            {"title", {"Furniture and decoration", "Мебель и декорации"}},
            {"close", {"Close", "Закрыть"}}, {"shop", {"Shop", "Магазин"}},
            {"inventory", {"My furniture", "Моя мебель"}}, {"placed", {"Placed", "В доме"}},
            {"categories", {"Categories", "Категории"}}, {"previous", {"Back", "Назад"}},
            {"next", {"Next", "Далее"}}, {"all", {"All", "Все"}},
            {"buy", {"Buy", "Купить"}}, {"place", {"Place", "Установить"}},
            {"take", {"Take", "Забрать"}}, {"sell", {"Sell", "Продать"}},
            {"sell.confirm", {"Sell \"%s\"? It leaves \"My furniture\" for good.",
                              "Продать “%s”? Предмет навсегда исчезнет из “Моя мебель”."}},
            {"select", {"Select an item", "Выберите предмет"}},
            {"select.preview", {"Select an item", "Выберите предмет"}},
            {"gold", {"gold", "золота"}}, {"gold.short", {"g", "зол."}},
            {"owned", {"Owned", "В инвентаре"}}, {"unavailable", {"Unavailable", "Недоступно"}},
            {"missing.model", {"Model is not loaded", "Модель не загружена"}},
            {"preview.unavailable", {"Preview unavailable", "Предпросмотр недоступен"}},
            {"style.nordic", {"Nordic", "Нордские"}}, {"style.dunmer", {"Dunmer", "Данмерские"}},
            {"style.imperial", {"Imperial", "Имперские"}}, {"style.other", {"Other", "Другие"}},
            {"group.furniture", {"Furniture", "Мебель"}}, {"group.trading", {"Trading", "Торговля"}},
            {"group.tables", {"Tables", "Столы"}}, {"group.seating", {"Seating", "Сиденья"}},
            {"group.beds", {"Beds", "Кровати"}}, {"group.storage", {"Wardrobes and shelves", "Шкафы и полки"}},
            {"group.textiles", {"Rugs and textiles", "Ковры и текстиль"}}, {"group.plants", {"Plants", "Растения"}},
            {"group.decor", {"Decor", "Декор"}}, {"group.containers", {"Containers", "Контейнеры"}},
            {"group.lighting", {"Lighting", "Освещение"}}, {"group.fireplaces", {"Fireplaces", "Камины"}},
            {"group.chests", {"Chests", "Сундуки"}}, {"group.crates", {"Barrels and crates", "Бочки и ящики"}},
            {"group.chairs", {"Chairs and stools", "Стулья и табуреты"}}, {"group.benches", {"Benches", "Скамейки"}},
            {"group.shelves", {"Shelves", "Полки"}}, {"group.wardrobes", {"Wardrobes", "Шкафы"}},
            {"group.rugs", {"Rugs", "Ковры"}}, {"group.lamps", {"Lamps", "Лампы"}},
            {"group.other", {"Other", "Другое"}},
            {"admin.edit", {"Edit", "Изменить"}}, {"admin.capture", {"Add furniture", "Добавить мебель"}},
            {"admin.refresh", {"Refresh", "Обновить каталог"}}, {"admin.title", {"Edit furniture", "Настройка мебели"}},
            {"admin.category", {"Category", "Категория"}}, {"admin.name.ru", {"Name (RU)", "Название (RU)"}},
            {"admin.name.en", {"Name (EN)", "Название (EN)"}}, {"admin.price", {"Price", "Цена"}},
            {"admin.hint", {"Changes are stored separately.", "Изменения сохраняются отдельно."}},
            {"admin.save", {"Save", "Сохранить"}}, {"admin.cancel", {"Cancel", "Отмена"}},
            {"admin.invalid", {"Check the fields", "Проверьте поля"}}
        };
        const auto found = fallback.find(key);
        if (found == fallback.end())
            return key;
        const bool russian = MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru";
        return russian ? found->second.second : found->second.first;
    }
    std::string furnitureGroup(const std::string& name)
    {
        static const std::map<std::string,std::string> groups={
            {"Мебель","furniture"},{"Торговля","trading"},{"Столы","tables"},{"Сиденья","seating"},{"Кровати","beds"},
            {"Шкафы и полки","storage"},{"Ковры и текстиль","textiles"},
            {"Растения","plants"},{"Декор","decor"},{"Контейнеры","containers"},
            {"Освещение","lighting"},{"Камины","fireplaces"},
            {"Сундуки","chests"},{"Бочки и ящики","crates"},
            {"Стулья и табуреты","chairs"},{"Скамейки","benches"},
            {"Полки и стеллажи","shelves"},{"Шкафы и комоды","wardrobes"},
            {"Ковры","rugs"},{"Светильники","lamps"},{"Прочее","other"}
        };
        auto it=groups.find(name);
        return it==groups.end()?name:furnitureText("group."+it->second);
    }

    // Never offer a model that the local engine did not load from its active
    // ESM/ESP. The server independently builds its authoritative assortment.
    bool hasLocalFurnitureRecord(const std::string& refId)
    {
        if (refId.empty()) return false;
        const auto& store=MWBase::Environment::get().getWorld()->getStore();
        if (const auto* r=store.get<ESM::Static>().search(refId)) return !r->mModel.empty();
        if (const auto* r=store.get<ESM::Container>().search(refId)) return !r->mModel.empty();
        if (const auto* r=store.get<ESM::Activator>().search(refId)) return !r->mModel.empty();
        if (const auto* r=store.get<ESM::Light>().search(refId)) return !r->mModel.empty();
        if (const auto* r=store.get<ESM::Miscellaneous>().search(refId)) return !r->mModel.empty();
        return false;
    }

    // CharacterPreview has one extra step that is easy to miss when reusing its
    // render-to-texture lighting: transparent mesh state must be adapted for a
    // premultiplied-alpha FBO. Without this, a model can render RGB correctly
    // but still arrive in MyGUI with alpha 0 and look like an empty/black box.
    class FurniturePreviewBlendVisitor : public osg::NodeVisitor
    {
    public:
        FurniturePreviewBlendVisitor()
            : osg::NodeVisitor(TRAVERSE_ALL_CHILDREN), mNoAlpha(new osg::Uniform("noAlpha", false)) {}

        void apply(osg::Node& node) override
        {
            osg::StateSet* original=node.getStateSet();
            osg::ref_ptr<osg::StateSet> replacement;
            if (original)
            {
                osg::BlendFunc* blend=dynamic_cast<osg::BlendFunc*>(
                    original->getAttribute(osg::StateAttribute::BLENDFUNC));
                if (blend || original->getBinNumber()==osg::StateSet::TRANSPARENT_BIN)
                {
                    replacement=new osg::StateSet(*original,osg::CopyOp::SHALLOW_COPY);
                    node.setStateSet(replacement);
                    if (blend)
                    {
                        osg::ref_ptr<osg::BlendFunc> adjusted=new osg::BlendFunc(*blend);
                        replacement->setAttribute(adjusted,osg::StateAttribute::ON);
                        if (blend->getDestination()==osg::BlendFunc::ONE_MINUS_SRC_ALPHA)
                            adjusted->setSourceAlpha(osg::BlendFunc::ONE);
                        else if (blend->getDestination()==osg::BlendFunc::ONE)
                            adjusted->setSourceAlpha(osg::BlendFunc::ZERO);
                    }
                }
                if (original->getMode(GL_BLEND)&osg::StateAttribute::ON)
                {
                    if (!replacement)
                    {
                        replacement=new osg::StateSet(*original,osg::CopyOp::SHALLOW_COPY);
                        node.setStateSet(replacement);
                    }
                    replacement->setTextureMode(7,GL_TEXTURE_2D,osg::StateAttribute::OFF);
                    replacement->addUniform(mNoAlpha);
                }
            }
            traverse(node);
        }
    private:
        osg::ref_ptr<osg::Uniform> mNoAlpha;
    };
}

namespace mwmp
{
    // Local-only render-to-texture preview: no world placement, no inventory
    // mutations and no network packets while the player rotates/zooms a model.
    class FurniturePreview
    {
    public:
        FurniturePreview()
        {
            auto* manager=dynamic_cast<MWGui::WindowManager*>(
                MWBase::Environment::get().getWindowManager());
            mRoot=manager ? manager->getFurniturePreviewRoot() : nullptr;
            if (!mRoot) return;
            mTexture=new osg::Texture2D;
            mTexture->setTextureSize(512,512);
            mTexture->setInternalFormat(GL_RGBA);
            mTexture->setFilter(osg::Texture::MIN_FILTER,osg::Texture::LINEAR);
            mTexture->setFilter(osg::Texture::MAG_FILTER,osg::Texture::LINEAR);
            mTexture->setUserValue("premultiplied alpha",true);
            mCamera=new osg::Camera;
            mCamera->setReferenceFrame(osg::Camera::ABSOLUTE_RF);
            mCamera->setRenderTargetImplementation(osg::Camera::FRAME_BUFFER_OBJECT,osg::Camera::PIXEL_BUFFER_RTT);
            mCamera->setClearMask(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            mCamera->setClearColor(osg::Vec4(0.72f,0.73f,0.75f,1.f));
            mCamera->setProjectionMatrixAsPerspective(35.f,1.f,0.1f,100000.f);
            mCamera->setViewport(0,0,512,512);
            mCamera->setRenderOrder(osg::Camera::PRE_RENDER);
            mCamera->setNodeMask(MWRender::Mask_RenderToTexture);
            mCamera->setComputeNearFarMode(osg::Camera::COMPUTE_NEAR_FAR_USING_BOUNDING_VOLUMES);
            // Match CharacterPreview: render every ordinary scene node, but do not
            // run the world update visitor through the isolated preview graph.
            mCamera->setCullMask(~(MWRender::Mask_UpdateVisitor));
            mCamera->setName("FurniturePreview");
            mCamera->attach(osg::Camera::COLOR_BUFFER,mTexture.get());
            // Use the same light-manager path as the inventory and character
            // generation previews. Plain osg::LightSource is ignored by the
            // shader lighting path and was the reason furniture rendered black.
            const bool ffp = MWBase::Environment::get().getResourceSystem()->getSceneManager()
                ->getLightingMethod() == SceneUtil::LightingMethod::FFP;
            mLightManager = new SceneUtil::LightManager(ffp);
            mLightManager->setStartLight(1);
            osg::ref_ptr<osg::StateSet> stateset = mLightManager->getOrCreateStateSet();
            stateset->setMode(GL_LIGHTING, osg::StateAttribute::ON);
            stateset->setMode(GL_NORMALIZE, osg::StateAttribute::ON);
            stateset->setMode(GL_CULL_FACE, osg::StateAttribute::ON);
            stateset->addUniform(new osg::Uniform("isInventoryPreview", true), osg::StateAttribute::OVERRIDE);
            stateset->addUniform(new osg::Uniform("isInterior", true), osg::StateAttribute::OVERRIDE);

            osg::ref_ptr<osg::Material> defaultMat = new osg::Material;
            defaultMat->setColorMode(osg::Material::OFF);
            defaultMat->setAmbient(osg::Material::FRONT_AND_BACK, osg::Vec4f(1, 1, 1, 1));
            defaultMat->setDiffuse(osg::Material::FRONT_AND_BACK, osg::Vec4f(1, 1, 1, 1));
            defaultMat->setSpecular(osg::Material::FRONT_AND_BACK, osg::Vec4f(0, 0, 0, 0));
            stateset->setAttribute(defaultMat);
            SceneUtil::ShadowManager::disableShadowsForStateSet(stateset);

            osg::ref_ptr<osg::Fog> fog = new osg::Fog;
            fog->setStart(10000000.f);
            fog->setEnd(10000000.f);
            stateset->setAttributeAndModes(fog, osg::StateAttribute::OFF | osg::StateAttribute::OVERRIDE);

            // CharacterPreview forces opaque fragments to alpha=1 because the
            // render target itself is transparent. Without this shader/FFP bridge
            // many furniture meshes are rendered into RGB but keep alpha=0, so
            // MyGUI shows an apparently empty preview. Keep this block in sync
            // with mwrender/characterpreview.cpp.
            osg::ref_ptr<osg::TexEnvCombine> noBlendAlphaEnv = new osg::TexEnvCombine;
            noBlendAlphaEnv->setCombine_Alpha(osg::TexEnvCombine::REPLACE);
            noBlendAlphaEnv->setSource0_Alpha(osg::TexEnvCombine::CONSTANT);
            noBlendAlphaEnv->setConstantColor(osg::Vec4(0.f, 0.f, 0.f, 1.f));
            noBlendAlphaEnv->setCombine_RGB(osg::TexEnvCombine::REPLACE);
            noBlendAlphaEnv->setSource0_RGB(osg::TexEnvCombine::PREVIOUS);
            osg::ref_ptr<osg::Texture2D> dummyTexture = new osg::Texture2D;
            dummyTexture->setInternalFormat(GL_DEPTH_COMPONENT);
            dummyTexture->setTextureSize(1, 1);
            dummyTexture->setShadowComparison(true);
            dummyTexture->setShadowCompareFunc(osg::Texture::ShadowCompareFunc::ALWAYS);
            stateset->setTextureAttributeAndModes(7, dummyTexture, osg::StateAttribute::ON);
            stateset->setTextureAttribute(7, noBlendAlphaEnv, osg::StateAttribute::ON);
            stateset->addUniform(new osg::Uniform("noAlpha", true), osg::StateAttribute::OVERRIDE);

            osg::ref_ptr<osg::LightModel> lightModel = new osg::LightModel;
            lightModel->setAmbientIntensity(osg::Vec4(0.f, 0.f, 0.f, 1.f));
            stateset->setAttributeAndModes(lightModel, osg::StateAttribute::ON);

            osg::ref_ptr<osg::Light> light = new osg::Light;
            const float diffuseR = std::min(1.45f, std::max(0.9f, 1.2f * Fallback::Map::getFloat("Inventory_DirectionalDiffuseR")));
            const float diffuseG = std::min(1.45f, std::max(0.9f, 1.2f * Fallback::Map::getFloat("Inventory_DirectionalDiffuseG")));
            const float diffuseB = std::min(1.45f, std::max(0.9f, 1.2f * Fallback::Map::getFloat("Inventory_DirectionalDiffuseB")));
            const float ambientR = std::min(0.6f, std::max(0.34f, Fallback::Map::getFloat("Inventory_DirectionalAmbientR")));
            const float ambientG = std::min(0.6f, std::max(0.34f, Fallback::Map::getFloat("Inventory_DirectionalAmbientG")));
            const float ambientB = std::min(0.6f, std::max(0.34f, Fallback::Map::getFloat("Inventory_DirectionalAmbientB")));
            const float azimuth = osg::DegreesToRadians(Fallback::Map::getFloat("Inventory_DirectionalRotationX"));
            const float altitude = osg::DegreesToRadians(Fallback::Map::getFloat("Inventory_DirectionalRotationY"));
            light->setLightNum(0);
            light->setPosition(osg::Vec4(-std::cos(azimuth) * std::sin(altitude),
                std::sin(azimuth) * std::sin(altitude), std::cos(altitude), 0.f));
            light->setDiffuse(osg::Vec4(diffuseR, diffuseG, diffuseB, 1.f));
            const osg::Vec4 ambient(ambientR, ambientG, ambientB, 1.f);
            if (MWBase::Environment::get().getResourceSystem()->getSceneManager()->getForceShaders())
            {
                lightModel->setAmbientIntensity(ambient);
                light->setAmbient(osg::Vec4(0.f, 0.f, 0.f, 1.f));
            }
            else
                light->setAmbient(ambient);
            light->setSpecular(osg::Vec4(0.f, 0.f, 0.f, 0.f));
            light->setConstantAttenuation(1.f);
            light->setLinearAttenuation(0.f);
            light->setQuadraticAttenuation(0.f);
            mLightManager->setSunlight(light);

            osg::ref_ptr<osg::LightSource> lamp = new osg::LightSource;
            lamp->setLight(light);
            lamp->setStateSetModes(*stateset, osg::StateAttribute::ON);
            mLightManager->addChild(lamp);

            mSpin=new osg::PositionAttitudeTransform;
            mCenter=new osg::MatrixTransform;
            mSpin->addChild(mCenter.get());
            mLightManager->addChild(mSpin.get());
            mCamera->addChild(mLightManager.get());
            mRoot->addChild(mCamera.get());
            mGuiTexture.reset(new osgMyGUI::OSGTexture(mTexture.get()));
        }
        ~FurniturePreview()
        {
            // Do not detach/free a live RTT graph inline. The render thread may
            // still hold raw StateSet/Drawable pointers from the previous cull.
            // Hand ownership to the delayed-retire queue instead.
            retireFurniturePreview(mRoot, mCamera, std::move(mGuiTexture));
            mRoot = nullptr;
            mCamera = nullptr;
            mTexture = nullptr;
            mLightManager = nullptr;
            mSpin = nullptr;
            mCenter = nullptr;
        }
        MyGUI::ITexture* texture() const {return mGuiTexture.get();}
        void visible(bool yes) { if (mCamera) mCamera->setNodeMask(yes?MWRender::Mask_RenderToTexture:0); }
        bool show(const std::string& refId)
        {
            if (!mCenter || refId.empty()) return false;
            const MWWorld::ESMStore& store=MWBase::Environment::get().getWorld()->getStore();
            std::string model;
            if (const auto* record=store.get<ESM::Static>().search(refId)) model=record->mModel;
            else if (const auto* record=store.get<ESM::Container>().search(refId)) model=record->mModel;
            else if (const auto* record=store.get<ESM::Activator>().search(refId)) model=record->mModel;
            else if (const auto* record=store.get<ESM::Light>().search(refId)) model=record->mModel;
            else if (const auto* record=store.get<ESM::Miscellaneous>().search(refId)) model=record->mModel;
            if (model.empty())return false;
            try
            {
                // The source ID must exist in the *local loaded* ESM/ESP store.
                // Never accept an arbitrary mesh path from a server message.
                // TES3 records can store either a relative mesh or a full
                // meshes\ path. Do not accidentally request meshes\meshes\...
                std::replace(model.begin(),model.end(),'/','\\');
                if (lower(model).compare(0,7,"meshes\\")==0) model.erase(0,7);
                auto* sceneManager=MWBase::Environment::get().getResourceSystem()->getSceneManager();
                osg::ref_ptr<osg::Node> node=sceneManager->getInstance("meshes\\"+model);
                if (!node) return false;
                // getInstance() may hand us the ordinary world shader variant.
                // CharacterPreview explicitly rebuilds shaders for its translucent
                // RTT target before applying the alpha visitor; without that step
                // shader-lit furniture can render as a completely black/empty box.
                sceneManager->recreateShaders(node, "objects", true);
                FurniturePreviewBlendVisitor blendVisitor;
                node->accept(blendVisitor);
                const osg::BoundingSphere bound=node->getBound();
                if (!bound.valid()) return false;
                const float radius=std::max(8.f,bound.radius());
                mCenter->removeChildren(0,mCenter->getNumChildren());
                mCenter->setMatrix(osg::Matrix::translate(-bound.center()));
                mCenter->addChild(node.get());
                mCenter->dirtyBound();
                mSpin->dirtyBound();
                mLightManager->dirtyBound();
                mRadius=radius;mYaw=0.f;mPitch=0.f;mZoom=3.1f;
                updateRotation();
                recamera();
                visible(true);
                return true;
            }
            catch(const std::exception& e)
            {
                Log(Debug::Warning)<<"Arena furniture preview: "<<refId<<": "<<e.what();
                return false;
            }
        }
        void rotate(float yawDelta, float pitchDelta)
        {
            mYaw += yawDelta;
            mPitch += pitchDelta;
            updateRotation();
        }
        void zoom(int delta)
        {
            mZoom=std::max(1.25f,std::min(7.f,mZoom-(delta>0?0.25f:-0.25f)));
            recamera();
        }
    private:
        void updateRotation()
        {
            if (!mSpin) return;
            // Horizontal drag rotates around the model's vertical axis; vertical
            // drag pitches it around the local X axis. Keeping both angles
            // independent makes the preview freely inspectable from above/below
            // without changing the world object or sending any network packet.
            const osg::Quat yaw(mYaw, osg::Vec3f(0.f, 0.f, 1.f));
            const osg::Quat pitch(mPitch, osg::Vec3f(1.f, 0.f, 0.f));
            mSpin->setAttitude(yaw * pitch);
        }
        void recamera()
        {
            if(mCamera) mCamera->setViewMatrixAsLookAt(
                osg::Vec3f(0.f,-mRadius*mZoom,mRadius*0.65f),
                osg::Vec3f(0.f,0.f,0.f),osg::Vec3f(0.f,0.f,1.f));
        }
        osg::ref_ptr<osg::Group> mRoot;
        osg::ref_ptr<osg::Camera> mCamera;
        osg::ref_ptr<osg::Texture2D> mTexture;
        osg::ref_ptr<SceneUtil::LightManager> mLightManager;
        osg::ref_ptr<osg::PositionAttitudeTransform> mSpin;
        osg::ref_ptr<osg::MatrixTransform> mCenter;
        std::unique_ptr<MyGUI::ITexture> mGuiTexture;
        float mRadius=60.f,mYaw=0.f,mPitch=0.f,mZoom=3.1f;
    };

    void FurnitureStoreWindow::collectRetiredPreviews()
    {
        collectFurniturePreviewGarbage();
    }

    FurnitureStoreWindow::FurnitureStoreWindow()
        : MWGui::WindowModal("arenamp_furniture.layout")
    {
        getWidget(mCategoryList,"CategoryList");
        getWidget(mStyleButton,"StyleButton");
        getWidget(mItemList,"ItemList");
        getWidget(mSearch,"SearchEdit");
        getWidget(mPreviewImage,"PreviewImage");
        getWidget(mPreviewCaption,"PreviewCaption");
        getWidget(mTitle,"ItemTitle");
        getWidget(mPrice,"ItemPrice");
        getWidget(mNotice,"NoticeText");
        getWidget(mPageLabel,"PageText");
        getWidget(mCategoryLabel,"CategoryLabel");

        getWidget(mPrimary,"PrimaryButton");
        getWidget(mSecondary,"SecondaryButton");
        getWidget(mPrevious,"PreviousButton");
        getWidget(mNext,"NextButton");
        mCategoryList->eventListChangePosition += MyGUI::newDelegate(this,&FurnitureStoreWindow::onCategory);
        mCategoryList->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onCategoryClick);
        mStyleButton->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onStyle);
        getWidget(mAdminEdit,"AdminEditButton");
        getWidget(mAdminCapture,"AdminCaptureButton");
        getWidget(mAdminRefresh,"AdminRefreshButton");
        // Keep the editor on its own modal layer, with an opaque background and
        // the same base dimensions as the purchase catalogue.
        mAdminPanel=MyGUI::Gui::getInstance().createWidget<MyGUI::Widget>(
            "FullBlackBG",0,0,700,416,MyGUI::Align::Default,"Popup","ArenaFurnitureValueEditor");
        mAdminPanel->setAlpha(1.f);
        mAdminPanel->setNeedMouseFocus(true);
        auto label=[this](const std::string& name,int y) {
            return mAdminPanel->createWidget<MyGUI::TextBox>(
                "SandText",16,y,668,24,MyGUI::Align::Default,name);
        };
        label("AdminEditTitle",12)->changeWidgetSkin("SandBrightText");
        label("AdminCategoryLabel",48);
        mAdminCategory=mAdminPanel->createWidget<MyGUI::Button>(
            "MW_Button",16,74,668,30,MyGUI::Align::Default,"AdminCategoryEdit");
        label("AdminRuLabel",116);
        mAdminNameRu=mAdminPanel->createWidget<MyGUI::EditBox>(
            "MW_TextBoxEditWithBorder",16,142,668,30,MyGUI::Align::Default,"AdminNameRuEdit");
        label("AdminEnLabel",184);
        mAdminNameEn=mAdminPanel->createWidget<MyGUI::EditBox>(
            "MW_TextBoxEditWithBorder",16,210,668,30,MyGUI::Align::Default,"AdminNameEnEdit");
        label("AdminPriceLabel",252);
        mAdminPrice=mAdminPanel->createWidget<MyGUI::EditBox>(
            "MW_TextBoxEditWithBorder",16,278,668,30,MyGUI::Align::Default,"AdminPriceEdit");
        label("AdminHintLabel",320);
        MyGUI::Button* save=mAdminPanel->createWidget<MyGUI::Button>(
            "MW_Button",16,370,326,30,MyGUI::Align::Default,"AdminSaveButton");
        MyGUI::Button* cancel=mAdminPanel->createWidget<MyGUI::Button>(
            "MW_Button",358,370,326,30,MyGUI::Align::Default,"AdminCancelButton");
        for (std::size_t i=0;i<mAdminPanel->getChildCount();++i)
        {
            MyGUI::Widget* child=mAdminPanel->getChildAt(i);
            mAdminGeometry.emplace_back(child,child->getCoord());
        }
        mAdminPanel->setVisible(false);
        mAdminCategory->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onAdminCategory);
        mAdminEdit->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onAdminEdit);
        mAdminCapture->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onAdminCapture);
        mAdminRefresh->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onAdminRefresh);
        save->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onAdminSave);
        cancel->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onAdminCancel);
        mItemList->eventListChangePosition += MyGUI::newDelegate(this,&FurnitureStoreWindow::onItem);
        mSearch->eventEditTextChange += MyGUI::newDelegate(this,&FurnitureStoreWindow::onSearch);
        mPreviewImage->setNeedMouseFocus(true);
        mPreviewImage->eventMouseButtonPressed += MyGUI::newDelegate(this,&FurnitureStoreWindow::onPreviewPressed);
        mPreviewImage->eventMouseDrag += MyGUI::newDelegate(this,&FurnitureStoreWindow::onPreviewDrag);
        mPreviewImage->eventMouseWheel += MyGUI::newDelegate(this,&FurnitureStoreWindow::onPreviewWheel);
        mPrevious->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onPrevious);
        mNext->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onNext);
        mPrimary->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onPrimary);
        mSecondary->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onSecondary);
        MyGUI::Button* button=nullptr;
        getWidget(button,"ShopTab"); button->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onTabShop);
        getWidget(button,"InventoryTab"); button->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onTabInventory);
        getWidget(button,"PlacedTab"); button->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onTabPlaced);
        getWidget(button,"CloseButton"); button->eventMouseButtonClick += MyGUI::newDelegate(this,&FurnitureStoreWindow::onClosePressed);
        cacheGeometry(mMainWidget);
        updateSelection();
    }

    FurnitureStoreWindow::~FurnitureStoreWindow()
    {
        mPreviewImage->setRenderItemTexture(nullptr);
        mPreview.reset();
        if (mEditorModalActive)
            MyGUI::InputManager::getInstance().removeWidgetModal(mAdminPanel);
        if (mAdminPanel) MyGUI::Gui::getInstance().destroyWidget(mAdminPanel);
    }

    void FurnitureStoreWindow::cacheGeometry(MyGUI::Widget* widget)
    {
        mGeometry.emplace_back(widget,widget->getCoord());
        for (std::size_t i=0;i<widget->getChildCount();++i)
            cacheGeometry(widget->getChildAt(i));
    }

    void FurnitureStoreWindow::showInPlayerMenu(MyGUI::Widget* pane)
    {
        if (!pane) return;
        mEmbeddedPane=pane;
        mLastEmbeddedCoord=MyGUI::IntCoord(-1,-1,-1,-1);
        mEmbeddedSiblings.clear();
        // The Player Menu already has its own red X and tab navigation. Hiding
        // this second Close button gives the catalogue a cleaner embedded header
        // and avoids returning to the retired intermediate decoration page.
        getWidget("CloseButton")->setVisible(false);
        // Preserve visibility so leaving the shop returns to the same category.
        for (std::size_t i=0;i<pane->getChildCount();++i)
            mEmbeddedSiblings.emplace_back(pane->getChildAt(i),pane->getChildAt(i)->getVisible());
        hideEmbeddedSiblings();
    }

    void FurnitureStoreWindow::hideEmbeddedSiblings()
    {
        for (const auto& entry:mEmbeddedSiblings)
            if (entry.first) entry.first->setVisible(false);
    }

    void FurnitureStoreWindow::syncPlayerMenu(bool visible)
    {
        if (!mEmbeddedPane) return;
        if (!visible || !mEmbeddedPane->getVisible())
        {
            onClosePressed(nullptr);
            return;
        }
        hideEmbeddedSiblings();
        const MyGUI::IntCoord current=mEmbeddedPane->getAbsoluteCoord();
        if (current.left!=mLastEmbeddedCoord.left || current.top!=mLastEmbeddedCoord.top ||
            current.width!=mLastEmbeddedCoord.width || current.height!=mLastEmbeddedCoord.height)
            resizeToViewport();
    }

    void FurnitureStoreWindow::resizeToViewport()
    {
        const MyGUI::IntSize screen=MyGUI::RenderManager::getInstance().getViewSize();
        const MyGUI::IntCoord pane=mEmbeddedPane ? mEmbeddedPane->getAbsoluteCoord() : MyGUI::IntCoord();
        if (mEmbeddedPane) mLastEmbeddedCoord=pane;
        const int availableWidth=mEmbeddedPane ? pane.width : screen.width-12;
        const int availableHeight=mEmbeddedPane ? pane.height : screen.height-12;
        const float scale=std::max(0.15f,std::min(1.f,std::min(availableWidth/700.f,availableHeight/416.f)));
        for (std::size_t i=1;i<mGeometry.size();++i)
        {
            const MyGUI::IntCoord& c=mGeometry[i].second;
            mGeometry[i].first->setCoord(static_cast<int>(c.left*scale),static_cast<int>(c.top*scale),
                std::max(1,static_cast<int>(c.width*scale)),std::max(1,static_cast<int>(c.height*scale)));
        }
        const int w=static_cast<int>(700*scale),h=static_cast<int>(416*scale);
        mMainWidget->setCoord(mEmbeddedPane?pane.left+std::max(0,(pane.width-w)/2):std::max(0,(screen.width-w)/2),
            mEmbeddedPane?pane.top+std::max(0,(pane.height-h)/2):std::max(0,(screen.height-h)/2),w,h);
        if (mAdminPanel)
        {
            // Match the catalogue even when it is embedded or the viewport rotates.
            mAdminPanel->setCoord(mMainWidget->getAbsoluteCoord());
            for (const auto& entry:mAdminGeometry)
            {
                const MyGUI::IntCoord& c=entry.second;
                entry.first->setCoord(static_cast<int>(c.left*scale),static_cast<int>(c.top*scale),
                    std::max(1,static_cast<int>(c.width*scale)),std::max(1,static_cast<int>(c.height*scale)));
            }
        }
    }

    void FurnitureStoreWindow::localize()
    {
        const auto caption=[this](MyGUI::Widget* root,const std::string& name,const std::string& key) {
            // Widgets loaded through MWGui::Layout are internally prefixed by
            // MyGUI. root->findWidget("ShopTab") therefore misses them and left
            // the English XML captions visible. Use Layout::getWidget for the
            // catalogue hierarchy; the value editor is created dynamically and
            // keeps literal names.
            MyGUI::Widget* widget=nullptr;
            if (root==mMainWidget)
            {
                try { widget=getWidget(name); } catch (...) { widget=nullptr; }
            }
            else if (root) widget=root->findWidget(name);
            if (widget)
            {
                const std::string translated=furnitureText(key);
                if (MyGUI::Button* button=widget->castType<MyGUI::Button>(false))
                    button->setCaption(translated);
                else if (MyGUI::TextBox* label=widget->castType<MyGUI::TextBox>(false))
                    label->setCaption(translated);
            }
        };
        caption(mMainWidget,"FurnitureTitle","title");
        caption(mMainWidget,"CloseButton","close");
        caption(mMainWidget,"ShopTab","shop");
        caption(mMainWidget,"InventoryTab","inventory");
        caption(mMainWidget,"PlacedTab","placed");
        caption(mMainWidget,"PreviousButton","previous");
        caption(mMainWidget,"NextButton","next");
        mCategoryLabel->setCaption(furnitureText("categories"));
        updateStyle();
        caption(mMainWidget,"AdminEditButton","admin.edit");
        caption(mMainWidget,"AdminCaptureButton","admin.capture");
        caption(mMainWidget,"AdminRefreshButton","admin.refresh");
        caption(mAdminPanel,"AdminEditTitle","admin.title");
        caption(mAdminPanel,"AdminCategoryLabel","admin.category");
        caption(mAdminPanel,"AdminRuLabel","admin.name.ru");
        caption(mAdminPanel,"AdminEnLabel","admin.name.en");
        caption(mAdminPanel,"AdminPriceLabel","admin.price");
        caption(mAdminPanel,"AdminHintLabel","admin.hint");
        caption(mAdminPanel,"AdminSaveButton","admin.save");
        caption(mAdminPanel,"AdminCancelButton","admin.cancel");
        if (mSelected.empty()) mNotice->setCaption(furnitureText("select"));
        rebuildCategories();updateStyle();rebuildItems();
    }

    void FurnitureStoreWindow::onOpen()
    {
        localize();
        getWidget("CloseButton")->setVisible(!mEmbeddedPane);
        resizeToViewport();
        if (!mEmbeddedPane) MWGui::WindowModal::onOpen();
        if(mPreview)mPreview->visible(true);
    }

    void FurnitureStoreWindow::onClose()
    {
        onAdminCancel(nullptr);
        if(mPreview)mPreview->visible(false);
        if (!mEmbeddedPane) MWGui::WindowModal::onClose();
    }

    bool FurnitureStoreWindow::exit()
    {
        onClosePressed(nullptr);
        return false;
    }

    void FurnitureStoreWindow::reset()
    {
        mCategories.clear();mCategoryKeys.clear();mPageItems.clear();mStock.clear();mPlaced.clear();mItemNames.clear();
        mVisibleIds.clear();mCategory.clear();mSubcategory.clear();mSelected.clear();
        mPage=1;mPages=1;mTab=Shop;mStyleIndex=0;mIsAdmin=false;mEditorCategories.clear();mEditorCategoryIndex=0;
        mAdminPanel->setVisible(false);mAdminEdit->setVisible(false);
        mAdminCapture->setVisible(false);mAdminRefresh->setVisible(false);
        if (isVisible()) onClosePressed(nullptr);
    }

    void FurnitureStoreWindow::send(const std::string& verb, const std::string& arg,
        int page,const std::string& subcategory) const
    {
        ArenaHousing::get().sendFurnitureAction(verb,arg,page,subcategory);
    }

    void FurnitureStoreWindow::requestPage()
    {
        if (mTab==Shop && !mCategory.empty()) send("CATEGORY",mCategory,mPage,mSubcategory);
    }

    std::string FurnitureStoreWindow::categoryCaption(const std::string& category) const
    {
        const auto key = mCategoryKeys.find(category);
        return key == mCategoryKeys.end() || key->second.empty()
            ? furnitureGroup(category) : furnitureText("group." + key->second);
    }

    void FurnitureStoreWindow::rebuildCategories()
    {
        mCategoryList->removeAllItems();
        if (mTab!=Shop) return;
        for (const auto& category:mCategories) mCategoryList->addItem(categoryCaption(category));
    }

    void FurnitureStoreWindow::updateStyle()
    {
        static const char* keys[]={"all","style.nordic","style.dunmer","style.imperial","style.other"};
        mStyleButton->setCaption(furnitureText(keys[mStyleIndex]));
        mStyleButton->setVisible(mTab==Shop);
    }

    void FurnitureStoreWindow::rebuildItems()
    {
        mItemList->removeAllItems();mVisibleIds.clear();
        const std::string filter=lower(mSearch->getOnlyText().asUTF8());
        if (mTab==Shop)
        {
            for (const Item& item:mPageItems)
            {
                if (!filter.empty() && lower(item.name).find(filter)==std::string::npos &&
                    lower(item.id).find(filter)==std::string::npos &&
                    lower(item.nameRu).find(filter)==std::string::npos &&
                    lower(item.nameEn).find(filter)==std::string::npos) continue;
                mVisibleIds.push_back(item.id);
                const std::string unavailable=hasLocalFurnitureRecord(item.id)
                    ? std::string() : " ["+furnitureText("unavailable")+"]";
                mItemList->addItem(item.name+"   ["+std::to_string(item.price)+" "+furnitureText("gold.short")+"]"+unavailable);
            }
            mPageLabel->setCaption(std::to_string(mPage)+" / "+std::to_string(mPages));
        }
        else if (mTab==Inventory)
        {
            for (const auto& entry:mStock)
            {
                if (entry.second<=0) continue;
                if (!filter.empty() && lower(entry.first).find(filter)==std::string::npos &&
                    lower(mItemNames.count(entry.first)?mItemNames[entry.first]:entry.first).find(filter)==std::string::npos) continue;
                mVisibleIds.push_back(entry.first);
                mItemList->addItem((mItemNames.count(entry.first)?mItemNames[entry.first]:entry.first)+"  x"+std::to_string(entry.second)
                    +(hasLocalFurnitureRecord(entry.first)?"":" ["+furnitureText("unavailable")+"]"));
            }
            mPageLabel->setCaption(furnitureText("inventory"));
        }
        else
        {
            for (const auto& entry:mPlaced)
            {
                if (!filter.empty() && lower(entry.second).find(filter)==std::string::npos) continue;
                mVisibleIds.push_back(entry.first);
                mItemList->addItem((mItemNames.count(entry.second)?mItemNames[entry.second]:entry.second)+" ["+entry.first+"]");
            }
            mPageLabel->setCaption(furnitureText("placed"));
        }
        mPrevious->setEnabled(mTab==Shop && mPage>1);
        mNext->setEnabled(mTab==Shop && mPage<mPages);
        updateSelection();
    }

    void FurnitureStoreWindow::updateSelection()
    {
        mSelected.clear();
        mPreviousDragX=0;
        const std::size_t index=mItemList->getIndexSelected();
        if (index<mVisibleIds.size()) mSelected=mVisibleIds[index];
        mPreviewImage->setRenderItemTexture(nullptr);
        mPreviewCaption->setCaption(furnitureText("select.preview"));
        mTitle->setCaption("");mPrice->setCaption("");
        mPrimary->setEnabled(!mSelected.empty());
        mSecondary->setEnabled(!mSelected.empty() && mTab==Inventory);
        mPrimary->setCaption(furnitureText(mTab==Shop?"buy":mTab==Inventory?"place":"take"));
        mAdminEdit->setVisible(mIsAdmin && mTab==Shop && !mSelected.empty());
        mAdminCapture->setVisible(mIsAdmin);
        mAdminRefresh->setVisible(mIsAdmin);
        mSecondary->setCaption(furnitureText("sell"));
        if (mSelected.empty()) return;
        if (mTab==Shop)
        {
            for (const Item& item:mPageItems) if (item.id==mSelected)
            {
                mTitle->setCaption(item.name);mPrice->setCaption(std::to_string(item.price)+" "+furnitureText("gold"));
                mPreviewCaption->setCaption(item.name);break;
            }
        }
        else
        {
            mTitle->setCaption(mTab==Inventory?(mItemNames.count(mSelected)?mItemNames[mSelected]:mSelected):
                (mItemNames.count(mPlaced[mSelected])?mItemNames[mPlaced[mSelected]]:mPlaced[mSelected]));
            mPreviewCaption->setCaption(mTitle->getCaption());
            if (mTab==Inventory) mPrice->setCaption(furnitureText("owned")+": "+std::to_string(mStock[mSelected]));
        }
        // Display the *loaded* model in a separate OSG RTT camera, never by
        // spawning a visible or networked world object.
        const std::string refId=mTab==Placed?mPlaced[mSelected]:mSelected;
        if (!hasLocalFurnitureRecord(refId))
        {
            mPrimary->setEnabled(false);
            mSecondary->setEnabled(false);
            mPreviewCaption->setCaption(furnitureText("missing.model"));
            return;
        }
        // Never create an RTT camera during the login/loading gap. On Android
        // the render root and player cell can exist on different lifecycle
        // ticks; constructing a preview in that gap can crash the GL thread.
        const MWWorld::Ptr player = MWBase::Environment::get().getWorld()->getPlayerPtr();
        if (player.isEmpty() || player.getCell() == nullptr)
        {
            mPreviewCaption->setCaption(furnitureText("preview.unavailable"));
            return;
        }
        if(!mPreview)mPreview.reset(new FurniturePreview());
        if(mPreview->show(refId))
        {
            mPreviewImage->setRenderItemTexture(mPreview->texture());
            mPreviewImage->getSubWidgetMain()->_setUVSet(MyGUI::FloatRect(0.f,0.f,1.f,1.f));
            // Keep the preview clean: the model name is enough; no rotate/zoom help text.
            mPreviewCaption->setCaption(mTitle->getCaption());
        }
        else mPreviewCaption->setCaption(furnitureText("preview.unavailable"));
    }

    void FurnitureStoreWindow::onPreviewPressed(MyGUI::Widget*,int x,int y,MyGUI::MouseButton)
    {
        mPreviousDragX=x;
        mPreviousDragY=y;
    }
    void FurnitureStoreWindow::onPreviewDrag(MyGUI::Widget*,int x,int y,MyGUI::MouseButton button)
    {
        if (button!=MyGUI::MouseButton::Left || !mPreview) return;
        // Local-only two-axis rotation: horizontal = yaw, vertical = pitch.
        const int previousX=mPreviousDragX;
        const int previousY=mPreviousDragY;
        mPreviousDragX=x;
        mPreviousDragY=y;
        mPreview->rotate((x-previousX)*0.012f, (y-previousY)*0.012f);
    }
    void FurnitureStoreWindow::onPreviewWheel(MyGUI::Widget*,int rel)
    {
        if(mPreview && rel!=0)mPreview->zoom(rel);
    }

    void FurnitureStoreWindow::receive(const std::string& payload)
    {
        const std::vector<std::string> f=fields(payload);
        if (f.empty()) return;
        const std::string& cmd=f[0];
        if (cmd=="FS_BEGIN")
        {
            mCategories.clear();mCategoryKeys.clear();mPageItems.clear();mItemNames.clear();mCategory.clear();mSubcategory="*";mStyleIndex=0;mTab=Shop;
            onAdminCancel(nullptr);mIsAdmin=false;mEditorCategories.clear();
            if (mEmbeddedPane)
            {
                // The parent Player Menu already owns GUI focus/input mode.
                setVisible(true);
                localize();
                resizeToViewport();
                if (mPreview) mPreview->visible(true);
            }
            else if (!isVisible())
            {
                auto* manager=MWBase::Environment::get().getWindowManager();
                const MWGui::GuiMode mode=static_cast<MWGui::GuiMode>(GUIController::GM_ARENAMP_Furniture);
                if (!manager->containsMode(mode)) manager->pushGuiMode(mode);
            }
        }
        else if (cmd=="FS_SELECT" && f.size()>=2)
        {
            for (std::size_t i=0;i<mVisibleIds.size();++i)
                if (mVisibleIds[i]==f[1]) {mItemList->setIndexSelected(i);updateSelection();break;}
        }
        else if (cmd=="FS_CATEGORY" && f.size()>=2)
        {
            if (std::find(mCategories.begin(),mCategories.end(),f[1])==mCategories.end()) mCategories.push_back(f[1]);
            if (f.size() >= 3) mCategoryKeys[f[1]] = f[2];
        }
        else if (cmd=="FS_SUBCAT" && f.size()>=3)
        {
            // Retired in H015: a single style-filter button replaces subcategory lists.
        }
        else if (cmd=="FS_END")
        {
            if (!mCategories.empty() && mCategory.empty()) mCategory=mCategories.front();
            rebuildCategories();updateStyle();rebuildItems();
        }
        else if (cmd=="FS_ITEMS_BEGIN" && f.size()>=4)
        {
            mCategory=f[1];mPage=std::max(1,std::atoi(f[2].c_str()));
            mPages=std::max(1,std::atoi(f[3].c_str()));
            mSubcategory=f.size()>=5?f[4]:"*";mPageItems.clear();
        }
        else if (cmd=="FS_ITEM" && f.size()>=6)
        {
            Item item;item.id=f[1];item.name=f[2];item.category=f[3];item.subcategory=f[4];
            item.price=std::max(0,std::atoi(f[5].c_str()));
            item.nameRu=f.size()>=7?f[6]:item.name;
            item.nameEn=f.size()>=8?f[7]:item.name;
            const std::string& localized = MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru"
                ? item.nameRu : item.nameEn;
            if (!localized.empty()) item.name = localized;
            mItemNames[item.id]=item.name;mPageItems.push_back(item);
        }
        else if (cmd=="FS_ITEMS_END") rebuildItems();
        else if (cmd=="FS_EDITOR_CATEGORY" && f.size()>=2)
        {
            if (std::find(mEditorCategories.begin(),mEditorCategories.end(),f[1])==mEditorCategories.end())
                mEditorCategories.push_back(f[1]);
            if (f.size() >= 3) mCategoryKeys[f[1]] = f[2];
        }
        else if (cmd=="FS_ADMIN" && f.size()>=2)
        {
            mIsAdmin=f[1]=="1";
            mAdminEdit->setVisible(mIsAdmin && mTab==Shop && !mSelected.empty());
            mAdminCapture->setVisible(mIsAdmin);
            mAdminRefresh->setVisible(mIsAdmin);
            if (!mIsAdmin) onAdminCancel(nullptr);
        }
        else if (cmd=="FS_STOCK_BEGIN") mStock.clear();
        else if (cmd=="FS_STOCK" && f.size()>=3)
        {
            mStock[f[1]]=std::max(0,std::atoi(f[2].c_str()));
            if (f.size()>=4 && !f[3].empty()) mItemNames[f[1]]=f[3];
            const std::size_t localized = MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru" ? 4 : 5;
            if (f.size()>localized && !f[localized].empty()) mItemNames[f[1]]=f[localized];
        }
        else if (cmd=="FS_STOCK_END") {if (mTab==Inventory) rebuildItems();}
        else if (cmd=="FS_PLACED_BEGIN") mPlaced.clear();
        else if (cmd=="FS_PLACED" && f.size()>=3)
        {
            mPlaced[f[1]]=f[2];
            if (f.size()>=4 && !f[3].empty()) mItemNames[f[2]]=f[3];
            const std::size_t localized = MWBase::Environment::get().getWindowManager()->getArenaLanguage() == "ru" ? 4 : 5;
            if (f.size()>localized && !f[localized].empty()) mItemNames[f[2]]=f[localized];
        }
        else if (cmd=="FS_PLACED_END") {if (mTab==Placed) rebuildItems();}
        else if (cmd=="FS_NOTICE" && f.size()>=2) mNotice->setCaption(f[1]);
    }

    void FurnitureStoreWindow::onTabShop(MyGUI::Widget*)
    {
        mTab=Shop;rebuildCategories();updateStyle();requestPage();rebuildItems();
    }
    void FurnitureStoreWindow::onTabInventory(MyGUI::Widget*)
    {
        mTab=Inventory;rebuildCategories();updateStyle();send("INVENTORY");rebuildItems();
    }
    void FurnitureStoreWindow::onTabPlaced(MyGUI::Widget*)
    {
        mTab=Placed;rebuildCategories();updateStyle();send("PLACED");rebuildItems();
    }
    void FurnitureStoreWindow::onClosePressed(MyGUI::Widget*)
    {
        auto* manager=MWBase::Environment::get().getWindowManager();
        const MWGui::GuiMode mode=static_cast<MWGui::GuiMode>(GUIController::GM_ARENAMP_Furniture);
        if (manager->containsMode(mode)) manager->removeGuiMode(mode);
        setVisible(false);
        if (mPreview) mPreview->visible(false);
        onAdminCancel(nullptr);
        for (const auto& entry:mEmbeddedSiblings)
            if (entry.first) entry.first->setVisible(entry.second);
        mEmbeddedSiblings.clear();
        mEmbeddedPane=nullptr;
        getWidget("CloseButton")->setVisible(true);
    }
    void FurnitureStoreWindow::selectCategory(std::size_t index)
    {
        if (mTab!=Shop || index>=mCategories.size()) return;
        const std::string& category=mCategories[index];
        // eventListChangePosition and the ordinary click may both fire for one
        // physical tap. Apply the category once, while keeping keyboard changes.
        if (mCategory==category && !mPageItems.empty()) return;
        mCategory=category;
        mPage=1;
        requestPage();
    }
    void FurnitureStoreWindow::onCategory(MyGUI::ListBox*,std::size_t index)
    {
        selectCategory(index);
    }
    void FurnitureStoreWindow::onCategoryClick(MyGUI::Widget*)
    {
        // A normal single click must immediately apply the selected category.
        // ListBox's change-position event can require a second click after focus
        // changes on some MyGUI/Android input paths, so use the click itself.
        selectCategory(mCategoryList->getIndexSelected());
    }
    void FurnitureStoreWindow::onStyle(MyGUI::Widget*)
    {
        if (mTab!=Shop) return;
        static const char* filters[]={"*","nordic","dunmer","imperial","other"};
        mStyleIndex=(mStyleIndex+1)%5;mSubcategory=filters[mStyleIndex];
        mPage=1;updateStyle();requestPage();
    }
    void FurnitureStoreWindow::onAdminEdit(MyGUI::Widget*)
    {
        if (!mIsAdmin || mTab!=Shop || mSelected.empty()) return;
        for (const Item& item:mPageItems) if(item.id==mSelected)
        {
            auto choice=std::find(mEditorCategories.begin(),mEditorCategories.end(),item.category);
            if (choice==mEditorCategories.end() || mEditorCategories.empty()) return;
            mEditorCategoryIndex=static_cast<std::size_t>(std::distance(mEditorCategories.begin(),choice));
            mAdminCategory->setCaption(categoryCaption(mEditorCategories[mEditorCategoryIndex]));
            mAdminNameRu->setOnlyText(item.nameRu);
            mAdminNameEn->setOnlyText(item.nameEn);
            mAdminPrice->setOnlyText(std::to_string(item.price));
            resizeToViewport();
            mMainWidget->setEnabled(false);
            mAdminPanel->setVisible(true);
            // The editor is a separate top-level widget. Register it as the
            // active MyGUI modal, otherwise the shop's modal would block it.
            if (!mEditorModalActive)
            {
                MyGUI::InputManager::getInstance().addWidgetModal(mAdminPanel);
                mEditorModalActive=true;
            }
            MWBase::Environment::get().getWindowManager()->setKeyFocusWidget(mAdminNameRu);
            break;
        }
    }
    void FurnitureStoreWindow::onAdminCapture(MyGUI::Widget*)
    {
        if (!mIsAdmin) return;
        if (GUIController* gui=Main::get().getGUIController())
            gui->beginFurnitureCapture();
        else
            ArenaHousing::get().armFurnitureCapture();
    }

    void FurnitureStoreWindow::onAdminRefresh(MyGUI::Widget*)
    {
        if (!mIsAdmin) return;
        send("REFRESH","_");
    }

    void FurnitureStoreWindow::onAdminCategory(MyGUI::Widget*)
    {
        if (mEditorCategories.empty())return;
        mEditorCategoryIndex=(mEditorCategoryIndex+1)%mEditorCategories.size();
        mAdminCategory->setCaption(categoryCaption(mEditorCategories[mEditorCategoryIndex]));
    }
    void FurnitureStoreWindow::onAdminCancel(MyGUI::Widget*)
    {
        if (mEditorModalActive)
        {
            MyGUI::InputManager::getInstance().removeWidgetModal(mAdminPanel);
            mEditorModalActive=false;
        }
        mAdminPanel->setVisible(false);
        mMainWidget->setEnabled(true);
    }
    void FurnitureStoreWindow::onAdminSave(MyGUI::Widget*)
    {
        if (!mIsAdmin || mSelected.empty()) return;
        if (mEditorCategories.empty() || mEditorCategoryIndex>=mEditorCategories.size())return;
        const std::string category=mEditorCategories[mEditorCategoryIndex];
        const std::string price=mAdminPrice->getOnlyText().asUTF8();
        const std::string ru=mAdminNameRu->getOnlyText().asUTF8();
        const std::string en=mAdminNameEn->getOnlyText().asUTF8();
        // The server enforces staff rights, active record, category and price.
        if (category.find('|')!=std::string::npos || ru.find('|')!=std::string::npos ||
            en.find('|')!=std::string::npos || ru.empty() || en.empty())
        {
            if (MyGUI::Widget* hintWidget = mAdminPanel->findWidget("AdminHintLabel"))
                if (MyGUI::TextBox* hint = hintWidget->castType<MyGUI::TextBox>(false))
                    hint->setCaption(furnitureText("admin.invalid"));
            return;
        }
        send("EDIT",mSelected+"|"+category+"|"+price+"|"+ru+"|"+en);
        onAdminCancel(nullptr);
    }
    void FurnitureStoreWindow::onItem(MyGUI::ListBox*,std::size_t) {updateSelection();}
    void FurnitureStoreWindow::onSearch(MyGUI::EditBox*) {rebuildItems();}
    void FurnitureStoreWindow::onPrevious(MyGUI::Widget*) {if (mTab==Shop && mPage>1){--mPage;requestPage();}}
    void FurnitureStoreWindow::onNext(MyGUI::Widget*) {if (mTab==Shop && mPage<mPages){++mPage;requestPage();}}
    void FurnitureStoreWindow::onPrimary(MyGUI::Widget*)
    {
        if (mSelected.empty())return;
        const std::string refId=mTab==Placed?mPlaced[mSelected]:mSelected;
        if (!hasLocalFurnitureRecord(refId)) return;
        send(mTab==Shop?"BUY":mTab==Inventory?"PLACE":"TAKE",mSelected);
    }
    void FurnitureStoreWindow::onSecondary(MyGUI::Widget*)
    {
        if (mTab!=Inventory || mSelected.empty())return;
        // HS1: selling is irreversible, so ask with the item's name first.
        mPendingSell=mSelected;
        const auto named=mItemNames.find(mSelected);
        const std::string name=named!=mItemNames.end()&&!named->second.empty()?named->second:mSelected;
        std::string question=furnitureText("sell.confirm");
        const std::string::size_type slot=question.find("%s");
        if (slot!=std::string::npos)question.replace(slot,2,name);
        MWGui::ConfirmationDialog* dialog=MWBase::Environment::get().getWindowManager()->getConfirmationDialog();
        dialog->askForConfirmation(question);
        dialog->eventOkClicked.clear();
        dialog->eventOkClicked+=MyGUI::newDelegate(this,&FurnitureStoreWindow::onSellConfirmed);
        dialog->eventCancelClicked.clear();
    }
    void FurnitureStoreWindow::onSellConfirmed()
    {
        const std::string refId=mPendingSell;
        mPendingSell.clear();
        if (!refId.empty() && mTab==Inventory)send("SELL",refId);
    }
}
