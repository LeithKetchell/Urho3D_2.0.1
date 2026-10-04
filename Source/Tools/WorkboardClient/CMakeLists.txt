# WorkboardClient — thin LAN client for remote workboard viewing
set (TARGET_NAME WorkboardClient)
define_source_files ()

# WorkboardBase is shared code living in WorkboardManager/
list (APPEND SOURCE_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/../WorkboardManager/WorkboardBase.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../WorkboardManager/WorkboardBase.h
)

setup_main_executable (NOBUNDLE)
