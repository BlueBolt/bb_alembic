##-*****************************************************************************
##
## Copyright (c) 2009-2013,
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
## *       Neither the name of Industrial Light & Magic nor the names of
## its contributors may be used to endorse or promote products derived
## from this software without specific prior written permission.
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


# We shall worry about windowsification later.

#-******************************************************************************
#-******************************************************************************
# FIRST, MTOA STUFF
#-******************************************************************************
#-******************************************************************************

# If MTOA_ROOT not set, use predefined paths
IF(NOT DEFINED MTOA_ROOT)
    IF ( ${CMAKE_HOST_UNIX} )
        IF( ${DARWIN} )
          # TODO: set to default install path when shipping out
          SET( ALEMBIC_MTOA_ROOT NOTFOUND )
        ELSE()
          # TODO: set to default install path when shipping out
          SET( ALEMBIC_MTOA_ROOT "/sww/tools/MTOA" )
        ENDIF()
    ELSE()
        IF ( ${WINDOWS} )
          # TODO: set to 32-bit or 64-bit path
          SET( ALEMBIC_MTOA_ROOT NOTFOUND )
        ELSE()
          SET( ALEMBIC_MTOA_ROOT NOTFOUND )
        ENDIF()
    ENDIF()
ELSE()
    # Prefer MTOA_ROOT set from the CMakeCache'd variable than default paths
    SET( ALEMBIC_MTOA_ROOT ${MTOA_ROOT})
ENDIF()

# Prefer MTOA_ROOT set from the environment over the CMakeCache'd variable
IF(NOT $ENV{MTOA_ROOT}x STREQUAL "x")
  SET( ALEMBIC_MTOA_ROOT $ENV{MTOA_ROOT})
ENDIF()


FIND_PATH( ALEMBIC_MTOA_INCLUDE_PATH utils/Version.h
           PATHS
           "${ALEMBIC_MTOA_ROOT}/include"
           NO_DEFAULT_PATH
           NO_CMAKE_ENVIRONMENT_PATH
           NO_CMAKE_PATH
           NO_SYSTEM_ENVIRONMENT_PATH
           NO_CMAKE_SYSTEM_PATH
           DOC "The directory where utils/Version.h resides" )

SET( ALEMBIC_MTOA_LIBMTOA ALEMBIC_MTOA_LIBMTOA-NOTFOUND )
FIND_LIBRARY( ALEMBIC_MTOA_LIBMTOA mtoa_api
              PATHS
              "${ALEMBIC_MTOA_ROOT}/lib"
              "${ALEMBIC_MTOA_ROOT}/bin"
              NO_DEFAULT_PATH
              NO_CMAKE_ENVIRONMENT_PATH
              NO_CMAKE_PATH
              NO_SYSTEM_ENVIRONMENT_PATH
              NO_CMAKE_SYSTEM_PATH
              DOC "The mtoa_api library" )

MESSAGE( STATUS "MTOA ROOT ${ALEMBIC_MTOA_ROOT}" )



IF( ${WINDOWS} )
  SET( MTOA_COMPILE_FLAGS "/c /nologo /MT /TP /DWIN32" )
  SET( MTOA_LINK_FLAGS "/nologo /dll /LIBPATH:\"%RMANTREE%\lib\" libmtoa_api.lib" )
ELSEIF( ${DARWIN} )
  SET( MTOA_COMPILE_FLAGS "-c" )
  SET( MTOA_LINK_FLAGS "-bundle -undefined dynamic_lookup" )
ELSEIF( ${LINUX} )
  SET( MTOA_COMPILE_FLAGS "-c -fPIC -D_LINUX" )
  SET( MTOA_LINK_FLAGS "-shared" )
ENDIF()

IF ( ( ${ALEMBIC_MTOA_INCLUDE_PATH} STREQUAL "ALEMBIC_MTOA_INCLUDE_PATH-NOTFOUND" ) OR
     ( ${ALEMBIC_MTOA_LIBMTOA} STREQUAL "ALEMBIC_MTOA_LIBMTOA-NOTFOUND" ) )
  MESSAGE( STATUS "MTOA not found" )
  SET( ALEMBIC_MTOA_FOUND FALSE )
ELSE()
  MESSAGE( STATUS "MTOA INCLUDE PATH: ${ALEMBIC_MTOA_INCLUDE_PATH}" )
  MESSAGE( STATUS "mtoa_api: ${ALEMBIC_MTOA_LIBMTOA}" )
  SET( ALEMBIC_MTOA_FOUND TRUE )
  SET( ALEMBIC_MTOA_LIBS ${ALEMBIC_MTOA_LIBMTOA} )
ENDIF()

##-*****************************************************************************
##-*****************************************************************************
# Macro for making MTOA plugins
##-*****************************************************************************
##-*****************************************************************************
MACRO(ADD_MTOA_CXX_PLUGIN PluginName SourceFile1 )

  IF( NOT ${ALEMBIC_MTOA_FOUND} )
    MESSAGE( FATAL_ERROR "MTOA is not found. :(" )
  ENDIF()

  GET_FILENAME_COMPONENT( PluginNameNoDirectory ${PluginName} NAME )
  GET_FILENAME_COMPONENT( PluginNameFullPath ${PluginName} ABSOLUTE )

  SET( TMP_SOURCES ${SourceFile1} ${ARGN} )
  SET( ${PluginName}_SOURCES ${TMP_SOURCES} )

  INCLUDE_DIRECTORIES( ${ALEMBIC_MTOA_INCLUDE_PATH} )

  ADD_LIBRARY( ${PluginName} MODULE ${TMP_SOURCES} )

  SET_TARGET_PROPERTIES( ${PluginName}
                         PROPERTIES
                         COMPILE_FLAGS ${MTOA_COMPILE_FLAGS}
                         LINK_FLAGS ${MTOA_LINK_FLAGS}
                         PREFIX "" )

  TARGET_LINK_LIBRARIES ( ${PluginName} ${ALEMBIC_MTOA_LIBMTOA} )

ENDMACRO(ADD_MTOA_CXX_PLUGIN)
