# Acceptance artifacts: hardened contour import

## Changes (repo)

- `Include/tessio.h`, `Source/tessio.c` — new hardened import entry
  `tessImportContours()` validating a binary contour container
  (magic/version/counts/coords) and returning distinguishable
  `TessParseError` codes: `TRUNCATED` (structure incomplete),
  `COORD_RANGE` (NaN/Inf/out of range), `NESTING` (orphan hole, hole
  outside parent, hole-in-hole), plus `BAD_MAGIC` / `UNSUPPORTED_VERSION`
  / `NO_MEMORY`. On error the tesselator is left untouched.
- `Source/tess.c` — root-cause fix: `tessAddContour()` now ignores
  contours with fewer than 2 vertices (a 1-vertex contour created a
  degenerate self-loop edge that crashed the sweep; found by fuzzing).
- `Fuzz/fuzz_import.c` — standalone fuzz driver: `fuzz_import [rounds] [seed]`.
- `Tools/tess_import.c` — CLI for the entry; `--verify` diffs the import
  path against the direct `tessAddContour()` path vertex by vertex.
- `Tests/libtess2_test.cc`, `BUILD.bazel` — parser/regression tests wired
  into the repo's own Bazel test suite.

## Artifacts (this directory)

- `samples/` — handmade minimal containers: `valid_quad.t2c`,
  `valid_hole.t2c`, `bad_truncated.t2c`, `bad_coord.t2c`,
  `bad_nesting_orphan.t2c`, `bad_nesting_outside.t2c`, `bad_magic.t2c`
  (regenerate with `python3 output/gen_samples.py`).
- `gen_samples.py` — sample generator.
- `bin/` — `tess_import`, `fuzz_import` built with ASan/UBSan/LSan.
- `logs/fuzz_seed*.log` — fuzz summaries.
- `run_acceptance.sh` — the recorded, non-interactive acceptance run.
- `acceptance.cast` — asciinema recording (`asciinema play` to view),
  titled segments `[1/5]`..`[5/5]` with pauses between them.

## Results

- Malformed samples: distinct exit/error codes (`TRUNCATED`=3,
  `COORD_RANGE`=4, `NESTING`=5, `BAD_MAGIC`=1), no crash.
- Zero regression: `bazelisk test //:libtess2_test` PASSED (repo suite
  incl. new tests); valid samples produce vertex-identical output via
  import path vs direct path (`REGRESSION: IDENTICAL`).
- Fuzz: 3 seeds x 200000 rounds under ASan/UBSan/LSan — no crash, no leak.
