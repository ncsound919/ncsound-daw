include_guard(GLOBAL)

# Shared target setup so every library and app in the repo compiles the same way.
# PCH is opt-in per target: set NC_PCH_HEADERS before calling nc_add_library.
function(nc_add_library name)
    cmake_parse_arguments(ARG "" "" "SOURCES;HEADERS" ${ARGN})

    add_library(${name} STATIC ${ARG_SOURCES} ${ARG_HEADERS})

    target_compile_features(${name} PUBLIC cxx_std_20)
    set_target_properties(${name} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        POSITION_INDEPENDENT_CODE ON)

    if(MSVC)
        target_compile_options(${name} PRIVATE /W4 /WX /permissive- /utf-8 /EHsc)
    else()
        target_compile_options(${name} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()

    if(NC_PCH_HEADERS)
        target_precompile_headers(${name} PRIVATE ${NC_PCH_HEADERS})
    endif()
endfunction()
