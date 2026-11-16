# Coplanar overlap resolution (z-fighting fix)

Imported models frequently contain faces that lie in the same plane and
overlap — duplicate layers, intersecting coplanar polygons. Triangulating
each face independently produces co-located overlapping triangles that
z-fight at render time. `Include/coplanar.h` / `Source/coplanar.c` add a
resolver on top of the libtess2 public API that detects such groups and
resolves them with a selectable strategy. **No existing libtess2 source
file is modified**; the resolver only calls the public API.

## Usage

```c
struct TessCoplanarOptions opt = tessCoplanarDefaultOptions();
opt.strategy = TESS_COPLANAR_MERGE;      // or TESS_COPLANAR_KEEP_ONE
struct TessCoplanarResult* r = tessCoplanarProcess(faces, face_count, &opt);
if (r->status == TESS_COPLANAR_OK) { /* r->vertices / r->triangles */ }
tessCoplanarFreeResult(r);
```

- `TESS_COPLANAR_MERGE` — union each overlapping coplanar group into a
  single triangulated layer (one `TESS_WINDING_NONZERO` tessellation of all
  contours in the group, with contour orientation normalized first). No
  overlap remains, so nothing z-fights. Surface coverage is preserved.
- `TESS_COPLANAR_KEEP_ONE` — keep the largest-area face of each overlapping
  coplanar group, drop the rest. Intended for exact duplicate layers;
  coverage unique to a dropped face is lost and reported as
  `stats.coverage_delta`.
- Faces that are not coplanar-overlapping are passed through: tessellated
  individually with the same parameters a caller would use directly, so
  their output is bit-exact identical to the old behavior (verified below).

Detection: per-face plane via Newell's method; two faces are coplanar when
`|dot(n1,n2)| >= 1 - normal_tol` and plane offsets differ by at most
`offset_tol`; within a coplanar group, faces overlap when
`sum_of_face_areas - union_area > area_rel_tol * sum_of_face_areas`.

Degenerate input is rejected before any tessellation: faces with fewer
than 3 vertices or NULL data (`TESS_COPLANAR_ERR_INVALID_INPUT`), faces
with two vertices closer than `vertex_epsilon` × bbox diagonal
(`TESS_COPLANAR_ERR_DUPLICATE_VERTEX`), and faces with
2·area ≤ `zero_area_tol` × bbox diagonal² (`TESS_COPLANAR_ERR_ZERO_AREA_FACE`).
`result->error_face` identifies the offending face.

Area conservation: after processing, `|output_area − expected_area| ≤
area_rel_tol · max(expected_area, 1)` must hold or the call fails with
`TESS_COPLANAR_ERR_AREA_MISMATCH`. `expected_area` is the union area for
MERGE and the kept-face area for KEEP_ONE; `output_area` is recomputed
independently from the final emitted vertex/index arrays, so the check
covers the whole emit pipeline. Geometric union correctness is verified
against closed-form ground truth in the test below (union area 13 known
analytically). `stats.coverage_delta = input_union_area − output_area`
reports any coverage intentionally dropped by KEEP_ONE.

## 1. Before/after comparison on the same input

Hand-crafted input set `zfight` (5 faces; A and B are unit squares at z=0
overlapping by 1, C is an exact duplicate of A, D is a triangle at x=5,
E is a square at z=5 — parallel to A's plane but offset, hence not
coplanar). Union area is analytically 13; the naive sum of face areas is
18, of which 5 is double-covered (4 from duplicate C, 1 from A∩B).

Measured output of `Tests/coplanar_test.c` (gcc 13.3.0, `-O0`; triangle
counts are deterministic, libtess2 has no randomness):

| variant                    | vertices | triangles | area  | notes                              |
|----------------------------|---------:|----------:|------:|------------------------------------|
| before (per-face)          |       19 |         9 | 18.00 | 5.0 of area double-covered → z-fight |
| after `TESS_COPLANAR_MERGE`|       17 |        13 | 13.00 | one layer, coverage_delta = 0      |
| after `TESS_COPLANAR_KEEP_ONE` |    11 |         5 | 10.00 | 2 faces dropped, coverage_delta = 3 |

Result-array memory (vertices + indices): before 336 B, merge 360 B,
keep-one 192 B.

The merged layer is not vertex-minimal: libtess2 retains the contour
intersection points (here (1,1) and (2,2)) as vertices of the planar
decomposition, so the L-shaped union (8 corners) triangulates to 10
triangles instead of the minimal 6. Coverage is exact; only the vertex
count is affected.

Strategy difference on this input: MERGE preserves the full union
(area 13, one non-overlapping layer of 13 triangles); KEEP_ONE emits
5 triangles covering area 10 — it removes the duplicate layer C (its
intended use) but also drops B's unique coverage (3.0), which is exactly
what `coverage_delta` reports. Use KEEP_ONE only when dropped faces are
known duplicates.

## 2. Failure paths: reproduction and expected errors

All reproduced by `Tests/coplanar_test.c` (section 2 of its output);
each was run and matched the expectation:

| input                                   | expected status (actual matched)                | error_face |
|-----------------------------------------|--------------------------------------------------|-----------:|
| face of 3 collinear points, at index 1  | `TESS_COPLANAR_ERR_ZERO_AREA_FACE` (2)           | 1          |
| square with vertex (1,0,0) repeated     | `TESS_COPLANAR_ERR_DUPLICATE_VERTEX` (3)         | 0          |
| face with only 2 vertices               | `TESS_COPLANAR_ERR_INVALID_INPUT` (1)            | 0          |
| NULL face array / face_count = 0        | `TESS_COPLANAR_ERR_INVALID_INPUT` (1)            | −1         |

Reproduce: build the test (below) and run it; or call
`tessCoplanarProcess()` with the inputs above — they are literal arrays
in `Tests/coplanar_test.c` (`collinear`, `dupVerts`, `twoVerts`).

## 3. Resource / memory upper bounds

Let F = number of faces, V = total input vertices, E = total contour
edges (E = V for closed contours).

- Validation: O(V) time per face for plane/bbox, O(n²) per face for the
  duplicate-vertex pair check (n = vertices in that face; Σn² ≤ V² worst
  case). O(1) extra memory.
- Coplanar grouping: union-find, O(F²) plane comparisons, O(F) memory.
- Overlap detection: one union tessellation per multi-face coplanar
  group — O(F) extra tessellations total, each freed before the next.
- Tessellation (libtess2 internals, per group): O(E_g log E_g) typical
  sweep time; vertices/edges/faces/dict nodes allocated from buckets.
  Intersecting edges create new vertices — O(E_g²) pathological worst
  case, O(E_g) typical.
- Peak memory: input + output arrays + one group's tessellation at a
  time. Output ≤ 12·V_out + 12·T_out bytes (3 floats per vertex, 3 ints
  per triangle, `TESSreal` = 4 B, `TESSindex` = 4 B), with
  V_out ≤ V + (intersection vertices) and T_out ≤ 2·V_out. Growable
  arrays double on realloc, so transiently ≤ 2× the final output size.
- Measured on the `zfight` set: 336–360 B of output (table above);
  the full test binary runs clean under ASan/UBSan
  (`-fsanitize=address,undefined`), no leaks.

## 4. Old-behavior bit-exact consistency

The resolver adds new files only; no existing libtess2 source is touched,
so the library itself is unchanged by construction. Additionally,
`Tests/coplanar_test.c` section 3 verifies empirically that for input
with no coplanar overlap (non-coplanar faces D, E plus two disjoint
squares F, G in the same plane), the resolver's output is **bit-exact
identical** to the old behavior (tessellating each face individually with
libtess2, same per-face normal, `TESS_WINDING_NONZERO`, polygons of 3):

```
vertex bytes compared: 180, triangle bytes compared: 84
result: BIT-EXACT IDENTICAL
```

`stats.faces_passthrough == 4` and `stats.groups_resolved == 0` confirm
nothing was re-routed. Emission order is ascending input face index, so
the concatenated output arrays compare equal byte for byte.

## Build and run

```sh
# standalone (no test framework needed)
gcc -I Include -I Source Tests/coplanar_test.c Source/bucketalloc.c Source/dict.c \
    Source/geom.c Source/mesh.c Source/priorityq.c Source/sweep.c Source/tess.c \
    Source/coplanar.c -lm -o coplanar_test
./coplanar_test        # exit code 0 = all checks passed

# bazel
bazel test //:coplanar_test
```

premake4.lua already globs `Source/*.c`, so the premake build picks up
`Source/coplanar.c` automatically.
