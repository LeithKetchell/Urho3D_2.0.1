# Open Asset Import Library (assimp)
# ----------------------------------------------------------------------
# Copyright (c) 2006-2024, assimp team
# All rights reserved.
#
# Redistribution and use of this software in source and binary forms,
# with or without modification, are permitted provided that the
# following conditions are met:
#
# * Redistributions of source code must retain the above
#   copyright notice, this list of conditions and the
#   following disclaimer.
#
# * Redistributions in binary form must reproduce the above
#   copyright notice, this list of conditions and the
#   following disclaimer in the documentation and/or other
#   materials provided with the distribution.
#
# * Neither the name of the assimp team, nor the names of its
#   contributors may be used to endorse or promote products
#   derived from this software without specific prior
#   written permission of the assimp team.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#----------------------------------------------------------------------

# Modified for Urho3D — upgraded from 4.1.0 to 6.0.4

PROJECT( Assimp )

# Urho3D: Static library only, no shared libs
SET(BUILD_SHARED_LIBS OFF)

OPTION( ASSIMP_DOUBLE_PRECISION
  "Set to ON to enable double precision processing"
  OFF
)

OPTION( ASSIMP_NO_EXPORT
  "Disable Assimp's export functionality."
  OFF
)

if (WIN32)
  ADD_DEFINITIONS( -DWIN32_LEAN_AND_MEAN )
endif()

# Define here the needed parameters — Assimp 6.0.4
SET (ASSIMP_VERSION_MAJOR 6)
SET (ASSIMP_VERSION_MINOR 0)
SET (ASSIMP_VERSION_PATCH 4)
SET (ASSIMP_VERSION ${ASSIMP_VERSION_MAJOR}.${ASSIMP_VERSION_MINOR}.${ASSIMP_VERSION_PATCH})
SET (ASSIMP_SOVERSION 6)
SET (PROJECT_VERSION "${ASSIMP_VERSION}")

SET( ASSIMP_PACKAGE_VERSION "0" CACHE STRING "the package-specific version used for uploading the sources" )

# Urho3D: Disable features we don't need
SET (ASSIMP_HUNTER_ENABLED OFF CACHE BOOL "" FORCE)
SET (ASSIMP_BUILD_DRACO OFF CACHE BOOL "" FORCE)
SET (ASSIMP_BUILD_USD_IMPORTER OFF CACHE BOOL "" FORCE)
SET (ASSIMP_INSTALL OFF CACHE BOOL "" FORCE)
SET (ASSIMP_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
SET (ASSIMP_BUILD_FRAMEWORK OFF CACHE BOOL "" FORCE)
SET (ASSIMP_ANDROID_JNIIOSYSTEM OFF CACHE BOOL "" FORCE)
SET (ASSIMP_COVERALLS OFF CACHE BOOL "" FORCE)
SET (ASSIMP_BUILD_NONFREE_C4D_IMPORTER OFF CACHE BOOL "" FORCE)
SET (ASSIMP_BUILD_VRML_IMPORTER OFF CACHE BOOL "" FORCE)
# X3D requires VRML/meshlab, M3D requires m3d.h — disable both
ADD_DEFINITIONS( -DASSIMP_BUILD_NO_X3D_IMPORTER -DASSIMP_BUILD_NO_X3D_EXPORTER
                 -DASSIMP_BUILD_NO_M3D_IMPORTER -DASSIMP_BUILD_NO_M3D_EXPORTER )
ADD_DEFINITIONS( -DASSIMP_BUILD_NO_C4D_IMPORTER )

# Urho3D: openddlparser config
ADD_DEFINITIONS( -DOPENDDLPARSER_BUILD )

# Urho3D: static lib defines
ADD_DEFINITIONS( -DOPENDDL_STATIC_LIBARY -DP2T_STATIC_EXPORTS )

# Urho3D: no-op for TARGET_USE_COMMON_OUTPUT_DIRECTORY (Assimp 6.0 expects this macro)
if (NOT COMMAND TARGET_USE_COMMON_OUTPUT_DIRECTORY)
  macro(TARGET_USE_COMMON_OUTPUT_DIRECTORY target)
    # no-op in Urho3D build
  endmacro()
endif()

# Urho3D: hardcoded git info
SET (GIT_BRANCH modified-for-urho3d)
SET (GIT_COMMIT_HASH 80799bd)

IF(NOT GIT_COMMIT_HASH)
    SET(GIT_COMMIT_HASH 0)
ENDIF(NOT GIT_COMMIT_HASH)

IF(ASSIMP_DOUBLE_PRECISION)
    ADD_DEFINITIONS(-DASSIMP_DOUBLE_PRECISION)
ENDIF(ASSIMP_DOUBLE_PRECISION)

# Urho3D: Configure revision.h into include/assimp/ so #include <assimp/revision.h> works
CONFIGURE_FILE(
  ${CMAKE_CURRENT_LIST_DIR}/include/assimp/revision.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/assimp/revision.h
)

CONFIGURE_FILE(
  ${CMAKE_CURRENT_LIST_DIR}/include/assimp/config.h.in
  ${CMAKE_CURRENT_BINARY_DIR}/include/assimp/config.h
)

# Urho3D: configure zlib header
CONFIGURE_FILE(
  ${CMAKE_CURRENT_SOURCE_DIR}/contrib/zlib/zconf.h.cmakein
  ${CMAKE_CURRENT_BINARY_DIR}/contrib/zlib/zconf.h @ONLY
)

# Urho3D: Use system zlib, fall back to bundled if not found
FIND_PACKAGE(ZLIB QUIET)
IF(NOT ZLIB_FOUND)
  message(STATUS "System zlib not found, using bundled contrib/zlib")
  SET(ZLIB_FOUND 1)
  SET(ZLIB_LIBRARIES "")
  SET(ZLIB_INCLUDE_DIR
    ${CMAKE_CURRENT_SOURCE_DIR}/contrib/zlib
    ${CMAKE_CURRENT_BINARY_DIR}/contrib/zlib
  )
ELSE()
  message(STATUS "Using system zlib: ${ZLIB_LIBRARIES}")
  ADD_DEFINITIONS(-DASSIMP_BUILD_NO_OWN_ZLIB)
ENDIF()
INCLUDE_DIRECTORIES(${ZLIB_INCLUDE_DIR})

INCLUDE_DIRECTORIES(
    ./
    include
    code
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/include
)

LIST(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake-modules" )

IF ( ASSIMP_NO_EXPORT )
  ADD_DEFINITIONS( -DASSIMP_BUILD_NO_EXPORT)
  MESSAGE( STATUS "Build an import-only version of Assimp." )
ENDIF( ASSIMP_NO_EXPORT )

SET ( ASSIMP_BUILD_ARCHITECTURE "" CACHE STRING
  "describe the current architecture."
)
IF    ( ASSIMP_BUILD_ARCHITECTURE STREQUAL "")
ELSE  ( ASSIMP_BUILD_ARCHITECTURE STREQUAL "")
  ADD_DEFINITIONS ( -D'ASSIMP_BUILD_ARCHITECTURE="${ASSIMP_BUILD_ARCHITECTURE}"' )
ENDIF ( ASSIMP_BUILD_ARCHITECTURE STREQUAL "")

SET ( ASSIMP_BUILD_COMPILER "" CACHE STRING
  "describe the current compiler."
)
IF    ( ASSIMP_BUILD_COMPILER STREQUAL "")
ELSE  ( ASSIMP_BUILD_COMPILER STREQUAL "")
  ADD_DEFINITIONS ( -D'ASSIMP_BUILD_COMPILER="${ASSIMP_BUILD_COMPILER}"' )
ENDIF ( ASSIMP_BUILD_COMPILER STREQUAL "")

MARK_AS_ADVANCED ( ASSIMP_BUILD_ARCHITECTURE ASSIMP_BUILD_COMPILER )

ADD_SUBDIRECTORY( code/ )
