// Repro harness: reads tessellation job from stdin.
// Format: vsize rev cdt winding elem poly ncontours
// then per contour: n followed by n*vsize floats
#include <stdio.h>
#include <stdlib.h>
#include "Include/tesselator.h"

int main(void) {
	int vsize, rev, cdt, winding, elem, poly, ncontours;
	if (scanf("%d %d %d %d %d %d %d", &vsize, &rev, &cdt, &winding, &elem, &poly, &ncontours) != 7) return 2;
	TESStesselator* tess = tessNewTess(0);
	if (!tess) return 2;
	if (rev) tessSetOption(tess, TESS_REVERSE_CONTOURS, 1);
	if (cdt) tessSetOption(tess, TESS_CONSTRAINED_DELAUNAY_TRIANGULATION, 1);
	for (int c = 0; c < ncontours; c++) {
		int n;
		if (scanf("%d", &n) != 1) return 2;
		float* buf = (float*)malloc(sizeof(float) * vsize * (n > 0 ? n : 1));
		for (int i = 0; i < n * vsize; i++)
			if (scanf("%f", &buf[i]) != 1) return 2;
		tessAddContour(tess, vsize, buf, sizeof(float) * vsize, n);
		free(buf);
	}
	tessTesselate(tess, winding, elem, poly, vsize, 0);
	volatile int ne = tessGetElementCount(tess);
	(void)ne;
	tessDeleteTess(tess);
	return 0;
}
