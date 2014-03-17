#ifndef _gpuCacheSample_h_
#define _gpuCacheSample_h_

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

///////////////////////////////////////////////////////////////////////////////
//
// ShapeSample
//
// Topology and attribute samples.
//
////////////////////////////////////////////////////////////////////////////////

#include <maya/MBoundingBox.h>
#include <maya/MMatrix.h>
#include <maya/MHWGeometry.h>

#include <boost/shared_array.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/weak_ptr.hpp>
#include <boost/make_shared.hpp>
#include <boost/functional/hash.hpp>

#include <Alembic/Util/Digest.h>

#include <tbb/mutex.h>

#include <vector>

#include <gpuCacheConfig.h>

namespace GPUCache {

/*==============================================================================
 * CLASS ArrayBase
 *============================================================================*/

// Non-templated base class for hash-consed arrays. 
class ArrayBase
{
public:
    typedef Alembic::Util::Digest Digest;
    
    /*----- classes -----*/

    // Helper classes for implementing boost::unordered_map of
    // IndexBuffers.
    struct Key 
    {
        Key(const size_t  bytes,
            const Digest& digest
        ) : fBytes(bytes),
            fDigest(digest)
        {}

        const size_t fBytes;
        const Digest fDigest;
    };
    
    struct KeyHash
        : std::unary_function<Key, std::size_t>
    {
        std::size_t operator()(Key const& key) const
        {
            std::size_t seed = 0;
            boost::hash_combine(seed, key.fBytes);
            boost::hash_combine(seed, key.fDigest.words[0]);
            boost::hash_combine(seed, key.fDigest.words[1]);
            return seed;
        }
    };
    
    struct KeyEqualTo
        : std::binary_function<Key, Key, bool>
    {
        bool operator()(Key const& x,
                        Key const& y) const
        {
            return (x.fBytes  == y.fBytes &&
                    x.fDigest == y.fDigest);
        }
    };


    /*----- static member functions -----*/

    typedef void (*Callback)(const Key& array);

    // Registers a callback that will be invoked each time a new array is
    // created.
    static void registerCreationCallback(Callback callback);

    // Unregisters a previously registered creation callback.
    static void unregisterCreationCallback(Callback callback);

    // Registers a callback that will be invoked each time an array is
    // destructed.
    static void registerDestructionCallback(Callback callback);

    // Unregisters a previously registered destruction callback.
    static void unregisterDestructionCallback(Callback callback);


    /*----- member functions -----*/

    virtual ~ArrayBase();

    // The number of bytes in the array.
    size_t bytes() const    { return fKey.fBytes; }
    
    // Returns the Murmur3 checksum of the array. This is used to
    // accelerate lookups in containers.
    Digest digest() const   { return fKey.fDigest; }
    
    // Returns the key of the array. This is used to accelerate
    // lookups in containers.
    const Key& key() const  { return fKey; }
    

protected:

    /*----- member functions -----*/

    ArrayBase(size_t bytes, const Digest& digest);
    

private:
    
    /*----- data members -----*/

    // Prohibited and not implemented.
    ArrayBase(const ArrayBase&);
    const ArrayBase& operator= (const ArrayBase&);

    // We need to store this data member in the base class because
    // they are used by the destructor.
    const Key fKey;
};


/*==============================================================================
 * CLASS Array
 *============================================================================*/

// A class that provides a minimalistic virtual interface to an
// array. This is used to encapsulate various memory management
// schemes.
template <class T>
class Array : public ArrayBase
{
public:

    /*----- member functions -----*/

    // Free this reference to the array. It might or might not delete
    // the encapsulated array depending on the detail of the memory
    // management. The destructor also takes care of removing its
    // entry from the ArrayRegistry.
    virtual ~Array();

    // The elements of the array. We do not provide operator[] because
    // it would involve a virtual call for every access to an element.
    virtual const T* get() const = 0;

    // The number of elements in the array.
    size_t size() const     { return bytes() / sizeof(T); }

protected:

    /*----- member functions -----*/

    Array(size_t size, const Digest& digest)
        : ArrayBase(size * sizeof(T), digest)
    {}
};


/*==============================================================================
 * CLASS ArrayRegistry
 *============================================================================*/

// A registry of all Array currently allocated in the process. This
// registry is used to ensure that we share arrays that have the same
// contents. The digest cryptographic hash-key is used to determine if
// two buffers have the same content.
//
// Note that using cryptographic hash-keys avoids the cost of having
// to compare full buffers but it opens-up the very small possibility
// that we end-up sharing unrelated buffers due to a hash-key
// collision. We therefore also include the size of the arrays in the
// lookup to make sure that a hash-key collision won't lead to access
// violations.
//
// ArrayRegistry is currently instantiated only for
// the index_t and float types.
//
// ArrayRegistry is thread-safe.
template <typename T>
class ArrayRegistry
{
public:
    typedef Alembic::Util::Digest Digest;

    // Returns the registry mutex. It must be held while calling
    // lookup() and insert().
    static tbb::mutex& mutex();
    
    // If an array with the same digest and size is found in the registry,
    // a pointer to that array is returned. Otherwise, a null pointer
    // is returned.
    //
    // NOTE: the registry mutex must be held by the current thread
    // while calling lookup().
    static boost::shared_ptr<Array<T> > lookup(
        const Digest& digest,
        size_t size
    );

    // Inserts the given array into the registry. This function
    // assumes that lookup() has been called before to ensure that an
    // array with the same content is not already present in the
    // registry.
    //
    // NOTE: the registry mutex must be held by the current thread
    // while calling insert().
    static void insert(boost::shared_ptr<Array<T> > array);
};


/*==============================================================================
 * CLASS SharedArray
 *============================================================================*/

// A wrapper around boost shared_array smart pointers.
template <class T>
class SharedArray : public Array<T>
{
public:
    typedef typename Array<T>::Digest Digest;

    /*----- static member functions -----*/

    // Returns a pointer to a Array that has the same
    // content as the buffer passed-in as determined by the computed
    // digest hash-key. 
    static boost::shared_ptr<Array<T> > create(
        const boost::shared_array<T>& data, size_t size);


    /*----- member functions -----*/

    virtual ~SharedArray();
    virtual const T* get() const;

private:

    /*----- member functions -----*/

    // The constructor is declare private to force user to go through
    // the create() factory member function.
#ifdef BOOST_HAS_RVALUE_REFS
    template< class T, class A1, class A2, class A3 >
    friend boost::shared_ptr< T > boost::make_shared( A1 && a1, A2 && a2, A3 && a3 );
#else
    template< class U, class A1, class A2, class A3 >
    friend  boost::shared_ptr< U > boost::make_shared(
        const A1& a1, const A2& a2, const A3& a3);
#endif
 
    SharedArray(
        const boost::shared_array<T>& data,
        size_t size,
        const Digest& digest
    ) : Array<T>(size, digest),
        fData(data)
    {}
 
    /*----- data members -----*/

    const boost::shared_array<T> fData;
};


/*==============================================================================
 * CLASS IndexBuffer
 *============================================================================*/

// A buffer containing vertex attribute data.
class IndexBuffer
{
public:
    typedef Alembic::Util::Digest Digest;
    typedef unsigned int index_t;

    /*----- classes -----*/

    // Helper classes for implementing boost::unordered_map of
    // IndexBuffers.
    struct Key 
    {
        Key(
            const boost::shared_ptr<Array<index_t> >& array,
            const size_t beginIdx,
            const size_t endIdx
        ) : fArray(array),
            fBeginIdx(beginIdx),
            fEndIdx(endIdx)
        {}

        const boost::shared_ptr<Array<index_t> >    fArray;
        const size_t                                fBeginIdx;
        const size_t                                fEndIdx;
    };
    
    struct KeyHash
        : std::unary_function<Key, std::size_t>
    {
        std::size_t operator()(Key const& key) const
        {
            std::size_t seed = 0;
            boost::hash_combine(seed, key.fArray.get());
            boost::hash_combine(seed, key.fBeginIdx);
            boost::hash_combine(seed, key.fEndIdx);
            return seed;
        }
    };
    
    struct KeyEqualTo
        : std::binary_function<Key, Key, bool>
    {
        bool operator()(Key const& x,
                        Key const& y) const
        {
            return (x.fArray    == y.fArray &&
                    x.fBeginIdx == y.fBeginIdx &&
                    x.fEndIdx   == y.fEndIdx);
        }
    };


    /*----- static member functions -----*/

    static boost::shared_ptr<IndexBuffer> create(
        const boost::shared_ptr<Array<index_t> >& array
    ) 
    { return create( array, 0, array->size() ); }
    
    // Note that the endIdx is exclusive following the STL iterator
    // convention.
    static boost::shared_ptr<IndexBuffer> create(
        const boost::shared_ptr<Array<index_t> >& array,
        const size_t beginIdx,
        const size_t endIdx
    );

    // Return the number of currently allocated IndexBuffer
    // within the process.
    static size_t nbAllocated();
    
    // Return the number of bytes occupied by the currently allocated
    // IndexBuffer's within the process.
    static size_t nbAllocatedBytes();

    // Free the index buffers that are used in Viewport 2.0
    static void freeViewport2Buffers();
    

    /*----- member functions -----*/

    ~IndexBuffer();

    const index_t* data() const     { return fArray->get() + fBeginIdx; }
    size_t numIndices() const       { return fEndIdx - fBeginIdx; }
    size_t bytes() const            { return numIndices() * sizeof(index_t); }

    // Returns the index buffer that is used in Viewport 2.0
    MHWRender::MIndexBuffer* buffer() const;
    // Free the index buffer that is used in Viewport 2.0
    void                     freeBuffer() const;
    // Returns true if the Viewport 2.0 buffer has been allocated
    bool                     bufferExists() const
    { return fIndexBuffer; }

    const boost::shared_ptr<Array<index_t> >& array() const
    { return fArray; }
    
    const size_t beginIdx() const
    { return fBeginIdx; }
    
    const size_t endIdx() const
    { return fEndIdx; }

private:

    /*----- member functions -----*/

    // The constructor is declare private to force user to go through
    // the create() factory member function.
#ifdef BOOST_HAS_RVALUE_REFS
    template< class T, class A1, class A2, class A3 >
    friend boost::shared_ptr< T > boost::make_shared( A1 && a1, A2 && a2, A3 && a3 );
#else
    template< class T, class A1, class A2, class A3 >
    friend  boost::shared_ptr< T > boost::make_shared(
        const A1& a1, const A2& a2, const A3& a3);
#endif

    IndexBuffer(
        const boost::shared_ptr<Array<index_t> >& array,
        const size_t beginIdx,
        const size_t endIdx
    ) : fArray(array),
        fBeginIdx(beginIdx),
        fEndIdx(endIdx)
    {}


    /*----- data members -----*/
    
    const   boost::shared_ptr<Array<index_t> >          fArray;
    const   size_t                                      fBeginIdx;
    const   size_t                                      fEndIdx;

    mutable tbb::mutex                                  fIndexBufferMutex;
    mutable boost::shared_ptr<MHWRender::MIndexBuffer>  fIndexBuffer;
};


/*==============================================================================
 * CLASS VertexBuffer
 *============================================================================*/

// A buffer containing vertex attribute data.
class VertexBuffer
{
public:
    typedef Alembic::Util::Digest Digest;
    
    /*----- classes -----*/

    // Helper classes for implementing boost::unordered_map of
    // VertexBuffers.
    struct Key 
    {
        Key(
            const boost::shared_ptr<Array<float> >&     array,
            const MHWRender::MVertexBufferDescriptor&   desc
        ) : fArray(array),
            fName(desc.name().asChar()),
            fSemantic(desc.semantic()),
            fDataType(desc.dataType()),
            fDimension(desc.dimension()),
            fOffset(desc.offset()),
            fStride(desc.stride())
        {}

        const boost::shared_ptr<Array<float> >  fArray;
        std::string fName;
        MHWRender::MGeometry::Semantic fSemantic;
        MHWRender::MGeometry::DataType fDataType;
        int fDimension;
        int fOffset;
        int fStride;
    };
    
    struct KeyHash
        : std::unary_function<Key, std::size_t>
    {
        std::size_t operator()(Key const& key) const
        {
            std::size_t seed = 0;
            boost::hash_combine(seed, key.fArray.get());
            boost::hash_combine(seed, key.fName);
            boost::hash_combine(seed, key.fSemantic);
            boost::hash_combine(seed, key.fDataType);
            boost::hash_combine(seed, key.fDimension);
            boost::hash_combine(seed, key.fOffset);
            boost::hash_combine(seed, key.fStride);
            return seed;
        }
    };
    
    struct KeyEqualTo
        : std::binary_function<Key, Key, bool>
    {
        bool operator()(Key const& x,
                        Key const& y) const
        {
            return (x.fArray     == y.fArray &&
                    x.fName      == y.fName &&
                    x.fSemantic  == y.fSemantic &&
                    x.fDataType  == y.fDataType &&
                    x.fDimension == y.fDimension &&
                    x.fOffset    == y.fOffset &&
                    x.fStride    == y.fStride);
        }
    };
    
    
    /*----- static member functions -----*/

    static boost::shared_ptr<VertexBuffer> createPositions(
        const boost::shared_ptr<Array<float> >& array);
    
    static boost::shared_ptr<VertexBuffer> createNormals(
        const boost::shared_ptr<Array<float> >& array);
    
    static boost::shared_ptr<VertexBuffer> createUVs(
        const boost::shared_ptr<Array<float> >& array);
    
    // Return the number of currently allocated IndexBuffer
    // within the process.
    static size_t nbAllocated();
    
    // Return the number of bytes occupied by the currently allocated
    // IndexBuffer's within the process.
    static size_t nbAllocatedBytes();

    // Free the index buffers that are used in Viewport 2.0
    static void freeViewport2Buffers();


    /*----- member functions -----*/

    ~VertexBuffer();

    const float* data() const
    { return fArray->get(); }

    size_t numVerts() const
    { return fArray->size() / fDescriptor.dimension(); }

    size_t bytes() const
    { return fArray->bytes(); }
    
    // Returns the vertex buffer that is used in Viewport 2.0
    MHWRender::MVertexBuffer* buffer() const;
    // Free the vertex buffer that is used in Viewport 2.0
    void                      freeBuffer() const;
    // Returns true if the Viewport 2.0 buffer has been allocated
    bool                      bufferExists() const
    { return fVertexBuffer; }

    const boost::shared_ptr<Array<float> >& array() const
    { return fArray; }
    
    const   MHWRender::MVertexBufferDescriptor& descriptor() const
    { return fDescriptor; }

    
private:

    // The constructor is declare private to force user to go through
    // the create() factory member function.
#ifdef BOOST_HAS_RVALUE_REFS
    template< class T, class A1, class A2 >
    friend boost::shared_ptr< T > boost::make_shared( A1 && a1, A2 && a2 );
#else
    template< class T, class A1, class A2 >
    friend  boost::shared_ptr< T > boost::make_shared(
        const A1& a1, const A2& a2);
#endif
    
    /*----- static member functions -----*/

    static boost::shared_ptr<VertexBuffer> create(
        const boost::shared_ptr<Array<float> >&     array,
        const MHWRender::MVertexBufferDescriptor&   desc
    );
    
    /*----- member functions -----*/

    VertexBuffer(
        const boost::shared_ptr<Array<float> >&     array,
        const MHWRender::MVertexBufferDescriptor&   desc
    ) 
        : fArray(array),
          fDescriptor(desc)
    {}


    /*----- data members -----*/
    
    const   boost::shared_ptr<Array<float> >            fArray;
    const   MHWRender::MVertexBufferDescriptor          fDescriptor;

    mutable tbb::mutex                                  fVertexBufferMutex;
    mutable boost::shared_ptr<MHWRender::MVertexBuffer> fVertexBuffer;
};


/*==============================================================================
 * CLASS ShapeSample
 *============================================================================*/

// A sample of the topology and vertex attributes for a given time.
class ShapeSample
{
public:

    /*----- static member functions -----*/

    static boost::shared_ptr<ShapeSample> create(
        double timeInSeconds,
        size_t numWires,
        size_t numVerts,
        const boost::shared_ptr<IndexBuffer>&  wireVertIndices,
		const boost::shared_ptr<IndexBuffer>&  triangleVertIndices,
        const boost::shared_ptr<VertexBuffer>& positions,
        const MBoundingBox& boundingBox,
        const MColor&       diffuseColor,
        bool                visibility)
    {
        return boost::make_shared<ShapeSample>(
            timeInSeconds,
            numWires, numVerts,
            wireVertIndices, triangleVertIndices,
            positions, boundingBox, diffuseColor, visibility);
    }

	static boost::shared_ptr<ShapeSample> create(
		double timeInSeconds,
		size_t numWires,
		size_t numVerts,
		const boost::shared_ptr<IndexBuffer>&  wireVertIndices,
		const std::vector<boost::shared_ptr<IndexBuffer> >&  triangleVertIndices,
		const boost::shared_ptr<VertexBuffer>& positions,
		const MBoundingBox& boundingBox,
        const MColor&       diffuseColor,
        bool                visibility)
	{
		return boost::make_shared<ShapeSample>(
			timeInSeconds,
			numWires, numVerts,
			wireVertIndices, triangleVertIndices,
			positions, boundingBox, diffuseColor, visibility);
	}

    static boost::shared_ptr<ShapeSample> createEmptySample( 
        double timeInSeconds)
    {
        return ShapeSample::create(
            timeInSeconds,
            0,
            0,
            boost::shared_ptr<IndexBuffer>(),
            boost::shared_ptr<IndexBuffer>(),
            boost::shared_ptr<VertexBuffer>(),
            MBoundingBox(),
            GPUCache::Config::kDefaultGrayColor,
            false);
    }

    static boost::shared_ptr<ShapeSample> createBoundingBoxPlaceHolderSample(
        double timeInSeconds, const MBoundingBox& bbox, bool visibility)
    {
        boost::shared_ptr<ShapeSample> sample = ShapeSample::create(
            timeInSeconds,
            0,
            0,
            boost::shared_ptr<IndexBuffer>(),
            boost::shared_ptr<IndexBuffer>(),
            boost::shared_ptr<VertexBuffer>(),
            bbox,
            GPUCache::Config::kDefaultGrayColor,
            visibility);
        sample->setBoundingBoxPlaceHolder();
        return sample;
    }

    /*----- member functions -----*/

    ~ShapeSample();

    void setNormals(const boost::shared_ptr<VertexBuffer>& normals);
    void setUVs(const boost::shared_ptr<VertexBuffer>& uvs);

    double timeInSeconds() const    { return fTimeInSeconds; }

    bool visibility() const { return fVisibility; }

    size_t numWires() const         { return fNumWires; }
    size_t numTriangles(size_t groupId) const 
    { return fTriangleVertIndices[groupId] ? fTriangleVertIndices[groupId]->numIndices()/3 : 0; }
    size_t numTriangles() const;
    size_t numVerts() const         { return fNumVerts; }
    

    const boost::shared_ptr<IndexBuffer>& wireVertIndices() const
    { return fWireVertIndices; }
	const boost::shared_ptr<IndexBuffer>& triangleVertIndices(size_t groupId) const
	{ return fTriangleVertIndices[groupId]; }
	const std::vector<boost::shared_ptr<IndexBuffer> >& triangleVertexIndexGroups() const
	{ return fTriangleVertIndices; }
	size_t numIndexGroups() const
	{ return fTriangleVertIndices.size(); }

    const boost::shared_ptr<VertexBuffer>& positions() const
    { return fPositions; }
    const MBoundingBox& boundingBox() const
    { return fBoundingBox; }
    const MColor& diffuseColor() const
    { return fDiffuseColor; }

    const boost::shared_ptr<VertexBuffer>& normals() const
    { return fNormals; }
    const boost::shared_ptr<VertexBuffer>& uvs() const
    { return fUVs; }

    bool isBoundingBoxPlaceHolder() const
    { return fBoundingBoxPlaceHolder; }
    void setBoundingBoxPlaceHolder()
    { fBoundingBoxPlaceHolder = true; }
    
private:

    /*----- member functions -----*/

    // The constructor is declare private to force user to go through
    // the create() factory member function.
#ifdef BOOST_HAS_RVALUE_REFS
    template< class T, class A1, class A2, class A3, class A4, class A5,
              class A6, class A7, class A8 , class A9 >
    friend boost::shared_ptr< T > boost::make_shared(
        A1 && a1, A2 && a2, A3 && a3, A4 && a4, A5 && a5,
        A6 && a6, A7 && a7, A8 && a8, A9 && a9 );
#else
    template< class U, class A1, class A2, class A3, class A4, class A5,
        class A6, class A7, class A8, class A9 >
    friend boost::shared_ptr< U > boost::make_shared(
        A1 const & a1, A2 const & a2, A3 const & a3, A4 const & a4, A5 const & a5,
        A6 const & a6, A7 const & a7, A8 const & a8, A9 const & a9 );
#endif
    
	ShapeSample(
        double timeInSeconds,
        size_t numWires,
        size_t numVerts,
        const boost::shared_ptr<IndexBuffer>&  wireVertIndices,
        const boost::shared_ptr<IndexBuffer>&  triangleVertIndices,
        const boost::shared_ptr<VertexBuffer>& positions,
        const MBoundingBox& boundingBox,
        const MColor&       diffuseColor,
        bool                visibility
    );

	ShapeSample(
        double timeInSeconds,
        size_t numWires,
        size_t numVerts,
        const boost::shared_ptr<IndexBuffer>&  wireVertIndices,
        const std::vector<boost::shared_ptr<IndexBuffer> >&  triangleVertIndices,
        const boost::shared_ptr<VertexBuffer>& positions,
        const MBoundingBox& boundingBox,
        const MColor&       diffuseColor,
        bool                visibility
    );

    /*----- data members -----*/
    
    const double  fTimeInSeconds;

    const size_t fNumWires;
    const size_t fNumVerts;

    // Mandatory attributes
    const boost::shared_ptr<IndexBuffer>                fWireVertIndices;
    const std::vector<boost::shared_ptr<IndexBuffer> >  fTriangleVertIndices;
    const boost::shared_ptr<VertexBuffer>               fPositions;
    const MBoundingBox                                  fBoundingBox;
    const MColor                                        fDiffuseColor;
    const bool                                          fVisibility;

    // Optional attributes
    boost::shared_ptr<VertexBuffer>                     fNormals;
    boost::shared_ptr<VertexBuffer>                     fUVs;

    // Flag that this sample is a bounding box place holder for the real geometry sample
    bool                                                fBoundingBoxPlaceHolder;
};

/*==============================================================================
 * CLASS XformSample
 *============================================================================*/

// A sample of the transform matrix for a given time.
class XformSample
{
public:

	/*----- static member functions -----*/

	static boost::shared_ptr<XformSample> create(
		double              timeInSeconds,
		const MMatrix&      xform,
		const MBoundingBox& boundingBox,
		bool                visibility)
	{
		return boost::make_shared<XformSample>(
			timeInSeconds, xform, boundingBox, visibility);
	}

	/*----- member functions -----*/

	~XformSample() {}

	double timeInSeconds() const            { return fTimeInSeconds; }
	const MMatrix& xform() const            { return fXform; }
    bool isReflection() const               { return fIsReflection; }
	const MBoundingBox& boundingBox() const { return fBoundingBox; }
	bool visibility() const                 { return fVisibility; }

private:

	/*----- member functions -----*/

	// The constructor is declare private to force user to go through
	// the create() factory member function.
#ifdef BOOST_HAS_RVALUE_REFS
	template< class T, class A1, class A2, class A3, class A4>
	friend boost::shared_ptr< T > boost::make_shared(
        A1 && a1, A2 && a2, A3 && a3, A4 && a4 );
#else
	template< class T, class A1, class A2, class A3, class A4>
	friend boost::shared_ptr< T > boost::make_shared(
        const A1& a1, const A2& a2, const A3& a3, const A4& a4 );
#endif

	XformSample(double                 timeInSeconds,
                const MMatrix&         xform,
                const MBoundingBox&    boundingBox,
                bool                   visibility)
        : fTimeInSeconds(timeInSeconds),
          fXform(xform),
          fIsReflection(xform.det3x3() < 0.0f),
          fBoundingBox(boundingBox),
          fVisibility(visibility)
	{}

	/*----- data members -----*/

	const double        fTimeInSeconds;
	const MMatrix       fXform;
    const bool          fIsReflection;
	const MBoundingBox  fBoundingBox;
	const bool          fVisibility;
};

} // namespace GPUCache

#endif
