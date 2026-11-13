/*
** Fuzz driver for the hardened contour import entry (tessImportContours).
**
** Usage:
**   fuzz_import [rounds] [seed]
**
** Generates deterministic pseudo-random contour containers (valid, truncated,
** out-of-range and mis-nested variants plus raw garbage), feeds them through
** the import entry and tessellates whatever parses. Intended to be built with
** ASan/LSan/UBSan: any crash or leak aborts the run. Prints a per-error-code
** tally at the end and exits non-zero if no rounds were run.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../Include/tessio.h"

static unsigned long long rngState;

static unsigned long long rnd( void )
{
	rngState ^= rngState << 13;
	rngState ^= rngState >> 7;
	rngState ^= rngState << 17;
	return rngState;
}

static unsigned int rndRange( unsigned int n )
{
	return (unsigned int)( rnd() % n );
}

/* Random float, including NaN/Inf/huge bit patterns. */
static float rndFloat( void )
{
	unsigned int bits = (unsigned int)rnd();
	float v;
	switch ( rndRange( 4 ) )
	{
	case 0: return (float)( (int)rndRange( 2001 ) - 1000 ) / 8.0f;
	case 1: return (float)( (int)rndRange( 33 ) - 16 ) * 1000000.0f;
	default:
		memcpy( &v, &bits, 4 );
		return v;
	}
}

/* Builds a mostly-structured container. */
static size_t genStructured( unsigned char* buf, size_t cap )
{
	size_t off = 0;
	unsigned int ncontours = rndRange( 6 );
	unsigned int i;
	unsigned char* countAt;

	buf[off++] = 0x54; buf[off++] = 0x45; buf[off++] = 0x43; buf[off++] = 0x31; /* magic */
	buf[off++] = 1; buf[off++] = 0; /* version */
	buf[off++] = 0; buf[off++] = 0; /* reserved */
	countAt = buf + off;
	off += 4;

	for ( i = 0; i < ncontours; i++ )
	{
		unsigned int nverts = rndRange( 12 );
		unsigned int flags = rndRange( 4 ) == 0 ? 1u : 0u;
		unsigned int v;
		if ( off + 8 + (size_t)nverts * 8 > cap ) break;
		buf[off++] = (unsigned char)nverts; buf[off++] = (unsigned char)( nverts >> 8 );
		buf[off++] = 0; buf[off++] = 0;
		buf[off++] = (unsigned char)flags; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
		for ( v = 0; v < nverts; v++ )
		{
			float x = rndFloat();
			float y = rndFloat();
			memcpy( buf + off, &x, 4 ); off += 4;
			memcpy( buf + off, &y, 4 ); off += 4;
		}
	}
	memcpy( countAt, &i, 4 );

	/* Randomly corrupt: truncate, or leave as is. */
	if ( off > 0 && rndRange( 3 ) == 0 )
		off = rndRange( (unsigned int)off );
	return off;
}

/* Builds a geometrically valid container (outer ring plus optional hole),
** then sometimes breaks nesting or coordinates. */
static size_t genValidish( unsigned char* buf, size_t cap )
{
	size_t off = 0;
	unsigned int i;
	const unsigned int nverts = 4 + rndRange( 5 );
	float cx = (float)( (int)rndRange( 200 ) - 100 );
	float cy = (float)( (int)rndRange( 200 ) - 100 );
	float radius = 10.0f + (float)rndRange( 100 );
	int withHole = rndRange( 2 );
	unsigned int ncontours = withHole ? 2u : 1u;
	unsigned int c;

	TESS_NOTUSED( cap );
	buf[off++] = 0x54; buf[off++] = 0x45; buf[off++] = 0x43; buf[off++] = 0x31;
	buf[off++] = 1; buf[off++] = 0; buf[off++] = 0; buf[off++] = 0;
	memcpy( buf + off, &ncontours, 4 ); off += 4;

	for ( c = 0; c < ncontours; c++ )
	{
		float r = c == 0 ? radius : radius * 0.5f;
		unsigned int flags = c == 0 ? 0u : 1u;
		memcpy( buf + off, &nverts, 4 ); off += 4;
		memcpy( buf + off, &flags, 4 ); off += 4;
		for ( i = 0; i < nverts; i++ )
		{
			/* Cheap regular-ish polygon without libm trig. */
			static const float dir[8][2] = {
				{1,0},{1,1},{0,1},{-1,1},{-1,0},{-1,-1},{0,-1},{1,-1}
			};
			float x = cx + dir[( i * 8 ) / nverts][0] * r;
			float y = cy + dir[( i * 8 ) / nverts][1] * r;
			memcpy( buf + off, &x, 4 ); off += 4;
			memcpy( buf + off, &y, 4 ); off += 4;
		}
	}

	switch ( rndRange( 5 ) )
	{
	case 0: /* break nesting: flag first ring as hole too */
		if ( off >= 20 ) buf[16] |= 1;
		break;
	case 1: /* break coordinates: poison one vertex with huge/NaN value */
		if ( off >= 28 )
		{
			float huge = 1e30f;
			unsigned int nanBits = 0x7fc00000u;
			size_t pos = 20 + (size_t)rndRange( (unsigned int)( off - 20 ) / 4 ) * 4;
			if ( rndRange( 2 ) ) memcpy( buf + pos, &huge, 4 );
			else memcpy( buf + pos, &nanBits, 4 );
		}
		break;
	case 2: /* truncate */
		off = 4 + rndRange( (unsigned int)( off - 4 ) );
		break;
	default:
		break;
	}
	return off;
}

int main( int argc, char** argv )
{
	static unsigned char buf[4096];
	long rounds = argc > 1 ? atol( argv[1] ) : 100000;
	long seed = argc > 2 ? atol( argv[2] ) : 1;
	long r;
	long tally[8] = {0};
	long tessellated = 0;

	if ( rounds <= 0 )
	{
		fprintf( stderr, "usage: %s [rounds] [seed]\n", argv[0] );
		return 2;
	}
	rngState = (unsigned long long)seed * 2654435761u + 1;

	for ( r = 0; r < rounds; r++ )
	{
		size_t size;
		TessParseError err;
		TESStesselator* tess;

		switch ( rndRange( 3 ) )
		{
		case 0:
			size = rndRange( sizeof(buf) );
			{
				size_t k;
				for ( k = 0; k < size; k++ ) buf[k] = (unsigned char)rnd();
			}
			break;
		case 1:
			size = genStructured( buf, sizeof(buf) );
			break;
		default:
			size = genValidish( buf, sizeof(buf) );
			break;
		}

		tess = tessNewTess( 0 );
		err = tessImportContours( tess, buf, size );
		if ( (int)err >= 0 && (int)err < 8 )
			tally[(int)err]++;
		if ( err == TESS_PARSE_OK )
		{
			static const int windings[] = {
				TESS_WINDING_ODD, TESS_WINDING_NONZERO, TESS_WINDING_POSITIVE,
				TESS_WINDING_NEGATIVE, TESS_WINDING_ABS_GEQ_TWO
			};
			const char* dump = getenv( "FUZZ_DUMP_LAST" );
			if ( dump )
			{
				FILE* f = fopen( dump, "wb" );
				if ( f ) { fwrite( buf, 1, size, f ); fclose( f ); }
			}
			tessTesselate( tess, windings[rndRange( 5 )], TESS_POLYGONS, 3, 2, 0 );
			tessellated++;
		}
		tessDeleteTess( tess );
	}

	printf( "fuzz_import: rounds=%ld seed=%ld tessellated=%ld\n", rounds, seed, tessellated );
	printf( "  OK=%ld BAD_MAGIC=%ld UNSUPPORTED_VERSION=%ld TRUNCATED=%ld\n",
		tally[0], tally[1], tally[2], tally[3] );
	printf( "  COORD_RANGE=%ld NESTING=%ld NO_MEMORY=%ld\n",
		tally[4], tally[5], tally[6] );
	printf( "fuzz_import: DONE (no crash, no leak)\n" );
	return 0;
}
