# Port of bullet3/examples/BulletRobotics — box stacking with increasing mass + fixed joint pairs
if (NOT URHO3D_PHYSICS)
    return ()
endif ()

set (TARGET_NAME 03_BS_BulletRobotics)
define_source_files (EXTRA_H_FILES ${COMMON_SAMPLE_H_FILES})
setup_main_executable ()
set_target_properties (${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BULLET_SAMPLES_OUTPUT_DIR})
