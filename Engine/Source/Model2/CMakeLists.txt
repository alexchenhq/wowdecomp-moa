file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(Model2 STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Model2
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

# MODIFIED: optional debug info for this library only, so a crash inside the M2
# loader can be resolved to a source line without rebuilding everything.
option(WOWDECOMP_DEBUG_MODEL2 "Compile Model2 with /Zi for post-mortem line numbers" OFF)
if (WOWDECOMP_DEBUG_MODEL2)
    target_compile_options(Model2 PRIVATE /Zi)
endif ()
