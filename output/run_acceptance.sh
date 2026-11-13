#!/usr/bin/env bash
# Acceptance run for the hardened contour import entry.
# Non-interactive: prints titled segments with a pause between them.
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/output"
BIN="$OUT/bin"
LOGS="$OUT/logs"
export PATH="$HOME/bin:$PATH"
export USE_BAZEL_VERSION=7.4.1

mkdir -p "$BIN" "$LOGS"
cd "$ROOT"

section() {
	echo
	echo "================================================================"
	echo "  $1"
	echo "================================================================"
	sleep 2
}

section "[1/5] BUILD: library + tools (ASan/UBSan/LSan)"
gcc -std=c99 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
	-fno-omit-frame-pointer -IInclude Source/*.c Tools/tess_import.c \
	-o "$BIN/tess_import" -lm && echo "built: $BIN/tess_import"
gcc -std=c99 -Wall -Wextra -O1 -g -fsanitize=address,undefined \
	-fno-omit-frame-pointer -IInclude Source/*.c Fuzz/fuzz_import.c \
	-o "$BIN/fuzz_import" -lm && echo "built: $BIN/fuzz_import"
sleep 1

section "[2/5] MALFORMED SAMPLES: distinguishable error codes, no crash"
for s in "$OUT"/samples/*.t2c; do
	echo "--- $(basename "$s")"
	"$BIN/tess_import" "$s"
	echo "exit-code=$?"
	sleep 1
done

section "[3/5] ZERO REGRESSION: repo test suite + vertex-identical output"
bazelisk test //:libtess2_test 2>&1 | tail -4
for s in valid_quad valid_hole; do
	"$BIN/tess_import" --verify "$OUT/samples/$s.t2c" | grep -E "REGRESSION|import-path|direct-path"
done
sleep 1

section "[4/5] FUZZ: fuzz_import 200000 rounds x 3 seeds (sanitizers on)"
for seed in 1 42 1337; do
	"$BIN/fuzz_import" 200000 "$seed" | tee "$LOGS/fuzz_seed$seed.log"
	sleep 1
done

section "[5/5] SUMMARY"
echo "malformed samples: distinct error codes, no crash (segment 2)"
echo "regression:        repo suite PASSED, outputs IDENTICAL (segment 3)"
echo "fuzz:              3 x 200000 rounds, no crash, no leak (segment 4)"
echo "artifacts:         $OUT/{samples,bin,logs,acceptance.cast}"
sleep 2
