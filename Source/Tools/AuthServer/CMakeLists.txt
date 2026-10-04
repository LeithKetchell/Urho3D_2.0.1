# AuthServer — private central authority binary
# NOT a public sample or tool — private server for TerrainNode network

if (NOT URHO3D_NETWORK OR NOT URHO3D_DATABASE_SQLITE)
    return ()
endif ()

# Define target name
set (TARGET_NAME AuthServer)

# Define source files — pull in shared ResourceMap from Sample 60
set (SAMPLE60_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../Samples/UrhoSamples/60_TerrainNode)
define_source_files (
    EXTRA_CPP_FILES ${SAMPLE60_DIR}/ResourceMap.cpp ${SAMPLE60_DIR}/EcosystemManager.cpp
                    ${CMAKE_CURRENT_SOURCE_DIR}/NPCPriorityCompute.cpp
    EXTRA_H_FILES   ${SAMPLE60_DIR}/ResourceMap.h ${SAMPLE60_DIR}/EcosystemManager.h
                    ${CMAKE_CURRENT_SOURCE_DIR}/NPCPriorityCompute.h
)
include_directories (${SAMPLE60_DIR})

# URHO3D_DATABASE_SQLITE is exported via Urho3D.h — no add_definitions needed

# Setup target
setup_main_executable (NOBUNDLE)
