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

#include "../Include/tessio.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define TESS_IO_HEADER_SIZE 12u
#define TESS_IO_CONTOUR_HEADER_SIZE 8u
#define TESS_IO_VERTEX_SIZE 8u /* two f32 per vertex */

typedef unsigned int tessio_u32;

static tessio_u32 readU32( const unsigned char* p )
{
	return (tessio_u32)p[0]
		| ((tessio_u32)p[1] << 8)
		| ((tessio_u32)p[2] << 16)
		| ((tessio_u32)p[3] << 24);
}

static float readF32( const unsigned char* p )
{
	float v;
	memcpy( &v, p, sizeof(v) );
	return v;
}

static int validCoord( float v )
{
	return isfinite( v )
		&& v >= TESS_MIN_VALID_INPUT_VALUE
		&& v <= TESS_MAX_VALID_INPUT_VALUE;
}

/* Even-odd point in ring test, computed in double for stability.
** Points exactly on an edge are treated as inside. */
static int pointInRing( const unsigned char* verts, tessio_u32 count, double px, double py )
{
	tessio_u32 i, j;
	int inside = 0;
	for ( i = 0, j = count - 1; i < count; j = i++ )
	{
		double xi = readF32( verts + i * TESS_IO_VERTEX_SIZE );
		double yi = readF32( verts + i * TESS_IO_VERTEX_SIZE + 4 );
		double xj = readF32( verts + j * TESS_IO_VERTEX_SIZE );
		double yj = readF32( verts + j * TESS_IO_VERTEX_SIZE + 4 );
		double dx = xj - xi;
		double dy = yj - yi;
		/* On-segment check. */
		double cross = ( px - xi ) * dy - ( py - yi ) * dx;
		if ( cross == 0.0
			&& px >= ( xi < xj ? xi : xj ) && px <= ( xi < xj ? xj : xi )
			&& py >= ( yi < yj ? yi : yj ) && py <= ( yi < yj ? yj : yi ) )
			return 1;
		if ( ( yi > py ) != ( yj > py )
			&& px < xi + ( py - yi ) / dy * dx )
			inside = !inside;
	}
	return inside;
}

const char* tessParseErrorString( TessParseError err )
{
	switch ( err )
	{
	case TESS_PARSE_OK: return "TESS_PARSE_OK";
	case TESS_PARSE_ERR_BAD_MAGIC: return "TESS_PARSE_ERR_BAD_MAGIC";
	case TESS_PARSE_ERR_UNSUPPORTED_VERSION: return "TESS_PARSE_ERR_UNSUPPORTED_VERSION";
	case TESS_PARSE_ERR_TRUNCATED: return "TESS_PARSE_ERR_TRUNCATED";
	case TESS_PARSE_ERR_COORD_RANGE: return "TESS_PARSE_ERR_COORD_RANGE";
	case TESS_PARSE_ERR_NESTING: return "TESS_PARSE_ERR_NESTING";
	case TESS_PARSE_ERR_NO_MEMORY: return "TESS_PARSE_ERR_NO_MEMORY";
	}
	return "TESS_PARSE_UNKNOWN";
}

TessParseError tessImportContours( TESStesselator* tess, const void* data, size_t size )
{
	const unsigned char* bytes = (const unsigned char*)data;
	tessio_u32 contourCount, i;
	size_t* offsets = 0;
	size_t off;
	TessParseError err = TESS_PARSE_OK;
	int currentOuter = -1;

	if ( tess == 0 || ( data == 0 && size > 0 ) )
		return TESS_PARSE_ERR_TRUNCATED;

	/* Pass 1: structural walk, recording each contour offset and
	** validating coordinate ranges. */
	if ( size < TESS_IO_HEADER_SIZE )
		return TESS_PARSE_ERR_TRUNCATED;
	if ( readU32( bytes ) != TESS_IO_MAGIC )
		return TESS_PARSE_ERR_BAD_MAGIC;
	if ( ( readU32( bytes + 4 ) & 0xffffu ) != TESS_IO_VERSION )
		return TESS_PARSE_ERR_UNSUPPORTED_VERSION;

	contourCount = readU32( bytes + 8 );
	/* Every contour needs at least its 8 byte header. */
	if ( (size_t)contourCount > ( size - TESS_IO_HEADER_SIZE ) / TESS_IO_CONTOUR_HEADER_SIZE )
		return TESS_PARSE_ERR_TRUNCATED;

	if ( contourCount > 0 )
	{
		offsets = (size_t*)malloc( (size_t)contourCount * sizeof(size_t) );
		if ( offsets == 0 )
			return TESS_PARSE_ERR_NO_MEMORY;
	}

	off = TESS_IO_HEADER_SIZE;
	for ( i = 0; i < contourCount; i++ )
	{
		tessio_u32 vertexCount = readU32( bytes + off );
		size_t coordBytes;
		tessio_u32 v;
		const unsigned char* verts;

		offsets[i] = off;
		off += TESS_IO_CONTOUR_HEADER_SIZE;
		verts = bytes + off;

		/* Guards both truncation and size_t overflow. */
		if ( (size_t)vertexCount > ( size - off ) / TESS_IO_VERTEX_SIZE )
		{
			err = TESS_PARSE_ERR_TRUNCATED;
			goto done;
		}
		coordBytes = (size_t)vertexCount * TESS_IO_VERTEX_SIZE;

		for ( v = 0; v < vertexCount; v++ )
		{
			float x = readF32( verts + (size_t)v * TESS_IO_VERTEX_SIZE );
			float y = readF32( verts + (size_t)v * TESS_IO_VERTEX_SIZE + 4 );
			if ( !validCoord( x ) || !validCoord( y ) )
			{
				err = TESS_PARSE_ERR_COORD_RANGE;
				goto done;
			}
		}
		off += coordBytes;
	}

	/* Pass 2: ring nesting validation. */
	for ( i = 0; i < contourCount; i++ )
	{
		const unsigned char* rec = bytes + offsets[i];
		tessio_u32 vertexCount = readU32( rec );
		int isHole = ( readU32( rec + 4 ) & TESS_IO_FLAG_HOLE ) != 0;

		if ( !isHole )
		{
			currentOuter = (int)i;
			continue;
		}

		/* Orphan hole: no preceding outer ring. */
		if ( currentOuter < 0 || vertexCount < 3 )
		{
			err = TESS_PARSE_ERR_NESTING;
			goto done;
		}

		{
			const unsigned char* outerRec = bytes + offsets[currentOuter];
			tessio_u32 outerCount = readU32( outerRec );
			const unsigned char* holeVerts = rec + TESS_IO_CONTOUR_HEADER_SIZE;
			double hx = readF32( holeVerts );
			double hy = readF32( holeVerts + 4 );
			tessio_u32 k;

			/* A hole must be contained in its parent outer ring. */
			if ( outerCount < 3
				|| !pointInRing( outerRec + TESS_IO_CONTOUR_HEADER_SIZE, outerCount, hx, hy ) )
			{
				err = TESS_PARSE_ERR_NESTING;
				goto done;
			}

			/* A hole must not be nested inside a sibling hole. */
			for ( k = (tessio_u32)currentOuter + 1; k < i; k++ )
			{
				const unsigned char* sibRec = bytes + offsets[k];
				tessio_u32 sibCount = readU32( sibRec );
				int sibIsHole = ( readU32( sibRec + 4 ) & TESS_IO_FLAG_HOLE ) != 0;
				if ( sibIsHole && sibCount >= 3
					&& pointInRing( sibRec + TESS_IO_CONTOUR_HEADER_SIZE, sibCount, hx, hy ) )
				{
					err = TESS_PARSE_ERR_NESTING;
					goto done;
				}
			}
		}
	}

	/* Pass 3: all validation passed, import the contours. */
	for ( i = 0; i < contourCount; i++ )
	{
		const unsigned char* rec = bytes + offsets[i];
		tessio_u32 vertexCount = readU32( rec );
		int isHole = ( readU32( rec + 4 ) & TESS_IO_FLAG_HOLE ) != 0;
		tessSetOption( tess, TESS_REVERSE_CONTOURS, isHole ? 1 : 0 );
		tessAddContour( tess, 2, rec + TESS_IO_CONTOUR_HEADER_SIZE,
			TESS_IO_VERTEX_SIZE, (int)vertexCount );
	}
	tessSetOption( tess, TESS_REVERSE_CONTOURS, 0 );

done:
	free( offsets );
	return err;
}
