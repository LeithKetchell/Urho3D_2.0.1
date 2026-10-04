# Bullet Physics Samples — ported from the Bullet SDK
if (NOT URHO3D_PHYSICS OR NOT URHO3D_BULLET_SAMPLES)
    return ()
endif ()
set (BULLET_SAMPLES_OUTPUT_DIR ${CMAKE_BINARY_DIR}/bin/BulletSamples CACHE INTERNAL "")
file (MAKE_DIRECTORY ${BULLET_SAMPLES_OUTPUT_DIR})
execute_process (COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/bin/Data ${BULLET_SAMPLES_OUTPUT_DIR}/Data)
execute_process (COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/bin/CoreData ${BULLET_SAMPLES_OUTPUT_DIR}/CoreData)
execute_process (COMMAND ${CMAKE_COMMAND} -E create_symlink ${CMAKE_BINARY_DIR}/bin/Autoload ${BULLET_SAMPLES_OUTPUT_DIR}/Autoload)
