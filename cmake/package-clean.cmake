# Runs inside CPack's staging tree after every install rule has fired, including
# the ones JUCE adds for its modules, headers and tools. This product ships the
# plug-in bundles and the docs, so anything else that landed is removed here.
set(staging "${CPACK_TEMPORARY_DIRECTORY}")
if(NOT staging)
    set(staging "${CPACK_TOPLEVEL_DIRECTORY}/NCSound DAW")
endif()
foreach(stray IN ITEMS bin include lib share src)
    file(REMOVE_RECURSE "${staging}/${stray}")
endforeach()
message(STATUS "package-clean: staged tree trimmed to the product payload")
