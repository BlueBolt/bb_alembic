##-*****************************************************************************
##
## Copyright (c) 2009-2011,
##  Sony Pictures Imageworks Inc. and
##  Industrial Light & Magic, a division of Lucasfilm Entertainment Company Ltd.
##
## All rights reserved.
##
## Redistribution and use in source and binary forms, with or without
## modification, are permitted provided that the following conditions are
## met:
## *       Redistributions of source code must retain the above copyright
## notice, this list of conditions and the following disclaimer.
## *       Redistributions in binary form must reproduce the above
## copyright notice, this list of conditions and the following disclaimer
## in the documentation and/or other materials provided with the
## distribution.
## *       Neither the name of Sony Pictures Imageworks, nor
## Industrial Light & Magic, nor the names of their contributors may be used
## to endorse or promote products derived from this software without specific
## prior written permission.
##
## THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
## "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
## LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
## A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
## OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
## SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
## LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
## DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
## THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
## (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
## OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
##
##-*****************************************************************************

PROJECT( ALEMBIC_ARCHIVE_NODE_MAYA_MODULE )

SET( H_FILES
    CacheAlembicUtil.h
    gpuCacheConfig.h
    gpuCacheDrawOverride.h
    gpuCacheDrawTraversal.h
    gpuCacheFrustum.h
    gpuCacheGeometry.h
    gpuCacheGLFT.h
    gpuCacheGLPickingSelect.h
    gpuCacheMaterialBakers.h
    gpuCacheMaterial.h
    gpuCacheMaterialNodes.h
    gpuCacheRasterSelect.h
    gpuCacheSample.h
    gpuCacheSelect.h
    gpuCacheShapeNode.h
    gpuCacheStrings.h
    gpuCacheSubSceneOverride.h
    gpuCacheTimeInterval.h
    gpuCacheUnitBoundingBox.h
    gpuCacheUtil.h
    gpuCacheVBOProxy.h
    gpuCacheVramQuery.h
)


#include <gpuCacheShapeNode.h>
#include <gpuCacheStrings.h>
#include <gpuCacheConfig.h>
#include <gpuCacheRasterSelect.h>
#include <gpuCacheGLPickingSelect.h>

SET( CXX_FILES
    CacheAlembicUtil.cpp
    gpuCacheConfig.cpp
    gpuCacheDrawOverride.cpp
    gpuCacheDrawTraversal.cpp
    gpuCacheFrustum.cpp
    gpuCacheGeometry.cpp
    gpuCacheGLFT.cpp
    gpuCacheGLPickingSelect.cpp
    gpuCacheMaterialBakers.cpp
    gpuCacheMaterial.cpp
    gpuCacheMaterialNodes.cpp
    gpuCachePluginMain.cpp
    gpuCacheRasterSelect.cpp
    gpuCacheSample.cpp
    gpuCacheSelect.cpp
    gpuCacheShapeNode.cpp
    gpuCacheSubSceneOverride.cpp
    gpuCacheUnitBoundingBox.cpp
    gpuCacheUtil.cpp
    gpuCacheVBOProxy.cpp
    gpuCacheVramQuery.cpp 
)

SET( SOURCE_FILES ${CXX_FILES} ${H_FILES} )

SET( CORE_LIBS
  AlembicAbc
  AlembicAbcGeom
  AlembicAbcCoreFactory
  AlembicAbcCoreOgawa
  AlembicAbcCoreHDF5
  AlembicAbcCoreAbstract
  AlembicAbcMaterial
  AlembicAbcCollection
  AlembicOgawa
  AlembicUtil )

INCLUDE_DIRECTORIES( ${ALEMBIC_HDF5_INCLUDE_PATH} )
INCLUDE_DIRECTORIES( ".." )
INCLUDE_DIRECTORIES( "../lib" )
INCLUDE_DIRECTORIES( ${ALEMBIC_PRMAN_INCLUDE_PATH} )
INCLUDE_DIRECTORIES( ${ALEMBIC_ARNOLD_INCLUDE_PATH} )
INCLUDE_DIRECTORIES( ${ALEMBIC_MTOA_INCLUDE_PATH} ) # Lets hard codee this path for now, later we will add a cmake file
INCLUDE_DIRECTORIES( "/development/playground/maya/include" )

ADD_MAYA_CXX_PLUGIN( bb_alembicArchiveShape ${SOURCE_FILES} )
TARGET_LINK_LIBRARIES( bb_alembicArchiveShape
  ${MAYA_LIBRARIES}
  ${CORE_LIBS}
  ${ALEMBIC_HDF5_LIBS}
  ${ALEMBIC_PRMAN_LIBPRMAN}
  ${ALEMBIC_GL_LIBS}
  ${ALEMBIC_ILMBASE_LIBS}
  ${ALEMBIC_MTOA_LIBMTOA}
  ${ALEMBIC_ARNOLD_LIBARNOLD}
  ${CMAKE_THREAD_LIBS_INIT}
  ${ZLIB_LIBRARIES} ${EXTERNAL_MATH_LIBS} )

INSTALL( TARGETS bb_alembicArchiveShape
         DESTINATION maya/$ENV{MAYA_VERSION}/plug-ins )



