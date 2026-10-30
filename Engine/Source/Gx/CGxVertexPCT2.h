#ifndef ENGINE_SOURCE_GX_CGXVERTEXPCT2_H
#define ENGINE_SOURCE_GX_CGXVERTEXPCT2_H

#include <cstddef>
#include <cstdint>
#include "Engine/Source/Tempest/C2Vector.h"
#include "Engine/Source/Tempest/C3Vector.h"
#include "Engine/Source/Tempest/CImVector.h"

// PCT with a second texture coordinate stage, 32 bytes, matching the layout the
// vertex declaration table expects for GxVBF_PCT2.
class CGxVertexPCT2 {
    public:
    /* 0000 */ C3Vector p;
    /* 000C */ CImVector c;
    /* 0010 */ C2Vector tc;
    /* 0018 */ C2Vector tc2;
};

#endif
