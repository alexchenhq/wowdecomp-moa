file(GLOB_RECURSE PRIVATE_SOURCES CONFIGURE_DEPENDS "*.cpp")

add_library(Gxu STATIC
    ${PRIVATE_SOURCES}
)

target_include_directories(Gxu
    PUBLIC
        ${CMAKE_SOURCE_DIR}
)

target_link_libraries(Gxu
    PUBLIC
        freetype
)

# The Slug switches only change how Gxu compiles, so they are scoped to this
# target instead of being global: flipping them recompiles about fifteen files
# rather than the whole client. Nothing outside Gxu uses the affected types
# (VERT, the font vertex typedef, the WriteGeometry signatures).
if (WOWDECOMP_SLUG_TEXT)
    target_compile_definitions(Gxu PRIVATE GXU_FONT_SLUG_TEXT=1)
endif ()
if (WOWDECOMP_SLUG_PROBE)
    target_compile_definitions(Gxu PRIVATE GXU_FONT_SLUG_PROBE=1)
endif ()
