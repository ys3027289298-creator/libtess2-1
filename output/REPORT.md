# 三角化越界问题：定位与修复记录

日期：2026-11-13 ｜ 仓库：libtess2 @ 8dbd648 ｜ 构建：Bazel 7.4.1（bazelisk）

## 1. Sanitizer 构建（不影响正常构建）

- `.bazelrc` 新增 opt-in 配置 `--config=asan`：AddressSanitizer + UndefinedBehaviorSanitizer，
  `-fno-sanitize-recover=all`、`detect_leaks=1`、UBSan `halt_on_error=1`。
- 默认构建/测试命令完全不变（`bazel test //...`、`-c opt`），无新增编译/链接选项，零性能影响。
- 用法：`bazel test --config=asan //...`
- 另新增 `.bazelversion = 7.4.1`（Bazel 9 与仓库锁定的 googletest 1.15.2 不兼容）。

## 2. 问题定位

复现路径：自造畸形轮廓 fuzz 驱动（确定性种子，覆盖 NaN/Inf/±0/极大值/近重合点/
重复点/自相交/多轮廓/CDT/反向轮廓/小 bucket 分配器），在开启
`TESS_CONSTRAINED_DELAUNAY_TRIANGULATION`（CDT）后稳定复现三类问题，根因相同：

### 根因：CDT 边翻转缺少前置条件检查（Source/tess.c `tessMeshRefineDelaunay`）

`tessMeshFlipEdge()`（Source/mesh.c:754）要求：两侧面都是三角形、两面只共享这一条边、
翻转后不退化为零长边。畸形轮廓（大量重合点/自相交）产生的网格不满足这些条件，
直接翻转会破坏网格拓扑：

- **现象 A（debug 构建）**：`tessMeshFlipEdge` 后置断言失败
  `mesh.c:801: Assertion 'a0->Onext->Sym->Lnext == a0' failed` → 进程 abort。
  复现：`output/repro/case1_mesh_corruption.txt`（27 顶点单轮廓，CDT+反向轮廓）。
  日志：`output/logs/prefix_assert_mesh_flip.log`
- **现象 B（release 构建）**：网格被破坏后，`OutputPolymesh` →
  `tessMeshMergeConvexFaces`（polySize=4 时）→ `VertCCW` 解引用空顶点，
  UBSan 报 `geom.c:160: member access within null pointer`；
  另一形态为 `tess.c:738` 访问未初始化内存（0xbebebebe）。
  复现：`output/repro/case2_null_vertex.txt`（13 顶点 3D 轮廓，CDT+polySize=4）。
  日志：`output/logs/prefix_ubsan_null_vertex.log`、`prefix_null_vertex_stacktrace.log`
- **现象 C**：`maxIter = maxFaces * maxFaces`（Source/tess.c:487）有符号 int 溢出
  （50970² 超 INT_MAX），UBSan 报 `signed integer overflow`；溢出后迭代上限为负，
  CDT 循环被静默跳过（行为未定义）。
  复现：`output/repro/case3_signed_overflow.txt`（486 顶点附近重合簇，产生 50970 个面）。
  日志：`output/logs/prefix_ubsan_signed_overflow.log`

## 3. 修复（Source/tess.c，仅两处小改）

1. 新增 `EdgeIsFlippable()`：翻转前检查——两侧均为三角形、两面不共享其他边、
   两侧三角形顶点各自互异、翻转不产生零长边。不满足则跳过该边。
   对正常（非退化）三角化这些条件恒成立，因此**正常输入输出逐顶点不变**。
2. `maxIter` 钳制：`maxFaces > 46340` 时取 `INT_MAX`（46340² < INT_MAX < 46341²），
   消除有符号溢出 UB；正常规模输入行为不变。

## 4. 验证结果

- **Sanitizer 构建可用**：`bazel test --config=asan //...` 通过（20 个用例）。
- **零越界**：
  - 三个最小化复现用例在修复后 debug/release + ASan/UBSan 下均干净退出（exit 0）。
  - 修复后重新进行 ASan+UBSan fuzz（debug 与 release 两种构建，畸形轮廓随机生成），
    无任何 sanitizer 报告，见 `output/logs/fuzz_final_status.log`。
- **正常输出零回归**：
  - 6 组正常轮廓（带孔多边形/星形/凹多边形/CDT/3D/polySize=3/4/连通多边形/边界轮廓），
    修复前后输出（顶点坐标+顶点索引+单元索引）逐一 diff 完全一致：
    `output/logs/golden_prefix.txt` vs `golden_postfix.txt`（`golden_diff.log` 为空）。
  - 新增黄金输出测试 `GoldenVertexOutputPolygonWithHole` / `GoldenVertexOutputStar`
    锁定逐顶点精确输出。
  - 正常构建 `bazel test //...` 与 `-c opt` 全部通过。
- **新增回归测试**（Tests/libtess2_test.cc）：
  - `DegenerateCdtContourDoesNotCorruptMesh`（现象 A 复现轮廓）
  - `DegenerateCdtMergeConvexFacesNullVertex`（现象 B 复现轮廓）
  - 两个黄金输出测试（逐顶点防回归）

## 5. 产物清单（output/）

- `REPORT.md`：本文件
- `repro/`：三个最小化畸形轮廓素材 + `repro.c` 复现工具（stdin 读轮廓）+ `fuzz_tess.c` 模糊测试驱动
- `logs/`：修复前 sanitizer 报告、调用栈、黄金输出与 diff、fuzz 最终状态、三种构建最终测试日志
- `recording/`：分段录屏（asciinema），脚本 `output/record_demo.sh`
