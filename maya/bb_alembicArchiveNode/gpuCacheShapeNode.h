#ifndef _gpuCacheShapeNode_h_
#define _gpuCacheShapeNode_h_

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

#include <gpuCacheGeometry.h>
#include <gpuCacheMaterial.h>
#include <CacheReader.h>

#include <maya/MPxSurfaceShape.h>
#include <maya/MPxSurfaceShapeUI.h>
#include <maya/MMessage.h>
#include <maya/MPxGeometryData.h>

#include <boost/shared_ptr.hpp>


namespace GPUCache {


///////////////////////////////////////////////////////////////////////////////
//
// ShapeNode
//
// Keeps track of animated shapes held in a memory cache.
//
////////////////////////////////////////////////////////////////////////////////


class ShapeNode : public MPxSurfaceShape
{
public:

    static void* creator();
    static MStatus initialize();
    static MStatus uninitialize();
    static MStatus init3dViewPostRenderCallbacks();

    static const MTypeId id;
    static const MString drawDbClassificationGeometry;
    static const MString drawDbClassificationSubScene;
    static const MString drawRegistrantId;
    static MObject aCacheFileName;
    static MObject aCacheGeomPath;
	static const char* nodeTypeName;
	static const char* selectionMaskName;

    enum BackgroundReadingState {
        kReadingHierarchyInProgress,
        kReadingShapesInProgress,
        kReadingDone
    };

    ShapeNode();
    ~ShapeNode();

    virtual void postConstructor();

    virtual bool isBounded() const;
    virtual MBoundingBox boundingBox() const;

	virtual bool getInternalValueInContext(const MPlug& plug,
        MDataHandle& dataHandle, MDGContext& ctx);
    virtual bool setInternalValueInContext(const MPlug& plug,
        const MDataHandle& dataHandle, MDGContext& ctx);
    virtual void copyInternalData(MPxNode* source);

    virtual MStringArray	getFilesToArchive( bool shortName = false,
                                               bool unresolvedName = false,
                                               bool markCouldBeImageSequence = false ) const;

	virtual bool match( const MSelectionMask & mask,
		const MObjectArray& componentList ) const;

	virtual bool excludeAsPluginShape() const;

    void refreshCachedGeometry();
    
    const GPUCache::SubNode::Ptr& getCachedGeometry() const;

    const GPUCache::MaterialGraphMap::Ptr& getCachedMaterial() const;

    const BackgroundReadingState backgroundReadingState() const;
    
    // Callback when the gpuCache node is removed from model (delete)
    void removedFromModelCB();

private:
    // Prohibited and not implemented.
    ShapeNode(const ShapeNode& obj);
    const ShapeNode& operator=(const ShapeNode& obj);

    bool setInternalValues(const MString& newFileName,
                           const MString& newGeomPath);
	
    mutable MString fCacheFileName;
    mutable MString fCacheGeomPath;

    mutable GPUCache::SubNode::Ptr                   fCachedGeometry;
    mutable GPUCache::MaterialGraphMap::Ptr          fCachedMaterial;
    mutable GlobalReaderCache::CacheReaderProxy::Ptr fCacheReaderProxy;
    mutable BackgroundReadingState                   fBackgroundReadingState;

    MCallbackId fRemoveFromModelCallbackId;
    
    static MCallbackId fsModelEditorChangedCallbackId;
};


///////////////////////////////////////////////////////////////////////////////
//
// DisplayPref
//
// Keeps track of the display preference.
//
////////////////////////////////////////////////////////////////////////////////

class DisplayPref
{
public:
    enum WireframeOnShadedMode {
        kWireframeOnShadedFull,
        kWireframeOnShadedReduced,
        kWireframeOnShadedNone
    };

    static WireframeOnShadedMode wireframeOnShadedMode();

    static MStatus initCallback();
    static MStatus removeCallback();

private:
    static void displayPrefChanged(void*);

    static WireframeOnShadedMode fsWireframeOnShadedMode;
    static MCallbackId fsDisplayPrefChangedCallbackId;
};


///////////////////////////////////////////////////////////////////////////////
//
// ShapeUI
//
// Displays animated shapes held in a memory cache.
//
////////////////////////////////////////////////////////////////////////////////

class ShapeUI : public MPxSurfaceShapeUI
{
public:

    static void* creator();

    ShapeUI();
    ~ShapeUI();

    virtual void getDrawRequests(const MDrawInfo & info,
                                 bool objectAndActiveOnly,
                                 MDrawRequestQueue & queue);

    // Viewport 1.0 draw
    virtual void draw(const MDrawRequest & request, M3dView & view) const;
    
	virtual bool snap(MSelectInfo &snapInfo) const;
	virtual bool select(MSelectInfo &selectInfo, MSelectionList &selectionList,
                        MPointArray &worldSpaceSelectPts ) const;


private:
    // Prohibited and not implemented.
	ShapeUI(const ShapeUI& obj);
	const ShapeUI& operator=(const ShapeUI& obj);

	static MPoint getPointAtDepth( MSelectInfo &selectInfo,double depth);

    // Helper functions for the viewport 1.0 drawing purposes.
    void drawBoundingBox(const MDrawRequest & request, M3dView & view ) const;
    void drawWireframe(const MDrawRequest & request, M3dView & view ) const;
    void drawShaded(const MDrawRequest & request, M3dView & view, bool depthOffset ) const;
    
	// Draw Tokens
	enum DrawToken {
        kBoundingBox,
		kDrawWireframe,
        kDrawWireframeOnShaded,
		kDrawSmoothShaded,
		kDrawSmoothShadedDepthOffset
	};
};

}

#endif
