cmake_minimum_required(VERSION 3.21)
project(WattsFun VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ── Qt ──────────────────────────────────────────────────────────────────────
find_package(Qt6 REQUIRED COMPONENTS Core Widgets Network)
find_package(Qt6 OPTIONAL_COMPONENTS NetworkAuth)
if(Qt6NetworkAuth_FOUND)
    message(STATUS "Qt6NetworkAuth found – Strava upload enabled")
else()
    message(STATUS "Qt6NetworkAuth NOT found – Strava upload disabled (install via Qt Maintenance Tool)")
endif()

# ── ANT+ PC SDK ─────────────────────────────────────────────────────────────
# Set ANTPLUS_SDK_DIR to the root of the Garmin ANT+ PC SDK, e.g.:
#   cmake .. -DANTPLUS_SDK_DIR="C:/ANT+_PC_SDK_x.xx"
set(ANTPLUS_SDK_DIR "" CACHE PATH "Root directory of the Garmin ANT+ PC SDK")

if(NOT ANTPLUS_SDK_DIR STREQUAL "")
    # ANT+ PC SDK 3.5 layout: headers in ANT_DLL/ and ANT_LIB/inc/, DLL in Bin/
    set(ANT_DLL_INCLUDE_DIR "${ANTPLUS_SDK_DIR}/ANT_DLL")
    set(ANT_LIB_INCLUDE_DIR "${ANTPLUS_SDK_DIR}/ANT_LIB/inc")
    set(ANT_DLL_FILE        "${ANTPLUS_SDK_DIR}/Bin/ANT_DLL.dll")
    if(EXISTS "${ANT_DLL_INCLUDE_DIR}/ant.h" AND EXISTS "${ANT_DLL_FILE}")
        set(ANTPLUS_AVAILABLE TRUE)
        message(STATUS "ANT+ SDK found at: ${ANTPLUS_SDK_DIR}")
    else()
        set(ANTPLUS_AVAILABLE FALSE)
        message(WARNING "ANT+ SDK files not found under ${ANTPLUS_SDK_DIR}. "
                        "Expected ${ANT_DLL_INCLUDE_DIR}/ant.h and ${ANT_DLL_FILE}")
    endif()
else()
    set(ANTPLUS_AVAILABLE FALSE)
    message(STATUS "ANTPLUS_SDK_DIR not set – ANT+ support will be compiled as stub")
endif()

# ── Sources ─────────────────────────────────────────────────────────────────
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/deviceselectiondialog.cpp
    src/dashboardwidget.cpp
    src/deviceconfig.cpp
    src/antmanager.cpp
    src/blemanager.cpp
    src/trainerdata.cpp
    src/tcxexporter.cpp
    src/traininglibrary.cpp
    src/workouteditordialog.cpp
)

if(Qt6NetworkAuth_FOUND)
    list(APPEND SOURCES src/stravauploader.cpp)
endif()

set(HEADERS
    src/mainwindow.h
    src/deviceselectiondialog.h
    src/dashboardwidget.h
    src/deviceconfig.h
    src/antmanager.h
    src/blemanager.h
    src/trainerdata.h
    src/iprotocolmanager.h
    src/tcxexporter.h
    src/stravauploader.h
    src/traininglibrary.h
    src/workouteditordialog.h
)

set(UI_FILES
    src/mainwindow.ui
    src/deviceselectiondialog.ui
)

set(RESOURCE_FILES
    resources/resources.qrc
)

set(WIN32_EXECUTABLE_ICON "${CMAKE_SOURCE_DIR}/resources/icon.ico")

add_executable(WattsFun WIN32
    ${SOURCES}
    ${HEADERS}
    ${UI_FILES}
    ${RESOURCE_FILES}
)

target_include_directories(WattsFun PRIVATE src)

target_link_libraries(WattsFun PRIVATE
    Qt6::Core
    Qt6::Widgets
    Qt6::Network
)

if(Qt6NetworkAuth_FOUND)
    target_compile_definitions(WattsFun PRIVATE STRAVA_ENABLED)
    target_link_libraries(WattsFun PRIVATE Qt6::NetworkAuth)
endif()

# ── ANT+ linkage ─────────────────────────────────────────────────────────────
if(ANTPLUS_AVAILABLE)
    target_compile_definitions(WattsFun PRIVATE ANTPLUS_ENABLED)
    # ANT_DLL.dll ships without an import library; loaded at runtime via QLibrary
    target_include_directories(WattsFun PRIVATE "${ANT_DLL_INCLUDE_DIR}" "${ANT_LIB_INCLUDE_DIR}")

    # Copy ANT_DLL.dll next to the executable
    add_custom_command(TARGET WattsFun POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ANT_DLL_FILE}"
            "$<TARGET_FILE_DIR:WattsFun>/ANT_DLL.dll"
        COMMENT "Copying ANT_DLL.dll"
    )
endif()

# ── WinRT / BLE ──────────────────────────────────────────────────────────────
target_link_libraries(WattsFun PRIVATE
    windowsapp          # WinRT projection
    runtimeobject       # RoInitialize
)
target_compile_options(WattsFun PRIVATE /await)   # coroutine support for WinRT

# ── Deploy Qt DLLs (optional helper) ─────────────────────────────────────────
if(WIN32)
    get_target_property(_qmake_exe Qt6::qmake IMPORTED_LOCATION)
    get_filename_component(_qt_bin_dir "${_qmake_exe}" DIRECTORY)
    find_program(WINDEPLOYQT_EXECUTABLE windeployqt HINTS "${_qt_bin_dir}")
    if(WINDEPLOYQT_EXECUTABLE)
        add_custom_command(TARGET WattsFun POST_BUILD
            COMMAND "${WINDEPLOYQT_EXECUTABLE}"
                --no-translations
                "$<TARGET_FILE:WattsFun>"
            COMMENT "Running windeployqt"
        )
    endif()
endif()

# ── Install rules ────────────────────────────────────────────────────────────
install(TARGETS WattsFun RUNTIME DESTINATION .)

# Install Qt runtime DLLs deployed by windeployqt
# We use install(DIRECTORY) on the build output dir after windeployqt has run
install(DIRECTORY "$<TARGET_FILE_DIR:WattsFun>/"
    DESTINATION .
    FILES_MATCHING
    PATTERN "*.dll"
    PATTERN "*.json"          # qt plugin metadata
    PATTERN "CMakeFiles" EXCLUDE
    PATTERN "*.dir"     EXCLUDE
    PATTERN "*.vcxproj*" EXCLUDE
    PATTERN "*.cmake"   EXCLUDE
    PATTERN "*.stamp"   EXCLUDE
    PATTERN "cmake_install.cmake" EXCLUDE
)

# Install Qt plugin subdirectories (platforms, styles, imageformats, etc.)
foreach(_plugdir generic iconengines imageformats networkinformation platforms styles tls)
    if(EXISTS "$<TARGET_FILE_DIR:WattsFun>/${_plugdir}")
        install(DIRECTORY "$<TARGET_FILE_DIR:WattsFun>/${_plugdir}"
            DESTINATION .
        )
    endif()
endforeach()

# Install ANT_DLL.dll if present
if(ANTPLUS_AVAILABLE)
    install(FILES "${ANT_DLL_FILE}" DESTINATION .)
endif()

# ── CPack / NSIS Installer ───────────────────────────────────────────────────
set(CPACK_PACKAGE_NAME "WattsFun")
set(CPACK_PACKAGE_VENDOR "WattsFun")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "WattsFun – Indoor Cycling Trainer")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_VERSION_MAJOR "${PROJECT_VERSION_MAJOR}")
set(CPACK_PACKAGE_VERSION_MINOR "${PROJECT_VERSION_MINOR}")
set(CPACK_PACKAGE_VERSION_PATCH "${PROJECT_VERSION_PATCH}")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "WattsFun")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE.txt")

# Icon for installer
set(CPACK_NSIS_MUI_ICON "${CMAKE_SOURCE_DIR}/resources/WattsFun.ico")
set(CPACK_NSIS_MUI_UNIICON "${CMAKE_SOURCE_DIR}/resources/WattsFun.ico")
set(CPACK_NSIS_INSTALLED_ICON_NAME "WattsFun.exe")
set(CPACK_PACKAGE_ICON "${CMAKE_SOURCE_DIR}/resources\\\\WattsFun.ico")

# Start menu & desktop shortcuts
set(CPACK_NSIS_CREATE_ICONS_EXTRA
    "CreateShortCut '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\WattsFun.lnk' '$INSTDIR\\\\WattsFun.exe'"
)
set(CPACK_NSIS_DELETE_ICONS_EXTRA
    "Delete '$SMPROGRAMS\\\\$STARTMENU_FOLDER\\\\WattsFun.lnk'"
)

# Allow user to modify PATH (disabled – not needed for GUI app)
set(CPACK_NSIS_MODIFY_PATH OFF)

# Generator
set(CPACK_GENERATOR "NSIS")

include(CPack)
