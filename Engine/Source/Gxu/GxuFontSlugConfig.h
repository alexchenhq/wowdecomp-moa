#ifndef ENGINE_SOURCE_GXU_GXUFONTSLUGCONFIG_H
#define ENGINE_SOURCE_GXU_GXUFONTSLUGCONFIG_H

// Build switches for the Slug text path.
//
// Both default to 0 so the shipping SDF client is never touched by half finished
// Slug work: with the switches off the preprocessor removes every Slug branch and
// the font code compiles exactly as it did before the work started.
//
//   GXU_FONT_SLUG_PROBE  draws the standalone debug quads in the corner
//   GXU_FONT_SLUG_TEXT   routes real text through Slug
//
// The vertex layout of a font batch flips with GXU_FONT_SLUG_TEXT (PCT -> PCT2),
// which is an all or nothing change: a build with the switch on cannot fall back
// to the SDF vertex layout half way through a string.

#ifndef GXU_FONT_SLUG_PROBE
#define GXU_FONT_SLUG_PROBE 0
#endif

#ifndef GXU_FONT_SLUG_TEXT
#define GXU_FONT_SLUG_TEXT 0
#endif

// Which faces the Slug text path takes.
//
// ALL_FACES sends every font file through it; the named face is only consulted
// when ALL_FACES is 0, which is how the first milestone was limited to one font.
//
// OUTLINED takes the outlined instances of those faces as well. The shader has no
// rim: coverage only carries distance inside half a pixel, and a rim rebuilt from
// it stair steps at every size (measured), so with this on, outlined text renders
// as plain text with no rim at all. Off keeps those instances on the distance
// field, which grows a proper rim.
#if GXU_FONT_SLUG_TEXT
#ifndef GXU_FONT_SLUG_ALL_FACES
#define GXU_FONT_SLUG_ALL_FACES 1
#endif
#ifndef GXU_FONT_SLUG_OUTLINED
#define GXU_FONT_SLUG_OUTLINED 1
#endif
// Rim pass: an extra draw of the dilated glyph, shaded from a signed distance.
// The body keeps its coverage shader untouched, which is the whole reason this
// is a separate pass instead of more work inside that loop. Off gives the plain
// text this path shipped with before.
#ifndef GXU_FONT_SLUG_RIM
#define GXU_FONT_SLUG_RIM 1
#endif
#ifndef GXU_FONT_SLUG_FACE
#define GXU_FONT_SLUG_FACE "Fonts\\FRIZQT__.TTF"
#endif
#endif

#endif
