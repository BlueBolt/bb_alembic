//-
//**************************************************************************/
// Copyright 2012 Autodesk, Inc. All rights reserved.
//
// Use of this software is subject to the terms of the Autodesk
// license agreement provided at the time of installation or download,
// or which otherwise accompanies this software in either electronic
// or hard copy form.
//**************************************************************************/
//+

#include <gpuCacheSubSceneOverride.h>
#include <gpuCacheShapeNode.h>
#include <gpuCacheUnitBoundingBox.h>
#include <gpuCacheFrustum.h>
#include <gpuCacheUtil.h>
#include <CacheReader.h>

#include <boost/foreach.hpp>
#include <boost/unordered_set.hpp>

#include <boost/multi_index_container.hpp>
#include <boost/multi_index/member.hpp>
#include <boost/multi_index/hashed_index.hpp>

#include <maya/MDagMessage.h>
#include <maya/MDGMessage.h>
#include <maya/MModelMessage.h>
#include <maya/MNodeMessage.h>
#include <maya/MSceneMessage.h>
#include <maya/MEventMessage.h>

#include <maya/MHWGeometryUtilities.h>
#include <maya/MAnimControl.h>
#include <maya/MDrawContext.h>
#include <maya/MFnAttribute.h>
#include <maya/MFnDagNode.h>
#include <maya/MGlobal.h>
#include <maya/MItDag.h>
#include <maya/MSelectionList.h>
#include <maya/MShaderManager.h>
#include <maya/MUserData.h>


namespace {

using namespace GPUCache;
using namespace MHWRender;

//==============================================================================
// LOCAL FUNCTIONS and CLASSES
//==============================================================================

// Guard pattern.
template<typename T>
class ScopedGuard : boost::noncopyable
{
public:
    ScopedGuard(T& value)
        : fValueRef(value), fValueBackup(value)
    {}

    ~ScopedGuard()
    {
        fValueRef = fValueBackup;
    }

private:
    T& fValueRef;
    T  fValueBackup;
};

// This class return the top-level bounding box of a sub-node hierarchy.
class BoundingBoxVisitor :  public SubNodeVisitor
{
public:
    BoundingBoxVisitor(double timeInSeconds)
      : fTimeInSeconds(timeInSeconds)
    {}
    virtual ~BoundingBoxVisitor() {}

    const MBoundingBox& boundingBox() const
    { return fBoundingBox; }

    virtual void visit(const XformData&   xform,
                       const SubNode&     subNode)
    {
        const boost::shared_ptr<const XformSample>& sample =
            xform.getSample(fTimeInSeconds);
        if (sample) {
            fBoundingBox = sample->boundingBox();
        }
    }

    virtual void visit(const ShapeData&   shape,
                       const SubNode&     subNode)
    {
        const boost::shared_ptr<const ShapeSample>& sample =
            shape.getSample(fTimeInSeconds);
        if (sample) {
            fBoundingBox = sample->boundingBox();
        }
    }

private:
    const double fTimeInSeconds;
    MBoundingBox fBoundingBox;
};

// The user data is attached on bounding box place holder render items.
// When the bounding box place holder is drawn, a post draw callback is
// triggered to hint the shape should be read in priority.
class SubNodeUserData : public MUserData
{
public:
    SubNodeUserData(const SubNode& subNode)
        : MUserData(false /*deleteAfterUse*/),
          fSubNode(subNode)
    {}

    virtual ~SubNodeUserData()
    {}

    void hintShapeReadOrder() const
    {
        // Hint the shape read order.
        // The shape will be loaded in priority.
        GlobalReaderCache::theCache().hintShapeReadOrder(fSubNode);
    }

private:
    const SubNode& fSubNode;
};

void BoundingBoxPlaceHolderDrawCallback(MDrawContext& context,
                                        const MRenderItemList& renderItemList,
                                        MShaderInstance* shader)
{
    int numRenderItems = renderItemList.length();
    for (int i = 0; i < numRenderItems; i++) {
        const MRenderItem* renderItem = renderItemList.itemAt(i);
        if (renderItem) {
            SubNodeUserData* userData =
                dynamic_cast<SubNodeUserData*>(renderItem->customData());
            if (userData) {
                userData->hintShapeReadOrder();
            }
        }
    }
}

void WireframePreDrawCallback(MDrawContext& context,
                              const MRenderItemList& renderItemList,
                              MShaderInstance* shader)
{
    // Wireframe on Shaded: Full / Reduced / None
    const DisplayPref::WireframeOnShadedMode wireOnShadedMode =
        DisplayPref::wireframeOnShadedMode();

    // Early out if we are not drawing Reduced/None wireframe.
    if (wireOnShadedMode == DisplayPref::kWireframeOnShadedFull) {
        assert(0);  // Only Reduced/None mode has callbacks.
        return;
    }

    // Wireframe on shaded.
    unsigned int displayStyle = context.getDisplayStyle();
    if (displayStyle & (MDrawContext::kGouraudShaded | MDrawContext::kTextured)) {
        const unsigned short pattern =
            (wireOnShadedMode == DisplayPref::kWireframeOnShadedReduced)
            ? Config::kLineStippleDotted  // Reduce: dotted line
            : 0;                          // None: no wire
        static const MString sDashPattern = "dashPattern";
        shader->setParameter(sDashPattern, pattern);
    }
}

void WireframePostDrawCallback(MDrawContext& context,
                               const MRenderItemList& renderItemList,
                               MShaderInstance* shader)
{
    // Wireframe on Shaded: Full / Reduced / None
    const DisplayPref::WireframeOnShadedMode wireOnShadedMode =
        DisplayPref::wireframeOnShadedMode();

    // Early out if we are not drawing reduced wireframe.
    if (wireOnShadedMode == DisplayPref::kWireframeOnShadedFull) {
        assert(0);  // Only Reduced/None mode has callbacks.
        return;
    }

    // Restore the default pattern.
    static const MString sDashPattern = "dashPattern";
    shader->setParameter(sDashPattern, Config::kLineStippleShortDashed);
}

MShaderInstance* getWireShaderInstance()
{
    MRenderer* renderer = MRenderer::theRenderer();
    if (!renderer) return NULL;
    const MShaderManager* shaderMgr = renderer->getShaderManager();
    if (!shaderMgr) return NULL;

    return shaderMgr->getFragmentShader("mayaDashLineShader", "", false);
}

MShaderInstance* getWireShaderInstanceWithCB()
{
    MRenderer* renderer = MRenderer::theRenderer();
    if (!renderer) return NULL;
    const MShaderManager* shaderMgr = renderer->getShaderManager();
    if (!shaderMgr) return NULL;

    return shaderMgr->getFragmentShader("mayaDashLineShader", "", false,
        WireframePreDrawCallback, WireframePostDrawCallback);
}

MShaderInstance* getBoundingBoxPlaceHolderShaderInstance()
{
    MRenderer* renderer = MRenderer::theRenderer();
    if (!renderer) return NULL;
    const MShaderManager* shaderMgr = renderer->getShaderManager();
    if (!shaderMgr) return NULL;

    return shaderMgr->getFragmentShader("mayaDashLineShader", "", false,
        NULL, BoundingBoxPlaceHolderDrawCallback);
}

MShaderInstance* getDiffuseColorShaderInstance()
{
    MRenderer* renderer = MRenderer::theRenderer();
    if (!renderer) return NULL;
    const MShaderManager* shaderMgr = renderer->getShaderManager();
    if (!shaderMgr) return NULL;

    return shaderMgr->getFragmentShader("mayaLambertSurface", "outSurfaceFinal", true);
}

void releaseShaderInstance(MShaderInstance*& shader)
{
    MRenderer* renderer = MRenderer::theRenderer();
    if (!renderer) return;
    const MShaderManager* shaderMgr = renderer->getShaderManager();
    if (!shaderMgr) return;

    if (shader) {
        shaderMgr->releaseShader(shader);
        shader = NULL;
    }
}

void setDiffuseColor(MShaderInstance* shader, const MColor& diffuseColor)
{
    if (shader) {
        // Color
        const float color[3] = {diffuseColor.r, diffuseColor.g, diffuseColor.b};
        shader->setParameter("color", color);

        // Transparency
        if (diffuseColor.a < 1.0f) {
            const float oneMinusAlpha =
                (diffuseColor.a >= 0.0f) ? 1.0f - diffuseColor.a : 1.0f;
            const float transparency[3] = {oneMinusAlpha, oneMinusAlpha, oneMinusAlpha};
            shader->setParameter("transparency", transparency);
            shader->setIsTransparent(true);
        }
        else {
            shader->setIsTransparent(false);
        }

        // Diffuse
        shader->setParameter("diffuse", 1.0f);
    }
}


//==============================================================================
// CLASS ShaderInstancePtr
//==============================================================================

// This class wraps a MShaderInstance* and its template shader.
class ShaderInstancePtr
{
public:
    // Invalid shader instance.
    ShaderInstancePtr()
    {}

    // Wraps a MShaderInstance* and its template MShaderInstance*.
    ShaderInstancePtr(boost::shared_ptr<MShaderInstance> shader,
                      boost::shared_ptr<MShaderInstance> source)
        : fShader(shader), fTemplate(source)
    {}

    ~ShaderInstancePtr()
    {}

    operator bool () const
    {
        return fShader && fTemplate;
    }

    MShaderInstance* operator->() const
    {
        assert(fShader);
        return fShader.get();
    }

    MShaderInstance* get() const
    {
        assert(fShader);
        return fShader.get();
    }

    boost::shared_ptr<MShaderInstance> getShader() const
    {
        assert(fShader);
        return fShader;
    }

    boost::shared_ptr<MShaderInstance> getTemplate() const
    {
        assert(fTemplate);
        return fTemplate;
    }

    void reset()
    {
        fShader.reset();
        fTemplate.reset();
    }

private:
    boost::shared_ptr<MShaderInstance> fShader;
    boost::shared_ptr<MShaderInstance> fTemplate;
};


//==============================================================================
// CLASS ShaderTemplatePtr
//==============================================================================

// This class wraps a MShaderInstance* as a template.
class ShaderTemplatePtr
{
public:
    // Invalid shader template.
    ShaderTemplatePtr()
    {}

    // Wrap a shader instance to be used as a template.
    ShaderTemplatePtr(boost::shared_ptr<MShaderInstance> source)
        : fTemplate(source)
    {}

    ~ShaderTemplatePtr()
    {}

    operator bool () const
    {
        return (fTemplate.get() != NULL);
    }

    MShaderInstance* get() const
    {
        assert(fTemplate);
        return fTemplate.get();
    }

    boost::shared_ptr<MShaderInstance> getTemplate() const
    {
        assert(fTemplate);
        return fTemplate;
    }

    typedef void (*Deleter)(MShaderInstance*);
    ShaderInstancePtr newShaderInstance(Deleter deleter) const
    {
        assert(fTemplate);
        boost::shared_ptr<MShaderInstance> newShader;
        newShader.reset(fTemplate->clone(), std::ptr_fun(deleter));
        return ShaderInstancePtr(newShader, fTemplate);
    }

private:
    boost::shared_ptr<MShaderInstance> fTemplate;
};


//==============================================================================
// CLASS ShaderCache
//==============================================================================

// This class manages the shader templates. A shader template can be used to create
// shader instances with different parameters.
class ShaderCache : boost::noncopyable
{
public:
    static ShaderCache& getInstance()
    {
        // Singleton
        static ShaderCache sSingleton;
        return sSingleton;
    }

    typedef void (*Deleter)(MShaderInstance*);

    ShaderInstancePtr newWireShader(Deleter deleter)
    {
        // Look for a cached shader.
        MString key = "_reserved_wire_shader_";
        FragmentAndShaderTemplateCache::nth_index<0>::type::iterator it =
            fFragmentCache.get<0>().find(key);

        // Found in cache.
        if (it != fFragmentCache.get<0>().end()) {
            ShaderTemplatePtr templateShader = it->ptr.lock();
            assert(templateShader);  // no staled pointer
            return templateShader.newShaderInstance(deleter);
        }

        // Not found. Get a new shader.
        ShaderTemplatePtr templateShader =
            wrapShaderTemplate(getWireShaderInstance());
        if (templateShader) {
            // Insert into cache.
            FragmentAndShaderTemplate entry;
            entry.fragmentAndOutput = key;
            entry.shader            = templateShader.get();
            entry.ptr               = templateShader.getTemplate();
            fFragmentCache.insert(entry);

            return templateShader.newShaderInstance(deleter);
        }

        assert(0);
        return ShaderInstancePtr();
    }

    ShaderInstancePtr newWireShaderWithCB(Deleter deleter)
    {
        // Look for a cached shader.
        MString key = "_reserved_wire_shader_with_cb_";
        FragmentAndShaderTemplateCache::nth_index<0>::type::iterator it =
            fFragmentCache.get<0>().find(key);

        // Found in cache.
        if (it != fFragmentCache.get<0>().end()) {
            ShaderTemplatePtr templateShader = it->ptr.lock();
            assert(templateShader);  // no staled pointer
            return templateShader.newShaderInstance(deleter);
        }

        // Not found. Get a new shader.
        ShaderTemplatePtr templateShader =
            wrapShaderTemplate(getWireShaderInstanceWithCB());
        if (templateShader) {
            // Insert into cache.
            FragmentAndShaderTemplate entry;
            entry.fragmentAndOutput = key;
            entry.shader            = templateShader.get();
            entry.ptr               = templateShader.getTemplate();
            fFragmentCache.insert(entry);

            return templateShader.newShaderInstance(deleter);
        }

        assert(0);
        return ShaderInstancePtr();
    }

    ShaderInstancePtr newBoundingBoxPlaceHolderShader(Deleter deleter)
    {
        // Look for a cached shader.
        MString key = "_reserved_bounding_box_place_holder_shader_";
        FragmentAndShaderTemplateCache::nth_index<0>::type::iterator it =
            fFragmentCache.get<0>().find(key);

        // Found in cache.
        if (it != fFragmentCache.get<0>().end()) {
            ShaderTemplatePtr templateShader = it->ptr.lock();
            assert(templateShader);  // no staled pointer
            return templateShader.newShaderInstance(deleter);
        }

        // Not found. Get a new shader.
        ShaderTemplatePtr templateShader =
            wrapShaderTemplate(getBoundingBoxPlaceHolderShaderInstance());
        if (templateShader) {
            // Insert into cache.
            FragmentAndShaderTemplate entry;
            entry.fragmentAndOutput = key;
            entry.shader            = templateShader.get();
            entry.ptr               = templateShader.getTemplate();
            fFragmentCache.insert(entry);

            return templateShader.newShaderInstance(deleter);
        }

        assert(0);
        return ShaderInstancePtr();
    }

    ShaderInstancePtr newDiffuseColorShader(Deleter deleter)
    {
        // Look for a cached shader.
        MString key = "_reserved_diffuse_color_shader_";
        FragmentAndShaderTemplateCache::nth_index<0>::type::iterator it =
            fFragmentCache.get<0>().find(key);

        // Found in cache.
        if (it != fFragmentCache.get<0>().end()) {
            ShaderTemplatePtr templateShader = it->ptr.lock();
            assert(templateShader);  // no staled pointer
            return templateShader.newShaderInstance(deleter);
        }

        // Not found. Get a new shader.
        ShaderTemplatePtr templateShader =
            wrapShaderTemplate(getDiffuseColorShaderInstance());
        if (templateShader) {
            // Insert into cache.
            FragmentAndShaderTemplate entry;
            entry.fragmentAndOutput = key;
            entry.shader            = templateShader.get();
            entry.ptr               = templateShader.getTemplate();
            fFragmentCache.insert(entry);

            return templateShader.newShaderInstance(deleter);
        }

        assert(0);
        return ShaderInstancePtr();
    }

    ShaderInstancePtr newFragmentShader(const MString& fragmentName,
                                        const MString& outputStructName,
                                        Deleter        deleter)
    {
        // Look for a cached shader.
        MString key = fragmentName + ":" + outputStructName;
        FragmentAndShaderTemplateCache::nth_index<0>::type::iterator it =
            fFragmentCache.get<0>().find(key);

        // Found in cache.
        if (it != fFragmentCache.get<0>().end()) {
            ShaderTemplatePtr templateShader = it->ptr.lock();
            assert(templateShader);  // no staled pointer
            return templateShader.newShaderInstance(deleter);
        }

        // Not found. Get a new shader.
        ShaderTemplatePtr templateShader =
            createFragmentShader(fragmentName, outputStructName);
        if (templateShader) {
            // Insert into cache.
            FragmentAndShaderTemplate entry;
            entry.fragmentAndOutput = key;
            entry.shader            = templateShader.get();
            entry.ptr               = templateShader.getTemplate();
            fFragmentCache.insert(entry);

            return templateShader.newShaderInstance(deleter);
        }

        assert(0);
        return ShaderInstancePtr();
    }

private:
    ShaderCache()  {}
    ~ShaderCache() {}

    // Release the MShaderInstance and remove the pointer from the cache.
    static void shaderTemplateDeleter(MShaderInstance* shader)
    {
        assert(shader);
        getInstance().removeShaderTemplateFromCache(shader);
        releaseShaderInstance(shader);
    }

    // Remove the pointer from the cache.
    void removeShaderTemplateFromCache(MShaderInstance* shader)
    {
        assert(shader);
        if (!shader) return;

        // Remove the MShaderInstance* from the cache.
        fFragmentCache.get<1>().erase(shader);
    }

    // Wrap the MShaderInstance* as template.
    ShaderTemplatePtr wrapShaderTemplate(MShaderInstance* shader)
    {
        assert(shader);
        if (!shader) return ShaderTemplatePtr();

        boost::shared_ptr<MShaderInstance> ptr;
        ptr.reset(shader, std::ptr_fun(shaderTemplateDeleter));
        return ShaderTemplatePtr(ptr);
    }

    // Create a shader template from a Maya fragment.
    ShaderTemplatePtr createFragmentShader(const MString& fragmentName,
                                           const MString& outputStructName)
    {
        MRenderer* renderer = MRenderer::theRenderer();
        if (!renderer) return ShaderTemplatePtr();
        const MShaderManager* shaderMgr = renderer->getShaderManager();
        if (!shaderMgr) return ShaderTemplatePtr();

        return wrapShaderTemplate(
            shaderMgr->getFragmentShader(fragmentName, outputStructName, true));
    }

private:
    struct FragmentAndShaderTemplate {
        MString                          fragmentAndOutput;
        MShaderInstance*                 shader;
        boost::weak_ptr<MShaderInstance> ptr;
    };
    typedef boost::multi_index_container<
        FragmentAndShaderTemplate,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                BOOST_MULTI_INDEX_MEMBER(FragmentAndShaderTemplate,MString,fragmentAndOutput),
                MStringHash
            >,
            boost::multi_index::hashed_unique<
                BOOST_MULTI_INDEX_MEMBER(FragmentAndShaderTemplate,MShaderInstance*,shader)
            >
        >
    > FragmentAndShaderTemplateCache;

    FragmentAndShaderTemplateCache  fFragmentCache;
};


//==============================================================================
// CLASS MaterialGraphTranslatorShaded
//==============================================================================

// This class translates a MaterialGraph to a MShaderInstance*
// that can be used in VP2.0.
class MaterialGraphTranslatorShaded : public ConcreteMaterialNodeVisitor
{
public:
    // Create a new shader instance.
    typedef void (*Deleter)(MShaderInstance*);
    MaterialGraphTranslatorShaded(Deleter deleter, double timeInSeconds)
        : fShader(), fDeleter(deleter), fTimeInSeconds(timeInSeconds)
    {}

    // Update an existing shader instance.
    MaterialGraphTranslatorShaded(ShaderInstancePtr& shader, double timeInSeconds)
        : fShader(shader), fDeleter(NULL), fTimeInSeconds(timeInSeconds)
    {}

    virtual ~MaterialGraphTranslatorShaded() {}

    ShaderInstancePtr getShader() const
    { return fShader; }

    virtual void visit(const LambertMaterial& node)
    {
        if (!fShader) {
            createShader("mayaLambertSurface", "outSurfaceFinal");
        }

        setupLambert(node);
    }

    virtual void visit(const PhongMaterial& node)
    {
        if (!fShader) {
            createShader("mayaPhongSurface", "outSurfaceFinal");
        }

        setupPhong(node);
        setupLambert(node);
    }

    // Nodes that can't be used as root material node.
    virtual void visit(const SurfaceMaterial& node) {}
    virtual void visit(const Texture2d& node) {}
    virtual void visit(const FileTexture& node) {}

private:
    void createShader(const MString& fragmentName,
                      const MString& structOutputName)
    {
        assert(fDeleter);
        fShader = ShaderCache::getInstance().newFragmentShader(
            fragmentName, structOutputName, fDeleter);
        assert(fShader);
    }

    void setupLambert(const LambertMaterial& lambert)
    {
        if (!fShader) return;

        // Color
        {
            const MColor color =
                ShadedModeColor::evaluateDefaultColor(lambert.Color, fTimeInSeconds);
            const float buffer[3] = {color.r, color.g, color.b};
            fShader->setParameter("color", buffer);
        }

        // Transparency
        {
            const MColor transparency =
                ShadedModeColor::evaluateColor(lambert.Transparency, fTimeInSeconds);
            const float buffer[3] = {transparency.r, transparency.g, transparency.b};
            fShader->setParameter("transparency", buffer);

            if (transparency.r > 0 || transparency.g > 0 || transparency.b > 0) {
                fShader->setIsTransparent(true);
            }
            else {
                fShader->setIsTransparent(false);
            }
        }

        // Ambient Color
        {
            const MColor ambientColor =
                ShadedModeColor::evaluateColor(lambert.AmbientColor, fTimeInSeconds);
            const float buffer[3] = {ambientColor.r, ambientColor.g, ambientColor.b};
            fShader->setParameter("ambientColor", buffer);
        }

        // Incandescence
        {
            const MColor incandescence =
                ShadedModeColor::evaluateColor(lambert.Incandescence, fTimeInSeconds);
            const float buffer[3] = {incandescence.r, incandescence.g, incandescence.b};
            fShader->setParameter("incandescence", buffer);
        }

        // Diffuse
        {
            const float diffuse =
                ShadedModeColor::evaluateFloat(lambert.Diffuse, fTimeInSeconds);
            fShader->setParameter("diffuse", diffuse);
        }

        // Translucence
        {
            const float translucence =
                ShadedModeColor::evaluateFloat(lambert.Translucence, fTimeInSeconds);
            fShader->setParameter("translucence", translucence);
        }

        // Translucence Depth
        {
            const float translucenceDepth =
                ShadedModeColor::evaluateFloat(lambert.TranslucenceDepth, fTimeInSeconds);
            fShader->setParameter("translucenceDepth", translucenceDepth);
        }

        // Translucence Focus
        {
            const float translucenceFocus =
                ShadedModeColor::evaluateFloat(lambert.TranslucenceFocus, fTimeInSeconds);
            fShader->setParameter("translucenceFocus", translucenceFocus);
        }

        // Hide Source
        {
            const bool hideSource =
                ShadedModeColor::evaluateBool(lambert.HideSource, fTimeInSeconds);
            fShader->setParameter("hideSource", hideSource);
        }

        // Glow Intensity
        {
            const float glowIntensity =
                ShadedModeColor::evaluateFloat(lambert.GlowIntensity, fTimeInSeconds);
            fShader->setParameter("glowIntensity", glowIntensity);
        }
    }

    void setupPhong(const PhongMaterial& phong)
    {
        if (!fShader) return;

        // Cosine Power
        {
            const float cosinePower =
                ShadedModeColor::evaluateFloat(phong.CosinePower, fTimeInSeconds);
            fShader->setParameter("cosinePower", cosinePower);
        }

        // Specular Color
        {
            const MColor specularColor =
                ShadedModeColor::evaluateColor(phong.SpecularColor, fTimeInSeconds);
            const float buffer[3] = {specularColor.r, specularColor.g, specularColor.b};
            fShader->setParameter("specularColor", buffer);
        }

        // Reflectivity
        {
            const float reflectivity =
                ShadedModeColor::evaluateFloat(phong.Reflectivity, fTimeInSeconds);
            fShader->setParameter("reflectivity", reflectivity);
        }

        // Reflected Color
        {
            const MColor reflectedColor =
                ShadedModeColor::evaluateColor(phong.ReflectedColor, fTimeInSeconds);
            const float buffer[3] = {reflectedColor.r, reflectedColor.g, reflectedColor.b};
            fShader->setParameter("reflectedColor", buffer);
        }
    }

    ShaderInstancePtr fShader;
    Deleter           fDeleter;
    const double      fTimeInSeconds;
};


//==============================================================================
// CLASS ShaderInstanceCache
//==============================================================================

// This class manages MShaderInstance across multiple gpuCache nodes.
// The cache returns a shared pointer to the requested MShaderInstance.
// The caller shouldn't modify the MShaderInstance* that is returned from
// getSharedXXXShader() because the shader instance might be shared
// with other render items.
// The caller is responsible to hold the pointer.
// If the reference counter goes 0, the MShaderInstance is released.
class ShaderInstanceCache : boost::noncopyable
{
public:
    static ShaderInstanceCache& getInstance()
    {
        // Singleton
        static ShaderInstanceCache sSingleton;
        return sSingleton;
    }

    ShaderInstancePtr getSharedWireShader(const MColor& color)
    {
        // Look for the cached MShaderInstance.
        ColorAndShaderInstanceCache::nth_index<0>::type::iterator it =
            fWireShaders.get<0>().find(color);

        // Found in cache.
        if (it != fWireShaders.get<0>().end()) {
            boost::shared_ptr<MShaderInstance> shader = it->ptr.lock();
            assert(shader);  // no staled pointer.
            return ShaderInstancePtr(shader, it->source);
        }

        // Not found. Get a new MShaderInstance.
        ShaderInstancePtr shader =
            ShaderCache::getInstance().newWireShader(shaderInstanceDeleter);
        if (shader) {
            // Wireframe dash-line pattern.
            shader->setParameter("dashPattern", Config::kLineStippleShortDashed);

            // Wireframe color.
            const float solidColor[4] = {color.r, color.g, color.b, 1.0f};
            shader->setParameter("solidColor", solidColor);

            // Insert into cache.
            ColorAndShaderInstance entry;
            entry.color  = color;
            entry.shader = shader.get();
            entry.ptr    = shader.getShader();
            entry.source = shader.getTemplate();
            fWireShaders.insert(entry);

            return shader;
        }

        assert(0);
        return ShaderInstancePtr();
    }

    ShaderInstancePtr getSharedWireShaderWithCB(const MColor& color)
    {
        // Look for the cached MShaderInstance.
        ColorAndShaderInstanceCache::nth_index<0>::type::iterator it =
            fWireShadersWithCB.get<0>().find(color);

        // Found in cache.
        if (it != fWireShadersWithCB.get<0>().end()) {
            boost::shared_ptr<MShaderInstance> shader = it->ptr.lock();
            assert(shader);  // no staled pointer.
            return ShaderInstancePtr(shader, it->source);
        }

        // Not found. Get a new MShaderInstance.
        ShaderInstancePtr shader =
            ShaderCache::getInstance().newWireShaderWithCB(shaderInstanceDeleter);
        if (shader) {
            // Wireframe dash-line pattern.
            shader->setParameter("dashPattern", Config::kLineStippleShortDashed);

            // Wireframe color.
            const float solidColor[4] = {color.r, color.g, color.b, 1.0f};
            shader->setParameter("solidColor", solidColor);

            // Insert into cache.
            ColorAndShaderInstance entry;
            entry.color  = color;
            entry.shader = shader.get();
            entry.ptr    = shader.getShader();
            entry.source = shader.getTemplate();
            fWireShadersWithCB.insert(entry);

            return shader;
        }

        assert(0);
        return ShaderInstancePtr();
    }

    ShaderInstancePtr getSharedBoundingBoxPlaceHolderShader(const MColor& color)
    {
        // Look for the cached MShaderInstance.
        ColorAndShaderInstanceCache::nth_index<0>::type::iterator it =
            fBoundingBoxPlaceHolderShaders.get<0>().find(color);

        // Found in cache.
        if (it != fBoundingBoxPlaceHolderShaders.get<0>().end()) {
            boost::shared_ptr<MShaderInstance> shader = it->ptr.lock();
            assert(shader);  // no staled pointer.
            return ShaderInstancePtr(shader, it->source);
        }

        // Not found. Get a new MShaderInstance.
        ShaderInstancePtr shader =
            ShaderCache::getInstance().newBoundingBoxPlaceHolderShader(shaderInstanceDeleter);
        if (shader) {
            // Wireframe dash-line pattern.
            shader->setParameter("dashPattern", Config::kLineStippleShortDashed);

            // Wireframe color.
            const float solidColor[4] = {color.r, color.g, color.b, 1.0f};
            shader->setParameter("solidColor", solidColor);

            // Insert into cache.
            ColorAndShaderInstance entry;
            entry.color  = color;
            entry.shader = shader.get();
            entry.ptr    = shader.getShader();
            entry.source = shader.getTemplate();
            fBoundingBoxPlaceHolderShaders.insert(entry);

            return shader;
        }

        assert(0);
        return ShaderInstancePtr();
    }

    ShaderInstancePtr getSharedDiffuseColorShader(const MColor& color)
    {
        // Look for the cached MShaderInstance.
        ColorAndShaderInstanceCache::nth_index<0>::type::iterator it =
            fDiffuseColorShaders.get<0>().find(color);

        // Found in cache.
        if (it != fDiffuseColorShaders.get<0>().end()) {
            boost::shared_ptr<MShaderInstance> shader = it->ptr.lock();
            assert(shader);  // no staled pointer.
            return ShaderInstancePtr(shader, it->source);
        }

        // Not found. Get a new MShaderInstance.
        ShaderInstancePtr shader =
            ShaderCache::getInstance().newDiffuseColorShader(shaderInstanceDeleter);
        if (shader) {
            // Set the diffuse color.
            setDiffuseColor(shader.get(), color);

            // Insert into cache.
            ColorAndShaderInstance entry;
            entry.color  = color;
            entry.shader = shader.get();
            entry.ptr    = shader.getShader();
            entry.source = shader.getTemplate();
            fDiffuseColorShaders.insert(entry);

            return shader;
        }

        assert(0);
        return ShaderInstancePtr();
    }

    // Create a unique lambert shader for diffuse color.
    // The caller can change the shader parameters for material animation.
    ShaderInstancePtr getUniqueDiffuseColorShader(const MColor& color)
    {
        ShaderInstancePtr shader =
            ShaderCache::getInstance().newDiffuseColorShader(shaderInstanceDeleter);
        if (shader) {
            // Set the diffuse color.
            setDiffuseColor(shader.get(), color);
            return shader;
        }

        return ShaderInstancePtr();
    }

    // This method will get a cached MShaderInstance for the given material.
    ShaderInstancePtr getSharedShadedMaterialShader(
        const MaterialGraph::Ptr& material,
        double                    timeInSeconds
    )
    {
        assert(material);
        if (!material) return ShaderInstancePtr();

        // Look for the cached MShaderInstance.
        MaterialAndShaderInstanceCache::nth_index<0>::type::iterator it =
            fShadedMaterialShaders.get<0>().find(material);

        // Found in cache.
        if (it != fShadedMaterialShaders.get<0>().end()) {
            boost::shared_ptr<MShaderInstance> shader = it->ptr.lock();
            assert(shader);  // no staled pointer.
            return ShaderInstancePtr(shader, it->source);
        }

        // Not found. Get a new MShaderInstance.
        const MaterialNode::Ptr& rootNode = material->rootNode();
        assert(rootNode);

        ShaderInstancePtr shader;
        if (rootNode) {
            MaterialGraphTranslatorShaded shadedTranslator(shaderInstanceDeleter, timeInSeconds);
            rootNode->accept(shadedTranslator);
            shader = shadedTranslator.getShader();
        }

        if (shader) {
            // Insert into cache.
            MaterialAndShaderInstance entry;
            entry.material      = material;
            entry.shader        = shader.get();
            entry.ptr           = shader.getShader();
            entry.source        = shader.getTemplate();
            entry.isAnimated    = material->isAnimated();
            entry.timeInSeconds = timeInSeconds;
            fShadedMaterialShaders.insert(entry);

            return shader;
        }

        assert(0);
        return ShaderInstancePtr();
    }

    void updateCachedShadedShaders(double timeInSeconds)
    {
        // Update all cached MShaderInstance* for shaded mode to the current time.
        BOOST_FOREACH (const MaterialAndShaderInstance& entry, fShadedMaterialShaders) {
            // Not animated. Skipping.
            if (!entry.isAnimated) continue;

            // Already up-to-date. Skipping.
            if (entry.timeInSeconds == timeInSeconds) continue;

            // Update the MShaderInstance*
            const MaterialNode::Ptr& rootNode = entry.material->rootNode();
            if (rootNode) {
                ShaderInstancePtr shader(entry.ptr.lock(), entry.source);
                if (shader) {
                    MaterialGraphTranslatorShaded shadedTranslator(shader, timeInSeconds);
                    rootNode->accept(shadedTranslator);
                }
            }

            // Remember the last update time.
            entry.timeInSeconds = timeInSeconds;
        }
    }

private:
    // Release the MShaderInstance and remove the pointer from the cache.
    static void shaderInstanceDeleter(MShaderInstance* shader)
    {
        assert(shader);
        getInstance().removeShaderInstanceFromCache(shader);
        releaseShaderInstance(shader);
    }

    // Remove the pointer from the cache.
    void removeShaderInstanceFromCache(MShaderInstance* shader)
    {
        assert(shader);
        if (!shader) return;

        // Remove the MShaderInstance* from the cache.
        fWireShaders.get<1>().erase(shader);
        fWireShadersWithCB.get<1>().erase(shader);
        fBoundingBoxPlaceHolderShaders.get<1>().erase(shader);
        fDiffuseColorShaders.get<1>().erase(shader);
        fShadedMaterialShaders.get<1>().erase(shader);
    }

private:
    ShaderInstanceCache()  {}
    ~ShaderInstanceCache() {}

    // MColor as hash key.
    struct MColorHash : std::unary_function<MColor, std::size_t>
    {
        std::size_t operator()(const MColor& key) const
        {
            std::size_t seed = 0;
            boost::hash_combine(seed, key.r);
            boost::hash_combine(seed, key.g);
            boost::hash_combine(seed, key.b);
            boost::hash_combine(seed, key.a);
            return seed;
        }
    };

    // MaterialGraph as hash key.
    struct MaterialGraphHash : std::unary_function<MaterialGraph::Ptr, std::size_t>
    {
        std::size_t operator()(const MaterialGraph::Ptr& key) const
        {
            return boost::hash_value(key.get());
        }
    };

    // MShaderInstance* cached by MColor as hash key.
    struct ColorAndShaderInstance {
        MColor                             color;
        MShaderInstance*                   shader;
        boost::weak_ptr<MShaderInstance>   ptr;
        boost::shared_ptr<MShaderInstance> source;
    };
    typedef boost::multi_index_container<
        ColorAndShaderInstance,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                BOOST_MULTI_INDEX_MEMBER(ColorAndShaderInstance,MColor,color),
                MColorHash
            >,
            boost::multi_index::hashed_unique<
                BOOST_MULTI_INDEX_MEMBER(ColorAndShaderInstance,MShaderInstance*,shader)
            >
        >
    > ColorAndShaderInstanceCache;

    // MShaderInstance* cached by MaterialGraph as hash key.
    struct MaterialAndShaderInstance {
        MaterialGraph::Ptr                 material;
        MShaderInstance*                   shader;
        boost::weak_ptr<MShaderInstance>   ptr;
        boost::shared_ptr<MShaderInstance> source;
        bool                               isAnimated;
        mutable double                     timeInSeconds;
    };
    typedef boost::multi_index_container<
        MaterialAndShaderInstance,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                BOOST_MULTI_INDEX_MEMBER(MaterialAndShaderInstance,MaterialGraph::Ptr,material),
                MaterialGraphHash
            >,
            boost::multi_index::hashed_unique<
                BOOST_MULTI_INDEX_MEMBER(MaterialAndShaderInstance,MShaderInstance*,shader)
            >
        >
    > MaterialAndShaderInstanceCache;

    ColorAndShaderInstanceCache    fWireShaders;
    ColorAndShaderInstanceCache    fWireShadersWithCB;
    ColorAndShaderInstanceCache    fBoundingBoxPlaceHolderShaders;
    ColorAndShaderInstanceCache    fDiffuseColorShaders;
    MaterialAndShaderInstanceCache fShadedMaterialShaders;
};


//==============================================================================
// CLASS ModelCallbacks
//==============================================================================

// This class manages model-level callbacks.
// gpuCache node-level callbacks are registered in GPUCache::SubSceneOverride.
class ModelCallbacks : boost::noncopyable
{
public:
    static ModelCallbacks& getInstance()
    {
        // Singleton
        static ModelCallbacks sSingleton;
        return sSingleton;
    }

    ModelCallbacks()
    {
        // Initialize DAG object attributes that affect display appearance
        // of their descendant shapes.
        fAttrsAffectAppearance.insert("visibility");
        fAttrsAffectAppearance.insert("lodVisibility");
        fAttrsAffectAppearance.insert("intermediateObject");
        fAttrsAffectAppearance.insert("template");
        fAttrsAffectAppearance.insert("drawOverride");
        fAttrsAffectAppearance.insert("overrideDisplayType");
        fAttrsAffectAppearance.insert("overrideLevelOfDetail");
        fAttrsAffectAppearance.insert("overrideShading");
        fAttrsAffectAppearance.insert("overrideTexturing");
        fAttrsAffectAppearance.insert("overridePlayback");
        fAttrsAffectAppearance.insert("overrideEnabled");
        fAttrsAffectAppearance.insert("overrideVisibility");
        fAttrsAffectAppearance.insert("overrideColor");
        fAttrsAffectAppearance.insert("useObjectColor");
        fAttrsAffectAppearance.insert("objectColor");
        fAttrsAffectAppearance.insert("ghosting");
        fAttrsAffectAppearance.insert("castsShadows");
        fAttrsAffectAppearance.insert("receiveShadows");

        // Hook model/scene/DG/event callbacks.
        fMayaExitingCallback = MSceneMessage::addCallback(
            MSceneMessage::kMayaExiting, MayaExitingCallback, NULL);
        fSelectionChangedCallback = MModelMessage::addCallback(
            MModelMessage::kActiveListModified, SelectionChangedCallback, this);
        fTimeChangeCallback = MDGMessage::addTimeChangeCallback(
            TimeChangeCallback, this);
        fRenderLayerChangeCallback = MEventMessage::addEventCallback(
            "renderLayerChange", RenderLayerChangeCallback, this);
        fRenderLayerManagerChangeCallback = MEventMessage::addEventCallback(
            "renderLayerManagerChange", RenderLayerChangeCallback, this);

        // Trigger the callback to initialize the selection list.
        selectionChanged();
    }

    ~ModelCallbacks()
    {
        MMessage::removeCallback(fMayaExitingCallback);
        MMessage::removeCallback(fSelectionChangedCallback);
        MMessage::removeCallback(fTimeChangeCallback);
        MMessage::removeCallback(fRenderLayerChangeCallback);
        MMessage::removeCallback(fRenderLayerManagerChangeCallback);
    }

    void registerSubSceneOverride(const ShapeNode* shapeNode, SubSceneOverride* subSceneOverride)
    {
        assert(shapeNode);
        if (!shapeNode) return;

        assert(subSceneOverride);
        if (!subSceneOverride) return;

        // Register the MPxSubSceneOverride to receive callbacks.
        fShapeNodes.insert(std::make_pair(shapeNode, subSceneOverride));
    }

    void deregisterSubSceneOverride(const ShapeNode* shapeNode)
    {
        assert(shapeNode);
        if (!shapeNode) return;

        // Deregister the MPxSubSceneOverride.
        fShapeNodes.erase(shapeNode);
    }

    // Detect selection change and dirty SubSceneOverride.
    void selectionChanged()
    {
        // Retrieve the current selection list.
        MSelectionList list;
        MGlobal::getActiveSelectionList(list);

        // Find all selected gpuCache nodes.
        ShapeNodeNameMap currentSelection;

        MDagPath   dagPath;
        MItDag     dagIt;
        MFnDagNode dagNode;
        for (unsigned int i = 0, size = list.length(); i < size; i++) {
            if (list.getDagPath(i, dagPath) && dagPath.isValid()) {
                // Iterate the DAG to find descendant gpuCache nodes.
                dagIt.reset(dagPath, MItDag::kDepthFirst, MFn::kPluginShape);
                for (; !dagIt.isDone(); dagIt.next()) {
                    if (dagNode.setObject(dagIt.currentItem()) && dagNode.typeId() == ShapeNode::id) {
                        const ShapeNode* shapeNode = (const ShapeNode*)dagNode.userNode();
                        if (shapeNode) {
                            currentSelection.insert(std::make_pair(dagIt.fullPathName(), shapeNode));
                        }
                    }
                }
            }
        }

        // Check Active -> Dormant
        BOOST_FOREACH (const ShapeNodeNameMap::value_type& val, fLastSelection) {
            if (currentSelection.find(val.first) == currentSelection.end()) {
                ShapeNodeSubSceneMap::iterator it = fShapeNodes.find(val.second);
                if (it != fShapeNodes.end() && it->second) {
                    it->second->dirtyEverything();
                }
            }
        }

        // Check Dormant -> Active
        BOOST_FOREACH (const ShapeNodeNameMap::value_type& val, currentSelection) {
            if (fLastSelection.find(val.first) == fLastSelection.end()) {
                ShapeNodeSubSceneMap::iterator it = fShapeNodes.find(val.second);
                if (it != fShapeNodes.end() && it->second) {
                    it->second->dirtyEverything();
                }
            }
        }

        fLastSelection.swap(currentSelection);
    }

    // Detect time change and dirty SubSceneOverride.
    void timeChanged()
    {
        BOOST_FOREACH (ShapeNodeSubSceneMap::value_type& val, fShapeNodes) {
            val.second->dirtyVisibility();   // visibility animation
            val.second->dirtyWorldMatrix();  // xform animation
            val.second->dirtyStreams();      // vertex animation
            val.second->dirtyMaterials();    // material animation
        }
    }

    // Detect render layer change and dirty SubSceneOverride.
    void renderLayerChanged()
    {
        BOOST_FOREACH (ShapeNodeSubSceneMap::value_type& val, fShapeNodes) {
            val.second->dirtyEverything();   // render layer change is destructive
        }
    }

    bool affectAppearance(const MString& attr) const
    {
        return (fAttrsAffectAppearance.find(attr) != fAttrsAffectAppearance.cend());
    }

private:
    static void MayaExitingCallback(void* clientData)
    {
        // Free VP2.0 buffers on exit.
        IndexBuffer::freeViewport2Buffers();
        VertexBuffer::freeViewport2Buffers();
        UnitBoundingBox::clear();
    }

    static void SelectionChangedCallback(void* clientData)
    {
        assert(clientData);
        static_cast<ModelCallbacks*>(clientData)->selectionChanged();
    }

    static void TimeChangeCallback(MTime& time, void* clientData)
    {
        assert(clientData);
        static_cast<ModelCallbacks*>(clientData)->timeChanged();
    }

    static void RenderLayerChangeCallback(void* clientData)
    {
        assert(clientData);
        static_cast<ModelCallbacks*>(clientData)->renderLayerChanged();
    }

private:
    MCallbackId fMayaExitingCallback;
    MCallbackId fSelectionChangedCallback;
    MCallbackId fTimeChangeCallback;
    MCallbackId fRenderLayerChangeCallback;
    MCallbackId fRenderLayerManagerChangeCallback;

    typedef boost::unordered_map<MString,const ShapeNode*,MStringHash> ShapeNodeNameMap;
    typedef boost::unordered_map<const ShapeNode*,SubSceneOverride*>   ShapeNodeSubSceneMap;

    ShapeNodeNameMap     fLastSelection;
    ShapeNodeSubSceneMap fShapeNodes;

    boost::unordered_set<MString, MStringHash> fAttrsAffectAppearance;
};


//==============================================================================
// Callbacks
//==============================================================================

// Instance Changed Callback
void InstanceChangedCallback(MDagPath& child, MDagPath& parent, void* clientData)
{
    assert(clientData);
    static_cast<SubSceneOverride*>(clientData)->dirtyEverything();
    static_cast<SubSceneOverride*>(clientData)->resetDagPaths();
}

// World Matrix Changed Callback
void WorldMatrixChangedCallback(MObject& transformNode,
    MDagMessage::MatrixModifiedFlags& modified,
    void* clientData)
{
    assert(clientData);
    static_cast<SubSceneOverride*>(clientData)->dirtyWorldMatrix();
}

// Parent Add/Remove Callback
void ParentChangedCallback(MDagPath& child, MDagPath& parent, void* clientData)
{
    // We register node dirty callbacks on all transform parents/ancestors.
    // If the parent is changed, we will have to re-register all callbacks.
    assert(clientData);
    // Clear the callbacks on parents.
    static_cast<SubSceneOverride*>(clientData)->clearNodeDirtyCallbacks();
    // Dirty the render items so we re-register callbacks again in update().
    static_cast<SubSceneOverride*>(clientData)->dirtyEverything();
}

// Node Dirty Callback
void NodeDirtyCallback(MObject& node, MPlug& plug, void* clientData)
{
    // One of the parent/ancestor has changed.
    // Dirty the SubSceneOverride if the attribute will affect
    // the appearance of the gpuCache shape.
    assert(clientData);
    MFnAttribute attr(plug.attribute());
    if (ModelCallbacks::getInstance().affectAppearance(attr.name())) {
        static_cast<SubSceneOverride*>(clientData)->dirtyEverything();
    }
}

}


namespace GPUCache {

using namespace MHWRender;


//==============================================================================
// CLASS SubSceneOverride::HierarchyStat
//==============================================================================

// This class contains the analysis result of the sub-node hierarchy.
class SubSceneOverride::HierarchyStat : boost::noncopyable
{
public:
    typedef boost::shared_ptr<const HierarchyStat> Ptr;

    // This is the status of a sub-node and its descendants.
    struct SubNodeStat
    {
        // False if the sub-node and all its descendants have no visibility animation.
        bool   isVisibilityAnimated;
        // False if the sub-node and all its descendants have no xform animation.
        bool   isXformAnimated;
        // False if the sub-node and all its descendants have no vertices animation.
        bool   isShapeAnimated;
        // False if the sub-node and all its descendants have no diffuse color animation.
        bool   isDiffuseColorAnimated;
        // The next sub-node id if we prune at this sub-node. (depth first, preorder)
        size_t nextSubNodeIndex;
        // The next shape sub-node id if we prune at this sub-node. (depth first, preorder)
        size_t nextShapeSubNodeIndex;

        SubNodeStat()
            : isVisibilityAnimated(false),
              isXformAnimated(false),
              isShapeAnimated(false),
              isDiffuseColorAnimated(false),
              nextSubNodeIndex(0),
              nextShapeSubNodeIndex(0)
        {}
    };

    ~HierarchyStat() {}

    void setStat(size_t subNodeIndex, SubNodeStat& stat)
    {
        if (subNodeIndex >= fStats.size()) {
            fStats.resize(subNodeIndex+1);
        }
        fStats[subNodeIndex] = stat;
    }

    const SubNodeStat& stat(size_t subNodeIndex) const
    { return fStats[subNodeIndex]; }

private:
    HierarchyStat() {}
    friend class HierarchyStatVisitor;

    std::vector<SubNodeStat> fStats;
};


//==============================================================================
// CLASS SubSceneOverride::HierarchyStatVisitor
//==============================================================================

// This class analyzes the sub-node hierarchy to help pruning non-animated sub-hierarchy.
class SubSceneOverride::HierarchyStatVisitor : public SubNodeVisitor
{
public:
    HierarchyStatVisitor(const SubNode::Ptr& geometry)
        : fGeometry(geometry),
          fIsParentVisibilityAnimated(false),
          fIsVisibilityAnimated(false),
          fIsParentXformAnimated(false),
          fIsXformAnimated(false),
          fIsShapeAnimated(false),
          fIsDiffuseColorAnimated(false),
          fSubNodeIndex(0),
          fShapeSubNodeIndex(0)
    {
        fHierarchyStat.reset(new HierarchyStat());
    }

    virtual ~HierarchyStatVisitor()
    {}

    const HierarchyStat::Ptr getStat() const
    { return fHierarchyStat; }

    virtual void visit(const XformData&   xform,
                       const SubNode&     subNode)
    {
        // Increase the sub-node counter.
        size_t thisSubNodeIndex = fSubNodeIndex;
        fSubNodeIndex++;

        // Is the visibility animated?
        bool isVisibilityAnimated = false;
        if (xform.getSamples().size() > 1) {
            const boost::shared_ptr<const XformSample>& sample =
                xform.getSamples().begin()->second;
            if (sample) {
                const bool oneVisibility = sample->visibility();
                BOOST_FOREACH (const XformData::SampleMap::value_type& val, xform.getSamples()) {
                    if (val.second && val.second->visibility() != oneVisibility) {
                        isVisibilityAnimated = true;
                        break;
                    }
                }
            }
        }

        // Is the xform animated?
        bool isXformAnimated = false;
        if (xform.getSamples().size() > 1) {
            const boost::shared_ptr<const XformSample>& sample =
                xform.getSamples().begin()->second;
            if (sample) {
                const MMatrix& oneMatrix = sample->xform();
                BOOST_FOREACH (const XformData::SampleMap::value_type& val, xform.getSamples()) {
                    if (val.second && val.second->xform() != oneMatrix) {
                        isXformAnimated = true;
                        break;
                    }
                }
            }
        }

        // Push the xform/visibility animated flag down the hierarchy.
        {
            ScopedGuard<bool> parentVisibilityGuard(fIsParentVisibilityAnimated);
            fIsParentVisibilityAnimated = fIsParentVisibilityAnimated || isVisibilityAnimated;

            ScopedGuard<bool> parentXformGuard(fIsParentXformAnimated);
            fIsParentXformAnimated = fIsParentXformAnimated || isXformAnimated;

            // Shape animated flags for all descendant shapes.
            bool isShapeAnimated        = false;
            bool isDiffuseColorAnimated = false;

            // Recursive calls into children
            BOOST_FOREACH (const SubNode::Ptr& child, subNode.getChildren()) {
                child->accept(*this);

                // Merge shape animated flags.
                isVisibilityAnimated   = isVisibilityAnimated   || fIsVisibilityAnimated;
                isXformAnimated        = isXformAnimated        || fIsXformAnimated;
                isShapeAnimated        = isShapeAnimated        || fIsShapeAnimated;
                isDiffuseColorAnimated = isDiffuseColorAnimated || fIsDiffuseColorAnimated;
            }

            // Pull shape animated flags up the hierarchy.
            fIsVisibilityAnimated   = isVisibilityAnimated;
            fIsXformAnimated        = isXformAnimated;
            fIsShapeAnimated        = isShapeAnimated;
            fIsDiffuseColorAnimated = isDiffuseColorAnimated;
        }

        appendStat(thisSubNodeIndex);
    }

    virtual void visit(const ShapeData&   shape,
                       const SubNode&     subNode)
    {
        // Increase the sub-node counter.
        size_t thisSubNodeIndex = fSubNodeIndex;
        fSubNodeIndex++;
        fShapeSubNodeIndex++;

        // Is the shape animated ?
        fIsShapeAnimated = shape.getSamples().size() > 1;

        // Is the diffuse color animated?
        fIsDiffuseColorAnimated = false;

        if (fIsShapeAnimated) {
            const boost::shared_ptr<const ShapeSample>& sample =
                shape.getSamples().begin()->second;
            if (sample) {
                const MColor& oneColor = sample->diffuseColor();
                BOOST_FOREACH (const ShapeData::SampleMap::value_type& val, shape.getSamples()) {
                    if (val.second && val.second->diffuseColor() != oneColor) {
                        fIsDiffuseColorAnimated = true;
                        break;
                    }
                }
            }
        }

        // Is the visibility animated?
        fIsVisibilityAnimated = false;

        if (fIsShapeAnimated) {
            const boost::shared_ptr<const ShapeSample>& sample =
                shape.getSamples().begin()->second;
            if (sample) {
                const bool oneVisibility = sample->visibility();
                BOOST_FOREACH (const ShapeData::SampleMap::value_type& val, shape.getSamples()) {
                    if (val.second && val.second->visibility() != oneVisibility) {
                        fIsVisibilityAnimated = true;
                        break;
                    }
                }
            }
        }

        // Shape's xform is not animated..
        fIsXformAnimated = false;

        appendStat(thisSubNodeIndex);
    }

    void appendStat(size_t subNodeIndex)
    {
        // Record the stat of this sub-node.
        HierarchyStat::SubNodeStat stat;
        stat.isVisibilityAnimated   = fIsVisibilityAnimated || fIsParentVisibilityAnimated;
        stat.isXformAnimated        = fIsXformAnimated || fIsParentXformAnimated;
        stat.isShapeAnimated        = fIsShapeAnimated;
        stat.isDiffuseColorAnimated = fIsDiffuseColorAnimated;
        stat.nextSubNodeIndex       = fSubNodeIndex;
        stat.nextShapeSubNodeIndex  = fShapeSubNodeIndex;

        fHierarchyStat->setStat(subNodeIndex, stat);
    }

private:
    const SubNode::Ptr fGeometry;
    bool               fIsParentVisibilityAnimated;
    bool               fIsVisibilityAnimated;
    bool               fIsParentXformAnimated;
    bool               fIsXformAnimated;
    bool               fIsShapeAnimated;
    bool               fIsDiffuseColorAnimated;
    size_t             fSubNodeIndex;
    size_t             fShapeSubNodeIndex;

    boost::shared_ptr<HierarchyStat> fHierarchyStat;
};


//==============================================================================
// CLASS SubSceneOverride::SubNodeRenderItems
//==============================================================================

// This class contains the render items for each sub node.
class SubSceneOverride::SubNodeRenderItems : boost::noncopyable
{
public:
    typedef boost::shared_ptr<SubNodeRenderItems> Ptr;

    SubNodeRenderItems()
        : fBoundingBoxItem(NULL),
          fActiveWireItem(NULL),
          fDormantWireItem(NULL),
          fIsBoundingBoxPlaceHolder(false),
          fIsSelected(false),
          fVisibility(true),
          fValidPoly(true)
    {}
    ~SubNodeRenderItems() {}

    void updateRenderItems(SubSceneOverride&   subSceneOverride,
                           MSubSceneContainer& container,
                           const MString&      subNodePrefix,
                           const MColor&       wireColor,
                           const ShapeData&    shape,
                           const SubNode&      subNode,
                           const bool          isSelected)
    {
        // Get the current shape sample.
        const boost::shared_ptr<const ShapeSample>& sample =
            shape.getSample(subSceneOverride.getTime());
        if (!sample) return;

        // Cache flags
        fIsBoundingBoxPlaceHolder = sample->isBoundingBoxPlaceHolder();
        fIsSelected               = isSelected;

        // Bounding box place holder.
        updateBoundingBoxItems(subSceneOverride, container, subNodePrefix, wireColor, subNode);

        // Dormant Wireframe
        updateDormantWireItems(subSceneOverride, container, subNodePrefix, wireColor);

        // Active Wireframe
        updateActiveWireItems(subSceneOverride, container, subNodePrefix, wireColor);

        // Shaded
        updateShadedItems(subSceneOverride, container, subNodePrefix, shape,
            sample->diffuseColor(), sample->numIndexGroups());
    }

    void updateVisibility(SubSceneOverride&   subSceneOverride,
                          MSubSceneContainer& container,
                          const bool          visibility,
                          const ShapeData&    shape)
    {
        // Recompute the shadow map if visibility changed.
        if (fVisibility != visibility) {
            MRenderer::setLightsAndShadowsDirty();
            fVisibility = visibility;
        }

        // Enable or disable render items.
        toggleBoundingBoxItem();
        toggleDormantWireItem();
        toggleActiveWireItem();
        toggleShadedItems();
    }

    void updateWorldMatrix(SubSceneOverride&   subSceneOverride,
                           MSubSceneContainer& container,
                           const MMatrix&      matrix,
                           const ShapeData&    shape)
    {
        // Set the world matrix.
        if (fBoundingBoxItem) {
            const boost::shared_ptr<const ShapeSample>& sample =
                shape.getSample(subSceneOverride.getTime());
            if (sample) {
                const MBoundingBox& boundingBox = sample->boundingBox();
                const MMatrix worldMatrix =
                    UnitBoundingBox::boundingBoxMatrix(boundingBox) * matrix;
                fBoundingBoxItem->setMatrix(&worldMatrix);
            }
        }

        if (fDormantWireItem) {
            fDormantWireItem->setMatrix(&matrix);
        }

        if (fActiveWireItem) {
            fActiveWireItem->setMatrix(&matrix);
        }

        BOOST_FOREACH (MRenderItem* shadedItem, fShadedItems) {
            shadedItem->setMatrix(&matrix);
        }

        // Recompute the shadow map if world matrix changed.
        if (fWorldMatrix != matrix) {
            MRenderer::setLightsAndShadowsDirty();
            fWorldMatrix = matrix;
        }
    }

    void updateStreams(SubSceneOverride&   subSceneOverride,
                       MSubSceneContainer& container,
                       const ShapeData&    shape)
    {
        const boost::shared_ptr<const ShapeSample>& sample =
            shape.getSample(subSceneOverride.getTime());
        if (!sample) return;

        // If this sample is an empty poly, we disable all render items and return.
        const bool validPoly = sample->numVerts() > 0 &&
                               sample->numWires() > 0 &&
                               sample->numTriangles() > 0 &&
                               sample->positions();
        // Recompute the shadow map as the mesh would "disappear".
        if (fValidPoly != validPoly) {
            MRenderer::setLightsAndShadowsDirty();
            fValidPoly = validPoly;
        }
        // Enable or disable render items.
        toggleBoundingBoxItem();
        toggleDormantWireItem();
        toggleActiveWireItem();
        toggleShadedItems();
        if (!validPoly) {
            // Nothing to do. Render items are disabled.
            return;
        }

        // Track if we have any changes.
        bool anythingChanged = false;

        // Update the wireframe streams.
        if (fDormantWireItem) {
            // Detect dormant wireframe buffer changes.
            const bool dormantWireChanged =
                fDormantWirePositions != sample->positions() ||
                fDormantWireIndices != sample->wireVertIndices();

            // Update dormant wire item with new buffers.
            if (dormantWireChanged) {
                MVertexBufferArray buffers;
                buffers.addBuffer("positions", sample->positions()->buffer());

                subSceneOverride.setGeometryForRenderItem(
                    *fDormantWireItem,
                    buffers,
                    *sample->wireVertIndices()->buffer(),
                    &sample->boundingBox()
                );

                // Remember the last buffers.
                anythingChanged = true;
                fDormantWirePositions = sample->positions();
                fDormantWireIndices   = sample->wireVertIndices();
            }
        }

        if (fActiveWireItem) {
            // Detect active wireframe buffer changes.
            const bool activeWireChanged =
                fActiveWirePositions != sample->positions() ||
                fActiveWireIndices != sample->wireVertIndices();

            // Update active wire item with new buffers.
            if (activeWireChanged) {
                MVertexBufferArray buffers;
                buffers.addBuffer("positions", sample->positions()->buffer());

                subSceneOverride.setGeometryForRenderItem(
                    *fActiveWireItem,
                    buffers,
                    *sample->wireVertIndices()->buffer(),
                    &sample->boundingBox()
                );

                // Remember the last buffers.
                anythingChanged = true;
                fActiveWirePositions = sample->positions();
                fActiveWireIndices   = sample->wireVertIndices();
            }
        }

        // Update the shaded streams.
        for (size_t groupId = 0; groupId < sample->numIndexGroups(); groupId++) {
            if (groupId >= fShadedItems.size()) break;  // background loading

            // Detect shaded buffer changes.
            assert(fTrianglePositions.size() == sample->numIndexGroups());
            assert(fTriangleNormals.size() == sample->numIndexGroups());
            assert(fTriangleUVs.size() == sample->numIndexGroups());
            assert(fTriangleIndices.size() == sample->numIndexGroups());
            const bool shadedChanged =
                fTrianglePositions[groupId] != sample->positions() ||
                fTriangleNormals[groupId] != sample->normals() ||
                fTriangleUVs[groupId] != sample->uvs() ||
                fTriangleIndices[groupId] != sample->triangleVertIndices(groupId);

            // Update shaded items with new buffers.
            if (shadedChanged) {
                MVertexBufferArray buffers;
                buffers.addBuffer("positions", sample->positions()->buffer());
                if (sample->normals()) {
                    buffers.addBuffer("normals", sample->normals()->buffer());
                }
                if (sample->uvs()) {
                    buffers.addBuffer("uvs", sample->uvs()->buffer());
                }

                subSceneOverride.setGeometryForRenderItem(
                    *fShadedItems[groupId],
                    buffers,
                    *sample->triangleVertIndices(groupId)->buffer(),
                    &sample->boundingBox()
                );

                // Remember the last buffers.
                anythingChanged = true;
                fTrianglePositions[groupId] = sample->positions();
                fTriangleNormals[groupId]   = sample->normals();
                fTriangleUVs[groupId]       = sample->uvs();
                fTriangleIndices[groupId]   = sample->triangleVertIndices(groupId);
            }
        }

        // Recompute the shadow map if mesh changed.
        if (anythingChanged) {
            MRenderer::setLightsAndShadowsDirty();
        }
    }

    void updateMaterials(SubSceneOverride&   subSceneOverride,
                         MSubSceneContainer& container,
                         const ShapeData&    shape)
    {
        const boost::shared_ptr<const ShapeSample>& sample =
            shape.getSample(subSceneOverride.getTime());
        if (!sample) return;

        for (size_t groupId = 0; groupId < sample->numIndexGroups(); groupId++) {
            if (groupId >= fShadedItems.size()) break;  // background loading
            if (groupId >= fSharedDiffuseColorShaders.size()) break;
            if (groupId >= fUniqueDiffuseColorShaders.size()) break;
            if (groupId >= fMaterialShaders.size()) break;

            // Update the diffuse color.
            MRenderItem* shadedItem = fShadedItems[groupId];
            if (shadedItem) {
                // First, check if the shader instance is created from a MaterialGraph.
                ShaderInstancePtr shader = fMaterialShaders[groupId];
                if (shader) {
                    // Nothing to do.
                    continue;
                }

                // Then, check if the shader instance is already unique to the render item.
                shader = fUniqueDiffuseColorShaders[groupId];
                if (shader) {
                    // Unique shader instance belongs to this render item.
                    // Set the diffuse color directly.
                    setDiffuseColor(shader.get(), sample->diffuseColor());
                    continue;
                }

                // Then, get a shared shader instance from cache.
                shader = ShaderInstanceCache::getInstance().getSharedDiffuseColorShader(
                    sample->diffuseColor());

                // If the shared shader instance is different from the existing one,
                // there is diffuse color animation.
                // We promote the shared shader instance to a unique shader instance.
                assert(fSharedDiffuseColorShaders[groupId]);  // set in updateRenderItems()
                if (shader != fSharedDiffuseColorShaders[groupId]) {
                    shader = ShaderInstanceCache::getInstance().getUniqueDiffuseColorShader(
                        sample->diffuseColor());

                    fSharedDiffuseColorShaders[groupId].reset();
                    fUniqueDiffuseColorShaders[groupId] = shader;

                    shadedItem->setShader(shader.get());
                }
            }
        }
    }

    void updateBoundingBoxItems(SubSceneOverride&   subSceneOverride,
                                MSubSceneContainer& container,
                                const MString&      subNodePrefix,
                                const MColor&       wireColor,
                                const SubNode&      subNode)
    {
        if (!fIsBoundingBoxPlaceHolder) {
            // This shape is no longer a bounding box place holder.
            // Remove the render item since we no longer need it.
            if (fBoundingBoxItem) {
                MUserData* userData = fBoundingBoxItem->customData();
                if (userData) {
                    fBoundingBoxItem->setCustomData(NULL);
                    delete userData;
                }
                container.remove(fBoundingBoxItem->name());
                fBoundingBoxItem = NULL;
            }
            return;
        }

        // Bounding box place holder render item.
        if (!fBoundingBoxItem) {
            // Create the bounding box render item.
            const MString boundingBoxItemName = subNodePrefix + ":boundingBox";
            fBoundingBoxItem = MRenderItem::Create(
                boundingBoxItemName,
                MGeometry::kLines,
                (MGeometry::DrawMode)(MGeometry::kWireframe | MGeometry::kShaded | MGeometry::kTextured),
                false
            );

            // Set the shader so that we can fill the geometry data.
            fBoundingBoxShader =
                ShaderInstanceCache::getInstance().getSharedBoundingBoxPlaceHolderShader(wireColor);
            if (fBoundingBoxShader) {
                fBoundingBoxItem->setShader(fBoundingBoxShader.get());
            }

            // Add to the container.
            container.add(fBoundingBoxItem);

            // Set unit bounding box buffer.
            MVertexBufferArray buffers;
            buffers.addBuffer("positions", UnitBoundingBox::positions()->buffer());
            subSceneOverride.setGeometryForRenderItem(
                *fBoundingBoxItem,
                buffers,
                *UnitBoundingBox::indices()->buffer(),
                &UnitBoundingBox::boundingBox()
            );

            // Add a custom data to indicate the sub-node.
            fBoundingBoxItem->setCustomData(new SubNodeUserData(subNode));
        }

        // Update shader color.
        fBoundingBoxShader =
            ShaderInstanceCache::getInstance().getSharedBoundingBoxPlaceHolderShader(wireColor);
        if (fBoundingBoxShader) {
            fBoundingBoxItem->setShader(fBoundingBoxShader.get());
        }

        toggleBoundingBoxItem();
    }

    void updateDormantWireItems(SubSceneOverride&   subSceneOverride,
                                MSubSceneContainer& container,
                                const MString&      subNodePrefix,
                                const MColor&       wireColor)
    {
        if (fIsBoundingBoxPlaceHolder) {
            // This shape is a bounding box place holder.
            if (fDormantWireItem) fDormantWireItem->enable(false);
            return;
        }

        // Update dormant wireframe item.
        if (!fDormantWireItem) {
            // Create the dormant wireframe render item.
            const MString dormantWireItemName = subNodePrefix + ":dormantWire";
            fDormantWireItem = MRenderItem::Create(
                dormantWireItemName,
                MGeometry::kLines,
                MGeometry::kWireframe,
                true    // wireframe on shaded
            );

            // Add to the container
            container.add(fDormantWireItem);
        }

        toggleDormantWireItem();

        // Dormant wireframe color.
        fDormantWireShader = (DisplayPref::wireframeOnShadedMode() == DisplayPref::kWireframeOnShadedFull)
            ? ShaderInstanceCache::getInstance().getSharedWireShader(wireColor)
            : ShaderInstanceCache::getInstance().getSharedWireShaderWithCB(wireColor);
        if (fDormantWireShader) {
            fDormantWireItem->setShader(fDormantWireShader.get());
        }
    }

    void updateActiveWireItems(SubSceneOverride&   subSceneOverride,
                               MSubSceneContainer& container,
                               const MString&      subNodePrefix,
                               const MColor&       wireColor)
    {
        if (fIsBoundingBoxPlaceHolder) {
            // This shape is a bounding box place holder or unselected.
            if (fActiveWireItem)  fActiveWireItem->enable(false);
            return;
        }

        if (!fActiveWireItem) {
            // Create the active wireframe render item.
            const MString activeWireItemName = subNodePrefix + ":activeWire";
            fActiveWireItem = MRenderItem::Create(
                activeWireItemName,
                MGeometry::kLines,
                (MGeometry::DrawMode)(MGeometry::kWireframe | MGeometry::kShaded | MGeometry::kTextured),
                true
            );

            // Add to the container.
            container.add(fActiveWireItem);
        }

        toggleActiveWireItem();

        // Active wireframe color.
        fActiveWireShader = (DisplayPref::wireframeOnShadedMode() == DisplayPref::kWireframeOnShadedFull)
            ? ShaderInstanceCache::getInstance().getSharedWireShader(wireColor)
            : ShaderInstanceCache::getInstance().getSharedWireShaderWithCB(wireColor);
        if (fActiveWireShader) {
            fActiveWireItem->setShader(fActiveWireShader.get());
        }
    }

    void updateShadedItems(SubSceneOverride&   subSceneOverride,
                           MSubSceneContainer& container,
                           const MString&      subNodePrefix,
                           const ShapeData&    shape,
                           const MColor&       diffuseColor,
                           const size_t        nbIndexGroups)
    {
        // Shaded render items.
        if (fIsBoundingBoxPlaceHolder) {
            // This shape is a bounding box place holder.
            BOOST_FOREACH (MRenderItem* item, fShadedItems) {
                item->enable(false);
            }
            return;
        }

        if (fShadedItems.empty()) {
            // Create a render item for each index group.
            fShadedItems.reserve(nbIndexGroups);

            // Each render item has an associated MShaderInstance.
            fSharedDiffuseColorShaders.reserve(nbIndexGroups);
            fUniqueDiffuseColorShaders.reserve(nbIndexGroups);
            fMaterialShaders.reserve(nbIndexGroups);

            // Cached buffers to check dirty.
            fTriangleIndices.resize(nbIndexGroups);
            fTrianglePositions.resize(nbIndexGroups);
            fTriangleNormals.resize(nbIndexGroups);
            fTriangleUVs.resize(nbIndexGroups);


            for (size_t groupId = 0; groupId < nbIndexGroups; groupId++) {
                const MString shadedItemName = subNodePrefix + ":shaded" + (int)groupId;
                MRenderItem* renderItem = MRenderItem::Create(
                    shadedItemName,
                    MGeometry::kTriangles,
                    (MGeometry::DrawMode)(MGeometry::kShaded | MGeometry::kTextured),
                    false
                );
                renderItem->setExcludedFromPostEffects(false);  // SSAO, etc..
                fShadedItems.push_back(renderItem);

                // Check if we have any material that is assigned to this index group.
                ShaderInstancePtr shader;
                const std::vector<MString>&  materialsAssignment = shape.getMaterials();
                const MaterialGraphMap::Ptr& materials = subSceneOverride.getMaterial();
                if (materials && groupId < materialsAssignment.size()) {
                    const MaterialGraph::Ptr graph = materials->find(materialsAssignment[groupId]);
                    if (graph) {
                        shader = ShaderInstanceCache::getInstance().getSharedShadedMaterialShader(
                            graph, subSceneOverride.getTime()
                        );
                    }
                }

                if (shader) {
                    // We have successfully created a material shader.
                    renderItem->setShader(shader.get());

                    fMaterialShaders.push_back(shader);
                    fSharedDiffuseColorShaders.push_back(ShaderInstancePtr());
                    fUniqueDiffuseColorShaders.push_back(ShaderInstancePtr());
                }
                else {
                    // There is no materials. Fallback to diffuse color.

                    // Let's assume that the diffuse color is not animated at beginning.
                    // If the diffuse color changes, we will promote the shared shader to
                    // a unique shader.
                    ShaderInstancePtr sharedShader =
                        ShaderInstanceCache::getInstance().getSharedDiffuseColorShader(diffuseColor);
                    if (sharedShader) {
                        renderItem->setShader(sharedShader.get());
                    }

                    fMaterialShaders.push_back(ShaderInstancePtr());
                    fSharedDiffuseColorShaders.push_back(sharedShader);
                    fUniqueDiffuseColorShaders.push_back(ShaderInstancePtr());
                }

                // Add to the container.
                container.add(renderItem);
            }
        }

        // Check if we can cast/receive shadows.
        const bool castsShadows   = subSceneOverride.castsShadows();
        const bool receiveShadows = subSceneOverride.receiveShadows();

        BOOST_FOREACH (MRenderItem* renderItem, fShadedItems) {
            // If Casts Shadows is changed, we will have to regenerate shadow maps.
            // Mark the lights and shadows dirty.
            if (renderItem->castsShadows() != castsShadows) {
                MRenderer::setLightsAndShadowsDirty();
            }

            // Set Casts Shadows and Receive Shadows.
            renderItem->castsShadows(castsShadows);
            renderItem->receivesShadows(receiveShadows);
        }

        toggleShadedItems();
    }

    // Enable or disable bounding box place holder item.
    void toggleBoundingBoxItem()
    {
        if (fBoundingBoxItem) {
            if (fIsBoundingBoxPlaceHolder) {
                fBoundingBoxItem->enable(fVisibility);
            }
            else {
                fBoundingBoxItem->enable(false);
            }
        }
    }

    // Enable or disable dormant wireframe item.
    void toggleDormantWireItem()
    {
        if (fDormantWireItem) {
            if (fIsBoundingBoxPlaceHolder) {
                fDormantWireItem->enable(false);
            }
            else {
                fDormantWireItem->enable(fVisibility && fValidPoly && !fIsSelected);
            }
        }
    }

    // Enable or disable active wireframe item.
    void toggleActiveWireItem()
    {
        if (fActiveWireItem) {
            if (fIsBoundingBoxPlaceHolder) {
                fActiveWireItem->enable(false);
            }
            else {
                fActiveWireItem->enable(fVisibility && fValidPoly && fIsSelected);
            }
        }
    }

    // Enable or disable shaded items.
    void toggleShadedItems()
    {
        BOOST_FOREACH (MRenderItem* shadedItem, fShadedItems) {
            if (fIsBoundingBoxPlaceHolder) {
                shadedItem->enable(false);
            }
            else {
                shadedItem->enable(fVisibility && fValidPoly);
            }
        }
    }

    void destroyRenderItems(MSubSceneContainer& container)
    {
        // Destroy all render items.
        if (fActiveWireItem) {
            container.remove(fActiveWireItem->name());
            fActiveWireItem = NULL;
        }

        if (fDormantWireItem) {
            container.remove(fDormantWireItem->name());
            fDormantWireItem = NULL;
        }

        if (fBoundingBoxItem) {
            MUserData* userData = fBoundingBoxItem->customData();
            if (userData) {
                fBoundingBoxItem->setCustomData(NULL);
                delete userData;
            }
            container.remove(fBoundingBoxItem->name());
            fBoundingBoxItem = NULL;
        }

        BOOST_FOREACH (MRenderItem* item, fShadedItems) {
            container.remove(item->name());
        }
        fShadedItems.clear();
    }

private:
    // Render items for this sub-node.
    MRenderItem*              fBoundingBoxItem;
    MRenderItem*              fActiveWireItem;
    MRenderItem*              fDormantWireItem;
    std::vector<MRenderItem*> fShadedItems;

    // Render item buffers for this sub-node.
    // We maintain shared buffers for each render item.
    boost::shared_ptr<const IndexBuffer>                fDormantWireIndices;
    boost::shared_ptr<const VertexBuffer>               fDormantWirePositions;
    boost::shared_ptr<const IndexBuffer>                fActiveWireIndices;
    boost::shared_ptr<const VertexBuffer>               fActiveWirePositions;
    std::vector<boost::shared_ptr<const IndexBuffer>  > fTriangleIndices;
    std::vector<boost::shared_ptr<const VertexBuffer> > fTrianglePositions;
    std::vector<boost::shared_ptr<const VertexBuffer> > fTriangleNormals;
    std::vector<boost::shared_ptr<const VertexBuffer> > fTriangleUVs;

    // The following flags control the enable/disable state of render items.
    bool fIsBoundingBoxPlaceHolder;  // The sub-node has not been loaded.
    bool fIsSelected;           // Selection state for this sub-node.
    bool fVisibility;           // Visibility for this sub-node.
    bool fValidPoly;            // False if the poly has 0 vertices.

    // World matrix for this sub-node.
    MMatrix fWorldMatrix;

    // Shader Instances for this sub-node.
    ShaderInstancePtr               fBoundingBoxShader;
    ShaderInstancePtr               fActiveWireShader;
    ShaderInstancePtr               fDormantWireShader;
    std::vector<ShaderInstancePtr>  fSharedDiffuseColorShaders;
    std::vector<ShaderInstancePtr>  fUniqueDiffuseColorShaders;
    std::vector<ShaderInstancePtr>  fMaterialShaders;
};


//==============================================================================
// CLASS SubSceneOverride::UpdateRenderItemsVisitor
//==============================================================================

// Update the render items.
class SubSceneOverride::UpdateRenderItemsVisitor : public SubNodeVisitor
{
public:
    UpdateRenderItemsVisitor(SubSceneOverride&      subSceneOverride,
                             MSubSceneContainer&    container,
                             const MString&         instancePrefix,
                             const MColor&          wireColor,
                             const bool             isSelected,
                             SubNodeRenderItemList& subNodeItems)
        : fSubSceneOverride(subSceneOverride),
          fContainer(container),
          fWireColor(wireColor),
          fIsSelected(isSelected),
          fSubNodeItems(subNodeItems),
          fLongName(instancePrefix),
          fSubNodeIndex(0)
    {}

    virtual ~UpdateRenderItemsVisitor()
    {}

    virtual void visit(const XformData&   xform,
                       const SubNode&     subNode)
    {
        // We use the hierarchical name to represent the unique render item name.
        ScopedGuard<MString> longNameGuard(fLongName);
        bool isTop = subNode.getParents().empty() && subNode.getName() == "|";
        if (!isTop) {
            fLongName += "|";
            fLongName += subNode.getName();
        }

        // Recursive calls into children
        BOOST_FOREACH (const SubNode::Ptr& child, subNode.getChildren()) {
            child->accept(*this);
        }
    }

    virtual void visit(const ShapeData&   shape,
                       const SubNode&     subNode)
    {
        // We use the hierarchical name to represent the unique render item name.
        const MString prevName = fLongName;
        fLongName += "|";
        fLongName += subNode.getName();

        // Update render items for this sub-node.
        updateRenderItems(shape, subNode);
        fSubNodeIndex++;

        // Restore to the previous name.
        fLongName = prevName;
    }

    void updateRenderItems(const ShapeData& shape, const SubNode& subNode)
    {
        // Create new sub-node render items.
        if (fSubNodeIndex >= fSubNodeItems.size()) {
            fSubNodeItems.push_back(boost::make_shared<SubNodeRenderItems>());
        }

        // Update the render items for this sub-node.
        fSubNodeItems[fSubNodeIndex]->updateRenderItems(
            fSubSceneOverride,
            fContainer,
            fLongName,
            fWireColor,
            shape,
            subNode,
            fIsSelected
        );
    }

private:
    SubSceneOverride&      fSubSceneOverride;
    MSubSceneContainer&    fContainer;
    const MColor&          fWireColor;
    const bool             fIsSelected;
    SubNodeRenderItemList& fSubNodeItems;

    MString fLongName;
    size_t  fSubNodeIndex;
};


//==============================================================================
// CLASS SubSceneOverride::UpdateVisitorWithPrune
//==============================================================================

// This class is a visitor for the sub-node hierarchy and allowing to prune
// a sub part of it.
// Curiously recurring template pattern.
// The derived class should implement the following two methods:
//
// Test if this sub-node and its descendants can be pruned.
// bool canPrune(const HierarchyStat::SubNodeStat& stat);
//
// Update the shape sub-node.
// void update(const ShapeData&         shape,
//             const SubNode&           subNode,
//             SubNodeRenderItems::Ptr& subNodeItems);
//
template<typename DERIVED>
class SubSceneOverride::UpdateVisitorWithPrune : public SubNodeVisitor
{
public:
    UpdateVisitorWithPrune(SubSceneOverride&      subSceneOverride,
                           MSubSceneContainer&    container,
                           SubNodeRenderItemList& subNodeItems)
        : fSubSceneOverride(subSceneOverride),
          fContainer(container),
          fSubNodeItems(subNodeItems),
          fDontPrune(false),
          fTraverseInvisible(false),
          fSubNodeIndex(0),
          fShapeSubNodeIndex(0)
    {}

    virtual ~UpdateVisitorWithPrune()
    {}

    // Disable prune.
    void setDontPrune(bool dontPrune)
    {
        fDontPrune = dontPrune;
    }

    // Traverse invisible sub-nodes.
    void setTraverseInvisible(bool traverseInvisible)
    {
        fTraverseInvisible = traverseInvisible;
    }

    virtual void visit(const XformData&   xform,
                       const SubNode&     subNode)
    {
        // Try to prune this sub-hierarchy.
        const HierarchyStat::Ptr& hierarchyStat = fSubSceneOverride.getHierarchyStat();
        if (hierarchyStat) {
            const HierarchyStat::SubNodeStat& stat = hierarchyStat->stat(fSubNodeIndex);

            if (!fDontPrune) {
                if (static_cast<DERIVED*>(this)->canPrune(stat)) {
                    // Prune this sub-hierarchy.
                    // Fast-forward to the next sub-node.
                    fSubNodeIndex      = stat.nextSubNodeIndex;
                    fShapeSubNodeIndex = stat.nextShapeSubNodeIndex;
                    return;
                }

                if (!fTraverseInvisible) {
                    const boost::shared_ptr<const XformSample>& sample =
                        xform.getSample(fSubSceneOverride.getTime());
                    if (sample && !sample->visibility()) {
                        // Invisible sub-node. Prune this sub-hierarchy.
                        // Fast-forward to the next sub-node.
                        fSubNodeIndex      = stat.nextSubNodeIndex;
                        fShapeSubNodeIndex = stat.nextShapeSubNodeIndex;
                        return;
                    }
                }
            }
        }

        fSubNodeIndex++;

        // Recursive calls into children.
        BOOST_FOREACH (const SubNode::Ptr& child, subNode.getChildren()) {
            child->accept(*this);
        }
    }

    virtual void visit(const ShapeData&   shape,
                       const SubNode&     subNode)
    {
        // Update the sub-node.
        assert(fShapeSubNodeIndex < fSubNodeItems.size());
        if (fShapeSubNodeIndex < fSubNodeItems.size()) {
            static_cast<DERIVED*>(this)->update(shape, subNode, fSubNodeItems[fShapeSubNodeIndex]);
        }
        fSubNodeIndex++;
        fShapeSubNodeIndex++;
    }

protected:
    SubSceneOverride&      fSubSceneOverride;
    MSubSceneContainer&    fContainer;
    SubNodeRenderItemList& fSubNodeItems;
    bool                   fDontPrune;
    bool                   fTraverseInvisible;

    size_t  fSubNodeIndex;
    size_t  fShapeSubNodeIndex;
};


//==============================================================================
// CLASS SubSceneOverride::UpdateVisibilityVisitor
//==============================================================================

// Update the visibility.
class SubSceneOverride::UpdateVisibilityVisitor :
    public SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateVisibilityVisitor>
{
public:
    typedef SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateVisibilityVisitor> ParentClass;

    UpdateVisibilityVisitor(SubSceneOverride&      subSceneOverride,
                            MSubSceneContainer&    container,
                            SubNodeRenderItemList& subNodeItems)
        : ParentClass(subSceneOverride, container, subNodeItems),
          fVisibility(true)
    {
        // The visibility visitor should always traverse into invisible sub-nodes
        // because we have to disable the render items for these invisible sub-nodes.
        setTraverseInvisible(true);
    }

    virtual ~UpdateVisibilityVisitor()
    {}

    bool canPrune(const HierarchyStat::SubNodeStat& stat)
    {
        return !stat.isVisibilityAnimated;
    }

    void update(const ShapeData&         shape,
                const SubNode&           subNode,
                SubNodeRenderItems::Ptr& subNodeItems)
    {
        // Get the shape sample.
        const boost::shared_ptr<const ShapeSample>& sample =
            shape.getSample(fSubSceneOverride.getTime());
        if (!sample) return;

        // Shape visibility.
        bool visibility = fVisibility && sample->visibility();

        subNodeItems->updateVisibility(
            fSubSceneOverride,
            fContainer,
            visibility,
            shape
        );
    }

    virtual void visit(const XformData&   xform,
                       const SubNode&     subNode)
    {
        // Get the xform sample.
        const boost::shared_ptr<const XformSample>& sample =
            xform.getSample(fSubSceneOverride.getTime());
        if (!sample) return;

        // Push visibility.
        ScopedGuard<bool> guard(fVisibility);
        fVisibility = fVisibility && sample->visibility();

        ParentClass::visit(xform, subNode);
    }

private:
    bool fVisibility;
};


//==============================================================================
// CLASS SubSceneOverride::UpdateWorldMatrixVisitor
//==============================================================================

// Update the world matrices.
class SubSceneOverride::UpdateWorldMatrixVisitor :
    public SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateWorldMatrixVisitor>
{
public:
    typedef SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateWorldMatrixVisitor> ParentClass;

    UpdateWorldMatrixVisitor(SubSceneOverride&      subSceneOverride,
                             MSubSceneContainer&    container,
                             const MMatrix&         dagMatrix,
                             SubNodeRenderItemList& subNodeItems)
        : ParentClass(subSceneOverride, container, subNodeItems),
          fMatrix(dagMatrix)
    {}

    virtual ~UpdateWorldMatrixVisitor()
    {}

    bool canPrune(const HierarchyStat::SubNodeStat& stat)
    {
        return !stat.isXformAnimated;
    }

    void update(const ShapeData&         shape,
                const SubNode&           subNode,
                SubNodeRenderItems::Ptr& subNodeItems)
    {
        subNodeItems->updateWorldMatrix(
            fSubSceneOverride,
            fContainer,
            fMatrix,
            shape
        );
    }

    virtual void visit(const XformData&   xform,
                       const SubNode&     subNode)
    {
        // Get the xform sample.
        const boost::shared_ptr<const XformSample>& sample =
            xform.getSample(fSubSceneOverride.getTime());
        if (!sample) return;

        // Push matrix.
        ScopedGuard<MMatrix> guard(fMatrix);
        fMatrix = sample->xform() * fMatrix;

        ParentClass::visit(xform, subNode);
    }

private:
    MMatrix fMatrix;
};


//==============================================================================
// CLASS SubSceneOverride::UpdateStreamsVisitor
//==============================================================================

// Update the streams.
class SubSceneOverride::UpdateStreamsVisitor :
    public SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateStreamsVisitor>
{
public:
    typedef SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateStreamsVisitor> ParentClass;

    UpdateStreamsVisitor(SubSceneOverride&      subSceneOverride,
                         MSubSceneContainer&    container,
                         SubNodeRenderItemList& subNodeItems)
        : ParentClass(subSceneOverride, container, subNodeItems)
    {}

    virtual ~UpdateStreamsVisitor()
    {}

    bool canPrune(const HierarchyStat::SubNodeStat& stat)
    {
        return !stat.isShapeAnimated;
    }

    void update(const ShapeData&         shape,
                const SubNode&           subNode,
                SubNodeRenderItems::Ptr& subNodeItems)
    {
        subNodeItems->updateStreams(
            fSubSceneOverride,
            fContainer,
            shape
        );
    }
};


//==============================================================================
// CLASS SubSceneOverride::UpdateDiffuseColorVisitor
//==============================================================================

// Update the streams.
class SubSceneOverride::UpdateDiffuseColorVisitor :
    public SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateDiffuseColorVisitor>
{
public:
    typedef SubSceneOverride::UpdateVisitorWithPrune<SubSceneOverride::UpdateDiffuseColorVisitor> ParentClass;

    UpdateDiffuseColorVisitor(SubSceneOverride&      subSceneOverride,
                              MSubSceneContainer&    container,
                              SubNodeRenderItemList& subNodeItems)
        : ParentClass(subSceneOverride, container, subNodeItems)
    {}

    virtual ~UpdateDiffuseColorVisitor()
    {}

    bool canPrune(const HierarchyStat::SubNodeStat& stat)
    {
        return !stat.isDiffuseColorAnimated;
    }

    void update(const ShapeData&         shape,
                const SubNode&           subNode,
                SubNodeRenderItems::Ptr& subNodeItems)
    {
        subNodeItems->updateMaterials(
            fSubSceneOverride,
            fContainer,
            shape
        );
    }
};


//==============================================================================
// CLASS SubSceneOverride::InstanceRenderItems
//==============================================================================

// This class contains the render items for an instance of gpuCache node.
class SubSceneOverride::InstanceRenderItems : boost::noncopyable
{
public:
    typedef boost::shared_ptr<InstanceRenderItems> Ptr;

    InstanceRenderItems()
        : fVisibility(true),
          fBoundingBoxItem(NULL),
          fVisibilityValid(false),
          fWorldMatrixValid(false),
          fStreamsValid(false),
          fMaterialsValid(false)
    {}

    ~InstanceRenderItems()
    {}

    // Update the bounding box render item.
    void updateRenderItems(SubSceneOverride&   subSceneOverride,
                           MSubSceneContainer& container,
                           const MDagPath&     dagPath,
                           const MString&      instancePrefix)
    {
        assert(dagPath.isValid());
        if (!dagPath.isValid()) return;

        // Set the path of this instance.
        fDagPath = dagPath;

        // Check if we can see the DAG node.
        fVisibility = dagPath.isVisible();

        // Early out if we can't see this instance.
        if (!fVisibility) {
            // Name prefix of this instance.
            unsigned int instancePrefixLength = instancePrefix.length();
            const char*  instancePrefixBuffer = instancePrefix.asChar();

            // Iterate and find render items for this instance.
            MSubSceneContainer::Iterator* iter = container.getIterator();
            MRenderItem* renderItem = NULL;
            while ((renderItem = iter->next()) != NULL) {
                MString name = renderItem->name();
                const char* renderItemName = name.asChar();
                if (strncmp(instancePrefixBuffer, renderItemName, instancePrefixLength) == 0) {
                    renderItem->enable(false);
                }
            }
            iter->destroy();


            // We have disabled all render items that belong to this instance.
            // When the DAG object is visible again, we need to restore visibility.
            fVisibilityValid = false;
            return;
        }

        // Check if this instance is selected.
        const DisplayStatus displayStatus =
            MGeometryUtilities::displayStatus(dagPath);
        fIsSelected = (displayStatus == kActive) ||
                      (displayStatus == kLead)   ||
                      (displayStatus == kHilite);

        // Get the wireframe color for the whole gpuCache node.
        const MColor wireColor = MGeometryUtilities::wireframeColor(dagPath);

        // Update the bounding box render item.
        if (!fBoundingBoxItem) {
            const MString boundingBoxName = instancePrefix + "BoundingBox";

            // Create the bounding box render item.
            fBoundingBoxItem = MRenderItem::Create(
                boundingBoxName,
                MGeometry::kLines,
                MGeometry::kBoundingBox,
                false);

            // Set the shader so that we can fill geometry data.
            fBoundingBoxShader =
                ShaderInstanceCache::getInstance().getSharedWireShader(wireColor);
            if (fBoundingBoxShader) {
                fBoundingBoxItem->setShader(fBoundingBoxShader.get());
            }

            // Add to the container.
            container.add(fBoundingBoxItem);

            // Set unit bounding box buffer.
            MVertexBufferArray buffers;
            buffers.addBuffer("positions", UnitBoundingBox::positions()->buffer());
            subSceneOverride.setGeometryForRenderItem(
                *fBoundingBoxItem,
                buffers,
                *UnitBoundingBox::indices()->buffer(),
                &UnitBoundingBox::boundingBox());
        }

        // Bounding box color
        fBoundingBoxShader =
            ShaderInstanceCache::getInstance().getSharedWireShader(wireColor);
        if (fBoundingBoxShader) {
            fBoundingBoxItem->setShader(fBoundingBoxShader.get());
        }

        fBoundingBoxItem->enable(true);

        // Update the sub-node render items.
        UpdateRenderItemsVisitor visitor(subSceneOverride, container,
            instancePrefix, wireColor, fIsSelected, fSubNodeItems);
        subSceneOverride.getGeometry()->accept(visitor);
    }

    void updateVisibility(SubSceneOverride&   subSceneOverride,
                          MSubSceneContainer& container)
    {
        assert(fDagPath.isValid());
        if (!fDagPath.isValid()) return;

        // Early out if we can't see this instance.
        if (!fVisibility) {
            return;
        }

        // Update the sub-node visibility.
        UpdateVisibilityVisitor visitor(subSceneOverride, container, fSubNodeItems);
        visitor.setDontPrune(!fVisibilityValid);
        subSceneOverride.getGeometry()->accept(visitor);
        fVisibilityValid = true;
    }

    void updateWorldMatrix(SubSceneOverride&   subSceneOverride,
                           MSubSceneContainer& container)
    {
        assert(fDagPath.isValid());
        if (!fDagPath.isValid()) return;

        // Early out if we can't see this instance.
        if (!fVisibility) {
            return;
        }

        // The DAG node's world matrix.
        const MMatrix pathMatrix = fDagPath.inclusiveMatrix();
        const bool pathMatrixChanged = fMatrix != pathMatrix;
        fMatrix = pathMatrix;

        // Update the bounding box render item's world matrix.
        if (fBoundingBoxItem) {
            BoundingBoxVisitor visitor(subSceneOverride.getTime());
            subSceneOverride.getGeometry()->accept(visitor);
            const MBoundingBox boundingBox = visitor.boundingBox();
            const MMatrix worldMatrix =
                UnitBoundingBox::boundingBoxMatrix(boundingBox) * fMatrix;
            fBoundingBoxItem->setMatrix(&worldMatrix);
        }

        // Update the sub-node world matrices
        UpdateWorldMatrixVisitor visitor(subSceneOverride, container,
            fMatrix, fSubNodeItems);
        visitor.setDontPrune(pathMatrixChanged || !fWorldMatrixValid);  // The DAG object's matrix has changed.
        subSceneOverride.getGeometry()->accept(visitor);
        fWorldMatrixValid = true;
    }

    void updateStreams(SubSceneOverride&   subSceneOverride,
                       MSubSceneContainer& container)
    {
        assert(fDagPath.isValid());
        if (!fDagPath.isValid()) return;

        // Early out if we can't see this instance.
        if (!fVisibility) {
            return;
        }

        // Update the sub-node streams.
        UpdateStreamsVisitor visitor(subSceneOverride, container, fSubNodeItems);
        visitor.setDontPrune(!fStreamsValid);
        subSceneOverride.getGeometry()->accept(visitor);
        fStreamsValid = true;
    }

    void updateMaterials(SubSceneOverride&   subSceneOverride,
                         MSubSceneContainer& container)
    {
        assert(fDagPath.isValid());
        if (!fDagPath.isValid()) return;

        // Early out if we can't see this instance.
        if (!fVisibility) {
            return;
        }

        // Update the sub-node diffuse color materials.
        UpdateDiffuseColorVisitor visitor(subSceneOverride, container, fSubNodeItems);
        visitor.setDontPrune(!fMaterialsValid);
        subSceneOverride.getGeometry()->accept(visitor);
        fMaterialsValid = true;
    }

    void destroyRenderItems(MSubSceneContainer& container)
    {
        // Destroy the bounding box render item for this instance.
        if (fBoundingBoxItem) {
            container.remove(fBoundingBoxItem->name());
        }

        // Destroy the sub node render items.
        BOOST_FOREACH (SubNodeRenderItems::Ptr& subNodeItem, fSubNodeItems) {
            subNodeItem->destroyRenderItems(container);
        }
    }

private:
    MDagPath              fDagPath;
    bool                  fIsSelected;
    bool                  fVisibility;
    MMatrix               fMatrix;
    MRenderItem*          fBoundingBoxItem;
    ShaderInstancePtr     fBoundingBoxShader;
    SubNodeRenderItemList fSubNodeItems;

    bool fVisibilityValid;
    bool fWorldMatrixValid;
    bool fStreamsValid;
    bool fMaterialsValid;
};


//==============================================================================
// CLASS SubSceneOverride
//==============================================================================

MPxSubSceneOverride* SubSceneOverride::creator(const MObject& object)
{
    return new SubSceneOverride(object);
}

SubSceneOverride::SubSceneOverride(const MObject& object)
    : MPxSubSceneOverride(object),
      fObject(object),
      fShapeNode(NULL),
      fUpdateRenderItemsRequired(true),
      fUpdateVisibilityRequired(true),
      fUpdateWorldMatrixRequired(true),
      fUpdateStreamsRequired(true),
      fUpdateMaterialsRequired(true),
      fOutOfViewFrustum(false),
      fOutOfViewFrustumUpdated(false),
      fWireOnShadedMode(DisplayPref::kWireframeOnShadedFull)
{
    // Extract the ShapeNode pointer.
    MFnDagNode dagNode(object);
    fShapeNode = (const ShapeNode*)dagNode.userNode();
    assert(fShapeNode);

    // Get all DAG paths.
    resetDagPaths();

    // Cache the non-networked plugs.
    fCastsShadowsPlug   = dagNode.findPlug("castsShadows", false);
    fReceiveShadowsPlug = dagNode.findPlug("receiveShadows", false);

    // Register callbacks
    MDagPath dagPath = MDagPath::getAPathTo(object);  // any path
    fInstanceAddedCallback = MDagMessage::addInstanceAddedDagPathCallback(
        dagPath, InstanceChangedCallback, this);
    fInstanceRemovedCallback = MDagMessage::addInstanceRemovedDagPathCallback(
        dagPath, InstanceChangedCallback, this);
    fWorldMatrixChangedCallback = MDagMessage::addWorldMatrixModifiedCallback(
        dagPath, WorldMatrixChangedCallback, this);
    registerNodeDirtyCallbacks();
    ModelCallbacks::getInstance().registerSubSceneOverride(fShapeNode, this);

    fUpdateTime = boost::date_time::microsec_clock<boost::posix_time::ptime>::local_time();
}

SubSceneOverride::~SubSceneOverride()
{
    // Deregister callbacks
    MMessage::removeCallback(fInstanceAddedCallback);
    MMessage::removeCallback(fInstanceRemovedCallback);
    MMessage::removeCallback(fWorldMatrixChangedCallback);
    MMessage::removeCallbacks(fNodeDirtyCallbacks);
    ModelCallbacks::getInstance().deregisterSubSceneOverride(fShapeNode);
}

MHWRender::DrawAPI SubSceneOverride::supportedDrawAPIs() const
{
    // We support both OpenGL and DX11 in VP2.0!
    return MHWRender::kAllDevices;
}

bool SubSceneOverride::requiresUpdate(const MSubSceneContainer& container,
                                      const MFrameContext&      frameContext) const
{
    assert(fShapeNode);
    if (!fShapeNode) return false;

    MRenderer* renderer = MRenderer::theRenderer();
    if (!renderer) return false;

    // Cache the DAG paths for all instances.
    if (fInstanceDagPaths.length() == 0) {
        SubSceneOverride* nonConstThis = const_cast<SubSceneOverride*>(this);
        MDagPath::getAllPathsTo(fObject, nonConstThis->fInstanceDagPaths);
    }

    // Get the cached geometry and materials.
    SubNode::Ptr          geometry = fShapeNode->getCachedGeometry();
    MaterialGraphMap::Ptr material = fShapeNode->getCachedMaterial();

    // Check if the cached geometry or materials have been changed.
    if (geometry != fGeometry || material != fMaterial) {
        return true;
    }

    // Check if the Wireframe on Shaded mode has been changed.
    if (fWireOnShadedMode != DisplayPref::wireframeOnShadedMode()) {
        return true;
    }

    // Skip update if all instances are out of view frustum.
    // Only cull when we are using default lights.
    // Shadow map generation requires the update() even if the whole
    // DAG object is out of the camera view frustum.
    if (geometry && frameContext.getLightingMode() == MFrameContext::kLightDefault) {
        // The world view proj inv matrix.
        const MMatrix viewProjInv = frameContext.getMatrix(MFrameContext::kViewProjInverseMtx);

        // The bounding box in local DAG transform space.
        BoundingBoxVisitor visitor(MAnimControl::currentTime().as(MTime::kSeconds));
        geometry->accept(visitor);

        bool outOfViewFrustum = true;
        for (unsigned int i = 0; i < fInstanceDagPaths.length(); i++) {
            const MMatrix worldInv = fInstanceDagPaths[i].inclusiveMatrixInverse();

            // Test view frustum.
            Frustum frustum(viewProjInv * worldInv,
                renderer->drawAPIIsOpenGL() ? Frustum::kOpenGL : Frustum::kDirectX);

            if (frustum.test(visitor.boundingBox()) != Frustum::kOutside) {
                outOfViewFrustum = false;
                break;
            }
        }

        // We know all the render items are going to be culled so skip update them.
        if (outOfViewFrustum) {
            // It's important to call update() once after the shape is out of the view frustum.
            // This will make sure all render items are going to be culled.
            // If the render items are still going to be culled in this frame,
            // we can then skip calling update().
            if (fOutOfViewFrustum && fOutOfViewFrustumUpdated) {
                return false;
            }
        }

        SubSceneOverride* nonConstThis = const_cast<SubSceneOverride*>(this);
        nonConstThis->fOutOfViewFrustum        = outOfViewFrustum;
        nonConstThis->fOutOfViewFrustumUpdated = false;
    }

    // Check if we are loading geometry in background.
    ShapeNode::BackgroundReadingState readingState = fShapeNode->backgroundReadingState();
    if (readingState != fReadingState) {
        // Force an update when reading is done.
        return true;
    }
    if (readingState != ShapeNode::kReadingDone) {
        // Don't update too frequently.
        boost::posix_time::ptime currentTime =
            boost::date_time::microsec_clock<boost::posix_time::ptime>::local_time();
        boost::posix_time::time_duration interval = currentTime - fUpdateTime;
        if (interval.total_milliseconds() >= (int)(Config::backgroundReadingRefresh() / 2)) {
            return true;
        }
        return false;
    }

    return fUpdateRenderItemsRequired ||
            fUpdateVisibilityRequired ||
            fUpdateWorldMatrixRequired ||
            fUpdateStreamsRequired ||
            fUpdateMaterialsRequired;
}

void SubSceneOverride::update(MSubSceneContainer&  container,
                              const MFrameContext& frameContext)
{
    assert(fShapeNode);
    if (!fShapeNode) return;

    // Register node dirty callbacks if necessary.
    if (fNodeDirtyCallbacks.length() == 0) {
        registerNodeDirtyCallbacks();
    }

    // Get the cached geometry and materials.
    SubNode::Ptr          geometry = fShapeNode->getCachedGeometry();
    MaterialGraphMap::Ptr material = fShapeNode->getCachedMaterial();

    // Remember the current time.
    fUpdateTime = boost::date_time::microsec_clock<boost::posix_time::ptime>::local_time();

    // Check if the cached geometry or materials have been changed.
    if (geometry != fGeometry || material != fMaterial) {
        // Set the cached geometry and materials.
        fGeometry = geometry;
        fMaterial = material;

        // Rebuild render items.
        fInstanceRenderItems.clear();
        container.clear();
        fHierarchyStat.reset();
        dirtyEverything();
    }

    // Check if we are loading geometry in background.
    ShapeNode::BackgroundReadingState readingState = fShapeNode->backgroundReadingState();
    if (readingState != fReadingState || readingState != ShapeNode::kReadingDone) {
        // Background reading has not finished. Update all render items.
        // (Remove bounding box render items and add shaded/wire render items.)
        fReadingState = readingState;
        dirtyEverything();
    }

    // Update the render items to match the Wireframe on Shaded mode.
    if (fWireOnShadedMode != DisplayPref::wireframeOnShadedMode()) {
        fWireOnShadedMode = DisplayPref::wireframeOnShadedMode();
        dirtyRenderItems();
    }

    // Current time in seconds
    fTimeInSeconds = MAnimControl::currentTime().as(MTime::kSeconds);

    // Update the render items.
    if (fUpdateRenderItemsRequired) {
        updateRenderItems(container, frameContext);
        fUpdateRenderItemsRequired = false;
    }

    // Update the visibility.
    if (fUpdateVisibilityRequired) {
        updateVisibility(container, frameContext);
        fUpdateVisibilityRequired = false;
    }

    // Update the world matrices.
    if (fUpdateWorldMatrixRequired) {
        updateWorldMatrix(container, frameContext);
        fUpdateWorldMatrixRequired = false;
    }

    // Update streams.
    if (fUpdateStreamsRequired) {
        updateStreams(container, frameContext);
        fUpdateStreamsRequired = false;
    }

    // Update materials.
    if (fUpdateMaterialsRequired) {
        updateMaterials(container, frameContext);
        fUpdateMaterialsRequired = false;
    }

    // Analysis the sub-node hierarchy so that we can prune it.
    if (!fHierarchyStat && fReadingState == ShapeNode::kReadingDone && fGeometry) {
        HierarchyStatVisitor visitor(fGeometry);
        fGeometry->accept(visitor);
        fHierarchyStat = visitor.getStat();

        // The geometry is fully loaded. Recompute the shadow map.
        MRenderer::setLightsAndShadowsDirty();
    }

    // We have done update() when the shape is out of view frustum.
    if (fOutOfViewFrustum) {
        fOutOfViewFrustumUpdated = true;
    }
}

void SubSceneOverride::dirtyEverything()
{
    dirtyRenderItems();
    dirtyVisibility();
    dirtyWorldMatrix();
    dirtyStreams();
    dirtyMaterials();
}

void SubSceneOverride::dirtyRenderItems()
{
    fUpdateRenderItemsRequired = true;
}

void SubSceneOverride::dirtyVisibility()
{
    fUpdateVisibilityRequired = true;
}

void SubSceneOverride::dirtyWorldMatrix()
{
    fUpdateWorldMatrixRequired = true;
}

void SubSceneOverride::dirtyStreams()
{
    fUpdateStreamsRequired = true;
}

void SubSceneOverride::dirtyMaterials()
{
    fUpdateMaterialsRequired = true;
}

void SubSceneOverride::resetDagPaths()
{
    fInstanceDagPaths.clear();
}

void SubSceneOverride::registerNodeDirtyCallbacks()
{
    assert(!fObject.isNull());
    if (fObject.isNull()) return;

    // Register callbacks to all parents.
    MDagPathArray paths;
    MDagPath::getAllPathsTo(fObject, paths);

    for (unsigned int i = 0; i < paths.length(); i++) {
        MDagPath dagPath = paths[i];

        // Register callbacks for this instance.
        while (dagPath.isValid() && dagPath.length() > 0) {
            MObject node = dagPath.node();

            // Monitor the parents and re-register callbacks.
            MCallbackId parentAddedCallback = MDagMessage::addParentAddedDagPathCallback(
                dagPath, ParentChangedCallback, this);
            MCallbackId parentRemovedCallback = MDagMessage::addParentRemovedDagPathCallback(
                dagPath, ParentChangedCallback, this);

            // Monitor parent display status changes.
            MCallbackId nodeDirtyCallback = MNodeMessage::addNodeDirtyPlugCallback(
                node, NodeDirtyCallback, this);

            // Add to array.
            fNodeDirtyCallbacks.append(parentAddedCallback);
            fNodeDirtyCallbacks.append(parentRemovedCallback);
            fNodeDirtyCallbacks.append(nodeDirtyCallback);

            dagPath.pop();
        }
    }
}

void SubSceneOverride::clearNodeDirtyCallbacks()
{
    if (fNodeDirtyCallbacks.length() > 0) {
        MMessage::removeCallbacks(fNodeDirtyCallbacks);
        fNodeDirtyCallbacks.clear();
    }
}

void SubSceneOverride::updateRenderItems(MHWRender::MSubSceneContainer&  container,
                                         const MHWRender::MFrameContext& frameContext)
{
    // Early out if the gpuCache node has no cached data.
    if (!fGeometry) {
        return;
    }

    // Match the number of the instances.
    unsigned int instanceCount = fInstanceDagPaths.length();
    if (instanceCount > fInstanceRenderItems.size()) {
        // Instance Added.
        unsigned int difference = (unsigned int)(instanceCount - fInstanceRenderItems.size());
        for (unsigned int i = 0; i < difference; i++) {
            fInstanceRenderItems.push_back(
                boost::make_shared<InstanceRenderItems>());
        }

        // Recompute shadow map.
        MRenderer::setLightsAndShadowsDirty();
    }
    else if (instanceCount < fInstanceRenderItems.size()) {
        // Instance Removed.
        unsigned int difference = (unsigned int)(fInstanceRenderItems.size() - instanceCount);
        for (unsigned int i = 0; i < difference; i++) {
            fInstanceRenderItems.back()->destroyRenderItems(container);
            fInstanceRenderItems.pop_back();
        }

        // Recompute shadow map.
        MRenderer::setLightsAndShadowsDirty();
    }
    assert(fInstanceDagPaths.length() == fInstanceRenderItems.size());

    // The MDagPath and MMatrix (world matrix) are the differences among instances.
    // We don't care the the instance number mapping. Just update the path and matrix.
    for (unsigned int i = 0; i < fInstanceRenderItems.size(); i++) {
        assert(fInstanceRenderItems[i]);
        // The name prefix for all render items of this instance
        // e.g. "1:" stands for the 2nd instance of the gpuCache node.
        const MString instancePrefix = MString("") + i + ":";

        // Update the bounding box render item.
        fInstanceRenderItems[i]->updateRenderItems(
            *this, container, fInstanceDagPaths[i], instancePrefix);
    }
}

void SubSceneOverride::updateVisibility(MHWRender::MSubSceneContainer&  container,
                                        const MHWRender::MFrameContext& frameContext)
{
    // Early out if the gpuCache node has no cached data.
    if (!fGeometry) {
        return;
    }

    // Update the visibility for all instances.
    BOOST_FOREACH (InstanceRenderItems::Ptr& instance, fInstanceRenderItems) {
        instance->updateVisibility(*this, container);
    }
}

void SubSceneOverride::updateWorldMatrix(MHWRender::MSubSceneContainer&  container,
                                         const MHWRender::MFrameContext& frameContext)
{
    // Early out if the gpuCache node has no cached data.
    if (!fGeometry) {
        return;
    }

    // Update the world matrix for all instances.
    BOOST_FOREACH (InstanceRenderItems::Ptr& instance, fInstanceRenderItems) {
        instance->updateWorldMatrix(*this, container);
    }
}

void SubSceneOverride::updateStreams(MHWRender::MSubSceneContainer&  container,
                                     const MHWRender::MFrameContext& frameContext)
{
    // Early out if the gpuCache node has no cached data.
    if (!fGeometry) {
        return;
    }

    // Update the streams for all instances.
    BOOST_FOREACH (InstanceRenderItems::Ptr& instance, fInstanceRenderItems) {
        instance->updateStreams(*this, container);
    }
}

void SubSceneOverride::updateMaterials(MHWRender::MSubSceneContainer&  container,
                                       const MHWRender::MFrameContext& frameContext)
{
    // Early out if the gpuCache node has no cached data.
    if (!fGeometry) {
        return;
    }

    // Update the diffuse color materials for all instances.
    BOOST_FOREACH (InstanceRenderItems::Ptr& instance, fInstanceRenderItems) {
        instance->updateMaterials(*this, container);
    }

    //Update the materials.
    ShaderInstanceCache::getInstance().updateCachedShadedShaders(fTimeInSeconds);
}

}
