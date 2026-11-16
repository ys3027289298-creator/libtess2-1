/*
** Standalone test + demo driver for the coplanar overlap resolver.
** No external test framework required; build with:
**   gcc -I Include -I Source Tests/coplanar_test.c Source/bucketalloc.c Source/dict.c \
**       Source/geom.c Source/mesh.c Source/priorityq.c Source/sweep.c Source/tess.c \
**       Source/coplanar.c -lm -o coplanar_test
** Exit code 0 means every check passed.
**
** Covers, in order:
**   1. Before/after comparison on one hand-crafted coplanar-overlapping input
**      (baseline per-face tessellation vs TESS_COPLANAR_MERGE vs
**      TESS_COPLANAR_KEEP_ONE): triangle counts and areas.
**   2. Failure paths: zero-area face, duplicate vertices, too few vertices,
**      NULL input - each with the expected error status.
**   3. Bit-exact consistency: for non-coplanar / non-overlapping input the
**      module output is byte-identical to driving libtess2 per face.
*/

#include "coplanar.h"
#include "tesselator.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures = 0;

static void check( int cond, const char* msg )
{
	if ( cond )
		printf( "  PASS: %s\n", msg );
	else
	{
		printf( "  FAIL: %s\n", msg );
		g_failures++;
	}
}

/* ------------------------------------------------------------------ */
/* Hand-crafted input set "zfight":                                   */
/*   A: unit square [0,2]x[0,2]   at z=0, area 4                      */
/*   B: unit square [1,3]x[1,3]   at z=0, area 4 (overlaps A by 1)    */
/*   C: exact duplicate of A      at z=0, area 4 (duplicate layer)    */
/*   D: right triangle at x=5,          area 2 (non-coplanar)         */
/*   E: square [10,12]^2          at z=5, area 4 (parallel, offset 5) */
/* Union area = (4+4-1) + 2 + 4 = 13. Naive sum of areas = 18.        */
/* ------------------------------------------------------------------ */

static const TESSreal faceA[] = { 0,0,0,  2,0,0,  2,2,0,  0,2,0 };
static const TESSreal faceB[] = { 1,1,0,  3,1,0,  3,3,0,  1,3,0 };
static const TESSreal faceC[] = { 0,0,0,  2,0,0,  2,2,0,  0,2,0 };
static const TESSreal faceD[] = { 5,0,0,  5,2,0,  5,0,2 };
static const TESSreal faceE[] = { 10,10,5,  12,10,5,  12,12,5,  10,12,5 };

static const struct TessCoplanarFace zfight_faces[] = {
	{ faceA, 4 }, { faceB, 4 }, { faceC, 4 }, { faceD, 3 }, { faceE, 4 },
};
#define ZFIGHT_FACE_COUNT 5

/* Baseline ("before"): tessellate each face individually with libtess2,
** concatenating the results - i.e. what an importer does without the
** coplanar resolver. The per-face normal is computed with Newell's method,
** exactly as the resolver's pass-through path does. */
static void faceNormal( const TESSreal* verts, int count, TESSreal* normal )
{
	double nx = 0, ny = 0, nz = 0;
	int i;
	for ( i = 0; i < count; i++ )
	{
		const TESSreal* pi = &verts[i * 3];
		const TESSreal* pj = &verts[( ( i + 1 ) % count ) * 3];
		nx += ( (double)pi[1] - pj[1] ) * ( (double)pi[2] + pj[2] );
		ny += ( (double)pi[2] - pj[2] ) * ( (double)pi[0] + pj[0] );
		nz += ( (double)pi[0] - pj[0] ) * ( (double)pi[1] + pj[1] );
	}
	{
		const double len = sqrt( nx * nx + ny * ny + nz * nz );
		normal[0] = (TESSreal)( nx / len );
		normal[1] = (TESSreal)( ny / len );
		normal[2] = (TESSreal)( nz / len );
	}
}

struct Soup
{
	int vertex_count;
	TESSreal* vertices;
	int triangle_count;
	TESSindex* triangles;
	double area;
};

static void soupFree( struct Soup* s )
{
	free( s->vertices );
	free( s->triangles );
	memset( s, 0, sizeof( *s ) );
}

static double triangleArea( const TESSreal* p0, const TESSreal* p1, const TESSreal* p2 )
{
	const double e1[3] = { p1[0] - p0[0], p1[1] - p0[1], p1[2] - p0[2] };
	const double e2[3] = { p2[0] - p0[0], p2[1] - p0[1], p2[2] - p0[2] };
	const double cx = e1[1] * e2[2] - e1[2] * e2[1];
	const double cy = e1[2] * e2[0] - e1[0] * e2[2];
	const double cz = e1[0] * e2[1] - e1[1] * e2[0];
	return 0.5 * sqrt( cx * cx + cy * cy + cz * cz );
}

static double soupArea( int triangle_count, const TESSindex* tris, const TESSreal* verts )
{
	double area = 0;
	int i;
	for ( i = 0; i < triangle_count; i++ )
		area += triangleArea( &verts[tris[i * 3 + 0] * 3],
							  &verts[tris[i * 3 + 1] * 3],
							  &verts[tris[i * 3 + 2] * 3] );
	return area;
}

/* Tessellates the faces one by one (the "before" behavior). */
static struct Soup baselineTessellate( const struct TessCoplanarFace* faces, int face_count )
{
	struct Soup soup;
	int i, vcap = 0, tcap = 0;
	memset( &soup, 0, sizeof( soup ) );
	for ( i = 0; i < face_count; i++ )
	{
		TESStesselator* tess = tessNewTess( NULL );
		TESSreal normal[3];
		int nv, ne, k;
		const TESSreal* tv;
		const TESSindex* te;
		faceNormal( faces[i].vertices, faces[i].count, normal );
		tessAddContour( tess, 3, faces[i].vertices, 3 * sizeof( TESSreal ), faces[i].count );
		if ( !tessTesselate( tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 3, normal ) )
		{
			fprintf( stderr, "baseline tessellation failed on face %d\n", i );
			tessDeleteTess( tess );
			continue;
		}
		nv = tessGetVertexCount( tess );
		ne = tessGetElementCount( tess );
		tv = tessGetVertices( tess );
		te = tessGetElements( tess );
		if ( soup.vertex_count + nv > vcap )
		{
			vcap = ( soup.vertex_count + nv ) * 2 + 16;
			soup.vertices = (TESSreal*)realloc( soup.vertices, (size_t)vcap * 3 * sizeof( TESSreal ) );
		}
		if ( soup.triangle_count + ne > tcap )
		{
			tcap = ( soup.triangle_count + ne ) * 2 + 16;
			soup.triangles = (TESSindex*)realloc( soup.triangles, (size_t)tcap * 3 * sizeof( TESSindex ) );
		}
		memcpy( soup.vertices + (size_t)soup.vertex_count * 3, tv, (size_t)nv * 3 * sizeof( TESSreal ) );
		for ( k = 0; k < ne * 3; k++ )
			soup.triangles[soup.triangle_count * 3 + k] = te[k] + soup.vertex_count;
		soup.vertex_count += nv;
		soup.triangle_count += ne;
		tessDeleteTess( tess );
	}
	soup.area = soupArea( soup.triangle_count, soup.triangles, soup.vertices );
	return soup;
}

static void printRow( const char* label, int verts, int tris, double area )
{
	printf( "  %-28s %8d %11d %12.4f\n", label, verts, tris, area );
}

static void testComparison( void )
{
	struct TessCoplanarOptions opt;
	struct TessCoplanarResult* merge;
	struct TessCoplanarResult* keep;
	struct Soup baseline;

	printf( "\n== 1. Before/after comparison, input set 'zfight' (%d faces) ==\n", ZFIGHT_FACE_COUNT );
	printf( "  %-28s %8s %11s %12s\n", "variant", "vertices", "triangles", "area" );

	baseline = baselineTessellate( zfight_faces, ZFIGHT_FACE_COUNT );
	printRow( "before (per-face, overlaps)", baseline.vertex_count, baseline.triangle_count, baseline.area );

	opt = tessCoplanarDefaultOptions();
	opt.strategy = TESS_COPLANAR_MERGE;
	merge = tessCoplanarProcess( zfight_faces, ZFIGHT_FACE_COUNT, &opt );
	check( merge != NULL && merge->status == TESS_COPLANAR_OK, "merge: status OK" );
	if ( merge != NULL && merge->status == TESS_COPLANAR_OK )
	{
		printRow( "after MERGE (one layer)", merge->vertex_count, merge->triangle_count, merge->stats.output_area );
		printf( "    input_union_area=%.4f expected_area=%.4f coverage_delta=%.4f groups_resolved=%d\n",
				merge->stats.input_union_area, merge->stats.expected_area,
				merge->stats.coverage_delta, merge->stats.groups_resolved );
		check( fabs( merge->stats.input_union_area - 13.0 ) < 1e-3, "merge: input union area == 13" );
		check( fabs( merge->stats.output_area - 13.0 ) < 1e-3, "merge: output area == 13 (conserved)" );
		check( merge->stats.groups_resolved == 1, "merge: exactly one overlapping group resolved" );
		check( merge->stats.faces_passthrough == 2, "merge: faces D and E passed through" );
	}

	opt.strategy = TESS_COPLANAR_KEEP_ONE;
	keep = tessCoplanarProcess( zfight_faces, ZFIGHT_FACE_COUNT, &opt );
	check( keep != NULL && keep->status == TESS_COPLANAR_OK, "keep-one: status OK" );
	if ( keep != NULL && keep->status == TESS_COPLANAR_OK )
	{
		printRow( "after KEEP_ONE (largest kept)", keep->vertex_count, keep->triangle_count, keep->stats.output_area );
		printf( "    input_union_area=%.4f expected_area=%.4f coverage_delta=%.4f faces_dropped=%d\n",
				keep->stats.input_union_area, keep->stats.expected_area,
				keep->stats.coverage_delta, keep->stats.faces_dropped );
		check( keep->stats.faces_dropped == 2, "keep-one: faces B and C dropped" );
		check( fabs( keep->stats.output_area - 10.0 ) < 1e-3, "keep-one: output area == 10 (4+2+4)" );
		check( fabs( keep->stats.coverage_delta - 3.0 ) < 1e-3, "keep-one: coverage delta == 3 (B's unique part)" );
	}

	printf( "  memory footprint of result arrays (vertices+triangles, bytes):\n" );
	printf( "    before: %zu  merge: %zu  keep-one: %zu\n",
			(size_t)baseline.vertex_count * 3 * sizeof( TESSreal ) + (size_t)baseline.triangle_count * 3 * sizeof( TESSindex ),
			merge ? (size_t)merge->vertex_count * 3 * sizeof( TESSreal ) + (size_t)merge->triangle_count * 3 * sizeof( TESSindex ) : 0,
			keep ? (size_t)keep->vertex_count * 3 * sizeof( TESSreal ) + (size_t)keep->triangle_count * 3 * sizeof( TESSindex ) : 0 );

	soupFree( &baseline );
	tessCoplanarFreeResult( merge );
	tessCoplanarFreeResult( keep );
}

static void testFailurePaths( void )
{
	static const TESSreal collinear[] = { 0,0,0,  1,0,0,  2,0,0 };
	static const TESSreal dupVerts[] = { 0,0,0,  1,0,0,  1,1,0,  0,1,0,  1,0,0 };
	static const TESSreal twoVerts[] = { 0,0,0,  1,0,0 };
	struct TessCoplanarResult* r;

	printf( "\n== 2. Failure paths (reproducible, expected errors) ==\n" );

	/* Zero-area face: three collinear points, as face #1 of two. */
	{
		const struct TessCoplanarFace faces[] = { { faceA, 4 }, { collinear, 3 } };
		r = tessCoplanarProcess( faces, 2, NULL );
		printf( "  zero-area face (3 collinear points at face index 1):\n" );
		printf( "    expected: status=%d (TESS_COPLANAR_ERR_ZERO_AREA_FACE), error_face=1\n",
				TESS_COPLANAR_ERR_ZERO_AREA_FACE );
		printf( "    actual:   status=%d, error_face=%d\n", r ? r->status : -1, r ? r->error_face : -1 );
		check( r != NULL && r->status == TESS_COPLANAR_ERR_ZERO_AREA_FACE && r->error_face == 1,
			   "zero-area face rejected with TESS_COPLANAR_ERR_ZERO_AREA_FACE" );
		tessCoplanarFreeResult( r );
	}

	/* Duplicate vertices: square with vertex (1,0,0) repeated. */
	{
		const struct TessCoplanarFace faces[] = { { dupVerts, 5 } };
		r = tessCoplanarProcess( faces, 1, NULL );
		printf( "  duplicate vertex ((1,0,0) appears twice in one face):\n" );
		printf( "    expected: status=%d (TESS_COPLANAR_ERR_DUPLICATE_VERTEX), error_face=0\n",
				TESS_COPLANAR_ERR_DUPLICATE_VERTEX );
		printf( "    actual:   status=%d, error_face=%d\n", r ? r->status : -1, r ? r->error_face : -1 );
		check( r != NULL && r->status == TESS_COPLANAR_ERR_DUPLICATE_VERTEX && r->error_face == 0,
			   "duplicate vertex rejected with TESS_COPLANAR_ERR_DUPLICATE_VERTEX" );
		tessCoplanarFreeResult( r );
	}

	/* Too few vertices. */
	{
		const struct TessCoplanarFace faces[] = { { twoVerts, 2 } };
		r = tessCoplanarProcess( faces, 1, NULL );
		printf( "  face with only 2 vertices:\n" );
		printf( "    expected: status=%d (TESS_COPLANAR_ERR_INVALID_INPUT), error_face=0\n",
				TESS_COPLANAR_ERR_INVALID_INPUT );
		printf( "    actual:   status=%d, error_face=%d\n", r ? r->status : -1, r ? r->error_face : -1 );
		check( r != NULL && r->status == TESS_COPLANAR_ERR_INVALID_INPUT && r->error_face == 0,
			   "2-vertex face rejected with TESS_COPLANAR_ERR_INVALID_INPUT" );
		tessCoplanarFreeResult( r );
	}

	/* NULL input. */
	r = tessCoplanarProcess( NULL, 0, NULL );
	printf( "  NULL face array:\n" );
	printf( "    expected: status=%d (TESS_COPLANAR_ERR_INVALID_INPUT)\n", TESS_COPLANAR_ERR_INVALID_INPUT );
	printf( "    actual:   status=%d\n", r ? r->status : -1 );
	check( r != NULL && r->status == TESS_COPLANAR_ERR_INVALID_INPUT,
		   "NULL input rejected with TESS_COPLANAR_ERR_INVALID_INPUT" );
	tessCoplanarFreeResult( r );
}

static void testBitExact( void )
{
	/* Non-coplanar faces D and E, plus two disjoint squares F and G in the
	** z=0 plane (coplanar with each other but not overlapping). None of
	** these may be altered by the resolver. */
	static const TESSreal faceF[] = { 20,20,0,  22,20,0,  22,22,0,  20,22,0 };
	static const TESSreal faceG[] = { 30,30,0,  32,30,0,  32,32,0,  30,32,0 };
	const struct TessCoplanarFace faces[] = {
		{ faceD, 3 }, { faceE, 4 }, { faceF, 4 }, { faceG, 4 },
	};
	const int face_count = 4;
	struct Soup baseline;
	struct TessCoplanarResult* r;
	int same;

	printf( "\n== 3. Old-behavior bit-exact consistency (non-coplanar / non-overlapping input) ==\n" );

	baseline = baselineTessellate( faces, face_count );
	r = tessCoplanarProcess( faces, face_count, NULL );
	check( r != NULL && r->status == TESS_COPLANAR_OK, "bit-exact set: status OK" );
	if ( r == NULL || r->status != TESS_COPLANAR_OK )
	{
		soupFree( &baseline );
		tessCoplanarFreeResult( r );
		return;
	}

	check( r->stats.faces_passthrough == face_count, "bit-exact set: all 4 faces passed through" );
	check( r->stats.groups_resolved == 0, "bit-exact set: no group resolved" );

	same = r->vertex_count == baseline.vertex_count &&
		   r->triangle_count == baseline.triangle_count &&
		   memcmp( r->vertices, baseline.vertices, (size_t)baseline.vertex_count * 3 * sizeof( TESSreal ) ) == 0 &&
		   memcmp( r->triangles, baseline.triangles, (size_t)baseline.triangle_count * 3 * sizeof( TESSindex ) ) == 0;
	printf( "  vertex bytes compared: %zu, triangle bytes compared: %zu\n",
			(size_t)baseline.vertex_count * 3 * sizeof( TESSreal ),
			(size_t)baseline.triangle_count * 3 * sizeof( TESSindex ) );
	printf( "  result: %s\n", same ? "BIT-EXACT IDENTICAL" : "DIFFERS" );
	check( same, "module output is bit-exact identical to per-face libtess2 baseline" );

	soupFree( &baseline );
	tessCoplanarFreeResult( r );
}

static void testOrientation( void )
{
	/* Brev is the same square as faceB but wound CW (opposite orientation).
	** The merge must normalize contour orientation before unioning. */
	static const TESSreal faceBrev[] = { 1,3,0,  3,3,0,  3,1,0,  1,1,0 };
	const struct TessCoplanarFace faces[] = { { faceA, 4 }, { faceBrev, 4 } };
	struct TessCoplanarResult* r;

	printf( "\n== 4. Orientation normalization (CW contour in a merge group) ==\n" );
	r = tessCoplanarProcess( faces, 2, NULL );
	check( r != NULL && r->status == TESS_COPLANAR_OK, "reversed-winding merge: status OK" );
	if ( r != NULL && r->status == TESS_COPLANAR_OK )
	{
		printf( "  union area: %.4f (expected 7.0)\n", r->stats.output_area );
		check( fabs( r->stats.output_area - 7.0 ) < 1e-3,
			   "reversed-winding contour unions correctly (area == 7)" );
	}
	tessCoplanarFreeResult( r );
}

int main( void )
{
	testComparison();
	testFailurePaths();
	testBitExact();
	testOrientation();

	printf( "\n== summary ==\n" );
	if ( g_failures == 0 )
		printf( "ALL CHECKS PASSED\n" );
	else
		printf( "%d CHECK(S) FAILED\n", g_failures );
	return g_failures == 0 ? 0 : 1;
}
