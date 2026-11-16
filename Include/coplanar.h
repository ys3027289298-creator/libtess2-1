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

#ifndef TESS_COPLANAR_H
#define TESS_COPLANAR_H

#include "tesselator.h"

#ifdef __cplusplus
extern "C" {
#endif

// Coplanar overlap resolver.
//
// Imported models often contain faces that lie in the same plane and overlap
// (duplicate layers, intersecting coplanar polygons). Triangulating such
// faces independently produces co-located, overlapping triangles which
// z-fight at render time. This module detects groups of coplanar,
// mutually overlapping faces and resolves them with a selectable strategy
// before triangulation:
//
//   TESS_COPLANAR_MERGE    - union the whole group into a single
//                            triangulated layer (no overlap remains).
//   TESS_COPLANAR_KEEP_ONE - keep the largest-area face of the group,
//                            drop the others (for exact duplicate layers).
//
// Faces that are not coplanar-overlapping are passed through unchanged:
// each such face is tessellated individually with the same parameters the
// caller would use directly, so the output for non-coplanar input is
// bit-exact identical to tessellating the faces one by one with libtess2.
//
// Degenerate input is rejected before any tessellation happens:
// faces with zero area (below tolerance) and faces containing duplicate
// vertices (within tolerance) make the call fail with a specific status.

enum TessCoplanarStrategy
{
	TESS_COPLANAR_MERGE = 0,	// Union overlapping coplanar faces into one layer.
	TESS_COPLANAR_KEEP_ONE = 1,	// Keep largest-area face of an overlapping group, drop the rest.
};

enum TessCoplanarStatus
{
	TESS_COPLANAR_OK = 0,
	TESS_COPLANAR_ERR_INVALID_INPUT = 1,	// NULL arguments, or a face with fewer than 3 vertices.
	TESS_COPLANAR_ERR_ZERO_AREA_FACE = 2,	// Face area is below zero_area_tol (e.g. collinear points).
	TESS_COPLANAR_ERR_DUPLICATE_VERTEX = 3,	// Two vertices of one face are within vertex_epsilon.
	TESS_COPLANAR_ERR_OUT_OF_MEMORY = 4,
	TESS_COPLANAR_ERR_TESSELATION_FAILED = 5,	// Internal libtess2 call failed.
	TESS_COPLANAR_ERR_AREA_MISMATCH = 6,	// Output area violates the conservation tolerance.
};

struct TessCoplanarOptions
{
	int strategy;			// One of TessCoplanarStrategy. Default TESS_COPLANAR_MERGE.
	TESSreal normal_tol;	// Coplanarity: |dot(n1, n2)| >= 1 - normal_tol. Default 1e-6.
	TESSreal offset_tol;	// Coplanarity: plane offset distance, absolute units. Default 1e-4.
	TESSreal vertex_epsilon;	// Duplicate vertex distance, relative to face bbox diagonal. Default 1e-6.
	TESSreal zero_area_tol;	// Zero-area rejection: 2*area <= zero_area_tol * bbox_diagonal^2. Default 1e-12.
	TESSreal area_rel_tol;	// Area conservation tolerance, relative. Default 1e-4.
};

struct TessCoplanarFace
{
	const TESSreal* vertices;	// 'count' vertices, 3 coordinates (xyz) each.
	int count;
};

struct TessCoplanarStats
{
	int faces_input;		// Number of input faces.
	int faces_passthrough;	// Faces emitted unchanged (not coplanar-overlapping).
	int groups_resolved;	// Overlapping coplanar groups that were resolved.
	int faces_dropped;		// Faces dropped by TESS_COPLANAR_KEEP_ONE.
	double input_union_area;	// Union surface area of the input faces.
	double expected_area;		// Surface area the selected strategy promises to preserve.
	double output_area;		// Surface area of the emitted triangles.
	double coverage_delta;		// input_union_area - output_area (coverage lost by KEEP_ONE).
};

struct TessCoplanarResult
{
	int status;			// One of TessCoplanarStatus.
	int error_face;			// Index of the face that caused the error, or -1.
	int vertex_count;		// Number of output vertices, 3 floats each.
	TESSreal* vertices;
	int triangle_count;		// Number of output triangles, 3 indices each.
	TESSindex* triangles;
	struct TessCoplanarStats stats;
};

// tessCoplanarDefaultOptions() - Returns the default option set.
struct TessCoplanarOptions tessCoplanarDefaultOptions( void );

// tessCoplanarProcess() - Detects and resolves coplanar overlapping faces.
// Parameters:
//   faces - array of input faces, each a simple polygon of 3D vertices.
//   face_count - number of faces.
//   options - option set, or NULL for defaults.
// Returns:
//   Result object (free with tessCoplanarFreeResult()). Check result->status:
//   TESS_COPLANAR_OK means vertices/triangles hold the resolved triangulation.
//   On error, error_face identifies the offending input face when applicable.
//   Returns NULL only if the result object itself could not be allocated.
struct TessCoplanarResult* tessCoplanarProcess( const struct TessCoplanarFace* faces,
						int face_count,
						const struct TessCoplanarOptions* options );

// tessCoplanarFreeResult() - Frees a result returned by tessCoplanarProcess().
void tessCoplanarFreeResult( struct TessCoplanarResult* result );

#ifdef __cplusplus
};
#endif

#endif // TESS_COPLANAR_H
