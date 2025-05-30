set(INKSCAPE_LIBS "")
set(INKSCAPE_INCS "")
set(INKSCAPE_INCS_SYS "")
set(INKSCAPE_CXX_FLAGS "")
set(INKSCAPE_CXX_FLAGS_DEBUG "")

list(APPEND INKSCAPE_INCS ${PROJECT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/src

    # generated includes
    ${CMAKE_BINARY_DIR}/include
)

# NDEBUG implies G_DISABLE_ASSERT
string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE_UPPER)
if(CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE_UPPER} MATCHES "-DNDEBUG")
    list(APPEND INKSCAPE_CXX_FLAGS "-DG_DISABLE_ASSERT")
endif()

# AddressSanitizer
# Clang's AddressSanitizer can detect more memory errors and is more powerful
# than compiling with _FORTIFY_SOURCE but has a performance impact (approx. 2x
# slower), so it's not suitable for release builds.
if(WITH_ASAN)
    list(APPEND INKSCAPE_CXX_FLAGS "-fsanitize=address -fno-omit-frame-pointer")
    list(APPEND INKSCAPE_LIBS "-fsanitize=address")
else()
    # Undefine first, to suppress 'warning: "_FORTIFY_SOURCE" redefined'
    list(APPEND INKSCAPE_CXX_FLAGS "-U_FORTIFY_SOURCE")
    list(APPEND INKSCAPE_CXX_FLAGS "-D_FORTIFY_SOURCE=2")
endif()

# Disable deprecated Gtk and friends
#list(APPEND INKSCAPE_CXX_FLAGS "-DGLIBMM_DISABLE_DEPRECATED")
#list(APPEND INKSCAPE_CXX_FLAGS "-DGTKMM_DISABLE_DEPRECATED")
#list(APPEND INKSCAPE_CXX_FLAGS "-DGDKMM_DISABLE_DEPRECATED")
#list(APPEND INKSCAPE_CXX_FLAGS "-DGTK_DISABLE_DEPRECATED")
#list(APPEND INKSCAPE_CXX_FLAGS "-DGDK_DISABLE_DEPRECATED")

# Errors for common mistakes
list(APPEND INKSCAPE_CXX_FLAGS "-fstack-protector-strong")
list(APPEND INKSCAPE_CXX_FLAGS "-Werror=format")                # e.g.: printf("%s", std::string("foo"))
list(APPEND INKSCAPE_CXX_FLAGS "-Werror=format-security")       # e.g.: printf(variable);
list(APPEND INKSCAPE_CXX_FLAGS "-Werror=ignored-qualifiers")    # e.g.: const int foo();
list(APPEND INKSCAPE_CXX_FLAGS "-Werror=return-type")           # non-void functions that don't return a value
list(APPEND INKSCAPE_CXX_FLAGS "-Wno-switch")                   # See !849 for discussion
list(APPEND INKSCAPE_CXX_FLAGS "-Wmisleading-indentation")
list(APPEND INKSCAPE_CXX_FLAGS_DEBUG "-Wcomment")
list(APPEND INKSCAPE_CXX_FLAGS_DEBUG "-Wunused-function")
list(APPEND INKSCAPE_CXX_FLAGS_DEBUG "-Wunused-variable")
list(APPEND INKSCAPE_CXX_FLAGS_DEBUG "-D_GLIBCXX_ASSERTIONS")
if (CMAKE_COMPILER_IS_GNUCC)
    list(APPEND INKSCAPE_CXX_FLAGS "-Wstrict-null-sentinel")    # For NULL instead of nullptr
    list(APPEND INKSCAPE_CXX_FLAGS_DEBUG "-fexceptions -grecord-gcc-switches -fasynchronous-unwind-tables")
    if(CXX_COMPILER_VERSION VERSION_GREATER 8.0)
        list(APPEND INKSCAPE_CXX_FLAGS_DEBUG "-fstack-clash-protection -fcf-protection")
    endif()
endif()

# Define the flags for profiling if desired:
if(WITH_PROFILING)
    set(BUILD_SHARED_LIBS off)
    SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -pg")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pg")
endif()

include(CheckCXXSourceCompiles)
CHECK_CXX_SOURCE_COMPILES("
#include <atomic>
#include <cstdint>
std::atomic<uint64_t> x (0);
int main() {
  uint64_t i = x.load(std::memory_order_relaxed);
  return 0;
}
"
LIBATOMIC_NOT_NEEDED)
IF (NOT LIBATOMIC_NOT_NEEDED)
    message(STATUS "  Adding -latomic to the libs.")
    list(APPEND INKSCAPE_LIBS "-latomic")
ENDIF()


# ----------------------------------------------------------------------------
# Helper macros
# ----------------------------------------------------------------------------

# Turns linker arguments like "-framework Foo" into "-Wl,-framework,Foo" to
# make them safe for appending to INKSCAPE_LIBS
macro(sanitize_ldflags_for_libs ldflags_var)
    # matches dash-argument followed by non-dash-argument
    string(REGEX REPLACE "(^|;)(-[^;]*);([^-])" "\\1-Wl,\\2,\\3" ${ldflags_var} "${${ldflags_var}}")
endmacro()


# ----------------------------------------------------------------------------
# Files we include
# ----------------------------------------------------------------------------
if(WIN32)
    # Set the link and include directories
    get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)

    list(APPEND INKSCAPE_LIBS "-lmscms")
    list(APPEND INKSCAPE_LIBS "-ldwmapi")

    list(APPEND INKSCAPE_CXX_FLAGS "-mms-bitfields")
    if(${CMAKE_CXX_COMPILER_ID} STREQUAL "GNU")
      list(APPEND INKSCAPE_CXX_FLAGS "-mwindows")
      list(APPEND INKSCAPE_CXX_FLAGS "-mthreads")
    endif()

    list(APPEND INKSCAPE_LIBS "-lwinpthread")

    if(HAVE_MINGW64)
        list(APPEND INKSCAPE_CXX_FLAGS "-m64")
    else()
        list(APPEND INKSCAPE_CXX_FLAGS "-m32")
    endif()

    # Fixes for windows.h and GTK4
    add_definitions(-DNOGDI)
    add_definitions(-D_NO_W32_PSEUDO_MODIFIERS)
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(INKSCAPE_DEP REQUIRED
                  harfbuzz>=2.6.5
                  pangocairo>=1.44
                  pangoft2
                  fontconfig
                  gsl
                  gmodule-2.0
                  #double-conversion
                  bdw-gc #boehm-demers-weiser gc
                  lcms2)

# remove this line and uncomment the doiuble-conversion above when double-conversion.pc file gets shipped on all platforms we support
find_package(DoubleConversion REQUIRED)  # lib2geom dependency

sanitize_ldflags_for_libs(INKSCAPE_DEP_LDFLAGS)
list(APPEND INKSCAPE_LIBS ${INKSCAPE_DEP_LDFLAGS})
list(APPEND INKSCAPE_INCS_SYS ${INKSCAPE_DEP_INCLUDE_DIRS})

add_definitions(${INKSCAPE_DEP_CFLAGS_OTHER})

if(WITH_JEMALLOC)
    find_package(JeMalloc)
    if (JEMALLOC_FOUND)
        list(APPEND INKSCAPE_LIBS ${JEMALLOC_LIBRARIES})
    else()
        set(WITH_JEMALLOC OFF)
    endif()
endif()

find_package(Iconv REQUIRED)
list(APPEND INKSCAPE_INCS_SYS ${Iconv_INCLUDE_DIRS})
list(APPEND INKSCAPE_LIBS ${Iconv_LIBRARIES})

find_package(Intl REQUIRED)
list(APPEND INKSCAPE_INCS_SYS ${Intl_INCLUDE_DIRS})
list(APPEND INKSCAPE_LIBS ${Intl_LIBRARIES})
add_definitions(${Intl_DEFINITIONS})

# Check for system-wide version of 2geom and fallback to internal copy if not found
if(NOT WITH_INTERNAL_2GEOM)
    pkg_check_modules(2Geom QUIET IMPORTED_TARGET GLOBAL 2geom>=${INKSCAPE_VERSION_MAJOR}.${INKSCAPE_VERSION_MINOR})
    if(2Geom_FOUND)
        add_library(2Geom::2geom ALIAS PkgConfig::2Geom)
    else()
        set(WITH_INTERNAL_2GEOM ON CACHE BOOL "Prefer internal copy of lib2geom" FORCE)
        message(STATUS "lib2geom not found, using internal copy in src/3rdparty/2geom")
    endif()
endif()
if(WITH_INTERNAL_2GEOM)
  set(2Geom_INCLUDE_DIRS ${CMAKE_SOURCE_DIR}/src/3rdparty/2geom/include)
endif()

if(APPLE)
    message(STATUS "New CMYK PDF exporter disabled on macOSX")
elseif(WITH_CAPYPDF)
    set(CAPY_PREFIX ${CMAKE_CURRENT_BINARY_DIR}/deps)
    set(CAPY_LIBDIR ${CAPY_PREFIX}/${CMAKE_INSTALL_LIBDIR})
    include(ExternalProject)
    ExternalProject_Add(capypdf
        URL https://github.com/jpakkane/capypdf/archive/refs/tags/0.16.0.zip
        URL_HASH SHA512=24b80a384ee2a78c17b3591a8c78a4677867bc3474771ad8e9dc245dfc960959689f0b19a9dbae3cf78d8b4aaaa66f5e7c924e5650341de0af74f654e3259027
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND meson setup . ../capypdf --libdir=${CAPY_LIBDIR} --prefix=${CAPY_PREFIX}
        BUILD_COMMAND meson compile
        INSTALL_COMMAND meson install
    )
    include_directories("${CAPY_PREFIX}/include/capypdf-0")
    link_directories("${CAPY_LIBDIR}")
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH}:${CAPY_LIBDIR}")
    list(APPEND INKSCAPE_LIBS -lcapypdf)
    add_definitions(-DWITH_CAPYPDF)
endif()

if(WITH_POPPLER)
    find_package(PopplerCairo)
    if(POPPLER_FOUND)
        if(ENABLE_POPPLER_CAIRO)
            if(POPPLER_CAIRO_FOUND AND POPPLER_GLIB_FOUND)
                set(HAVE_POPPLER_CAIRO ON)
            endif()
        endif()

        list(APPEND INKSCAPE_INCS_SYS ${POPPLER_INCLUDE_DIRS})
        list(APPEND INKSCAPE_LIBS     ${POPPLER_LIBRARIES})
        add_definitions(${POPPLER_DEFINITIONS})
        add_definitions(-DWITH_POPPLER)
    else()
        set(WITH_POPPLER OFF)
        set(ENABLE_POPPLER_CAIRO OFF)
    endif()
else()
    set(ENABLE_POPPLER_CAIRO OFF)
endif()

if(WITH_LIBWPG)
    pkg_check_modules(LIBWPG libwpg-0.3 librevenge-0.0 librevenge-stream-0.0)
    if(LIBWPG_FOUND)
        sanitize_ldflags_for_libs(LIBWPG_LDFLAGS)
        list(APPEND INKSCAPE_INCS_SYS ${LIBWPG_INCLUDE_DIRS})
        list(APPEND INKSCAPE_LIBS     ${LIBWPG_LDFLAGS})
        add_definitions(${LIBWPG_DEFINITIONS})
    else()
        set(WITH_LIBWPG OFF)
    endif()
endif()

if(WITH_LIBVISIO)
    pkg_check_modules(LIBVISIO libvisio-0.1 librevenge-0.0 librevenge-stream-0.0)
    if(LIBVISIO_FOUND)
        sanitize_ldflags_for_libs(LIBVISIO_LDFLAGS)
        list(APPEND INKSCAPE_INCS_SYS ${LIBVISIO_INCLUDE_DIRS})
        list(APPEND INKSCAPE_LIBS     ${LIBVISIO_LDFLAGS})
        add_definitions(${LIBVISIO_DEFINITIONS})
    else()
        set(WITH_LIBVISIO OFF)
    endif()
endif()

if(WITH_LIBCDR)
    pkg_check_modules(LIBCDR libcdr-0.1 librevenge-0.0 librevenge-stream-0.0)
    if(LIBCDR_FOUND)
        sanitize_ldflags_for_libs(LIBCDR_LDFLAGS)
        list(APPEND INKSCAPE_INCS_SYS ${LIBCDR_INCLUDE_DIRS})
        list(APPEND INKSCAPE_LIBS     ${LIBCDR_LDFLAGS})
        add_definitions(${LIBCDR_DEFINITIONS})
    else()
        set(WITH_LIBCDR OFF)
    endif()
endif()

FIND_PACKAGE(JPEG)
IF(JPEG_FOUND)
    list(APPEND INKSCAPE_INCS_SYS ${JPEG_INCLUDE_DIR})
    list(APPEND INKSCAPE_LIBS ${JPEG_LIBRARIES})
    set(HAVE_JPEG ON)
ENDIF()

find_package(PNG REQUIRED)
list(APPEND INKSCAPE_INCS_SYS ${PNG_PNG_INCLUDE_DIR})
list(APPEND INKSCAPE_LIBS ${PNG_LIBRARY})

find_package(Potrace REQUIRED)
list(APPEND INKSCAPE_INCS_SYS ${POTRACE_INCLUDE_DIRS})
list(APPEND INKSCAPE_LIBS ${POTRACE_LIBRARIES})

if(WITH_SVG2)
    add_definitions(-DWITH_MESH -DWITH_CSSBLEND -DWITH_SVG2)
else()
    add_definitions(-UWITH_MESH -UWITH_CSSBLEND -UWITH_SVG2)
endif()

# ----------------------------------------------------------------------------
# CMake's builtin
# ----------------------------------------------------------------------------

# Include dependencies:

pkg_check_modules(
    MM REQUIRED
    cairomm-1.16
    pangomm-2.48
    gdk-pixbuf-2.0
    graphene-1.0
    )
list(APPEND INKSCAPE_CXX_FLAGS ${MM_CFLAGS_OTHER})
list(APPEND INKSCAPE_INCS_SYS ${MM_INCLUDE_DIRS})
list(APPEND INKSCAPE_LIBS ${MM_LIBRARIES})
link_directories(${MM_LIBRARY_DIRS})

# if system's gtk4 is new enough for gtkmm, pick it, otherwise ignore it and gtkmm build will build it
pkg_check_modules(G4 gtk4>=4.14.0)
list(APPEND INKSCAPE_CXX_FLAGS ${G4_CFLAGS_OTHER})
list(APPEND INKSCAPE_INCS_SYS ${G4_INCLUDE_DIRS})
list(APPEND INKSCAPE_LIBS ${G4_LIBRARIES})
link_directories(${G4_LIBRARY_DIRS})

pkg_check_modules(
    GTKMM4
    glibmm-2.68>=2.78.1
    gtkmm-4.0>=4.13.3
    )
    list(APPEND INKSCAPE_CXX_FLAGS ${GTKMM4_CFLAGS_OTHER})
    list(APPEND INKSCAPE_INCS_SYS ${GTKMM4_INCLUDE_DIRS})
    list(APPEND INKSCAPE_LIBS ${GTKMM4_LIBRARIES})
    link_directories(${GTKMM4_LIBRARY_DIRS})

if(NOT GTKMM4_FOUND)
    message("GTKMM too old, gtkmm 4.14.0 and glibmm 2.78.1 will be compiled from source")
    include(ExternalProject)
    ExternalProject_Add(gtkmm
        URL https://download.gnome.org/sources/gtkmm/4.14/gtkmm-4.14.0.tar.xz
        URL_HASH SHA256=9350a0444b744ca3dc69586ebd1b6707520922b6d9f4f232103ce603a271ecda
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND meson setup --libdir lib . ../gtkmm --prefix=${CMAKE_CURRENT_BINARY_DIR}/deps
        BUILD_COMMAND meson compile
        INSTALL_COMMAND meson install
        )
    ExternalProject_Add(glibmm
        URL https://download.gnome.org/sources/glibmm/2.78/glibmm-2.78.1.tar.xz
        URL_HASH SHA256=f473f2975d26c3409e112ed11ed36406fb3843fa975df575c22d4cb843085f61
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND meson setup --libdir lib . ../glibmm --prefix=${CMAKE_CURRENT_BINARY_DIR}/deps
        BUILD_COMMAND meson compile
        INSTALL_COMMAND meson install
        )
    include_directories(${CMAKE_CURRENT_BINARY_DIR}/deps/include/gtkmm-4.0 ${CMAKE_CURRENT_BINARY_DIR}/deps/include/glibmm-2.68/ ${CMAKE_CURRENT_BINARY_DIR}/deps/lib/gtkmm-4.0/include ${CMAKE_CURRENT_BINARY_DIR}/deps/lib/glibmm-2.68/include ${CMAKE_CURRENT_BINARY_DIR}/deps/include/gtk-4.0/ ${CMAKE_CURRENT_BINARY_DIR}/deps/include/giomm-2.68/ ${CMAKE_CURRENT_BINARY_DIR}/deps/lib/giomm-2.68/include)
    link_directories(${CMAKE_CURRENT_BINARY_DIR}/deps/lib)
    list(APPEND INKSCAPE_LIBS -lgtkmm-4.0 -lglibmm-2.68)
    set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_RPATH}:${CMAKE_BINARY_DIR}/deps/lib")

    # check we can actually build it
    message("To build gtkmm4, you need the packages glslc, mm-common, and libgstreamer-plugins-bad1.0-dev")

    find_program(glslc glslc REQUIRED)
    find_program(mmcp mm-common-prepare REQUIRED)
    pkg_check_modules(TMP-gtkmm-gstreamer gstreamer-player-1.0 REQUIRED)
endif()

if(WITH_LIBSPELLING)
    pkg_check_modules(LIBSPELLING libspelling-1)
    if("${LIBSPELLING_FOUND}")
        message(STATUS "Using libspelling")
        list(APPEND INKSCAPE_INCS_SYS ${LIBSPELLING_INCLUDE_DIRS})
        sanitize_ldflags_for_libs(LIBSPELLING_LDFLAGS)
        list(APPEND INKSCAPE_LIBS ${LIBSPELLING_LDFLAGS})
    else()
        set(WITH_LIBSPELLING OFF)
    endif()
endif()

if(WITH_GSOURCEVIEW)
    pkg_check_modules(GSOURCEVIEW gtksourceview-5)
    if("${GSOURCEVIEW_FOUND}")
        message(STATUS "Using gtksourceview-5")
        list(APPEND INKSCAPE_INCS_SYS ${GSOURCEVIEW_INCLUDE_DIRS})
        sanitize_ldflags_for_libs(GSOURCEVIEW_LDFLAGS)
        list(APPEND INKSCAPE_LIBS ${GSOURCEVIEW_LDFLAGS})
    else()
        set(WITH_GSOURCEVIEW OFF)
    endif()
endif()

# stacktrace print on crash
if(WIN32)
    find_package(Boost 1.19.0 REQUIRED COMPONENTS filesystem stacktrace_windbg)
    list(APPEND INKSCAPE_LIBS "-lole32")
    list(APPEND INKSCAPE_LIBS "-ldbgeng")
    add_definitions("-DBOOST_STACKTRACE_USE_WINDBG")
elseif(APPLE)
    find_package(Boost 1.19.0 REQUIRED COMPONENTS filesystem stacktrace_basic)
    list(APPEND INKSCAPE_CXX_FLAGS "-D_GNU_SOURCE")
else()
    find_package(Boost 1.19.0 REQUIRED COMPONENTS filesystem)
    # The package stacktrace_backtrace may not be available on all distros.
    find_package(Boost 1.19.0 COMPONENTS stacktrace_backtrace)
    if (BOOST_FOUND)
        list(APPEND INKSCAPE_LIBS "-lbacktrace")
        add_definitions("-DBOOST_STACKTRACE_USE_BACKTRACE")
    else() # fall back to stacktrace_basic
        find_package(Boost 1.19.0 REQUIRED COMPONENTS stacktrace_basic)
        list(APPEND INKSCAPE_CXX_FLAGS "-D_GNU_SOURCE")
    endif()
endif()
# enable explicit debug symbols
set(CMAKE_ENABLE_EXPORTS ON)



if (CMAKE_COMPILER_IS_GNUCC AND CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7 AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9)
    list(APPEND INKSCAPE_LIBS "-lstdc++fs")
endif()

list(APPEND INKSCAPE_INCS_SYS ${Boost_INCLUDE_DIRS})
# list(APPEND INKSCAPE_LIBS ${Boost_LIBRARIES})

#find_package(OpenSSL)
#list(APPEND INKSCAPE_INCS_SYS ${OPENSSL_INCLUDE_DIR})
#list(APPEND INKSCAPE_LIBS ${OPENSSL_LIBRARIES})

find_package(LibXslt REQUIRED)
list(APPEND INKSCAPE_INCS_SYS ${LIBXSLT_INCLUDE_DIR})
list(APPEND INKSCAPE_LIBS ${LIBXSLT_LIBRARIES})
add_definitions(${LIBXSLT_DEFINITIONS})

find_package(LibXml2 REQUIRED)
list(APPEND INKSCAPE_INCS_SYS ${LIBXML2_INCLUDE_DIR})
list(APPEND INKSCAPE_LIBS ${LIBXML2_LIBRARIES})
add_definitions(${LIBXML2_DEFINITIONS})

find_package(ZLIB REQUIRED)
list(APPEND INKSCAPE_INCS_SYS ${ZLIB_INCLUDE_DIRS})
list(APPEND INKSCAPE_LIBS ${ZLIB_LIBRARIES})

if(WITH_GNU_READLINE)
  pkg_check_modules(Readline readline)
  if(Readline_FOUND)
    message(STATUS "Found GNU Readline: ${Readline_LIBRARY}")
    list(APPEND INKSCAPE_INCS_SYS ${Readline_INCLUDE_DIRS})
    list(APPEND INKSCAPE_LIBS ${Readline_LDFLAGS})
  else()
    message(STATUS "Did not find GNU Readline")
    set(WITH_GNU_READLINE OFF)
  endif()
endif()

if(WITH_IMAGE_MAGICK)
    # we want "<" but pkg_check_modules only offers "<=" for some reason; let's hope nobody actually has 7.0.0
    pkg_check_modules(MAGICK ImageMagick++<=7)
    if(MAGICK_FOUND)
        set(WITH_GRAPHICS_MAGICK OFF)  # prefer ImageMagick for now and disable GraphicsMagick if found
    else()
        set(WITH_IMAGE_MAGICK OFF)
    endif()
endif()
if(WITH_GRAPHICS_MAGICK)
    pkg_check_modules(MAGICK GraphicsMagick++)
    if(NOT MAGICK_FOUND)
        set(WITH_GRAPHICS_MAGICK OFF)
    endif()
endif()
if(MAGICK_FOUND)
    sanitize_ldflags_for_libs(MAGICK_LDFLAGS)
    list(APPEND INKSCAPE_LIBS ${MAGICK_LDFLAGS})
    add_definitions(${MAGICK_CFLAGS_OTHER})
    list(APPEND INKSCAPE_INCS_SYS ${MAGICK_INCLUDE_DIRS})

    set(WITH_MAGICK ON) # enable 'Extensions > Raster'
endif()

set(ENABLE_NLS OFF)
if(WITH_NLS)
    find_package(Gettext)
    if(GETTEXT_FOUND)
        message(STATUS "Found gettext + msgfmt to convert language files. Translation enabled")
        set(ENABLE_NLS ON)
    else(GETTEXT_FOUND)
        message(STATUS "Cannot find gettext + msgfmt to convert language file. Translation won't be enabled")
        set(WITH_NLS OFF)
    endif(GETTEXT_FOUND)
    find_program(GETTEXT_XGETTEXT_EXECUTABLE xgettext)
    if(GETTEXT_XGETTEXT_EXECUTABLE)
        message(STATUS "Found xgettext. inkscape.pot will be re-created if missing.")
    else()
        message(STATUS "Did not find xgettext. inkscape.pot can't be re-created.")
    endif()
endif(WITH_NLS)

pkg_check_modules(SIGC++ REQUIRED sigc++-3.0>=3.6)
sanitize_ldflags_for_libs(SIGC++_LDFLAGS)
list(APPEND INKSCAPE_LIBS ${SIGC++_LDFLAGS})
list(APPEND INKSCAPE_CXX_FLAGS ${SIGC++_CFLAGS_OTHER} "-DSIGCXX_DISABLE_DEPRECATED")

pkg_check_modules(EPOXY REQUIRED epoxy )
sanitize_ldflags_for_libs(EPOXY_LDFLAGS)
list(APPEND INKSCAPE_LIBS ${EPOXY_LDFLAGS})
list(APPEND INKSCAPE_CXX_FLAGS ${EPOXY_CFLAGS_OTHER})


# end Dependencies



# Set include directories and CXX flags
# (INKSCAPE_LIBS are set as target_link_libraries for inkscape_base in src/CMakeLists.txt)

foreach(flag ${INKSCAPE_CXX_FLAGS})
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${flag}")
endforeach()
foreach(flag ${INKSCAPE_CXX_FLAGS_DEBUG})
    set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} ${flag}")
endforeach()

# Add color output to ninja
if ("${CMAKE_GENERATOR}" MATCHES "Ninja")
    add_compile_options (-fdiagnostics-color)
endif ()

list(REMOVE_DUPLICATES INKSCAPE_LIBS)
list(REMOVE_DUPLICATES INKSCAPE_INCS_SYS)

include_directories(${INKSCAPE_INCS})
include_directories(SYSTEM ${INKSCAPE_INCS_SYS})

include(${CMAKE_CURRENT_LIST_DIR}/ConfigChecks.cmake) # TODO: Check if this needs to be "hidden" here

unset(INKSCAPE_INCS)
unset(INKSCAPE_INCS_SYS)
unset(INKSCAPE_CXX_FLAGS)
unset(INKSCAPE_CXX_FLAGS_DEBUG)
