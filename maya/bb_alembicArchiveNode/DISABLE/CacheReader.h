#ifndef _CacheReader_h_
#define _CacheReader_h_

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

#include <maya/MFileObject.h>
#include <maya/MString.h>

#include <map>
#include <boost/shared_ptr.hpp>
#include <boost/make_shared.hpp>

// Forward Declarations
class CacheReader;


//==============================================================================
// CLASS GlobalReaderCache
//==============================================================================

class GlobalReaderCache
{
public:
    static GlobalReaderCache& theCache();
    static int maxNumOpenFiles();

    // A CacheReaderProxy represents a request to a reader
    class CacheReaderProxy
    {
    public:
        typedef boost::shared_ptr<CacheReaderProxy> Ptr;

        ~CacheReaderProxy();
        const MFileObject& file() { return fFile; }

    private:
#ifdef BOOST_HAS_RVALUE_REFS
        template< class T, class A1 >
        friend boost::shared_ptr< T > boost::make_shared( A1 && a1 );
#else
        friend boost::shared_ptr<CacheReaderProxy> 
            boost::make_shared<CacheReaderProxy>(const MFileObject& file);
#endif

        CacheReaderProxy(const MFileObject& file);

        MFileObject fFile;
    };

    // A CacheReaderHolder represents the ownership of a reader
    // as long as the CacheReaderHolder is held by the user, the reader
    // will not be closed.
    class CacheReaderHolder
    {
    public:
        CacheReaderHolder(boost::shared_ptr<CacheReaderProxy> proxy);
        ~CacheReaderHolder();

        boost::shared_ptr<CacheReader> getCacheReader();

    private:
        boost::shared_ptr<CacheReaderProxy> fProxy;
        boost::shared_ptr<CacheReader>      fReader;
    };

    boost::shared_ptr<CacheReaderProxy> getCacheReaderProxy(const MFileObject& file);

    // ASync (Background) read methods.
    // We allow gpuCache nodes to load the cache file in a single TBB thread.
    //

	// Schedule an async read. This function will return immediately.
	bool scheduleRead(const MObject&         node, 
                      const MString&         geometryPath,
                      CacheReaderProxy::Ptr& proxy);

    // Pull the hierarchy data.
    bool pullHierarchy(const MObject&                   node, 
                       GPUCache::SubNode::Ptr&          geometry,
                       MString&                         validatedGeometryPath,
                       GPUCache::MaterialGraphMap::Ptr& materials);

    // Pull the shape data.
    bool pullShape(const MObject&          node, 
                   GPUCache::SubNode::Ptr& geometry);

    // Hint which shape should be read first.
    void hintShapeReadOrder(const GPUCache::SubNode& subNode);

    // Cancel the async read.
    void cancelRead(const MObject& node);

    // Wait for the async read.
    void waitForRead(const MObject& node);

    // Check if the worker thread is being interrupted.
    bool isInterrupted();

    // Temporarily pause the async read.
    // We assume that the reader can only be accessed from one thread at a time.
    // When this method is returned, the worker thread is paused so that the main thread
    // can call reader methods without being blocked.
    void pauseRead();

    // Resume the paused worker thread.
    void resumeRead();

    // Check if the worker thread is paused. (called by the worker thread)
    bool isPaused();

    // Block the worker thread until notified. (called by the worker thread)
    void pauseUntilNotified();

private:
    friend class CacheReader;
    friend class CacheReaderProxy;
    class Impl;
    class Scheduler;
    
    // Prohibited and not implemented.
    GlobalReaderCache(const GlobalReaderCache&);
    const GlobalReaderCache& operator= (const GlobalReaderCache&);

    // Increase/Decrease the reference count for the file.
    // If the reference count is 0, the cache reader is deleted.
    void increaseFileRef(const MFileObject& file);
    void decreaseFileRef(const MFileObject& file);

    // Acquire/Release the ownership of the reader.
    // If the ownership count is 0, the cache reader may be closed.
    boost::shared_ptr<CacheReader> acquireOwnership(const MFileObject& file);
    void releaseOwnership(const MFileObject& file);

    GlobalReaderCache();
    ~GlobalReaderCache();

    boost::shared_ptr<Impl>      fImpl;
    boost::shared_ptr<Scheduler> fScheduler;
};


//==============================================================================
// CLASS CacheReader
//==============================================================================

class CacheReader
{
public:
    typedef boost::shared_ptr<CacheReader> CreateFunction(const MFileObject& file);
    static void registerReader(const MString& impl, CreateFunction* func);

    // Returns true if the cache file could be properly opened.
    virtual bool valid() const = 0;

    // Returns true if the geometry path points to a valid object in the
    // cache file.
    //
    // If the geometry path is invalid, validateGeomPath will be set
    // to the closest valid path. Otherwise, validateGeomPath will set
    // be set to the same value as geomPath. 
    virtual bool validateGeomPath(
        const MString& geomPath, MString& validatedGeomPath) const = 0;

    // Read all the hierarchy of geometric objects located below the
    // object identified by the specified geometry path.
    virtual GPUCache::SubNode::Ptr readScene(
        const MString& geomPath, bool needUVs) = 0;

    // Read the hierarchy below the object identified by the specified geometry path.
    // This method will not fill array buffers. Shapes are marked as bounding box place holder.
    // The shape paths below are returned.
    virtual GPUCache::SubNode::Ptr readHierarchy(
        const MString& geomPath, bool needUVs) = 0;

    // Read the shape identified by the specified geometry path.
    virtual GPUCache::SubNode::Ptr readShape(
        const MString& geomPath, bool needUVs) = 0;

    // Read the materials inside the Alembic archive.
    virtual GPUCache::MaterialGraphMap::Ptr readMaterials() = 0;

    // Read the animation time range of the Alembic archive.
    // Returns false if the range is not available.
    virtual bool readAnimTimeRange(GPUCache::TimeInterval& range) = 0;
    
protected:
    
    CacheReader() {}
    virtual ~CacheReader() {}

    
private:
    friend class GlobalReaderCache::Impl;
    static boost::shared_ptr<CacheReader> create(const MString& impl,
        const MFileObject& file);

    // Prohibited and not implemented.
    CacheReader(const CacheReader&);
    const CacheReader& operator=(const CacheReader&);

    static std::map<std::string,CreateFunction*> fsRegistry;
};


//==============================================================================
// CLASS CacheReaderInterruptException
//==============================================================================

class CacheReaderInterruptException : public std::exception
{
public:
    CacheReaderInterruptException(const std::string& str) throw()
        : fWhat(str)
    {}

    virtual ~CacheReaderInterruptException() throw()
    {}

    virtual const char* what() const throw()
    { return fWhat.c_str(); }

private:
    std::string fWhat;
};


#endif

