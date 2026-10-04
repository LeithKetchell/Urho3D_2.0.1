# Port of bullet3/examples/InverseDynamics — NOT PORTABLE — Requires btMultiBody (featherstone)
if (NOT URHO3D_PHYSICS)
    return ()
endif ()

set (TARGET_NAME 15_BS_InverseDynamics)
define_source_files (EXTRA_H_FILES ${COMMON_SAMPLE_H_FILES})
setup_main_executable ()
set_target_properties (${TARGET_NAME} PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${BULLET_SAMPLES_OUTPUT_DIR})
