# Resolves the ChordStudio plug-in sources that the mix rack compiles in.
#
# This dependency has no git remote and the copy on disk has no .git, so it
# cannot be pinned by commit hash. A known-good closure is vendored under
# third_party/chordstudio and locked by sha256 in its lock.cmake; refreshing the
# copy rewrites both together (tools/vendor-chordstudio.py).
#
# Lookup order:
#   1. -DNC_PLUGINS_ROOT=<path>          explicit, wins over everything
#   2. NC_PLUGINS_ROOT environment var   a live checkout on a dev machine
#   3. third_party/chordstudio           the vendored copy (default)
#
# The vendored lock doubles as the required-file list, so there is exactly one
# place that knows which files this dependency consists of. Only the vendored
# copy is hash-checked: a live checkout is expected to move.
#
# Licence: the ChordStudio snapshot is GPL-3.0, and the mix rack links it
# statically, so the derivative is bound by GPL-3.0 regardless of whether these
# files are copied or read from a sibling checkout. See docs/DEPENDENCIES.md.

set(_nc_chordstudio_vendored "${CMAKE_CURRENT_LIST_DIR}/../third_party/chordstudio")

if(DEFINED NC_PLUGINS_ROOT AND NOT "${NC_PLUGINS_ROOT}" STREQUAL "")
    set(_nc_chordstudio_mode "cache override")
elseif(DEFINED ENV{NC_PLUGINS_ROOT} AND NOT "$ENV{NC_PLUGINS_ROOT}" STREQUAL "")
    set(NC_PLUGINS_ROOT "$ENV{NC_PLUGINS_ROOT}")
    set(_nc_chordstudio_mode "environment override")
else()
    set(NC_PLUGINS_ROOT "${_nc_chordstudio_vendored}")
    set(_nc_chordstudio_mode "vendored")
endif()

get_filename_component(NC_PLUGINS_ROOT "${NC_PLUGINS_ROOT}"
                       ABSOLUTE BASE_DIR "${CMAKE_CURRENT_LIST_DIR}")

set(_nc_lock_file "${_nc_chordstudio_vendored}/lock.cmake")
if(NOT EXISTS "${_nc_lock_file}")
    message(FATAL_ERROR
        "third_party/chordstudio/lock.cmake is missing, so the ChordStudio\n"
        "dependency has no file list and no provenance. Restore it from git, or\n"
        "regenerate it from a checkout:\n"
        "  python tools/vendor-chordstudio.py <path-to-ChordStudio>")
endif()
include("${_nc_lock_file}")

set(_nc_missing "")
set(_nc_drift "")
foreach(_nc_entry IN LISTS NC_CHORDSTUDIO_LOCK)
    string(FIND "${_nc_entry}" "=" _nc_eq REVERSE)
    string(SUBSTRING "${_nc_entry}" 0 ${_nc_eq} _nc_rel)
    math(EXPR _nc_start "${_nc_eq} + 1")
    string(SUBSTRING "${_nc_entry}" ${_nc_start} -1 _nc_want)

    set(_nc_path "${NC_PLUGINS_ROOT}/${_nc_rel}")
    if(NOT EXISTS "${_nc_path}")
        list(APPEND _nc_missing "  ${_nc_rel}")
    elseif(_nc_chordstudio_mode STREQUAL "vendored")
        file(SHA256 "${_nc_path}" _nc_got)
        if(NOT _nc_got STREQUAL _nc_want)
            list(APPEND _nc_drift "  ${_nc_rel}")
        endif()
    endif()
endforeach()

if(_nc_missing)
    string(REPLACE ";" "\n" _nc_missing "${_nc_missing}")
    message(FATAL_ERROR
        "ChordStudio sources not found under:\n  ${NC_PLUGINS_ROOT}\n"
        "Missing:\n${_nc_missing}\n\n"
        "The default is the vendored copy at third_party/chordstudio. Either\n"
        "restore that directory from git, or point at a live checkout:\n"
        "  cmake -S . -B build -DNC_PLUGINS_ROOT=<path-to-ChordStudio>\n"
        "or set the NC_PLUGINS_ROOT environment variable.")
endif()

if(_nc_drift)
    string(REPLACE ";" "\n" _nc_drift "${_nc_drift}")
    message(FATAL_ERROR
        "The vendored ChordStudio copy no longer matches its lock:\n"
        "${_nc_drift}\n\n"
        "Nothing should edit third_party/chordstudio by hand. If the copy was\n"
        "intentionally refreshed, commit lock.cmake with it; otherwise restore\n"
        "the files from git.")
endif()

list(LENGTH NC_CHORDSTUDIO_LOCK _nc_count)
message(STATUS "ChordStudio sources: ${NC_PLUGINS_ROOT} (${_nc_chordstudio_mode}, ${_nc_count} files)")
