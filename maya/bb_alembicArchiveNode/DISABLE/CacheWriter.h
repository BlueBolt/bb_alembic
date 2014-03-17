#ifndef _CacheWriter_h_
#define _CacheWriter_h_

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

// Includes
#include <gpuCacheGeometry.h>
#include <gpuCacheMaterial.h>

#include <maya/MMatrix.h>
#include <maya/MFnTransform.h>
#include <maya/MTime.h>
#include <maya/MString.h>
#include <maya/MFileObject.h>

#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>
#include <vector>

// Forward Declarations
class CacheXformSampler;
class CacheMeshSampler;
class MFnMesh;


//==============================================================================
// CLASS CacheWriter
//==============================================================================

class CacheWriter
{
public:
    // Create a cache writer
    static boost::shared_ptr<CacheWriter> create(const MString& impl,
        const MFileObject& file, char compressLevel, const MString& dataFormat);
    
    typedef boost::shared_ptr<CacheWriter> CreateFunction(const MFileObject& file,
        char compressLevel, const MString& dataFormat);
    static void registerWriter(const MString& impl, CreateFunction func);

    virtual ~CacheWriter() {}
    virtual bool valid() const = 0;

    // Write the hierarchy of nodes under the given top level node to
    // the cache file.
    virtual void writeSubNodeHierarchy(
        const GPUCache::SubNode::Ptr& topNode, 
        double secondsPerSample, double startTimeInSeconds) = 0;

    // Write the materials to the cache file.
    virtual void writeMaterials(
        const GPUCache::MaterialGraphMap::Ptr& materialGraphMap,
        double secondsPerSample, double startTimeInSeconds) = 0;

    // Returns the actual file name the implementation is writing
    virtual const MFileObject& getFileObject() const = 0;

protected:
    CacheWriter() {}

private:
    // Prohibited and not implemented.
    CacheWriter(const CacheWriter&);
    const CacheWriter& operator=(const CacheWriter&);

    static std::map<std::string,void*> fsRegistry;
};


//==============================================================================
// CLASS CacheXformSampler
//==============================================================================

class CacheXformSampler
{
public:
    static boost::shared_ptr<CacheXformSampler> create(const MObject& xformObject);

    ~CacheXformSampler();

    // Bake a sample at the current time.
    void addSample();

    bool isAnimated() const {
        return fXformAnimated || fVisibilityAnimated; }

	boost::shared_ptr<const GPUCache::XformSample> getSample(double timeInSeconds);

private:
    CacheXformSampler(const MObject& xformObject);

#ifdef BOOST_HAS_RVALUE_REFS
    template< class T, class A1 >
    friend boost::shared_ptr< T > boost::make_shared( A1 && a1 );
#else
    template< class U, class A1 >
    friend boost::shared_ptr< U > boost::make_shared( const A1& a1 );
#endif
    
    MFnTransform    fXform;
    bool            fIsFirstSample;
    
	// local matrix
    MMatrix         fXformSample;
    bool            fXformAnimated;

	// local visibility
    bool            fVisibilitySample;
    bool            fVisibilityAnimated;
};


//==============================================================================
// CLASS CacheMeshSampler
//==============================================================================

class CacheMeshSampler
{
public:
    static boost::shared_ptr<CacheMeshSampler> create(bool needUVs);

    ~CacheMeshSampler();

    bool addSample(MObject meshObject, bool visibility);
	bool addSampleFromMesh(MFnMesh& mesh);

    bool isAnimated() const 
    {
        return fIsAnimated;
    }

    // diffuseColor is per-instance attribute. CacheMeshSampler does not care about
    // instances so the diffuse color is passed from outside (Bakers).
    // getSample() is called for each instance.
    boost::shared_ptr<const GPUCache::ShapeSample>
        getSample(double timeInSeconds, const MColor& diffuseColor);
    
private:
    CacheMeshSampler(bool needUVs);

#ifdef BOOST_HAS_RVALUE_REFS
    template< class T, class A1 >
    friend boost::shared_ptr< T > boost::make_shared( A1 && a1 );
#else
    template< class U, class A1 >
    friend boost::shared_ptr< U > boost::make_shared( const A1& a1 );
#endif

    struct AttributeSet
    {
        size_t fNumWires;
        size_t fNumTriangles;
        size_t fNumVerts;

        boost::shared_ptr<GPUCache::IndexBuffer>    fWireVertIndices;
        std::vector<boost::shared_ptr<GPUCache::IndexBuffer> > fTriangleVertIndices;
        boost::shared_ptr<GPUCache::VertexBuffer>   fPositions;
        boost::shared_ptr<GPUCache::VertexBuffer>   fNormals;
        boost::shared_ptr<GPUCache::VertexBuffer>   fUVs;
        MBoundingBox                                fBoundingBox;
        bool                                        fVisibility;

        AttributeSet() 
            : fNumWires(0), fNumTriangles(0), fNumVerts(0), fVisibility(true)
        {}
        AttributeSet(
            MObject meshObject, bool visibility, bool needUVs);
        AttributeSet(MFnMesh& mesh, bool needUVs);

        // Replaces the animated channels contained in this
        // AttributeSet with the one contained in "newer". Returns
        // whether any attributes are animated.
        bool updateAnimatedChannels(
            bool& animated, const AttributeSet& newer,
            const MString& path=MString());
    };

    const bool fNeedUVs;
    bool fIsAnimated;
    AttributeSet fAttributeSet;
};

#endif

