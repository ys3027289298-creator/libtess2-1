#!/bin/bash
# Demo recording script: runs the full sanitizer verification flow
# non-interactively. Segments are titled, with pauses between segments.
# Usage: asciinema rec -c output/record_demo.sh output/recording/demo.cast
set -u
cd "$(dirname "$0")/.."
ROOT=$(pwd)
WORK=/tmp/tess_demo
PAUSE=2

banner() {
	echo
	echo "================================================================"
	echo "  $1"
	echo "================================================================"
	sleep "$PAUSE"
}

banner "[1/6] Sanitizer 构建开关（不影响正常构建）"
echo '$ tail -12 .bazelrc'
tail -12 .bazelrc
sleep "$PAUSE"
echo '$ bazel test //...        # 正常构建'
bazel test //... 2>&1 | tail -3
sleep "$PAUSE"

banner "[2/6] Sanitizer 构建可用：bazel test --config=asan //..."
echo '$ bazel test --config=asan //...'
bazel test --config=asan //... 2>&1 | tail -3
sleep "$PAUSE"

banner "[3/6] 复现：修复前代码 + 畸形轮廓（ASan/UBSan）"
echo '# 从 git HEAD（未修复）取出源码构建 repro'
rm -rf "$WORK"
mkdir -p "$WORK"
git archive HEAD Source Include | tar -x -C "$WORK"
cp output/repro/repro.c "$WORK/"
cd "$WORK"
echo '$ gcc -fsanitize=address,undefined repro.c Source/*.c -o repro_dbg'
gcc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I. -IInclude repro.c Source/*.c -o repro_dbg -lm
gcc -g -O1 -DNDEBUG -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I. -IInclude repro.c Source/*.c -o repro_rel -lm
cd "$ROOT"
echo '$ ./repro_dbg < case1_mesh_corruption.txt   # CDT 翻转破坏网格（断言）'
"$WORK/repro_dbg" < output/repro/case1_mesh_corruption.txt 2>&1 | head -2
echo "exit=$?"
sleep 1
echo '$ ./repro_rel < case2_null_vertex.txt        # release 下空顶点访问（UB）'
"$WORK/repro_rel" < output/repro/case2_null_vertex.txt 2>&1 | head -2
sleep 1
echo '$ ./repro_rel < case3_signed_overflow.txt    # maxIter 有符号溢出（UB）'
"$WORK/repro_rel" < output/repro/case3_signed_overflow.txt 2>&1 | head -2
sleep "$PAUSE"

banner "[4/6] 修复内容：Source/tess.c（CDT 翻转守卫 + 溢出钳制）"
echo '$ git diff Source/tess.c'
git diff Source/tess.c
sleep "$PAUSE"

banner "[5/6] 修复后：同一组畸形轮廓零越界"
echo '# 用修复后的源码重新构建 repro'
mkdir -p "$WORK/fixed"
cp -r Source Include "$WORK/fixed/"
cp output/repro/repro.c "$WORK/fixed/"
cd "$WORK/fixed"
gcc -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I. -IInclude repro.c Source/*.c -o repro_dbg -lm
gcc -g -O1 -DNDEBUG -fsanitize=address,undefined -fno-sanitize-recover=all \
	-I. -IInclude repro.c Source/*.c -o repro_rel -lm
cd "$ROOT"
for f in case1_mesh_corruption case2_null_vertex case3_signed_overflow; do
	echo "$ ./repro_dbg < $f.txt"
	"$WORK/fixed/repro_dbg" < "output/repro/$f.txt" && echo "OK (exit 0, 无 sanitizer 输出)"
	echo "$ ./repro_rel < $f.txt"
	"$WORK/fixed/repro_rel" < "output/repro/$f.txt" && echo "OK (exit 0, 无 sanitizer 输出)"
	sleep 1
done
echo '$ bazel test //... && bazel test -c opt //... && bazel test --config=asan //...'
bazel test //... 2>&1 | tail -2
bazel test -c opt //... 2>&1 | tail -2
bazel test --config=asan //... 2>&1 | tail -2
sleep "$PAUSE"

banner "[6/6] 正常输入逐顶点零回归 + 大规模 fuzz 零越界"
echo '# 修复前后 6 组正常轮廓输出（顶点+索引）逐一 diff：'
echo '  golden_prefix.txt vs golden_postfix.txt -> 完全一致（见 output/logs/golden_diff.log）'
diff output/logs/golden_prefix.txt output/logs/golden_postfix.txt && echo "diff: 无差异"
sleep 1
echo '# 修复后 ASan+UBSan fuzz（debug+release，畸形轮廓随机生成）结果：'
cat output/logs/fuzz_final_status.log
sleep "$PAUSE"
echo "全部验证完成。"
