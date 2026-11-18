# Release packaging (CPack).
#
# The application and both plug-ins are deliverables that build entirely from
# this tree, so all three are packaged together. The ZIP installs the app at the
# archive root, the plug-ins under VST3/ and Standalone/, and the licences and
# notices beside them. NSIS/WIX are added only when their toolchains are
# installed, so `cpack` still produces the portable archive on a machine that
# has no installer toolchain.

option(NC_ENABLE_PACKAGING "Generate CPack install and packaging rules" ON)

if(NOT NC_ENABLE_PACKAGING)
    return()
endif()

set(CPACK_PACKAGE_NAME "NCSound DAW")
set(CPACK_PACKAGE_VENDOR "NCSound")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "NCSound DAW plug-in suite (808 Designer, Chop Station)")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "NCSound DAW")
set(CPACK_PACKAGE_FILE_NAME "ncsound-daw-${PROJECT_VERSION}-${CMAKE_SYSTEM_PROCESSOR}")

# Payload: copy JUCE's artefact bundles rather than install(TARGETS), because on
# Windows a .vst3 is a directory bundle and the whole tree is what a host needs.
foreach(plugin IN ITEMS "NCEightOHeight:eightoheight" "NCChopStation:chopstation")
    string(REPLACE ":" ";" plugin_parts "${plugin}")
    list(GET plugin_parts 0 plugin_target)
    list(GET plugin_parts 1 plugin_dir)
    set(plugin_artefacts "${CMAKE_BINARY_DIR}/plugins/${plugin_dir}/${plugin_target}_artefacts/$<CONFIG>")
    # Only the loadable bundle: the same folder also holds the linker's .exp,
    # .lib and .pdb output, which must never ship.
    install(DIRECTORY "${plugin_artefacts}/VST3/" DESTINATION "VST3" COMPONENT plugins
        PATTERN "*.exp" EXCLUDE
        PATTERN "*.lib" EXCLUDE
        PATTERN "*.pdb" EXCLUDE
        PATTERN "*.ilk" EXCLUDE)
    install(DIRECTORY "${plugin_artefacts}/Standalone/" DESTINATION "Standalone" COMPONENT plugins)
endforeach()

install(DIRECTORY "${CMAKE_CURRENT_LIST_DIR}/../docs/" DESTINATION "docs" COMPONENT docs OPTIONAL)
install(FILES "${CMAKE_CURRENT_LIST_DIR}/../LICENSE" "${CMAKE_CURRENT_LIST_DIR}/../THIRD-PARTY-NOTICES.md" DESTINATION "." COMPONENT docs OPTIONAL)

# CPack's NSIS generator emits `File /r "${INST_DIR}\plugins\*.*"` per component,
# and NSIS's *.* wildcard does not match bare directories. The plugins component
# holds only bundle folders (Standalone/, VST3/*.vst3/), so that section had no
# files to install and the NSIS script aborted before producing an installer.
# One file at the top of the component keeps the section valid; it is also a
# useful note in the installed tree.
set(NC_PLUGINS_LAYOUT "${CMAKE_CURRENT_BINARY_DIR}/plugins-layout.txt")
file(WRITE "${NC_PLUGINS_LAYOUT}"
     "NCSound DAW plug-ins\n\n"
     "Standalone/  standalone application build\n"
     "VST3/        VST3 bundles; copy this folder into your VST3 directory\n")
install(FILES "${NC_PLUGINS_LAYOUT}" DESTINATION "plugins" COMPONENT plugins)

# Package only our components. Without this, JUCE's own juceaide install rule
# (no component) lands a compiler tool in bin/ inside the product archive.
set(CPACK_COMPONENTS_ALL "app;plugins;docs")

set(CPACK_GENERATOR "ZIP")
if(WIN32)
    # NSIS is open source and runs from an extracted zip, so the installer does
    # not require a system-wide install: point NC_MAKENSIS at a makensis.exe, or
    # drop the NSIS zip's contents beside the checkout.
    find_program(NC_MAKENSIS_EXECUTABLE makensis
                 HINTS "${NC_MAKENSIS}" "$ENV{NC_MAKENSIS}"
                       "${CMAKE_CURRENT_LIST_DIR}/../tools/nsis"
                 PATH_SUFFIXES Bin)
    find_program(NC_WIX_CANDLE_EXECUTABLE candle)
    if(NC_MAKENSIS_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "NSIS64")
        set(CPACK_NSIS_EXECUTABLE "${NC_MAKENSIS_EXECUTABLE}")
    elseif(NC_WIX_CANDLE_EXECUTABLE)
        list(APPEND CPACK_GENERATOR "WIX")
    endif()
endif()
if(CPACK_GENERATOR MATCHES "NSIS")
    set(CPACK_NSIS_DISPLAY_NAME "NCSound DAW")
    set(CPACK_NSIS_PACKAGE_NAME "NCSound DAW")
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_PACKAGE_INSTALL_REGISTRY_KEY "NCSoundDAW")
    set(CPACK_NSIS_MODIFY_PATH OFF)
endif()

set(CPACK_PRE_BUILD_SCRIPTS "${CMAKE_CURRENT_LIST_DIR}/package-clean.cmake")

include(CPack)
