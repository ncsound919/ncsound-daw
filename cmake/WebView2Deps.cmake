# WebView2 is a Windows-only build dependency of the hub UI. app/CMakeLists.txt
# asks for NEEDS_WEBVIEW2 and links the statically linked loader, so configure
# needs the Microsoft.Web.WebView2 NuGet package.
#
# JUCE's FindWebView2.cmake knows two sources only: the NuGet package folder
# under %USERPROFILE%, and the JUCE_WEBVIEW2_PACKAGE_LOCATION variable. On the
# release machine the package lives outside the repository, so that variable was
# set in one build/CMakeCache.txt and nowhere else - every fresh build directory
# then failed inside juce_add_gui_app with "Could NOT find WebView2". Accepting
# the location from the environment keeps the choice explicit while letting a
# clean build directory and CI configure with no cache edits.
#
# The directory passed must contain the Microsoft.Web.WebView2* package
# directory; that is the layout FindWebView2.cmake globs for.

if(NOT WIN32)
    return()
endif()

# An explicit JUCE_WEBVIEW2_PACKAGE_LOCATION, from -D or the cache, wins.
if(JUCE_WEBVIEW2_PACKAGE_LOCATION)
    return()
endif()

set(_nc_webview2_requested "")
if(NC_WEBVIEW2_PACKAGE_LOCATION)
    set(_nc_webview2_requested "${NC_WEBVIEW2_PACKAGE_LOCATION}")
elseif(DEFINED ENV{NC_WEBVIEW2_PACKAGE_LOCATION})
    set(_nc_webview2_requested "$ENV{NC_WEBVIEW2_PACKAGE_LOCATION}")
endif()

if(_nc_webview2_requested)
    # Only a package directory is usable. JUCE globs this path, takes the first
    # match and looks for build/native/include/WebView2.h inside it, and a file
    # such as Microsoft.Web.WebView2.nuspec matches the glob as well - so
    # accepting any match would hand JUCE a path it cannot use.
    file(GLOB _nc_webview2_candidates LIST_DIRECTORIES true
         "${_nc_webview2_requested}/*Microsoft.Web.WebView2*")
    set(_nc_webview2_package "")
    foreach(_nc_candidate IN LISTS _nc_webview2_candidates)
        if(IS_DIRECTORY "${_nc_candidate}" AND
           EXISTS "${_nc_candidate}/build/native/include/WebView2.h")
            set(_nc_webview2_package "${_nc_candidate}")
            break()
        endif()
    endforeach()

    if(NOT _nc_webview2_package)
        message(FATAL_ERROR
            "NC_WEBVIEW2_PACKAGE_LOCATION is \"${_nc_webview2_requested}\", but no "
            "usable Microsoft.Web.WebView2* package directory is in it (expected "
            "build/native/include/WebView2.h inside the package). Point it at the "
            "directory that contains the package folder, not at the package "
            "folder itself.")
    endif()

    message(STATUS "WebView2 package: ${_nc_webview2_package}")
    # A normal variable, not a cache entry: a cache entry would bake this
    # machine's absolute path into any build directory configured with the
    # environment variable set.
    set(JUCE_WEBVIEW2_PACKAGE_LOCATION "${_nc_webview2_requested}")
    return()
endif()

# Nothing supplied: JUCE will fall back to the NuGet folder. Report the problem
# here instead of deep inside juce_add_gui_app, and name both fixes.
file(GLOB _nc_webview2_nuget
     "$ENV{USERPROFILE}/AppData/Local/PackageManagement/NuGet/Packages/*Microsoft.Web.WebView2*")
if(NOT _nc_webview2_nuget)
    message(FATAL_ERROR
        "The WebView2 package was not found and the hub UI requires it "
        "(juce_add_gui_app NEEDS_WEBVIEW2).
"
        "Either point NC_WEBVIEW2_PACKAGE_LOCATION at the directory containing "
        "the Microsoft.Web.WebView2* package directory, or install it:
"
        "  Register-PackageSource -provider NuGet -name nugetRepository "
        "-location https://www.nuget.org/api/v2
"
        "  Install-Package Microsoft.Web.WebView2 -Scope CurrentUser "
        "-RequiredVersion 1.0.3485.44 -Source nugetRepository")
endif()
