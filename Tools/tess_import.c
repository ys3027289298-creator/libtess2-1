/*
** Command line front-end for the hardened contour import entry.
**
** Usage:
**   tess_import <file.t2c>          Parse a contour container and tessellate.
**   tess_import --verify <file.t2c> Additionally compare the import path
**                                   against the direct tessAddContour() path
**                                   vertex by vertex (zero-regression check).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Include/tessio.h"

static unsigned char* readFile( const char* path, size_t* outSize )
{
	FILE* f = fopen( path, "rb" );
	unsigned char* buf;
	long n;
	if ( !f ) return 0;
	fseek( f, 0, SEEK_END );
	n = ftell( f );
	fseek( f, 0, SEEK_SET );
	buf = (unsigned char*)malloc( n > 0 ? (size_t)n : 1 );
	if ( !buf ) { fclose( f ); return 0; }
	if ( n > 0 && fread( buf, 1, (size_t)n, f ) != (size_t)n )
	{
		free( buf );
		fclose( f );
		return 0;
	}
	fclose( f );
	*outSize = (size_t)n;
	return buf;
}

static int tessellateAndPrint( TESStesselator* tess, const char* tag )
{
	int nv, ne, i;
	const TESSreal* verts;
	if ( !tessTesselate( tess, TESS_WINDING_POSITIVE, TESS_POLYGONS, 3, 2, 0 ) )
	{
		printf( "%s: tessellation failed, status=%d\n", tag, (int)tessGetStatus( tess ) );
		return 0;
	}
	nv = tessGetVertexCount( tess );
	ne = tessGetElementCount( tess );
	verts = tessGetVertices( tess );
	printf( "%s: vertices=%d elements=%d\n", tag, nv, ne );
	for ( i = 0; i < nv; i++ )
		printf( "  v%d: %.9g %.9g\n", i, (double)verts[i*2], (double)verts[i*2+1] );
	return 1;
}

/* Adds the contours of an already-validated container directly through the
** classic library entry points (the pre-hardening code path). */
static void addContoursDirect( TESStesselator* tess, const unsigned char* data )
{
	unsigned int contourCount, i;
	size_t off = 12;
	memcpy( &contourCount, data + 8, 4 );
	for ( i = 0; i < contourCount; i++ )
	{
		unsigned int vertexCount, flags;
		memcpy( &vertexCount, data + off, 4 );
		memcpy( &flags, data + off + 4, 4 );
		off += 8;
		tessSetOption( tess, TESS_REVERSE_CONTOURS, ( flags & 1u ) ? 1 : 0 );
		tessAddContour( tess, 2, data + off, 8, (int)vertexCount );
		off += (size_t)vertexCount * 8;
	}
	tessSetOption( tess, TESS_REVERSE_CONTOURS, 0 );
}

static int verifyIdentical( const unsigned char* data, size_t size )
{
	TESStesselator* a = tessNewTess( 0 );
	TESStesselator* b = tessNewTess( 0 );
	int same = 0;
	if ( tessImportContours( a, data, size ) != TESS_PARSE_OK )
	{
		printf( "verify: input does not parse, nothing to compare\n" );
	}
	else
	{
		int nva, nvb, nea, neb;
		addContoursDirect( b, data );
		tessellateAndPrint( a, "import-path" );
		tessellateAndPrint( b, "direct-path" );
		nva = tessGetVertexCount( a ); nvb = tessGetVertexCount( b );
		nea = tessGetElementCount( a ); neb = tessGetElementCount( b );
		same = nva == nvb && nea == neb
			&& memcmp( tessGetVertices( a ), tessGetVertices( b ),
				(size_t)nva * 2 * sizeof(TESSreal) ) == 0
			&& memcmp( tessGetElements( a ), tessGetElements( b ),
				(size_t)nea * 3 * sizeof(TESSindex) ) == 0;
		printf( "REGRESSION: %s (vertices %s, elements %s)\n",
			same ? "IDENTICAL" : "DIFFERS",
			nva == nvb && memcmp( tessGetVertices( a ), tessGetVertices( b ),
				(size_t)nva * 2 * sizeof(TESSreal) ) == 0 ? "match" : "MISMATCH",
			nea == neb && memcmp( tessGetElements( a ), tessGetElements( b ),
				(size_t)nea * 3 * sizeof(TESSindex) ) == 0 ? "match" : "MISMATCH" );
	}
	tessDeleteTess( a );
	tessDeleteTess( b );
	return same;
}

int main( int argc, char** argv )
{
	const char* path;
	unsigned char* data;
	size_t size = 0;
	int verify = 0;
	TessParseError err;
	TESStesselator* tess;

	if ( argc == 3 && strcmp( argv[1], "--verify" ) == 0 )
	{
		verify = 1;
		path = argv[2];
	}
	else if ( argc == 2 )
	{
		path = argv[1];
	}
	else
	{
		fprintf( stderr, "usage: %s [--verify] <file.t2c>\n", argv[0] );
		return 2;
	}

	data = readFile( path, &size );
	if ( !data )
	{
		fprintf( stderr, "cannot read %s\n", path );
		return 2;
	}

	tess = tessNewTess( 0 );
	err = tessImportContours( tess, data, size );
	printf( "parse: %s (%d)\n", tessParseErrorString( err ), (int)err );
	if ( err == TESS_PARSE_OK )
		tessellateAndPrint( tess, "import-path" );
	tessDeleteTess( tess );

	if ( verify && err == TESS_PARSE_OK && !verifyIdentical( data, size ) )
	{
		free( data );
		return 1;
	}
	free( data );
	return err == TESS_PARSE_OK ? 0 : (int)err;
}
