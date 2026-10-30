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

// The single face that goes through Slug while the rest keep using the distance
// field. Compared without case, as it is stored as the font file path.
#if GXU_FONT_SLUG_TEXT
#ifndef GXU_FONT_SLUG_FACE
#define GXU_FONT_SLUG_FACE "Fonts\\FRIZQT__.TTF"
#endif
#endif

#endif
