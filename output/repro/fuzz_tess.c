// Deterministic malformed-contour fuzz driver for libtess2 (scratch tool).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "Include/tesselator.h"

static unsigned long long rng_state;
static unsigned int rnd(void) {
	rng_state ^= rng_state << 13;
	rng_state ^= rng_state >> 7;
	rng_state ^= rng_state << 17;
	return (unsigned int)(rng_state >> 32);
}
static float frand(void) { return (float)(rnd() & 0xFFFFFF) / (float)0xFFFFFF; }

static float gen_coord(int strategy) {
	switch (strategy) {
	case 0: return frand() * 2.0f - 1.0f;
	case 1: return (frand() * 2.0f - 1.0f) * 3.40282347e+38f;
	case 2: return (frand() * 2.0f - 1.0f) * 1e-38f;
	case 3: return (float)(rnd() % 5) - 2.0f; // heavy duplication
	case 4: return frand() < 0.5f ? 0.0f : -0.0f;
	case 5: return nanf("");
	case 6: return frand() < 0.5f ? INFINITY : -INFINITY;
	case 7: return 1.0f; // collinear-ish
	case 8: return (float)(rnd() % 3); // coarse grid, many coincidences
	case 9: return frand() * 2e-7f - 1e-7f; // near-coincident cluster
	}
	return 0.0f;
}

static void* xalloc(void* u, unsigned int s) { (void)u; return malloc(s); }
static void* xrealloc(void* u, void* p, unsigned int s) { (void)u; return realloc(p, s); }
static void xfree(void* u, void* p) { (void)u; free(p); }

int main(int argc, char** argv) {
	unsigned long long seed_lo = 1, seed_hi = 1000000;
	if (argc > 1) seed_lo = strtoull(argv[1], 0, 10);
	if (argc > 2) seed_hi = strtoull(argv[2], 0, 10);
	float* contour = (float*)malloc(sizeof(float) * 3 * 4096);

	for (unsigned long long iter = seed_lo; iter < seed_hi; iter++) {
		unsigned long long seed = iter;
		rng_state = seed * 2654435761ULL + 0x9E3779B97F4A7C15ULL;
		TESSalloc alloc;
		TESStesselator* tess;
		int ncontours, c, winding, elem, poly, vsize, use_custom;

		alloc.memalloc = xalloc; alloc.memrealloc = xrealloc; alloc.memfree = xfree;
		alloc.userData = 0;
		use_custom = (seed % 3 == 0);
		if (use_custom) {
			// Tiny buckets stress bucket-boundary paths.
			alloc.meshEdgeBucketSize = 1 + rnd() % 4;
			alloc.meshVertexBucketSize = 1 + rnd() % 4;
			alloc.meshFaceBucketSize = 1 + rnd() % 4;
			alloc.dictNodeBucketSize = 1 + rnd() % 4;
			alloc.regionBucketSize = 1 + rnd() % 4;
			alloc.extraVertices = rnd() % 3;
			tess = tessNewTess(&alloc);
		} else {
			tess = tessNewTess(0);
		}
		if (!tess) continue;

		vsize = (rnd() % 4 == 0) ? 3 : 2;
		int opt_rev = (rnd() % 3 == 0);
		int opt_cdt = (rnd() % 3 == 0);
		if (opt_rev) tessSetOption(tess, TESS_REVERSE_CONTOURS, 1);
		if (opt_cdt) tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
		unsigned long long dump_iter = getenv("FUZZ_DUMP") ? strtoull(getenv("FUZZ_DUMP"), 0, 10) : 0;
		int do_dump = (dump_iter == iter);
		if (do_dump) printf("// iter %llu vsize=%d rev=%d cdt=%d custom=%d\n", iter, vsize, opt_rev, opt_cdt, use_custom);

		ncontours = 1 + rnd() % 3;
		for (c = 0; c < ncontours; c++) {
			int n;
			int i;
			int mix = rnd() % 2;
			int strategy = rnd() % 10;
			if (rnd() % 4 == 0) n = rnd() % 4;
			else if (rnd() % 8 == 0) n = 64 + rnd() % 33;
			else n = rnd() % 65;
			for (i = 0; i < n; i++) {
				int s = mix ? (rnd() % 10) : strategy;
				contour[i * vsize + 0] = gen_coord(s);
				contour[i * vsize + 1] = gen_coord(s);
				if (vsize == 3) contour[i * vsize + 2] = gen_coord(s);
			}
			tessAddContour(tess, vsize, contour, sizeof(float) * vsize, n);
			if (do_dump) {
				printf("static const float c%d[] = {", c);
				for (i = 0; i < n * vsize; i++) printf("%s%.9g", i ? "," : "", contour[i]);
				printf("}; // n=%d\n", n);
			}
		}
		winding = rnd() % 5;
		elem = rnd() % 3;
		poly = (rnd() % 2) ? 3 : 4;
		if (do_dump) printf("// winding=%d elem=%d poly=%d\n", winding, elem, poly);
		if (do_dump) fflush(stdout);
		if (getenv("FUZZ_TRACE")) { fprintf(stderr, "iter %llu\n", iter); fflush(stderr); }
		tessTesselate(tess, winding, elem, poly, vsize, 0);
		{
			volatile int ne = tessGetElementCount(tess);
			volatile int nv = tessGetVertexCount(tess);
			(void)ne; (void)nv;
		}
		tessDeleteTess(tess);
		if (iter % 100000 == 0) { fprintf(stderr, "iter %llu ok\n", iter); }
	}
	free(contour);
	printf("done\n");
	return 0;
}
