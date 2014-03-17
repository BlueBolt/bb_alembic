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

#include <gpuCacheSample.h>
#include <gpuCacheVBOProxy.h>

#include <Alembic/Util/Murmur3.h>

#include <boost/weak_ptr.hpp>
#include <boost/unordered_map.hpp>
#include <boost/foreach.hpp>

#include <cassert>

namespace {

using namespace GPUCache;

//==============================================================================
// LOCAL FUNCTIONS & CLASSES
//==============================================================================

//==============================================================================
// CLASS ArrayBaseImp
//==============================================================================

class ArrayBaseImp 
{
public:
    typedef ArrayBase::Callback Callback;
    typedef ArrayBase::Key      Key;
    
    static void registerCreationCallback(Callback callback)
    {
        creationCallbacks.push_back(callback);
    }
    
    static void unregisterCreationCallback(Callback callback)
    {
        Callbacks::iterator it = std::find(
            creationCallbacks.begin(), creationCallbacks.end(), callback);
        if (it != creationCallbacks.end()) {
            creationCallbacks.erase(it);
        }
    }

    static void invokeCreationCallback(const Key& key)
    {
        BOOST_FOREACH(const Callback& callback, creationCallbacks) {
            (*callback)(key);
        }
    }
    
    static void registerDestructionCallback(Callback callback)
    {
        destructionCallbacks.push_back(callback);
    }
        
    static void unregisterDestructionCallback(Callback callback)
    {
        Callbacks::iterator it = std::find(
            destructionCallbacks.begin(), destructionCallbacks.end(), callback);
        if (it != destructionCallbacks.end()) {
            destructionCallbacks.erase(it);
        }
    }

    static void invokeDestructionCallback(const Key& key)
    {
        BOOST_FOREACH(const Callback& callback, destructionCallbacks) {
            (*callback)(key);
        }
    }
    
private:
    typedef std::vector<ArrayBase::Callback> Callbacks;
    
    static Callbacks creationCallbacks;
    static Callbacks destructionCallbacks;
};

ArrayBaseImp::Callbacks ArrayBaseImp::creationCallbacks;
ArrayBaseImp::Callbacks ArrayBaseImp::destructionCallbacks;


//==============================================================================
// CLASS ArrayRegistryImp
//==============================================================================

template <typename T>
class ArrayRegistryImp
{
public:
    typedef ArrayBase::Digest     Digest;
    typedef ArrayBase::Key        Key;
    typedef ArrayBase::KeyHash    KeyHash;
    typedef ArrayBase::KeyEqualTo KeyEqualTo;

    static ArrayRegistryImp<T>& singleton()
    { return fsSingleton; }

    ~ArrayRegistryImp()
    {
        // Unfortunately, we can't check that all buffers have been
        // freed here. The reason is Maya does not take the time to
        // clean-up the dependency graph when exiting. Therefore,
        // there might still exist some ShapeNode alived at exit time
        // and these will indirectly keep these buffers alive.
        //
        // The way to check that the mechanism is working correctly is
        // therefore to perform the following MEL commands "file -f
        // -new; gpuCache -q -sgs;" and check that everything has
        // been freed. The gpuCache regression test does that.
        //
        // assert(fMap.size() == 0);
    } 

    tbb::mutex& mutex() 
    { return fMutex; }
    
    
    boost::shared_ptr<Array<T> > lookup(
        const Digest& digest,
        size_t size
    )
    {
        typename Map::const_iterator it =
            fMap.find(Key(size * sizeof(T), digest));
        if (it != fMap.end()) {
            // Might return null if the weak_ptr<> is now dangling
            // but not yet removed from the map...
            boost::shared_ptr<Array<T> > ret = it->second.lock();
            if (!ret) {
                // Get rid of the stalled entry so that insert() can
                // work properly.
                fMap.erase(it);
            }
            return ret;
        }
        else {
            return boost::shared_ptr<Array<T> >();
        }
    }   

    void insert(boost::shared_ptr<Array<T> > array)
    {
        fMap.insert(std::make_pair(array->key(), array));
    }

    void removeIfStaled(const Key& key)
    {
        typename Map::const_iterator it = fMap.find(key);
        if (it != fMap.end()) {
            // Might return null if the weak_ptr<> is now dangling
            // but not yet removed from the map...
            boost::shared_ptr<Array<T> > ret = it->second.lock();
            if (!ret) {
                // Get rid of the stalled entry so that insert() can
                // work properly.
                fMap.erase(it);
            }
        }
    }

private:
    typedef boost::unordered_map<
        Key,
        boost::weak_ptr<Array<T> >,
        KeyHash,
        KeyEqualTo> Map;

    static ArrayRegistryImp fsSingleton;

    tbb::mutex fMutex;
    Map fMap;
};

template <typename T>
ArrayRegistryImp<T> ArrayRegistryImp<T>::fsSingleton;

template class ArrayRegistryImp<IndexBuffer::index_t>;
template class ArrayRegistryImp<float>;


//==============================================================================
// CLASS IndexBufferRegistry
//==============================================================================

class IndexBufferRegistry
{
public:
    typedef IndexBuffer::index_t    index_t;
    typedef IndexBuffer::Key        Key;
    typedef IndexBuffer::KeyHash    KeyHash;
    typedef IndexBuffer::KeyEqualTo KeyEqualTo;
    
    static IndexBufferRegistry& singleton()
    { return fsSingleton; }

    ~IndexBufferRegistry() {}

    tbb::mutex& mutex() 
    { return fMutex; }
    
    
    boost::shared_ptr<IndexBuffer> lookup(
        const boost::shared_ptr<Array<index_t> >& array,
        const size_t beginIdx,
        const size_t endIdx
    )
    {
        Map::const_iterator it = fMap.find(Key(array, beginIdx, endIdx));
        if (it != fMap.end()) {
            // Might return null if the weak_ptr<> is now dangling
            // but not yet removed from the map...
            boost::shared_ptr<IndexBuffer> ret = it->second.lock();
            if (!ret) {
                // Get rid of the stalled entry so that insert() can
                // work properly.
                fMap.erase(it);
            }
            return ret;
        }
        else {
            return boost::shared_ptr<IndexBuffer>();
        }
    }   

    void insert(boost::shared_ptr<IndexBuffer> buffer)
    {
        fMap.insert(
            std::make_pair(
                Key(buffer->array(), buffer->beginIdx(), buffer->endIdx()),
                buffer));
    }

    void removeIfStaled(
        const boost::shared_ptr<Array<index_t> >& array,
        const size_t beginIdx,
        const size_t endIdx
    )
    {
        Map::const_iterator it = fMap.find(Key(array, beginIdx, endIdx));
        if (it != fMap.end()) {
            // Might return null if the weak_ptr<> is now dangling
            // but not yet removed from the map...
            boost::shared_ptr<IndexBuffer> ret = it->second.lock();
            if (!ret) {
                // Get rid of the stalled entry so that insert() can
                // work properly.
                fMap.erase(it);
            }
        }
    }

    size_t nbAllocated()
    { return fMap.size(); }
    
    size_t nbAllocatedBytes()
    {
        size_t bytes = 0;
        BOOST_FOREACH(const Map::value_type& v, fMap) {
            boost::shared_ptr<IndexBuffer> buf = v.second.lock();
            if (buf) {
                bytes += buf->bytes();
            }
        }
        return bytes;
    }

    void freeViewport2Buffers()
    {
        BOOST_FOREACH(const Map::value_type& v, fMap) {
            boost::shared_ptr<IndexBuffer> buf = v.second.lock();
            if (buf) {
                buf->freeBuffer();
            }
        }
    }

private:
    typedef boost::unordered_map<
        Key,
        boost::weak_ptr<IndexBuffer>,
        KeyHash,
        KeyEqualTo
    > Map;

    static IndexBufferRegistry fsSingleton;

    tbb::mutex fMutex;
    Map fMap;
};

IndexBufferRegistry IndexBufferRegistry::fsSingleton;


//==============================================================================
// CLASS VertexBufferRegistry
//==============================================================================

class VertexBufferRegistry
{
public:
    typedef VertexBuffer::Key Key;
    typedef VertexBuffer::KeyHash KeyHash;
    typedef VertexBuffer::KeyEqualTo KeyEqualTo;
    
    static VertexBufferRegistry& singleton()
    { return fsSingleton; }

    ~VertexBufferRegistry() {}

    tbb::mutex& mutex() 
    { return fMutex; }
    
    boost::shared_ptr<VertexBuffer> lookup(
        const boost::shared_ptr<Array<float> >&     array,
        const MHWRender::MVertexBufferDescriptor&   desc
    )
    {
        Map::const_iterator it = fMap.find(Key(array, desc));
        if (it != fMap.end()) {
            // Might return null if the weak_ptr<> is now dangling
            // but not yet removed from the map...
            boost::shared_ptr<VertexBuffer> ret = it->second.lock();
            if (!ret) {
                // Get rid of the stalled entry so that insert() can
                // work properly.
                fMap.erase(it);
            }
            return ret;
        }
        else {
            return boost::shared_ptr<VertexBuffer>();
        }
    }   

    void insert(boost::shared_ptr<VertexBuffer> buffer)
    {
        fMap.insert(
            std::make_pair(
                Key(buffer->array(), buffer->descriptor()),
                buffer));
    }

    void removeIfStaled(
        const boost::shared_ptr<Array<float> >&     array,
        const MHWRender::MVertexBufferDescriptor&   desc
    )
    {
        Map::const_iterator it = fMap.find(Key(array, desc));
        if (it != fMap.end()) {
            // Might return null if the weak_ptr<> is now dangling
            // but not yet removed from the map...
            boost::shared_ptr<VertexBuffer> ret = it->second.lock();
            if (!ret) {
                // Get rid of the stalled entry so that insert() can
                // work properly.
                fMap.erase(it);
            }
        }
    }

    size_t nbAllocated()
    { return fMap.size(); }
    
    size_t nbAllocatedBytes()
    {
        size_t bytes = 0;
        BOOST_FOREACH(const Map::value_type& v, fMap) {
            boost::shared_ptr<VertexBuffer> buf = v.second.lock();
            if (buf) {
                bytes += buf->bytes();
            }
        }
        return bytes;
    }

    void freeViewport2Buffers()
    {
        BOOST_FOREACH(const Map::value_type& v, fMap) {
            boost::shared_ptr<VertexBuffer> buf = v.second.lock();
            if (buf) {
                buf->freeBuffer();
            }
        }
    }

private:
    typedef boost::unordered_map<
        Key,
        boost::weak_ptr<VertexBuffer>,
        KeyHash,
        KeyEqualTo
    > Map;

    static VertexBufferRegistry fsSingleton;

    tbb::mutex fMutex;
    Map fMap;
};

VertexBufferRegistry VertexBufferRegistry::fsSingleton;

}


namespace GPUCache {

//==============================================================================
// CLASS ArrayBase
//==============================================================================

void ArrayBase::registerCreationCallback(Callback callback)
{
    ArrayBaseImp::registerCreationCallback(callback);
}

void ArrayBase::unregisterCreationCallback(Callback callback)
{
    ArrayBaseImp::unregisterCreationCallback(callback);
}

void ArrayBase::registerDestructionCallback(Callback callback)
{
    ArrayBaseImp::registerDestructionCallback(callback);
}

void ArrayBase::unregisterDestructionCallback(Callback callback)
{
    ArrayBaseImp::unregisterDestructionCallback(callback);
}

ArrayBase::ArrayBase(size_t bytes, const Digest& digest)
    : fKey(bytes, digest)
{
    ArrayBaseImp::invokeCreationCallback(fKey);
}

ArrayBase::~ArrayBase()
{
    ArrayBaseImp::invokeDestructionCallback(fKey);
}

//==============================================================================
// CLASS Array
//==============================================================================

template <typename T>
Array<T>::~Array()
{
    tbb::mutex::scoped_lock lock(ArrayRegistryImp<T>::singleton().mutex());
    ArrayRegistryImp<T>::singleton().removeIfStaled(key());
}

template class Array<IndexBuffer::index_t>;
template class Array<float>;


//==============================================================================
// CLASS ArrayRegistry
//==============================================================================

template <typename T>
tbb::mutex& ArrayRegistry<T>::mutex()
{
    return ArrayRegistryImp<T>::singleton().mutex();
}

template <typename T>
boost::shared_ptr<Array<T> > ArrayRegistry<T>::lookup(
    const Digest& digest,
    size_t size
)
{
    boost::shared_ptr<Array<T> > result =
        ArrayRegistryImp<T>::singleton().lookup(digest, size);

    assert(!result || result->digest() == digest);
    assert(!result || result->bytes()  == size * sizeof(T));
    
    return result;
}

template <typename T>
void ArrayRegistry<T>::insert(
    boost::shared_ptr<Array<T> > array
)
{
    ArrayRegistryImp<T>::singleton().insert(array);
}

template class ArrayRegistry<IndexBuffer::index_t>;
template class ArrayRegistry<float>;


//==============================================================================
// CLASS SharedArray
//==============================================================================

template <typename T>
boost::shared_ptr<Array<T> >
SharedArray<T>::create(
    const boost::shared_array<T>& data, size_t size)
{
    // Compute the Murmur3 cryptographic hash-key.
    Digest digest;
    Alembic::Util::MurmurHash3_x64_128(
        data.get(), size * sizeof(T), sizeof(T), digest.words);

    // We first look if a similar array already exists in the
    // cache. If so, we return the cached array to promote sharing as
    // much as possible.
    boost::shared_ptr<Array<T> > ret;
    {
        tbb::mutex::scoped_lock lock(ArrayRegistry<T>::mutex());

        ret = ArrayRegistry<T>::lookup(digest, size);
        
        if (!ret) {
            ret = boost::make_shared<SharedArray<T> >(
                data, size, digest);
            ArrayRegistry<T>::insert(ret);
        }
    }

    return ret;
}

template <typename T>
SharedArray<T>::~SharedArray()
{}

template <typename T>
const T* SharedArray<T>::get() const
{
    return fData.get();
}

template class SharedArray<IndexBuffer::index_t>;
template class SharedArray<float>;


//==============================================================================
// CLASS IndexBuffer
//==============================================================================

boost::shared_ptr<IndexBuffer> IndexBuffer::create(
    const boost::shared_ptr<Array<index_t> >& array,
    const size_t beginIdx,
    const size_t endIdx
)
{
    // We first look if a similar array already exists in the
    // cache. If so, we return the cached array to promote sharing as
    // much as possible.
    boost::shared_ptr<IndexBuffer> ret;
    {
        tbb::mutex::scoped_lock lock(
            IndexBufferRegistry::singleton().mutex());

        ret = IndexBufferRegistry::singleton().lookup(
            array, beginIdx, endIdx);
        
        if (!ret) {
            ret = boost::make_shared<IndexBuffer>(
                array, beginIdx, endIdx);
            IndexBufferRegistry::singleton().insert(ret);
        }
    }

    return ret;
}

size_t IndexBuffer::nbAllocated()
{
    tbb::mutex::scoped_lock lock(
        IndexBufferRegistry::singleton().mutex());
    return IndexBufferRegistry::singleton().nbAllocated();
}

    
size_t IndexBuffer::nbAllocatedBytes()
{
    tbb::mutex::scoped_lock lock(
        IndexBufferRegistry::singleton().mutex());
    return IndexBufferRegistry::singleton().nbAllocatedBytes();
}

void IndexBuffer::freeViewport2Buffers()
{
    tbb::mutex::scoped_lock lock(
        IndexBufferRegistry::singleton().mutex());
    IndexBufferRegistry::singleton().freeViewport2Buffers();
}

IndexBuffer::~IndexBuffer()
{
    tbb::mutex::scoped_lock lock(
        IndexBufferRegistry::singleton().mutex());
    IndexBufferRegistry::singleton().removeIfStaled(
        fArray, fBeginIdx, fEndIdx);
}

MHWRender::MIndexBuffer* IndexBuffer::buffer() const
{
    using namespace MHWRender;
    
    const size_t size = numIndices();

    tbb::mutex::scoped_lock(fIndexBufferMutex);
    if (!fIndexBuffer && size != 0) {
        fIndexBuffer =
            boost::shared_ptr<MIndexBuffer>(
                new MIndexBuffer(MGeometry::kUnsignedInt32));

        index_t* buf = (index_t*)fIndexBuffer->acquire((int)size, true /*writeOnly - we don't need the current buffer values*/);
        memcpy(buf, data(), size * sizeof(index_t));
        fIndexBuffer->commit(buf);
    }
    
    return fIndexBuffer.get();
}

void IndexBuffer::freeBuffer() const
{
    tbb::mutex::scoped_lock(fIndexBufferMutex);
    if (fIndexBuffer) {
        fIndexBuffer.reset();
    }
}

//==============================================================================
// CLASS VertexBuffer
//==============================================================================

boost::shared_ptr<VertexBuffer>
VertexBuffer::createPositions(
    const boost::shared_ptr<Array<float> >& array)
{
    return create(array,
                  MHWRender::MVertexBufferDescriptor(
                      MString(""),
                      MHWRender::MGeometry::kPosition,
                      MHWRender::MGeometry::kFloat, 3));
}

boost::shared_ptr<VertexBuffer>
VertexBuffer::createNormals(
    const boost::shared_ptr<Array<float> >& array)
{
    return create(array,
                  MHWRender::MVertexBufferDescriptor(
                      MString(""),
                      MHWRender::MGeometry::kNormal,
                      MHWRender::MGeometry::kFloat, 3));
}

boost::shared_ptr<VertexBuffer>
VertexBuffer::createUVs(
    const boost::shared_ptr<Array<float> >& array)
{
    return create( array,
                   MHWRender::MVertexBufferDescriptor(
                       MString("mayaUVIn"),
                       MHWRender::MGeometry::kTexture,
                       MHWRender::MGeometry::kFloat, 2));
}

boost::shared_ptr<VertexBuffer>
VertexBuffer::create(
    const boost::shared_ptr<Array<float> >&     array,
    const MHWRender::MVertexBufferDescriptor&   desc)
{
    // We first look if a similar array already exists in the
    // cache. If so, we return the cached array to promote sharing as
    // much as possible.
    boost::shared_ptr<VertexBuffer> ret;
    {
        tbb::mutex::scoped_lock lock(
            VertexBufferRegistry::singleton().mutex());

        ret = VertexBufferRegistry::singleton().lookup(array, desc);
        
        if (!ret) {
            ret = boost::make_shared<VertexBuffer>(
                array, desc);
            VertexBufferRegistry::singleton().insert(ret);
        }
    }

    return ret;
}

size_t VertexBuffer::nbAllocated()
{
    tbb::mutex::scoped_lock lock(
        VertexBufferRegistry::singleton().mutex());
    return VertexBufferRegistry::singleton().nbAllocated();
}

    
size_t VertexBuffer::nbAllocatedBytes()
{
    tbb::mutex::scoped_lock lock(
        VertexBufferRegistry::singleton().mutex());
    return VertexBufferRegistry::singleton().nbAllocatedBytes();
}

void VertexBuffer::freeViewport2Buffers()
{
    tbb::mutex::scoped_lock lock(
        VertexBufferRegistry::singleton().mutex());
    VertexBufferRegistry::singleton().freeViewport2Buffers();
}

VertexBuffer::~VertexBuffer()
{
    tbb::mutex::scoped_lock lock(
        VertexBufferRegistry::singleton().mutex());
    VertexBufferRegistry::singleton().removeIfStaled(
        fArray, fDescriptor);
}

MHWRender::MVertexBuffer* VertexBuffer::buffer() const
{
    using namespace MHWRender;
    
    const size_t size = fArray->size();
    
    tbb::mutex::scoped_lock(fVertexBufferMutex);
    if (!fVertexBuffer && size != 0) {
    
        // FIXME: Assumes 32-bit float data.
        assert(fDescriptor.dataType() == MGeometry::kFloat);
        
        fVertexBuffer =
            boost::shared_ptr<MVertexBuffer>(new MVertexBuffer(fDescriptor));

        float* buf = (float*)fVertexBuffer->acquire((int) numVerts(), true /*writeOnly - we don't need the current buffer values*/);
        memcpy(buf, data(), size * sizeof(float));
        fVertexBuffer->commit(buf);
    }
    
    return fVertexBuffer.get();
}

void VertexBuffer::freeBuffer() const
{
    tbb::mutex::scoped_lock(fVertexBufferMutex);
    if (fVertexBuffer) {
        fVertexBuffer.reset();
    }
}


//==============================================================================
// CLASS ShapeSample
//==============================================================================

ShapeSample::ShapeSample(
    double timeInSeconds,
    size_t numWires,
    size_t numVerts,
    const boost::shared_ptr<IndexBuffer>&  wireVertIndices,
    const boost::shared_ptr<IndexBuffer>&  triangleVertIndices,
    const boost::shared_ptr<VertexBuffer>& positions,
    const MBoundingBox& boundingBox,
    const MColor&       diffuseColor,
    bool                visibility
) 
    : fTimeInSeconds(timeInSeconds),
      fNumWires(numWires),
      fNumVerts(numVerts),
      fWireVertIndices(wireVertIndices),
	  fTriangleVertIndices(
	  std::vector<boost::shared_ptr<IndexBuffer> >(1, triangleVertIndices)),
      fPositions(positions),
      fBoundingBox(boundingBox),
      fDiffuseColor(diffuseColor),
      fVisibility(visibility),
      fBoundingBoxPlaceHolder(false)
{
    assert( wireVertIndices ? (wireVertIndices->numIndices() == 2 * fNumWires) : (fNumWires == 0) );
    assert( positions ? (positions->numVerts() == fNumVerts) : (fNumVerts == 0) );
}

ShapeSample::ShapeSample(
    double timeInSeconds,
    size_t numWires,
    size_t numVerts,
    const boost::shared_ptr<IndexBuffer>&  wireVertIndices,
    const std::vector<boost::shared_ptr<IndexBuffer> >&  triangleVertIndices,
    const boost::shared_ptr<VertexBuffer>& positions,
    const MBoundingBox& boundingBox,
    const MColor&       diffuseColor,
    bool                visibility
) 
    : fTimeInSeconds(timeInSeconds),
      fNumWires(numWires),
      fNumVerts(numVerts),
      fWireVertIndices(wireVertIndices),
      fTriangleVertIndices(triangleVertIndices),
      fPositions(positions),
      fBoundingBox(boundingBox),
      fDiffuseColor(diffuseColor),
      fVisibility(visibility),
      fBoundingBoxPlaceHolder(false)
{
    assert( wireVertIndices ? (wireVertIndices->numIndices() == 2 * fNumWires) : (fNumWires == 0) );
    assert( positions ? (positions->numVerts() == fNumVerts) : (fNumVerts == 0) );
}

ShapeSample::~ShapeSample()
{}

size_t ShapeSample::numTriangles() const
{
    size_t result = 0;
    for(size_t i=0; i<numIndexGroups(); ++i) {
        result += numTriangles(i);
    }
    return result;
}

void ShapeSample::setNormals(
    const boost::shared_ptr<VertexBuffer>& normals
)
{
    assert( !normals || normals->numVerts() == fNumVerts );
    fNormals = normals;
}

void ShapeSample::setUVs(
    const boost::shared_ptr<VertexBuffer>& uvs
)
{
    assert( !uvs || uvs->numVerts() == fNumVerts );
    fUVs = uvs;
}

}
