#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <vector>

#include "gtest/gtest.h"
#include "tesselator.h"
#include "tessio.h"

namespace {

constexpr int kComponentCount = 2;
constexpr int kNumTriangleVertices = 3;

struct Vector2f {
  float x;
  float y;
};

void* HeapAlloc(void* user_data, unsigned int size) { return malloc(size); }
void* HeapRealloc(void* user_data, void* ptr, unsigned int size) {
  return realloc(ptr, size);
}
void HeapFree(void* user_data, void* ptr) { free(ptr); }

// Adds a polygon with a hole to the tessellator.
//
// a = outer loop
// b = inner loop
// x = interior
// o = exterior
//
// +aaaaaaaaaaaaaa+
// a xx | xx | xx a
// a xx | xx | xx a
// a----+bbbb+----a
// a xx b oo b xx a
// a xx b oo b xx a
// a----+bbbb+----a
// a xx | xx | xx a
// a xx | xx | xx a
// +aaaaaaaaaaaaaa+
//
// This should tessellate to 8 triangles.
void AddPolygonWithHole(TESStesselator* tess) {
  std::vector<Vector2f> outer_loop = {
      {0, 0},
      {3, 0},
      {3, 3},
      {0, 3},
  };
  std::vector<Vector2f> inner_hole = {
      {1, 1},
      {2, 1},
      {2, 2},
      {1, 2},
  };

  tessSetOption(tess, TESS_REVERSE_CONTOURS, 0);
  tessAddContour(tess, kComponentCount, outer_loop.data(),
                 kComponentCount * sizeof(TESSreal), outer_loop.size());
  tessSetOption(tess, TESS_REVERSE_CONTOURS, 1);
  tessAddContour(tess, kComponentCount, inner_hole.data(),
                 kComponentCount * sizeof(TESSreal), inner_hole.size());
}

void AddPolyline(TESStesselator* tess, const std::vector<Vector2f>& polyline) {
  tessAddContour(tess, kComponentCount, polyline.data(),
                 kComponentCount * sizeof(TESSreal), polyline.size());
}

class Libtess2Test : public testing::Test {
 protected:
  Libtess2Test(TESSalloc* alloc) { tess = tessNewTess(alloc); }

  Libtess2Test() : Libtess2Test(nullptr) {}

  ~Libtess2Test() {
    if (tess != nullptr) {
      tessDeleteTess(tess);
    }
  }

  void SetUp() override { ASSERT_NE(tess, nullptr); }

  TESStesselator* tess;
};

static TESSalloc custom_alloc{
    HeapAlloc,
    HeapRealloc,
    HeapFree,
    /*user_data=*/nullptr,
    /*meshEdgeBucketSize=*/512,
    /*meshVertexBucketSize=*/512,
    /*meshFaceBucketSize=*/256,
    /*dictNodeBucketSize=*/512,
    /*regionBucketSize=*/256,
    /*extraVertices=*/0,
};

class Libtess2CustomAllocTest : public Libtess2Test {
 protected:
  Libtess2CustomAllocTest() : Libtess2Test(&custom_alloc) {}
};

// Tests that tessellation succeeds when the default allocator is used.
TEST_F(Libtess2Test, DefaultAllocSuccess) {
  // Add the polygon and tessellate it.
  AddPolygonWithHole(tess);
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);

  // It should take 8 triangles to represent the polygon with the hole.
  EXPECT_EQ(tessGetElementCount(tess), 8);
}

TEST_F(Libtess2CustomAllocTest, CustomAllocSuccess) {
  // Add the polygon and tessellate it.
  AddPolygonWithHole(tess);
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);

  // It should take 8 triangles to represent the polygon with the hole.
  EXPECT_EQ(tessGetElementCount(tess), 8);
}

TEST_F(Libtess2Test, EmptyPolyline) {
  AddPolyline(tess, {});
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 0);
}

TEST_F(Libtess2Test, SingleLine) {
  AddPolyline(tess, {{0, 0}, {0, 1}});
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 0);
}

TEST_F(Libtess2Test, SingleVertexContour) {
  // A one-vertex contour used to create a degenerate self-loop edge that
  // crashed the sweep; it must be ignored instead.
  AddPolyline(tess, {{1, 1}});
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 0);
}

TEST_F(Libtess2Test, SingleTriangle) {
  AddPolyline(tess, {{0, 0}, {0, 1}, {1, 0}});
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 1);
}

TEST_F(Libtess2Test, UnitQuad) {
  AddPolyline(tess, {{0, 0}, {0, 1}, {1, 1}, {1, 0}});
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 2);
}

TEST_F(Libtess2Test, GetStatusInvalidInput) {
  AddPolyline(tess, {{-2e+37f, 0.f}, {0, 5}, {1e37f, -5}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetStatus(tess), TESS_STATUS_INVALID_INPUT);
}

TEST_F(Libtess2Test, GetStatusOk) {
  AddPolyline(tess, {{0, 0}, {0, 1}, {1, 1}, {1, 0}});
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetStatus(tess), TESS_STATUS_OK);
}

TEST_F(Libtess2Test, FloatOverflowQuad) {
  constexpr float kFloatMin = std::numeric_limits<float>::min();
  constexpr float kFloatMax = std::numeric_limits<float>::max();

  // Add the polygon and tessellate it.
  AddPolyline(tess, {{kFloatMin, kFloatMin},
                     {kFloatMin, kFloatMax},
                     {kFloatMax, kFloatMax},
                     {kFloatMax, kFloatMin}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
}

TEST_F(Libtess2Test, SingularityQuad) {
  AddPolyline(tess, {{0, 0}, {0, 0}, {0, 0}, {0, 0}});
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 0);
}

TEST_F(Libtess2Test, DegenerateQuad) {
  // A quad that's extremely close to a giant triangle, with an extra sliver.
  // Caused a segfault previously.
  AddPolyline(tess, {{0.f, 3.40282347e+38f},
                     {0.64113313f, -1.f},
                     {-0.f, -0.f},
                     {-3.40282347e+38f, 1.f}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
}

TEST_F(Libtess2Test, WidthOverflowsTri) {
  AddPolyline(tess, {{-2e+38f, 0}, {0, 0}, {2e+38f, -1}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
}

TEST_F(Libtess2Test, HeightOverflowsTri) {
  AddPolyline(tess, {{0, 0}, {0, 2e+38f}, {-1, -2e+38f}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
}

TEST_F(Libtess2Test, AreaOverflowsTri) {
  AddPolyline(tess, {{-2e+37f, 0.f}, {0, 5}, {1e37f, -5}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
}

TEST_F(Libtess2Test, NanQuad) {
  AddPolyline(tess, {{std::nanf(""), std::nanf("")},
                     {std::nanf(""), std::nanf("")},
                     {std::nanf(""), std::nanf("")},
                     {std::nanf(""), std::nanf("")}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 0);
}

TEST_F(Libtess2Test, AvoidsCrashWhileFindingIntersection) {
  // Previously, this failed an assert while finding an intersection because
  // that fell back to taking a midpoint between two coordinates in a way that
  // could get the wrong answer because of the sum overflowing max float.
  AddPolyline(tess, {{-1.f, 0.f},
                     {0.868218958f, 0.f},
                     {0.902460039f, 0.0649746507f},
                     {-0.f, 0.854620099f},
                     {-1.f, 0.784999669f},
                     {0.f, 0.f},
                     {-1.f, 1.f},
                     {1.f, 1.f},
                     {0.f, -1.f},
                     {3.40282347e+38f, 3.40282347e+38f},
                     {-1.f, -1.f},
                     {-0.f, 0.442898333f},
                     {0.33078745f, -0.f},
                     {-0.f, 1.f},
                     {-1.f, 0.f},
                     {1.f, -0.f},
                     {0.f, 0.186138511f},
                     {0.212649569f, 0.886535764f},
                     {1.f, 0.34795785f},
                     {0.f, 0.788870096f},
                     {0.853441715f, -1.f},
                     {-1.f, 1.f},
                     {1.f, -0.994903505f},
                     {1.f, 0.105880626f},
                     {3.40282347e+38f, 3.40282347e+38f},
                     {-1.f, 3.40282347e+38f},
                     {-0.f, 0.34419331f},
                     {1.f, 1.f}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
}

TEST_F(Libtess2Test, AvoidsCrashInAddRightEdges) {
  AddPolyline(tess, {{{-0.5f, 1.f},
                      {3.40282347e+38f, 0.f},
                      {0.349171013f, 1.f},
                      {1.f, 0.f},
                      {1.f, -0.f},
                      {0.594775498f, -0.f},
                      {0.f, -0.f},
                      {-0.f, 1.f},
                      {0.f, 1.f},
                      {2.20929384f, 1.f},
                      {1.f, 1.f},
                      {-0.f, -0.f},
                      {3.40282347e+38f, -0.f},
                      {-1.f, 0.f},
                      {1.70141173e+38f, 0.391036272f},
                      {3.40282347e+38f, 0.371295959f},
                      {3.40282347e+38f, -0.f},
                      {0.f, 0.234747186f},
                      {-1.f, 1.f},
                      {-1.f, -0.f},
                      {3.40282347e+38f, 1.f},
                      {-0.f, -0.f},
                      {3.40282347e+38f, 1.f},
                      {0.434241712f, 0.f},
                      {1.f, 0.211511821f},
                      {3.40282347e+38f, 1.f}}});
  EXPECT_EQ(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
}

}  // namespace

namespace {

// Builds a contour container buffer (see Include/tessio.h for the format).
class ContainerBuilder {
 public:
  ContainerBuilder() {
    AppendU32(TESS_IO_MAGIC);
    AppendU16(TESS_IO_VERSION);
    AppendU16(0);
    count_offset_ = buf_.size();
    AppendU32(0);
  }

  void AddRing(std::initializer_list<Vector2f> verts, bool hole = false) {
    AppendU32(static_cast<uint32_t>(verts.size()));
    AppendU32(hole ? TESS_IO_FLAG_HOLE : 0u);
    for (const Vector2f& v : verts) {
      AppendF32(v.x);
      AppendF32(v.y);
    }
    contour_count_++;
    memcpy(buf_.data() + count_offset_, &contour_count_, 4);
  }

  const std::vector<uint8_t>& buf() const { return buf_; }
  std::vector<uint8_t>& mut_buf() { return buf_; }

 private:
  void AppendU16(uint16_t v) {
    buf_.push_back(static_cast<uint8_t>(v));
    buf_.push_back(static_cast<uint8_t>(v >> 8));
  }
  void AppendU32(uint32_t v) {
    for (int i = 0; i < 4; i++) buf_.push_back(static_cast<uint8_t>(v >> (8 * i)));
  }
  void AppendF32(float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    AppendU32(bits);
  }

  std::vector<uint8_t> buf_;
  size_t count_offset_ = 0;
  uint32_t contour_count_ = 0;
};

}  // namespace

TEST_F(Libtess2Test, ImportValidPolygonWithHole) {
  ContainerBuilder b;
  b.AddRing({{0, 0}, {3, 0}, {3, 3}, {0, 3}});
  b.AddRing({{1, 1}, {2, 1}, {2, 2}, {1, 2}}, /*hole=*/true);
  EXPECT_EQ(tessImportContours(tess, b.buf().data(), b.buf().size()),
            TESS_PARSE_OK);
  EXPECT_NE(tessTesselate(tess, TESS_WINDING_POSITIVE, TESS_POLYGONS,
                          kNumTriangleVertices, kComponentCount, nullptr),
            0);
  EXPECT_EQ(tessGetElementCount(tess), 8);
}

TEST_F(Libtess2Test, ImportTruncated) {
  ContainerBuilder b;
  b.AddRing({{0, 0}, {3, 0}, {3, 3}, {0, 3}});
  // Cut the buffer mid-vertex.
  EXPECT_EQ(tessImportContours(tess, b.buf().data(), b.buf().size() - 5),
            TESS_PARSE_ERR_TRUNCATED);
  // Header alone is also incomplete.
  EXPECT_EQ(tessImportContours(tess, b.buf().data(), 4),
            TESS_PARSE_ERR_TRUNCATED);
}

TEST_F(Libtess2Test, ImportCoordOutOfRange) {
  ContainerBuilder b;
  b.AddRing({{0, 0}, {1e30f, 0}, {0, 3}});
  EXPECT_EQ(tessImportContours(tess, b.buf().data(), b.buf().size()),
            TESS_PARSE_ERR_COORD_RANGE);

  ContainerBuilder nan;
  nan.AddRing({{0, 0}, {std::nanf(""), 0}, {0, 3}});
  EXPECT_EQ(tessImportContours(tess, nan.buf().data(), nan.buf().size()),
            TESS_PARSE_ERR_COORD_RANGE);
}

TEST_F(Libtess2Test, ImportNestingAnomaly) {
  // Orphan hole: no preceding outer ring.
  ContainerBuilder orphan;
  orphan.AddRing({{1, 1}, {2, 1}, {2, 2}, {1, 2}}, /*hole=*/true);
  EXPECT_EQ(tessImportContours(tess, orphan.buf().data(), orphan.buf().size()),
            TESS_PARSE_ERR_NESTING);

  // Hole outside its parent outer ring.
  ContainerBuilder outside;
  outside.AddRing({{0, 0}, {3, 0}, {3, 3}, {0, 3}});
  outside.AddRing({{10, 10}, {20, 10}, {20, 20}, {10, 20}}, /*hole=*/true);
  EXPECT_EQ(tessImportContours(tess, outside.buf().data(), outside.buf().size()),
            TESS_PARSE_ERR_NESTING);

  // Hole nested inside another hole.
  ContainerBuilder nested;
  nested.AddRing({{0, 0}, {10, 0}, {10, 10}, {0, 10}});
  nested.AddRing({{1, 1}, {9, 1}, {9, 9}, {1, 9}}, /*hole=*/true);
  nested.AddRing({{2, 2}, {3, 2}, {3, 3}, {2, 3}}, /*hole=*/true);
  EXPECT_EQ(tessImportContours(tess, nested.buf().data(), nested.buf().size()),
            TESS_PARSE_ERR_NESTING);
}

TEST_F(Libtess2Test, ImportBadMagicAndVersion) {
  ContainerBuilder b;
  b.AddRing({{0, 0}, {3, 0}, {3, 3}, {0, 3}});
  std::vector<uint8_t> bad_magic = b.buf();
  bad_magic[0] ^= 0xff;
  EXPECT_EQ(tessImportContours(tess, bad_magic.data(), bad_magic.size()),
            TESS_PARSE_ERR_BAD_MAGIC);
  std::vector<uint8_t> bad_version = b.buf();
  bad_version[4] = 99;
  EXPECT_EQ(tessImportContours(tess, bad_version.data(), bad_version.size()),
            TESS_PARSE_ERR_UNSUPPORTED_VERSION);
}
