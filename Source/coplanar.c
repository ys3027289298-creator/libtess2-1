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

#include "coplanar.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Per-face geometric data, computed during validation. */
struct FaceInfo
{
	double normal[3];	/* Unit normal (Newell's method). */
	double offset;		/* Plane offset: dot(normal, centroid). */
	double area;		/* Polygon area (half the Newell normal length). */
	double diag2;		/* Squared bbox diagonal, used for relative tolerances. */
};

static void faceNormalAndArea( const TESSreal* verts, int count, double* normal, double* area )
{
	/* Newell's method; the normal length equals twice the polygon area. */
	double nx = 0, ny = 0, nz = 0;
	int i, j;
	for ( i = 0; i < count; i++ )
	{
		const TESSreal* pi = &verts[i * 3];
		j = ( i + 1 ) % count;
		const TESSreal* pj = &verts[j * 3];
		nx += ( (double)pi[1] - pj[1] ) * ( (double)pi[2] + pj[2] );
		ny += ( (double)pi[2] - pj[2] ) * ( (double)pi[0] + pj[0] );
		nz += ( (double)pi[0] - pj[0] ) * ( (double)pi[1] + pj[1] );
	}
	*area = 0.5 * sqrt( nx * nx + ny * ny + nz * nz );
	normal[0] = nx;
	normal[1] = ny;
	normal[2] = nz;
}

static double faceDiag2( const TESSreal* verts, int count )
{
	TESSreal lo[3], hi[3];
	int i, k;
	for ( k = 0; k < 3; k++ )
		lo[k] = hi[k] = verts[k];
	for ( i = 1; i < count; i++ )
		for ( k = 0; k < 3; k++ )
		{
			if ( verts[i * 3 + k] < lo[k] ) lo[k] = verts[i * 3 + k];
			if ( verts[i * 3 + k] > hi[k] ) hi[k] = verts[i * 3 + k];
		}
	{
		double dx = (double)hi[0] - lo[0];
		double dy = (double)hi[1] - lo[1];
		double dz = (double)hi[2] - lo[2];
		return dx * dx + dy * dy + dz * dz;
	}
}

static int findRoot( int* parent, int i )
{
	while ( parent[i] != i )
	{
		parent[i] = parent[parent[i]];
		i = parent[i];
	}
	return i;
}

/* Tessellates a set of faces as one libtess2 job with TESS_WINDING_NONZERO
** (the union of the contours). Contours whose winding disagrees with
** 'normal' are reversed so the union is well defined. The output arrays are
** malloc'ed and owned by the caller. Returns a TessCoplanarStatus code. */
static int tessellateGroup( const struct TessCoplanarFace* faces, const int* members, int member_count,
							const struct FaceInfo* infos, const double* normal,
							TESSreal** out_verts, int* out_vert_count,
							TESSindex** out_tris, int* out_tri_count, double* out_area )
{
	TESStesselator* tess;
	TESSreal* verts = NULL;
	TESSindex* tris = NULL;
	const TESSreal* tv;
	const TESSindex* te;
	int nv, ne, i, status = TESS_COPLANAR_OK;
	double area = 0;

	*out_verts = NULL;
	*out_vert_count = 0;
	*out_tris = NULL;
	*out_tri_count = 0;
	*out_area = 0;

	tess = tessNewTess( NULL );
	if ( tess == NULL )
		return TESS_COPLANAR_ERR_OUT_OF_MEMORY;

	for ( i = 0; i < member_count; i++ )
	{
		const int f = members[i];
		const struct TessCoplanarFace* face = &faces[f];
		const struct FaceInfo* info = &infos[f];
		const double dot = info->normal[0] * normal[0] + info->normal[1] * normal[1] +
						   info->normal[2] * normal[2];
		tessSetOption( tess, TESS_REVERSE_CONTOURS, dot < 0 ? 1 : 0 );
		tessAddContour( tess, 3, face->vertices, 3 * sizeof( TESSreal ), face->count );
		if ( tessGetStatus( tess ) != TESS_STATUS_OK )
		{
			status = TESS_COPLANAR_ERR_INVALID_INPUT;
			goto done;
		}
	}

	{
		TESSreal n[3];
		n[0] = (TESSreal)normal[0];
		n[1] = (TESSreal)normal[1];
		n[2] = (TESSreal)normal[2];
		if ( !tessTesselate( tess, TESS_WINDING_NONZERO, TESS_POLYGONS, 3, 3, n ) )
		{
			status = TESS_COPLANAR_ERR_TESSELATION_FAILED;
			goto done;
		}
	}

	nv = tessGetVertexCount( tess );
	ne = tessGetElementCount( tess );
	tv = tessGetVertices( tess );
	te = tessGetElements( tess );

	verts = (TESSreal*)malloc( (size_t)nv * 3 * sizeof( TESSreal ) );
	tris = (TESSindex*)malloc( (size_t)ne * 3 * sizeof( TESSindex ) );
	if ( verts == NULL || tris == NULL )
	{
		status = TESS_COPLANAR_ERR_OUT_OF_MEMORY;
		goto done;
	}
	memcpy( verts, tv, (size_t)nv * 3 * sizeof( TESSreal ) );

	{
		int nt = 0;
		for ( i = 0; i < ne; i++ )
		{
			const TESSindex i0 = te[i * 3 + 0];
			const TESSindex i1 = te[i * 3 + 1];
			const TESSindex i2 = te[i * 3 + 2];
			const TESSreal *p0, *p1, *p2;
			double e1[3], e2[3], cx, cy, cz;
			if ( i0 == TESS_UNDEF || i1 == TESS_UNDEF || i2 == TESS_UNDEF )
				continue;	/* Defensive: polySize is 3, so this should not happen. */
			tris[nt * 3 + 0] = i0;
			tris[nt * 3 + 1] = i1;
			tris[nt * 3 + 2] = i2;
			nt++;
			p0 = &verts[i0 * 3];
			p1 = &verts[i1 * 3];
			p2 = &verts[i2 * 3];
			e1[0] = (double)p1[0] - p0[0]; e1[1] = (double)p1[1] - p0[1]; e1[2] = (double)p1[2] - p0[2];
			e2[0] = (double)p2[0] - p0[0]; e2[1] = (double)p2[1] - p0[1]; e2[2] = (double)p2[2] - p0[2];
			cx = e1[1] * e2[2] - e1[2] * e2[1];
			cy = e1[2] * e2[0] - e1[0] * e2[2];
			cz = e1[0] * e2[1] - e1[1] * e2[0];
			area += 0.5 * sqrt( cx * cx + cy * cy + cz * cz );
		}
		ne = nt;
	}

	*out_verts = verts;
	*out_vert_count = nv;
	*out_tris = tris;
	*out_tri_count = ne;
	*out_area = area;
	verts = NULL;
	tris = NULL;

done:
	free( verts );
	free( tris );
	tessDeleteTess( tess );
	return status;
}

/* Appends one tessellation to the result, remapping triangle indices. */
static int emit( struct TessCoplanarResult* result,
				 TESSreal** vcap_buf, int* vcap, TESSindex** tcap_buf, int* tcap,
				 const TESSreal* verts, int nv, const TESSindex* tris, int nt )
{
	if ( result->vertex_count + nv > *vcap )
	{
		int cap = *vcap > 0 ? *vcap : 64;
		TESSreal* p;
		while ( cap < result->vertex_count + nv )
			cap *= 2;
		p = (TESSreal*)realloc( *vcap_buf, (size_t)cap * 3 * sizeof( TESSreal ) );
		if ( p == NULL )
			return TESS_COPLANAR_ERR_OUT_OF_MEMORY;
		*vcap_buf = p;
		*vcap = cap;
	}
	if ( result->triangle_count + nt > *tcap )
	{
		int cap = *tcap > 0 ? *tcap : 64;
		TESSindex* p;
		while ( cap < result->triangle_count + nt )
			cap *= 2;
		p = (TESSindex*)realloc( *tcap_buf, (size_t)cap * 3 * sizeof( TESSindex ) );
		if ( p == NULL )
			return TESS_COPLANAR_ERR_OUT_OF_MEMORY;
		*tcap_buf = p;
		*tcap = cap;
	}
	memcpy( *vcap_buf + (size_t)result->vertex_count * 3, verts, (size_t)nv * 3 * sizeof( TESSreal ) );
	{
		int i;
		TESSindex* dst = *tcap_buf + (size_t)result->triangle_count * 3;
		for ( i = 0; i < nt * 3; i++ )
			dst[i] = tris[i] + result->vertex_count;
	}
	result->vertex_count += nv;
	result->triangle_count += nt;
	return TESS_COPLANAR_OK;
}

struct TessCoplanarOptions tessCoplanarDefaultOptions( void )
{
	struct TessCoplanarOptions opt;
	opt.strategy = TESS_COPLANAR_MERGE;
	opt.normal_tol = (TESSreal)1e-6;
	opt.offset_tol = (TESSreal)1e-4;
	opt.vertex_epsilon = (TESSreal)1e-6;
	opt.zero_area_tol = (TESSreal)1e-12;
	opt.area_rel_tol = (TESSreal)1e-4;
	return opt;
}

struct TessCoplanarResult* tessCoplanarProcess( const struct TessCoplanarFace* faces,
												int face_count,
												const struct TessCoplanarOptions* options )
{
	struct TessCoplanarResult* result;
	struct TessCoplanarOptions opt;
	struct FaceInfo* infos = NULL;
	int* parent = NULL;
	int* members = NULL;
	TESSreal* vbuf = NULL;
	TESSindex* tbuf = NULL;
	int vcap = 0, tcap = 0;
	int i, j;

	result = (struct TessCoplanarResult*)calloc( 1, sizeof( struct TessCoplanarResult ) );
	if ( result == NULL )
		return NULL;
	result->status = TESS_COPLANAR_OK;
	result->error_face = -1;

	opt = options != NULL ? *options : tessCoplanarDefaultOptions();

	if ( faces == NULL || face_count <= 0 )
	{
		result->status = TESS_COPLANAR_ERR_INVALID_INPUT;
		return result;
	}

	infos = (struct FaceInfo*)calloc( (size_t)face_count, sizeof( struct FaceInfo ) );
	parent = (int*)malloc( (size_t)face_count * sizeof( int ) );
	members = (int*)malloc( (size_t)face_count * sizeof( int ) );
	if ( infos == NULL || parent == NULL || members == NULL )
	{
		result->status = TESS_COPLANAR_ERR_OUT_OF_MEMORY;
		goto done;
	}

	/* Phase 1: validate every face and compute its plane. */
	for ( i = 0; i < face_count; i++ )
	{
		const TESSreal* verts = faces[i].vertices;
		const int count = faces[i].count;
		double diag2, dup_eps2, nlen;
		double centroid[3] = { 0, 0, 0 };

		if ( verts == NULL || count < 3 )
		{
			result->status = TESS_COPLANAR_ERR_INVALID_INPUT;
			result->error_face = i;
			goto done;
		}

		diag2 = faceDiag2( verts, count );

		/* Reject duplicate vertices (any pair closer than the relative epsilon). */
		dup_eps2 = (double)opt.vertex_epsilon * opt.vertex_epsilon * diag2;
		for ( j = 0; j < count; j++ )
		{
			int k;
			for ( k = j + 1; k < count; k++ )
			{
				const double dx = (double)verts[j * 3 + 0] - verts[k * 3 + 0];
				const double dy = (double)verts[j * 3 + 1] - verts[k * 3 + 1];
				const double dz = (double)verts[j * 3 + 2] - verts[k * 3 + 2];
				if ( dx * dx + dy * dy + dz * dz <= dup_eps2 )
				{
					result->status = TESS_COPLANAR_ERR_DUPLICATE_VERTEX;
					result->error_face = i;
					goto done;
				}
			}
			centroid[0] += verts[j * 3 + 0];
			centroid[1] += verts[j * 3 + 1];
			centroid[2] += verts[j * 3 + 2];
		}
		centroid[0] /= count;
		centroid[1] /= count;
		centroid[2] /= count;

		/* Reject zero-area faces (e.g. collinear or collapsed points). */
		faceNormalAndArea( verts, count, infos[i].normal, &infos[i].area );
		if ( infos[i].area * 2 <= (double)opt.zero_area_tol * diag2 )
		{
			result->status = TESS_COPLANAR_ERR_ZERO_AREA_FACE;
			result->error_face = i;
			goto done;
		}

		nlen = infos[i].area * 2;
		infos[i].normal[0] /= nlen;
		infos[i].normal[1] /= nlen;
		infos[i].normal[2] /= nlen;
		infos[i].offset = infos[i].normal[0] * centroid[0] + infos[i].normal[1] * centroid[1] +
						  infos[i].normal[2] * centroid[2];
		infos[i].diag2 = diag2;
	}

	/* Phase 2: group coplanar faces (union-find over the plane relation). */
	for ( i = 0; i < face_count; i++ )
		parent[i] = i;
	for ( i = 0; i < face_count; i++ )
	{
		for ( j = i + 1; j < face_count; j++ )
		{
			double dot = infos[i].normal[0] * infos[j].normal[0] +
						 infos[i].normal[1] * infos[j].normal[1] +
						 infos[i].normal[2] * infos[j].normal[2];
			double di = infos[i].offset, dj = infos[j].offset;
			if ( dot < 0 )
			{
				dot = -dot;
				dj = -dj;
			}
			if ( dot >= 1.0 - (double)opt.normal_tol && fabs( di - dj ) <= (double)opt.offset_tol )
			{
				const int ri = findRoot( parent, i );
				const int rj = findRoot( parent, j );
				if ( ri != rj )
					parent[rj] = ri;
			}
		}
	}

	/* Phase 3: resolve each group. */
	result->stats.faces_input = face_count;
	for ( i = 0; i < face_count; i++ )
	{
		int member_count = 0;
		double sum_area = 0, union_area = 0;
		double group_normal[3] = { 0, 0, 0 };
		TESSreal* gverts = NULL;
		TESSindex* gtris = NULL;
		int gnv = 0, gnt = 0, k, status;

		if ( findRoot( parent, i ) != i )
			continue;

		for ( j = 0; j < face_count; j++ )
			if ( findRoot( parent, j ) == i )
				members[member_count++] = j;

		if ( member_count == 1 )
		{
			/* Singleton: not coplanar with anything, pass through unchanged. */
			const int f = members[0];
			status = tessellateGroup( faces, members, 1, infos, infos[f].normal,
									  &gverts, &gnv, &gtris, &gnt, &union_area );
			if ( status != TESS_COPLANAR_OK )
			{
				free( gverts );
				free( gtris );
				result->status = status;
				result->error_face = f;
				goto done;
			}
			status = emit( result, &vbuf, &vcap, &tbuf, &tcap, gverts, gnv, gtris, gnt );
			free( gverts );
			free( gtris );
			if ( status != TESS_COPLANAR_OK )
			{
				result->status = status;
				goto done;
			}
			result->stats.faces_passthrough++;
			result->stats.input_union_area += infos[f].area;
			result->stats.expected_area += infos[f].area;
			continue;
		}

		/* Multi-face group: average the aligned normals for a stable group plane. */
		for ( k = 0; k < member_count; k++ )
		{
			const struct FaceInfo* info = &infos[members[k]];
			double dot = info->normal[0] * infos[members[0]].normal[0] +
						 info->normal[1] * infos[members[0]].normal[1] +
						 info->normal[2] * infos[members[0]].normal[2];
			const double sign = dot < 0 ? -1.0 : 1.0;
			group_normal[0] += sign * info->normal[0];
			group_normal[1] += sign * info->normal[1];
			group_normal[2] += sign * info->normal[2];
			sum_area += info->area;
		}
		{
			const double len = sqrt( group_normal[0] * group_normal[0] +
									 group_normal[1] * group_normal[1] +
									 group_normal[2] * group_normal[2] );
			group_normal[0] /= len;
			group_normal[1] /= len;
			group_normal[2] /= len;
		}

		/* Union tessellation of the group: its area tells us whether the
		** faces actually overlap (sum of areas > union area). */
		status = tessellateGroup( faces, members, member_count, infos, group_normal,
								  &gverts, &gnv, &gtris, &gnt, &union_area );
		if ( status != TESS_COPLANAR_OK )
		{
			free( gverts );
			free( gtris );
			result->status = status;
			goto done;
		}

		result->stats.input_union_area += union_area;

		if ( sum_area - union_area <= (double)opt.area_rel_tol * sum_area )
		{
			/* Coplanar but not overlapping: pass each face through unchanged. */
			free( gverts );
			free( gtris );
			gverts = NULL;
			gtris = NULL;
			for ( k = 0; k < member_count; k++ )
			{
				const int f = members[k];
				double face_area = 0;
				status = tessellateGroup( faces, &f, 1, infos, infos[f].normal,
										  &gverts, &gnv, &gtris, &gnt, &face_area );
				if ( status != TESS_COPLANAR_OK )
				{
					free( gverts );
					free( gtris );
					result->status = status;
					result->error_face = f;
					goto done;
				}
				status = emit( result, &vbuf, &vcap, &tbuf, &tcap, gverts, gnv, gtris, gnt );
				free( gverts );
				free( gtris );
				gverts = NULL;
				gtris = NULL;
				if ( status != TESS_COPLANAR_OK )
				{
					result->status = status;
					goto done;
				}
				result->stats.faces_passthrough++;
				result->stats.expected_area += infos[f].area;
			}
			continue;
		}

		/* Overlapping coplanar group: resolve with the selected strategy. */
		result->stats.groups_resolved++;
		if ( opt.strategy == TESS_COPLANAR_KEEP_ONE )
		{
			int kept = members[0];
			for ( k = 1; k < member_count; k++ )
				if ( infos[members[k]].area > infos[kept].area )
					kept = members[k];
			free( gverts );
			free( gtris );
			gverts = NULL;
			gtris = NULL;
			status = tessellateGroup( faces, &kept, 1, infos, infos[kept].normal,
									  &gverts, &gnv, &gtris, &gnt, &union_area );
			if ( status != TESS_COPLANAR_OK )
			{
				free( gverts );
				free( gtris );
				result->status = status;
				result->error_face = kept;
				goto done;
			}
			result->stats.faces_dropped += member_count - 1;
			result->stats.expected_area += infos[kept].area;
		}
		else
		{
			/* TESS_COPLANAR_MERGE: the union tessellation is the output. */
			result->stats.expected_area += union_area;
		}

		status = emit( result, &vbuf, &vcap, &tbuf, &tcap, gverts, gnv, gtris, gnt );
		free( gverts );
		free( gtris );
		if ( status != TESS_COPLANAR_OK )
		{
			result->status = status;
			goto done;
		}
	}

	/* Phase 4: area accounting and the conservation check. */
	result->vertices = vbuf;
	result->triangles = tbuf;
	vbuf = NULL;
	tbuf = NULL;
	{
		double output_area = 0;
		for ( i = 0; i < result->triangle_count; i++ )
		{
			const TESSreal* p0 = &result->vertices[result->triangles[i * 3 + 0] * 3];
			const TESSreal* p1 = &result->vertices[result->triangles[i * 3 + 1] * 3];
			const TESSreal* p2 = &result->vertices[result->triangles[i * 3 + 2] * 3];
			const double e1[3] = { (double)p1[0] - p0[0], (double)p1[1] - p0[1], (double)p1[2] - p0[2] };
			const double e2[3] = { (double)p2[0] - p0[0], (double)p2[1] - p0[1], (double)p2[2] - p0[2] };
			const double cx = e1[1] * e2[2] - e1[2] * e2[1];
			const double cy = e1[2] * e2[0] - e1[0] * e2[2];
			const double cz = e1[0] * e2[1] - e1[1] * e2[0];
			output_area += 0.5 * sqrt( cx * cx + cy * cy + cz * cz );
		}
		result->stats.output_area = output_area;
	}
	result->stats.coverage_delta = result->stats.input_union_area - result->stats.output_area;
	{
		const double expected = result->stats.expected_area;
		const double tol = (double)opt.area_rel_tol * ( expected > 1.0 ? expected : 1.0 );
		if ( fabs( result->stats.output_area - expected ) > tol )
			result->status = TESS_COPLANAR_ERR_AREA_MISMATCH;
	}

done:
	free( infos );
	free( parent );
	free( members );
	free( vbuf );
	free( tbuf );
	return result;
}

void tessCoplanarFreeResult( struct TessCoplanarResult* result )
{
	if ( result == NULL )
		return;
	free( result->vertices );
	free( result->triangles );
	free( result );
}
