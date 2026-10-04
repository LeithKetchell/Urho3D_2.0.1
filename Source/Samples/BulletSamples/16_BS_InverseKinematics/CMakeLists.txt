# Port of bullet3/examples/InverseKinematics — NOT PORTABLE — Requires custom IK solver on btMultiBody
if (NOT URHO3D_PHYSICS)
    return ()
endif ()

set (TARGET_NAME 16_BS_InverseKinematics)
define_source_files (EXTRA_H_FILES ${COMMON_SAMPLE_H_FILES})
setup_main_executable ()
set_target_properties (${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BULLET_SAMPLES_OUTPUT_DIR})
