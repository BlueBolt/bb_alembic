//-*****************************************************************************
//
// Copyright (c) 2009-2011,
//  Sony Pictures Imageworks Inc. and
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
// Industrial Light & Magic, nor the names of their contributors may be used
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

#include "AbcWriteJob.h"
#include <Alembic/AbcCoreHDF5/All.h>
namespace
{
    bool hasDuplicates(const util::ShapeSet & dagPath)
    {
        std::set<std::string> roots;
        const util::ShapeSet::const_iterator end = dagPath.end();
        for (util::ShapeSet::const_iterator it = dagPath.begin();
            it != end; it++)
        {
            MFnTransform mFn(it->node());
            if (roots.count(mFn.name().asChar()) > 0)
                return true;
            else
                roots.insert(mFn.name().asChar());
        }
        return false;
    }

    class CallWriteVisitor : public boost::static_visitor<>
    {
        public:
            explicit CallWriteVisitor(double iFrame): mFrame(iFrame) {}

            void operator()(MayaCameraWriterPtr & iNode)
            {
                iNode->write();
            }

            void operator()(MayaLocatorWriterPtr & iNode)
            {
                iNode->write();
            }

            void operator()(MayaMeshWriterPtr & iNode)
            {
                iNode->write();
            }

            void operator()(MayaNurbsCurveWriterPtr & iNode)
            {
                iNode->write();
            }

            void operator()(MayaNurbsSurfaceWriterPtr & iNode)
            {
                iNode->write();
            }

            void operator()(MayaPointPrimitiveWriterPtr & iNode)
            {
                iNode->write(mFrame);
            }



        private:
            double mFrame;
    };

    void addToString(std::string & str,
        const std::string & name, unsigned int value)
    {
        if (value > 0)
        {
            std::stringstream ss;
            ss << value;
            str += name + std::string(" ") + ss.str() + std::string(" ");
        }
    }

    // increment each CV counter according to the node type in mShapeList
    class IncrementCVCountsVisitor : public boost::static_visitor<>
    {
        public:
            IncrementCVCountsVisitor()
            {
                mCVsArray[0] = 0;   // increment onto NurbsAnimCVs
                mCVsArray[1] = 0;   // increment onto CurveAnimCVs
                mCVsArray[2] = 0;   // increment onto PointAnimCVs
                mCVsArray[3] = 0;   // increment onto SubDAnimCVs
                mCVsArray[4] = 0;   // increment onto PolyAnimCVs
            }


            void operator()(MayaNurbsSurfaceWriterPtr & iNode)
            {
                mCVsArray[0] += iNode->getNumCVs();
            }

            void operator()(MayaLocatorWriterPtr & iNode) {}

            void operator()(MayaNurbsCurveWriterPtr & iNode)
            {
                mCVsArray[1] += iNode->getNumCVs();
            }

            void operator()(MayaCameraWriterPtr & iNode) {}

            void operator()(MayaPointPrimitiveWriterPtr & iNode)
            {
                mCVsArray[2] += iNode->getNumCVs();
            }

            void operator()(MayaMeshWriterPtr & iNode)
            {
                if (iNode->isSubD())
                    mCVsArray[3] += iNode->getNumCVs();
                else
                    mCVsArray[4] += iNode->getNumCVs();
            }

        unsigned int mCVsArray[5];
    };

    void processCallback(std::string iCallback, bool isMelCallback,
        double iFrame, const MBoundingBox & iBbox)
    {
        if (iCallback.empty())
            return;

        size_t pos = iCallback.find("#FRAME#");
        if ( pos != std::string::npos )
        {
            std::stringstream sstrm;
            sstrm.precision(std::numeric_limits<double>::digits10);
            sstrm << iFrame;
            std::string str = sstrm.str();

            iCallback.replace(pos, 7, str);
        }

        pos = iCallback.find("#BOUNDS#");
        if ( pos != std::string::npos )
        {
            std::stringstream sstrm;
            sstrm.precision(std::numeric_limits<float>::digits10);
            sstrm << " " << iBbox.min().x << " " << iBbox.min().y << " " <<
                iBbox.min().z << " " << iBbox.max().x << " " <<
                iBbox.max().y << " " <<iBbox.max().z;

            std::string str = sstrm.str();

            iCallback.replace(pos, 8, str);
        }

        pos = iCallback.find("#BOUNDSARRAY#");
        if ( pos != std::string::npos )
        {
            std::stringstream sstrm;
            sstrm.precision(std::numeric_limits<float>::digits10);

            if (isMelCallback)
            {
                sstrm << " {";
            }
            else
            {
                sstrm << " [";
            }

            sstrm << iBbox.min().x << "," << iBbox.min().y << "," <<
                iBbox.min().z << "," << iBbox.max().x << "," <<
                iBbox.max().y << "," << iBbox.max().z;

            if (isMelCallback)
            {
                sstrm << "} ";
            }
            else
            {
                sstrm << "] ";
            }
            std::string str = sstrm.str();
            iCallback.replace(pos, 13, str);
        }

        if (isMelCallback)
            MGlobal::executeCommand(iCallback.c_str(), true);
        else
            MGlobal::executePythonCommand(iCallback.c_str(), true);
    }
}

AbcWriteJob::AbcWriteJob(const char * iFileName,
    std::set<double> & iTransFrames,
    Alembic::AbcCoreAbstract::TimeSamplingPtr iTransTime,
    std::set<double> & iShapeFrames,
    Alembic::AbcCoreAbstract::TimeSamplingPtr iShapeTime,
    const JobArgs & iArgs)
{
    MStatus status;
    mFileName = iFileName;
    mBoxIndex = 0;
    mArgs = iArgs;
    mShapeSamples = 1;
    mTransSamples = 1;

    if (mArgs.useSelectionList)
    {

        // get the active selection
        MSelectionList activeList;
        MGlobal::getActiveSelectionList(activeList);
        mSList = activeList;
        unsigned int selectionSize = activeList.length();
        for (unsigned int index = 0; index < selectionSize; index ++)
        {
            MDagPath dagPath;
            status = activeList.getDagPath(index, dagPath);
            if (status == MS::kSuccess)
            {
                unsigned int length = dagPath.length();
                while (--length)
                {
                    dagPath.pop();
                    mSList.add(dagPath, MObject::kNullObj, true);
                }
            }
        }
    }

    mTransFrames = iTransFrames;
    mShapeFrames = iShapeFrames;

    // only needed during creation of the transforms and shapes
    mTransTime = iTransTime;
    mTransTimeIndex = 0;
    mShapeTime = iShapeTime;
    mShapeTimeIndex = 0;

    // should have at least 1 value
    assert(!mTransFrames.empty() && !mShapeFrames.empty());

    mFirstFrame = *(mTransFrames.begin());
    std::set<double>::iterator last = mTransFrames.end();
    last--;
    mLastFrame = *last;
    last = mShapeFrames.end();
    last--;

    double lastShapeFrame = *last;
    if (lastShapeFrame > mLastFrame)
        mLastFrame = lastShapeFrame;
}

void AbcWriteJob::getBoundingBox(const MMatrix & eMInvMat)
{
    MStatus status;

    // short-circuit if the selection flag is on but this node is not in the
    // active selection

    // MGlobal::isSelected(ob) doesn't work, because DG node and DAG node is
    // not the same even if they refer to the same MObject
    if (mArgs.useSelectionList && !mSList.hasItem(mCurDag))
        return;

    MObject ob = mCurDag.node();

    // check for riCurves flag for flattening all curve object to
    // one curve group
    MFnDependencyNode fnDepNode(ob, &status);
    MPlug riCurvesPlug = fnDepNode.findPlug("riCurves", &status);
    if ( status == MS::kSuccess && riCurvesPlug.asBool() == true)
    {
        MFnDagNode mFn(mCurDag, &status);
        if (status == MS::kSuccess)
        {
            MBoundingBox box = mFn.boundingBox();
            box.transformUsing(mCurDag.exclusiveMatrix()*eMInvMat);
            mCurBBox.expand(box);
        }
    }
    else if (ob.hasFn(MFn::kTransform))
    {
        MFnTransform fnTrans(ob);

        // loop through the children, making sure to push and pop them
        // from the MDagPath
        unsigned int numChild = mCurDag.childCount();
        for (unsigned int i = 0; i < numChild; ++i)
        {
            mCurDag.push(mCurDag.child(i));
            getBoundingBox(eMInvMat);
            mCurDag.pop();
        }
    }
    else if (ob.hasFn(MFn::kParticle) || ob.hasFn(MFn::kMesh)
        || ob.hasFn(MFn::kNurbsCurve)
        || ob.hasFn(MFn::kNurbsSurface) )
    {
        if (util::isIntermediate(mCurDag.node()))
            return;

        MFnDagNode mFn(mCurDag, &status);
        if (status == MS::kSuccess)
        {
            MBoundingBox box = mFn.boundingBox();
            box.transformUsing(mCurDag.exclusiveMatrix()*eMInvMat);
            mCurBBox.expand(box);
        }
    }
}

bool AbcWriteJob::checkCurveGrp()
{
    MItDag itDag(MItDag::kBreadthFirst, MFn::kNurbsCurve);
    itDag.reset(mCurDag, MItDag::kBreadthFirst, MFn::kNurbsCurve);

    bool init = false;
    int degree = 0;
    MFnNurbsCurve::Form form = MFnNurbsCurve::kInvalid;
    for (; !itDag.isDone(); itDag.next())
    {
        MDagPath curvePath;
        if (itDag.getPath(curvePath) == MS::kSuccess)
        {
            MObject curve = curvePath.node();
            if (!util::isIntermediate(curve) && curve.hasFn(MFn::kNurbsCurve))
            {
                MFnNurbsCurve fn(curvePath);
                if (!init)
                {
                    degree = fn.degree();
                    form = fn.form();
                    init = true;
                }
                else
                {
                    if (degree != fn.degree() || form != fn.form())
                        return false;
                }
            }
        }
    }
    return true;
}

void AbcWriteJob::setup(double iFrame, MayaTransformWriterPtr iParent)
{
    MStatus status;

    // short-circuit if selection flag is on but this node isn't actively
    // selected
    if (mArgs.useSelectionList && !mSList.hasItem(mCurDag))
        return;

    MObject ob = mCurDag.node();

    // skip all intermediate nodes (and their children)
    if (util::isIntermediate(ob))
    {
        return;
    }

    // skip nodes that aren't renderable (and their children)
    if (mArgs.excludeInvisible && !util::isRenderable(ob))
    {
        return;
    }

    // look for riCurves flag for flattening all curve objects to a curve group
    MFnDependencyNode fnDepNode(ob, &status);
    MPlug riCurvesPlug = fnDepNode.findPlug("riCurves", &status);
    bool riCurvesVal = riCurvesPlug.asBool();
    bool writeOutAsGroup = false;
    if (riCurvesVal)
    {
        writeOutAsGroup = checkCurveGrp();
        if (writeOutAsGroup == false)
        {
            MString msg = "Curves have different degrees or close ";
            msg += "states, not writing out as curve group";
            MGlobal::displayWarning(msg);
        }
    }
    if ( status == MS::kSuccess && riCurvesVal && writeOutAsGroup)
    {
        MayaNurbsCurveWriterPtr nurbsCurve;
        if (iParent == NULL)
        {
            Alembic::Abc::OObject obj = mRoot.getTop();
            nurbsCurve = MayaNurbsCurveWriterPtr(new MayaNurbsCurveWriter(
                mCurDag, obj, mShapeTimeIndex, true, mArgs));
        }
        else
        {
            Alembic::Abc::OObject obj = iParent->getObject();
            nurbsCurve = MayaNurbsCurveWriterPtr(new MayaNurbsCurveWriter(
                mCurDag, obj, mShapeTimeIndex, true, mArgs));
        }

        if (nurbsCurve->isAnimated() && mShapeTimeIndex != 0)
        {
            MayaNodePtr nd = nurbsCurve;
            mShapeList.push_back(nd);
            mStats.mCurveAnimNum++;
            mStats.mCurveAnimCurves += nurbsCurve->getNumCurves();
            mStats.mCurveAnimCVs += nurbsCurve->getNumCVs();
        }
        else
        {
            mStats.mCurveStaticNum++;
            mStats.mCurveStaticCurves += nurbsCurve->getNumCurves();
            mStats.mCurveStaticCVs += nurbsCurve->getNumCVs();
        }

        AttributesWriterPtr attrs = nurbsCurve->getAttrs();
        if (mShapeTimeIndex != 0 && attrs->isAnimated())
            mShapeAttrList.push_back(attrs);
    }
    else if (ob.hasFn(MFn::kTransform))
    {
        MFnTransform fnTrans(ob, &status);
        if (status != MS::kSuccess)
        {
            MString msg = "Initialize transform node ";
            msg += mCurDag.fullPathName();
            msg += " failed, skipping.";
            MGlobal::displayWarning(msg);
            return;
        }

        MayaTransformWriterPtr trans;

        // parented to the root case
        if (iParent == NULL)
        {
            Alembic::Abc::OObject obj = mRoot.getTop();
            trans = MayaTransformWriterPtr(new MayaTransformWriter(
                obj, mCurDag, mTransTimeIndex, mArgs));
        }
        else
        {
            trans = MayaTransformWriterPtr(new MayaTransformWriter(
                *iParent, mCurDag, mTransTimeIndex, mArgs));
        }

        if (trans->isAnimated() && mTransTimeIndex != 0)
        {
            mTransList.push_back(trans);
            mStats.mTransAnimNum++;
        }
        else
            mStats.mTransStaticNum++;

        AttributesWriterPtr attrs = trans->getAttrs();
        if (mTransTimeIndex != 0 && attrs->isAnimated())
            mTransAttrList.push_back(attrs);

        // loop through the children, making sure to push and pop them
        // from the MDagPath
        unsigned int numChild = mCurDag.childCount();
        for (unsigned int i = 0; i < numChild; ++i)
        {
            mCurDag.push(mCurDag.child(i));
            setup(iFrame, trans);
            mCurDag.pop();
        }
    }
    else if (ob.hasFn(MFn::kLocator))
    {
        MFnDependencyNode fnLocator(ob, & status);
        if (status != MS::kSuccess)
        {
            MString msg = "Initialize locator node ";
            msg += mCurDag.fullPathName();
            msg += " failed, skipping.";
            MGlobal::displayWarning(msg);
            return;
        }

        if (iParent != NULL)
        {
            Alembic::Abc::OObject obj = iParent->getObject();
            MayaLocatorWriterPtr locator(new MayaLocatorWriter(
                mCurDag, obj, mShapeTimeIndex, mArgs));

            if (locator->isAnimated() && mShapeTimeIndex != 0)
            {
                MayaNodePtr nd = locator;
                mShapeList.push_back(nd);
                mStats.mLocatorAnimNum++;
            }
            else
            {
                mStats.mLocatorStaticNum++;
            }

            AttributesWriterPtr attrs = locator->getAttrs();
            if (mShapeTimeIndex != 0 && attrs->isAnimated())
                mShapeAttrList.push_back(attrs);
        }
        else
        {
            MString err = "Can't translate ";
            err += fnLocator.name() + " since it doesn't have a parent.";
            MGlobal::displayError(err);
        }
    }
    else if (ob.hasFn(MFn::kParticle))
    {
        MFnParticleSystem mFnParticle(ob, &status);
        if (status != MS::kSuccess)
        {
            MString msg = "Initialize particle system ";
            msg += mCurDag.fullPathName();
            msg += " failed, skipping.";
            MGlobal::displayWarning(msg);
            return;
        }

        if (iParent != NULL)
        {
            Alembic::Abc::OObject obj = iParent->getObject();
            MayaPointPrimitiveWriterPtr particle(new MayaPointPrimitiveWriter(
                iFrame, mCurDag, obj, mShapeTimeIndex, mArgs));

            if (particle->isAnimated() && mShapeTimeIndex != 0)
            {
                MayaNodePtr nd = particle;
                mShapeList.push_back(nd);
                mStats.mPointAnimNum++;
                mStats.mPointAnimCVs += particle->getNumCVs();
            }
            else
            {
                mStats.mPointStaticNum++;
                mStats.mPointStaticCVs += particle->getNumCVs();
            }

            AttributesWriterPtr attrs = particle->getAttrs();
            if (mShapeTimeIndex != 0 && attrs->isAnimated())
                mShapeAttrList.push_back(attrs);
        }
        else
        {
            MString err = "Can't translate ";
            err += mFnParticle.name() + " since it doesn't have a parent.";
            MGlobal::displayError(err);
        }
    }
    else if (ob.hasFn(MFn::kMesh))
    {
        MFnMesh fnMesh(ob, &status);
        if (status != MS::kSuccess)
        {
            MString msg = "Initialize mesh node ";
            msg += mCurDag.fullPathName();
            msg += " failed, skipping.";
            MGlobal::displayWarning(msg);
            return;
        }

        if (iParent != NULL)
        {
            Alembic::Abc::OObject obj = iParent->getObject();
            MayaMeshWriterPtr mesh(new MayaMeshWriter(mCurDag, obj,
                mShapeTimeIndex, mArgs));

            if (mesh->isAnimated() && mShapeTimeIndex != 0)
            {
                MayaNodePtr nd = mesh;
                mShapeList.push_back(nd);
                if (mesh->isSubD())
                {
                    mStats.mSubDAnimNum++;
                    mStats.mSubDAnimCVs += mesh->getNumCVs();
                    mStats.mSubDAnimFaces += mesh->getNumFaces();
                }
                else
                {
                    mStats.mPolyAnimNum++;
                    mStats.mPolyAnimCVs += mesh->getNumCVs();
                    mStats.mPolyAnimFaces += mesh->getNumFaces();
                }
            }
            else
            {
                if (mesh->isSubD())
                {
                    mStats.mSubDStaticNum++;
                    mStats.mSubDStaticCVs += mesh->getNumCVs();
                    mStats.mSubDStaticFaces += mesh->getNumFaces();
                }
                else
                {
                    mStats.mPolyStaticNum++;
                    mStats.mPolyStaticCVs += mesh->getNumCVs();
                    mStats.mPolyStaticFaces += mesh->getNumFaces();
                }
            }

            AttributesWriterPtr attrs = mesh->getAttrs();
            if (mShapeTimeIndex != 0 && attrs->isAnimated())
                mShapeAttrList.push_back(attrs);
        }
        else
        {
            MString err = "Can't translate ";
            err += fnMesh.name() + " since it doesn't have a parent.";
            MGlobal::displayError(err);
        }
    }
    else if (ob.hasFn(MFn::kCamera))
    {
        MFnCamera fnCamera(ob, &status);
        if (status != MS::kSuccess)
        {
            MString msg = "Initialize camera node ";
            msg += mCurDag.fullPathName();
            msg += " failed, skipping.";
            MGlobal::displayWarning(msg);
            return;
        }

        if (iParent != NULL)
        {
            Alembic::Abc::OObject obj = iParent->getObject();
            MayaCameraWriterPtr camera(new MayaCameraWriter(
                mCurDag, obj, mShapeTimeIndex, mArgs));

            if (camera->isAnimated() && mShapeTimeIndex != 0)
            {
                MayaNodePtr nd = camera;
                mShapeList.push_back(nd);
                mStats.mCameraAnimNum++;
            }
            else
                mStats.mCameraStaticNum++;

            AttributesWriterPtr attrs = camera->getAttrs();
            if (mShapeTimeIndex != 0 && attrs->isAnimated())
                mShapeAttrList.push_back(attrs);
        }
        else
        {
            MString err = "Can't translate ";
            err += fnCamera.name() + " since it doesn't have a parent.";
            MGlobal::displayError(err);
        }
    }
    else if (ob.hasFn(MFn::kNurbsSurface))
    {
        MFnNurbsSurface fnNurbsSurface(ob, &status);
        if (status != MS::kSuccess)
        {
            MString msg = "Initialize nurbs surface ";
            msg += mCurDag.fullPathName();
            msg += " failed, skipping.";
            MGlobal::displayWarning(msg);
            return;
        }

        if (iParent != NULL)
        {
            Alembic::Abc::OObject obj = iParent->getObject();
            MayaNurbsSurfaceWriterPtr nurbsSurface(new MayaNurbsSurfaceWriter(
                mCurDag, obj,  mShapeTimeIndex, mArgs));

            if (nurbsSurface->isAnimated() && mShapeTimeIndex != 0)
            {
                MayaNodePtr nd = nurbsSurface;
                mShapeList.push_back(nd);
                mStats.mNurbsAnimNum++;
                mStats.mNurbsAnimCVs += nurbsSurface->getNumCVs();
            }
            else
            {
                mStats.mNurbsStaticNum++;
                mStats.mNurbsStaticCVs += nurbsSurface->getNumCVs();
            }

            AttributesWriterPtr attrs = nurbsSurface->getAttrs();
            if (mShapeTimeIndex != 0 && attrs->isAnimated())
                mShapeAttrList.push_back(attrs);
        }
        else
        {
            MString err = "Can't translate ";
            err += fnNurbsSurface.name() + " since it doesn't have a parent.";
            MGlobal::displayError(err);
        }
    }
    else if (ob.hasFn(MFn::kNurbsCurve))
    {
        MFnNurbsCurve fnNurbsCurve(ob, &status);
        if (status != MS::kSuccess)
        {
            MString msg = "Initialize curve node ";
            msg += mCurDag.fullPathName();
            msg += " failed, skipping.";
            MGlobal::displayWarning(msg);
            return;
        }

        if (iParent != NULL)
        {
            Alembic::Abc::OObject obj = iParent->getObject();
            MayaNurbsCurveWriterPtr nurbsCurve(new MayaNurbsCurveWriter(
                mCurDag, obj, mShapeTimeIndex, false, mArgs));

            if (nurbsCurve->isAnimated() && mShapeTimeIndex != 0)
            {
                MayaNodePtr nd = nurbsCurve;
                mShapeList.push_back(nd);
                mStats.mCurveAnimNum++;
                mStats.mCurveAnimCurves++;
                mStats.mCurveAnimCVs += nurbsCurve->getNumCVs();
            }
            else
            {
                mStats.mCurveStaticNum++;
                mStats.mCurveStaticCurves++;
                mStats.mCurveStaticCVs += nurbsCurve->getNumCVs();
            }

            AttributesWriterPtr attrs = nurbsCurve->getAttrs();
            if (mShapeTimeIndex != 0 && attrs->isAnimated())
                mShapeAttrList.push_back(attrs);
        }
        else
        {
            MString err = "Can't translate ";
            err += fnNurbsCurve.name() + " since it doesn't have a parent.";
            MGlobal::displayError(err);
        }
    }
    else
    {
        MString warn = mCurDag.fullPathName() + " is an unsupported type of ";
        warn += ob.apiTypeStr();
        MGlobal::displayWarning(warn);
    }
}


AbcWriteJob::~AbcWriteJob()
{
}

bool AbcWriteJob::eval(double iFrame)
{
    if (iFrame == mFirstFrame)
    {
        // check if the shortnames of any two nodes are the same
        // if so, exit here
        if (hasDuplicates(mArgs.dagPaths))
        {
            throw std::runtime_error("The names of root nodes are the same");
        }

        std::string appWriter = "Maya ";
        appWriter += MGlobal::mayaVersion().asChar();
        appWriter += " AbcExport v";
        appWriter += ABCEXPORT_VERSION;

        std::string userInfo = "Exported from: ";
        userInfo += MFileIO::currentFile().asChar();

        // these symbols can't be in the meta data
        if (userInfo.find('=') != std::string::npos ||
            userInfo.find(';') != std::string::npos)
        {
            userInfo = "";
        }

        mRoot = CreateArchiveWithInfo(Alembic::AbcCoreHDF5::WriteArchive(),
            mFileName, appWriter, userInfo,
            Alembic::Abc::ErrorHandler::kThrowPolicy);
        mShapeTimeIndex = mRoot.addTimeSampling(*mShapeTime);
        mTransTimeIndex = mRoot.addTimeSampling(*mTransTime);

        mBoxProp =  Alembic::AbcGeom::CreateOArchiveBounds(mRoot,
            mTransTimeIndex);

        if (!mRoot.valid())
        {
            MGlobal::displayError("Unable to create abc file");
            throw std::runtime_error("Unable to create abc file");
        }

        util::ShapeSet::const_iterator end = mArgs.dagPaths.end();
        for (util::ShapeSet::const_iterator it = mArgs.dagPaths.begin();
            it != end; ++it)
        {
            mCurDag = *it;
            setup(iFrame * util::spf(), MayaTransformWriterPtr());
        }
        perFrameCallback(iFrame);
    }
    else
    {
        std::set<double>::iterator checkFrame = mShapeFrames.find(iFrame);
        bool foundShapeFrame = false;
        if (checkFrame != mShapeFrames.end())
        {
            assert(mRoot != NULL);
            foundShapeFrame = true;
            mShapeSamples ++;
            std::vector< MayaNodePtr >::iterator it = mShapeList.begin();
            std::vector< MayaNodePtr >::iterator end = mShapeList.end();
            CallWriteVisitor visit(iFrame * util::spf());
            IncrementCVCountsVisitor cntVisitor;
            for (; it != end; it++)
            {
                boost::apply_visitor(visit, *it);
                boost::apply_visitor(cntVisitor, *it);
            }
            mStats.mNurbsAnimCVs += cntVisitor.mCVsArray[0];
            mStats.mCurveAnimCVs += cntVisitor.mCVsArray[1];
            mStats.mPointAnimCVs += cntVisitor.mCVsArray[2];
            mStats.mSubDAnimCVs  += cntVisitor.mCVsArray[3];
            mStats.mPolyAnimCVs  += cntVisitor.mCVsArray[4];

            std::vector< AttributesWriterPtr >::iterator sattrCur =
                mShapeAttrList.begin();

            std::vector< AttributesWriterPtr >::iterator sattrEnd =
                mShapeAttrList.end();

            for(; sattrCur != sattrEnd; sattrCur++)
            {
                (*sattrCur)->write();
            }
        }

        checkFrame = mTransFrames.find(iFrame);
        bool foundTransFrame = false;
        if (checkFrame != mTransFrames.end())
        {
            assert(mRoot.valid());
            foundTransFrame = true;
            mTransSamples ++;
            std::vector< MayaTransformWriterPtr >::iterator tcur =
                mTransList.begin();

            std::vector< MayaTransformWriterPtr >::iterator tend =
                mTransList.end();

            for (; tcur != tend; tcur++)
            {
                (*tcur)->write();
            }

            std::vector< AttributesWriterPtr >::iterator tattrCur =
                mTransAttrList.begin();

            std::vector< AttributesWriterPtr >::iterator tattrEnd =
                mTransAttrList.end();

            for(; tattrCur != tattrEnd; tattrCur++)
            {
                (*tattrCur)->write();
            }
        }

        if (foundTransFrame || foundShapeFrame)
            perFrameCallback(iFrame);
    }

    if (iFrame == mLastFrame)
    {
        postCallback(iFrame);
        return true;
    }

    return false;
}

void AbcWriteJob::perFrameCallback(double iFrame)
{
    MBoundingBox bbox;

    util::ShapeSet::iterator it = mArgs.dagPaths.begin();
    const util::ShapeSet::iterator end = mArgs.dagPaths.end();
    for (; it != end; it ++)
    {
        mCurDag = *it;

        mCurBBox.clear();
        MMatrix eMInvMat;
        if (mArgs.worldSpace)
        {
            eMInvMat.setToIdentity();
        }
        else
        {
            eMInvMat = mCurDag.exclusiveMatrixInverse();
        }

        getBoundingBox(eMInvMat);
        bbox.expand(mCurBBox);
    }

    Alembic::Abc::V3d min(bbox.min().x, bbox.min().y, bbox.min().z);
    Alembic::Abc::V3d max(bbox.max().x, bbox.max().y, bbox.max().z);
    Alembic::Abc::Box3d b(min, max);
    mBoxProp.set(b);

    processCallback(mArgs.melPerFrameCallback, true, iFrame, bbox);
    processCallback(mArgs.pythonPerFrameCallback, false, iFrame, bbox);
}


// write the frame ranges and statistic string on the root
// Also call the post callbacks
void AbcWriteJob::postCallback(double iFrame)
{
    std::string statsStr = "";

    addToString(statsStr, "SubDStaticNum", mStats.mSubDStaticNum);
    addToString(statsStr, "SubDAnimNum", mStats.mSubDAnimNum);
    addToString(statsStr, "SubDStaticCVs", mStats.mSubDStaticCVs);
    addToString(statsStr, "SubDAnimCVs", mStats.mSubDAnimCVs);
    addToString(statsStr, "SubDStaticFaces", mStats.mSubDStaticFaces);
    addToString(statsStr, "SubDAnimFaces", mStats.mSubDAnimFaces);

    addToString(statsStr, "PolyStaticNum", mStats.mPolyStaticNum);
    addToString(statsStr, "PolyAnimNum", mStats.mPolyAnimNum);
    addToString(statsStr, "PolyStaticCVs", mStats.mPolyStaticCVs);
    addToString(statsStr, "PolyAnimCVs", mStats.mPolyAnimCVs);
    addToString(statsStr, "PolyStaticFaces", mStats.mPolyStaticFaces);
    addToString(statsStr, "PolyAnimFaces", mStats.mPolyAnimFaces);

    addToString(statsStr, "CurveStaticNum", mStats.mCurveStaticNum);
    addToString(statsStr, "CurveStaticCurves", mStats.mCurveStaticCurves);
    addToString(statsStr, "CurveAnimNum", mStats.mCurveAnimNum);
    addToString(statsStr, "CurveAnimCurves", mStats.mCurveAnimCurves);
    addToString(statsStr, "CurveStaticCVs", mStats.mCurveStaticCVs);
    addToString(statsStr, "CurveAnimCVs", mStats.mCurveAnimCVs);

    addToString(statsStr, "PointStaticNum", mStats.mPointStaticNum);
    addToString(statsStr, "PointAnimNum", mStats.mPointAnimNum);
    addToString(statsStr, "PointStaticCVs", mStats.mPointStaticCVs);
    addToString(statsStr, "PointAnimCVs", mStats.mPointAnimCVs);

    addToString(statsStr, "NurbsStaticNum", mStats.mNurbsStaticNum);
    addToString(statsStr, "NurbsAnimNum", mStats.mNurbsAnimNum);
    addToString(statsStr, "NurbsStaticCVs", mStats.mNurbsStaticCVs);
    addToString(statsStr, "NurbsAnimCVs", mStats.mNurbsAnimCVs);

    addToString(statsStr, "TransStaticNum", mStats.mTransStaticNum);
    addToString(statsStr, "TransAnimNum", mStats.mTransAnimNum);

    addToString(statsStr, "LocatorStaticNum", mStats.mLocatorStaticNum);
    addToString(statsStr, "LocatorAnimNum", mStats.mLocatorAnimNum);

    addToString(statsStr, "CameraStaticNum", mStats.mCameraStaticNum);
    addToString(statsStr, "CameraAnimNum", mStats.mCameraAnimNum);

    if (statsStr.length() > 0)
    {
        Alembic::Abc::OStringProperty stats(mRoot.getTop().getProperties(),
            "statistics");
        stats.set(statsStr);
    }

    if (mTransTimeIndex != 0)
    {
        MString propName;
        propName += static_cast<int>(mTransTimeIndex);
        propName += ".samples";
        Alembic::Abc::OUInt32Property samp(mRoot.getTop().getProperties(),
            propName.asChar());
        samp.set(mTransSamples);
    }

    if (mShapeTimeIndex != 0 && mShapeTimeIndex != mTransTimeIndex)
    {
        MString propName;
        propName += static_cast<int>(mShapeTimeIndex);
        propName += ".samples";
        Alembic::Abc::OUInt32Property samp(mRoot.getTop().getProperties(),
            propName.asChar());
        samp.set(mShapeSamples);
    }

    MBoundingBox bbox;

    if (mArgs.melPostCallback.find("#BOUNDS#") != std::string::npos ||
        mArgs.pythonPostCallback.find("#BOUNDS#") != std::string::npos ||
        mArgs.melPostCallback.find("#BOUNDSARRAY#") != std::string::npos ||
        mArgs.pythonPostCallback.find("#BOUNDSARRAY#") != std::string::npos)
    {
        util::ShapeSet::const_iterator it = mArgs.dagPaths.begin();
        const util::ShapeSet::const_iterator end = mArgs.dagPaths.end();
        for (; it != end; it ++)
        {
            mCurDag = *it;

            mCurBBox.clear();
            MMatrix eMInvMat;
            if (mArgs.worldSpace)
            {
                eMInvMat.setToIdentity();
            }
            else
            {
                eMInvMat = mCurDag.exclusiveMatrixInverse();
            }

            getBoundingBox(eMInvMat);
            bbox.expand(mCurBBox);
        }
    }

    processCallback(mArgs.melPostCallback, true, iFrame, bbox);
    processCallback(mArgs.pythonPostCallback, false, iFrame, bbox);
}


