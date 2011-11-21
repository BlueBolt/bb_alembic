//-*****************************************************************************
//
// Copyright (c) 2009-2011,
//  Sony Pictures Imageworks, Inc. and
//  Industrial Light & Magic, a division of Lucasfilm Entertainment Company Ltd.
//
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
// *       Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
// *       Redistributions in binary form must reproduce the above
// copyright notice, this list of conditions and the following disclaimer
// in the documentation and/or other materials provided with the
// distribution.
// *       Neither the name of Sony Pictures Imageworks, nor
// Industrial Light & Magic nor the names of their contributors may be used
// to endorse or promote products derived from this software without specific
// prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
// "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
// LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
// A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
// OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
// SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
// LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
// DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
// THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
// (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
// OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
//-*****************************************************************************

#ifndef ABCPREVIEW_ALEMBIC_NODE_H_
#define ABCPREVIEW_ALEMBIC_NODE_H_

#include <Alembic/AbcGeom/All.h>
#include <Alembic/AbcCoreHDF5/All.h>
#include <Alembic/Util/All.h>

#include "Drawable.h"
#include "GLCamera.h"
#include "NodeIteratorVisitorHelper.h"

#include <maya/MDataHandle.h>
#include <maya/MDGContext.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MPxLocatorNode.h>
#include <maya/MStatus.h>
#include <maya/MString.h>

#include <set>
#include <vector>
#include <string>

using namespace SimpleAbcViewer;

class delightAlembicArchive : public MPxLocatorNode
{
public:
    delightAlembicArchive();
    virtual ~delightAlembicArchive();

    // avoid calling createSceneVisitor twice by getting the
    // list of hdf reader pointers
    void setReaderPtrList(const WriterData & iData)
    {
        mData = iData;
    }

    static const MTypeId mMayaNodeId;

    // input attributes
    static MObject mTimeAttr;
    static MObject mAbcFileNameAttr;

    // output informational attrs
    static MObject mStartFrameAttr;
    static MObject mEndFrameAttr;

    // override virtual methods from MPxNode
    virtual MStatus compute(const MPlug & plug, MDataBlock & dataBlock);

    // return a pointer to a new instance of the class
    // (derived from MPxNode) that implements the new node type
    static void* creator() { return (new delightAlembicArchive()); }

    // initialize all the attributes to default values
    static MStatus initialize();

    void   setDebugMode(bool iDebugOn){ mDebugOn = iDebugOn; }

	virtual void		draw( M3dView & view, const MDagPath & path,
			    	   M3dView::DisplayStyle style, M3dView:: DisplayStatus );
	virtual bool		isBounded() const;
	virtual bool		drawLast() const;
	virtual bool		excludeAsLocator() const;

protected:
    std::string m_fileName;
	Alembic::AbcGeom::IArchive m_archive;
	Alembic::AbcGeom::IObject m_topObject;
	Alembic::AbcGeom::chrono_t m_minTime;
    Alembic::AbcGeom::chrono_t m_maxTime;
	DrawablePtr m_drawable;
	Imath::Box3d m_bounds;
    GLCamera m_cam;
    float m_pointSize;

private:

    // flag indicating if the input file should be opened again
    bool    mFileInitialized;

    // flag indicating either this is the first time a mesh plug is computed or
    // there's a topology change from last frame to this one
    bool    mSubDInitialized;
    bool    mPolyInitialized;

    double   mSequenceStartTime;
    double   mSequenceEndTime;
    double   mCurTime;

    bool    mDebugOn;

    // bool for each output plug, (the 2 transform plugs are lumped together,
    // when updating) this is to prevent rereading the same
    // frame when above or below the frame range
    std::vector<bool> mOutRead;

    bool    mConnect;
    bool    mCreateIfNotFound;
    bool    mRemoveIfNoUpdate;
    MString mConnectRootNodes;

    WriterData mData;
};

#endif  // ABCPREVIEW_ALEMBIC_NODE_H_
