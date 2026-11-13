/*
** SGI FREE SOFTWARE LICENSE B (Version 2.0, Sept. 18, 2008)
** Copyright (C) [dates of first publication] Silicon Graphics, Inc.
** All Rights Reserved.
**
** Permission is hereby granted, free of charge, to any person obtaining a copy
** of this software and associated documentation files (the "Software"), to deal
** in the Software without restriction, including without limitation the rights
** to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
** of the Software, and to permit persons to whom the Software is furnished to do so,
** subject to the following conditions:
**
** The above copyright notice including the dates of first publication and either this
** permission notice or a reference to http://oss.sgi.com/projects/FreeB/ shall be
** included in all copies or substantial portions of the Software.
**
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
** INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A
** PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL SILICON GRAPHICS, INC.
** BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
** TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE
** OR OTHER DEALINGS IN THE SOFTWARE.
**
** Except as contained in this notice, the name of Silicon Graphics, Inc. shall not
** be used in advertising or otherwise to promote the sale, use or other dealings in
** this Software without prior written authorization from Silicon Graphics, Inc.
*/

#ifndef TESSIO_H
#define TESSIO_H

#include <stddef.h>
#include "tesselator.h"

#ifdef __cplusplus
extern "C" {
#endif

// Binary contour container format (multi-byte integers are little-endian):
//
//   u32 magic        = TESS_IO_MAGIC
//   u16 version      = TESS_IO_VERSION
//   u16 reserved     = 0
//   u32 contourCount
//   then, contourCount times:
//     u32 vertexCount
//     u32 flags        (bit 0 set: ring is a hole of the current outer ring)
//     vertexCount * (f32 x, f32 y)
//
// Outer rings are expected counter-clockwise, holes clockwise (the same
// convention as used with TESS_REVERSE_CONTOURS, see tessImportContours).

#define TESS_IO_MAGIC 0x31434554u /* "TEC1" */
#define TESS_IO_VERSION 1

#define TESS_IO_FLAG_HOLE 1u

// Distinguishable error codes for malformed contour containers.
typedef enum TessParseError
{
	TESS_PARSE_OK = 0,
	TESS_PARSE_ERR_BAD_MAGIC,			// not a contour container at all
	TESS_PARSE_ERR_UNSUPPORTED_VERSION,	// container version not understood
	TESS_PARSE_ERR_TRUNCATED,			// structure incomplete (header/counts/coordinates cut short)
	TESS_PARSE_ERR_COORD_RANGE,			// coordinate NaN, infinite or outside the valid input range
	TESS_PARSE_ERR_NESTING,				// ring nesting anomaly (orphan hole, hole outside parent, hole in hole)
	TESS_PARSE_ERR_NO_MEMORY,			// scratch allocation failed
} TessParseError;

// tessParseErrorString() - Returns a human readable name for a parse error.
const char* tessParseErrorString( TessParseError err );

// tessImportContours() - Validates a contour container and, only if fully
// valid, adds all contours to the tesselator.
// Parameters:
//   tess - pointer to tesselator object.
//   data - pointer to the container bytes.
//   size - number of bytes available at data.
// Returns:
//   TESS_PARSE_OK on success (all contours added), otherwise a distinguishable
//   error code and the tesselator is left untouched.
// Hole-flagged rings are added with TESS_REVERSE_CONTOURS enabled, matching
// the outer-CCW / hole-CW convention of the container format.
TessParseError tessImportContours( TESStesselator* tess, const void* data, size_t size );

#ifdef __cplusplus
};
#endif

#endif // TESSIO_H
