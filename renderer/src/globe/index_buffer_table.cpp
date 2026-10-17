#include <hongtu/renderer/globe/index_buffer_table.hpp>
#include <cstring>

namespace hongtu::renderer {

using namespace HongTu::RHI;

namespace {

constexpr i32 N = kTileGridSize;      // 33
constexpr i32 C = kTileCellsPerSide;  // 32

inline u32 vid(i32 col, i32 row) { return static_cast<u32>(row * N + col); }

// ---------------------------------------------------------------------------
// Emit a standard 2-triangle quad for a single cell (col, row).
// ---------------------------------------------------------------------------
inline void emitQuad(std::vector<u32>& out, i32 col, i32 row) {
  const u32 tl = vid(col,     row);
  const u32 tr = vid(col + 1, row);
  const u32 bl = vid(col,     row + 1);
  const u32 br = vid(col + 1, row + 1);
  out.push_back(tl); out.push_back(bl); out.push_back(tr);
  out.push_back(tr); out.push_back(bl); out.push_back(br);
}

// ---------------------------------------------------------------------------
// Interior cells.
//
// Covers every cell NOT in a coarse-edge strip and NOT in a corner block.
//
// For a coarse left edge (ls==2): skip col=0
// For a coarse right edge (rs==2): skip col=C-1=31
// For a coarse top edge (ts==2): skip row=0
// For a coarse bottom edge (bs==2): skip row=C-1=31
//
// When two coarse edges meet at a corner (e.g. ls==2 && ts==2), the corner
// block covers a 2×2-cell region at (col=0..1, row=0..1).  But col=1 and
// row=1 are already excluded by the individual-edge skip above for col=0 and
// row=0 respectively.  The corner block's "inner" cell at (col=1, row=1) IS
// included in the interior range [1, C-1) × [1, C-1).  We therefore skip it
// here via an explicit corner-cell check.
// ---------------------------------------------------------------------------
void emitInterior(std::vector<u32>& out, i32 ls, i32 rs, i32 ts, i32 bs) {
  const bool cl = (ls == 2), cr = (rs == 2), ct = (ts == 2), cb = (bs == 2);

  const i32 col_lo = cl ? 1 : 0;
  const i32 col_hi = cr ? C - 1 : C;  // exclusive
  const i32 row_lo = ct ? 1 : 0;
  const i32 row_hi = cb ? C - 1 : C;  // exclusive

  // Corner "inner" cells to skip: (1,1), (C-2,1), (1,C-2), (C-2,C-2)
  // These are the cells inside the 2×2 corner block that would otherwise
  // be emitted by the interior loop.
  const bool skip_tl_inner = cl && ct;  // skip cell (1,1)
  const bool skip_tr_inner = cr && ct;  // skip cell (C-2, 1)
  const bool skip_bl_inner = cl && cb;  // skip cell (1, C-2)
  const bool skip_br_inner = cr && cb;  // skip cell (C-2, C-2)

  for (i32 row = row_lo; row < row_hi; ++row) {
    for (i32 col = col_lo; col < col_hi; ++col) {
      // Skip the "inner" corner cell covered by a corner block.
      if (skip_tl_inner && col == 1     && row == 1)     continue;
      if (skip_tr_inner && col == C - 2 && row == 1)     continue;
      if (skip_bl_inner && col == 1     && row == C - 2) continue;
      if (skip_br_inner && col == C - 2 && row == C - 2) continue;
      emitQuad(out, col, row);
    }
  }
}

// ---------------------------------------------------------------------------
// Coarse edge strips.
//
// Each strip covers one cell-column or cell-row adjacent to the tile boundary.
// The strip is divided into pairs of 2 cells (2 rows or 2 cols) to stitch the
// "fine-only" odd edge vertex into the interior without leaving T-junctions.
//
// Strip range [lo, hi) must satisfy: (hi - lo) is even (or zero).
// The strip does NOT cover the corner 2×2-cell blocks; ranges are adjusted
// by the caller to skip those.
// ---------------------------------------------------------------------------

// Left strip: col=0, cell rows [row_lo, row_hi) in steps of 2.
void emitCoarseLeft(std::vector<u32>& out, i32 row_lo, i32 row_hi) {
  for (i32 r = row_lo; r + 2 <= row_hi; r += 2) {
    const u32 e0  = vid(0, r);
    const u32 e2  = vid(0, r + 2);
    const u32 in0 = vid(1, r);
    const u32 in1 = vid(1, r + 1);
    const u32 in2 = vid(1, r + 2);
    out.push_back(e0);  out.push_back(e2);  out.push_back(in1);
    out.push_back(e0);  out.push_back(in1); out.push_back(in0);
    out.push_back(e2);  out.push_back(in2); out.push_back(in1);
  }
}

// Right strip: col=C=32, cell rows [row_lo, row_hi) in steps of 2.
void emitCoarseRight(std::vector<u32>& out, i32 row_lo, i32 row_hi) {
  for (i32 r = row_lo; r + 2 <= row_hi; r += 2) {
    const u32 e0  = vid(N - 1, r);
    const u32 e2  = vid(N - 1, r + 2);
    const u32 in0 = vid(N - 2, r);
    const u32 in1 = vid(N - 2, r + 1);
    const u32 in2 = vid(N - 2, r + 2);
    out.push_back(e0);  out.push_back(in1); out.push_back(e2);
    out.push_back(e0);  out.push_back(in0); out.push_back(in1);
    out.push_back(e2);  out.push_back(in1); out.push_back(in2);
  }
}

// Top strip: row=0, cell cols [col_lo, col_hi) in steps of 2.
void emitCoarseTop(std::vector<u32>& out, i32 col_lo, i32 col_hi) {
  for (i32 c = col_lo; c + 2 <= col_hi; c += 2) {
    const u32 e0  = vid(c,     0);
    const u32 e2  = vid(c + 2, 0);
    const u32 in0 = vid(c,     1);
    const u32 in1 = vid(c + 1, 1);
    const u32 in2 = vid(c + 2, 1);
    out.push_back(e0);  out.push_back(in1); out.push_back(e2);
    out.push_back(e0);  out.push_back(in0); out.push_back(in1);
    out.push_back(e2);  out.push_back(in1); out.push_back(in2);
  }
}

// Bottom strip: row=C=32, cell rows [col_lo, col_hi) in steps of 2.
void emitCoarseBottom(std::vector<u32>& out, i32 col_lo, i32 col_hi) {
  for (i32 c = col_lo; c + 2 <= col_hi; c += 2) {
    const u32 e0  = vid(c,     N - 1);
    const u32 e2  = vid(c + 2, N - 1);
    const u32 in0 = vid(c,     N - 2);
    const u32 in1 = vid(c + 1, N - 2);
    const u32 in2 = vid(c + 2, N - 2);
    out.push_back(e0);  out.push_back(e2);  out.push_back(in1);
    out.push_back(e0);  out.push_back(in1); out.push_back(in0);
    out.push_back(e2);  out.push_back(in2); out.push_back(in1);
  }
}

// ---------------------------------------------------------------------------
// Corner blocks.
//
// Emitted only when BOTH adjacent edges are coarse.  Covers a 2×2-cell block
// (4 cells) using an 8-triangle fan around the hub vertex at (col±1, row±1).
//
// Naming convention for the 3×3 vertex sub-grid:
//   tl tc tr     (top row of sub-grid)
//   ml m  mr     (middle row)
//   bl bm br     (bottom row)
//
// "Fine-only" vertices: those on a coarse tile boundary edge at odd positions.
//   For top-left corner: tc=vid(1,0) on top edge, ml=vid(0,1) on left edge.
// These must not appear as endpoints of a tile-perimeter edge in the mesh;
// i.e. they must only be shared between interior-facing triangle edges.
// ---------------------------------------------------------------------------

// Top-left corner: ls==2 AND ts==2
// Sub-grid: cols 0..2, rows 0..2.  Hub: m=vid(1,1).
// Covered cells: (0,0), (1,0), (0,1), (1,1).
void emitCornerTopLeft(std::vector<u32>& out) {
  const u32 tl = vid(0, 0);
  const u32 tc = vid(1, 0);  // fine-only top edge vertex
  const u32 tr = vid(2, 0);
  const u32 ml = vid(0, 1);  // fine-only left edge vertex
  const u32 m  = vid(1, 1);  // hub
  const u32 mr = vid(2, 1);
  const u32 bl = vid(0, 2);
  const u32 bm = vid(1, 2);
  const u32 br = vid(2, 2);

  // Tile-boundary edges in this block:
  //   Left edge:  tl-ml (✓ boundary, appears once) and ml-bl (✓ boundary, appears once)
  //   Top edge:   tl-tc (✓ boundary, appears once) and tc-tr (✓ boundary, appears once)
  //
  // All 8 triangles share hub m.  tc and ml each appear as a non-perimeter
  // vertex (only in edges that are shared between two triangles or connect
  // to interior vertices).
  out.push_back(tl); out.push_back(ml); out.push_back(m);   // T1: left-boundary tl-ml
  out.push_back(tl); out.push_back(m);  out.push_back(tc);  // T2: top-boundary tl-tc
  out.push_back(tc); out.push_back(m);  out.push_back(mr);  // T3
  out.push_back(tc); out.push_back(mr); out.push_back(tr);  // T4: top-boundary tc-tr
  out.push_back(ml); out.push_back(bl); out.push_back(m);   // T5: left-boundary ml-bl
  out.push_back(bl); out.push_back(bm); out.push_back(m);   // T6
  out.push_back(bm); out.push_back(br); out.push_back(m);   // T7
  out.push_back(br); out.push_back(mr); out.push_back(m);   // T8
}

// Top-right corner: rs==2 AND ts==2
// Sub-grid: cols N-3..N-1 (30..32), rows 0..2.  Hub: m=vid(N-2,1).
// Covered cells: (C-2,0), (C-1,0), (C-2,1), (C-1,1).
// Fine-only verts: tc=vid(N-2,0) on top edge, mr=vid(N-1,1) on right edge.
void emitCornerTopRight(std::vector<u32>& out) {
  const u32 tl = vid(N - 3, 0);
  const u32 tc = vid(N - 2, 0);  // fine-only top edge vertex
  const u32 tr = vid(N - 1, 0);
  const u32 ml = vid(N - 3, 1);
  const u32 m  = vid(N - 2, 1);  // hub
  const u32 mr = vid(N - 1, 1);  // fine-only right edge vertex
  const u32 bl = vid(N - 3, 2);
  const u32 bm = vid(N - 2, 2);
  const u32 br = vid(N - 1, 2);

  // Tile-boundary edges: top tc-tr, tr-mr, top tl-tc
  // (Note: tl-tc is top boundary, tc-tr is top boundary, tr-mr is right boundary)
  out.push_back(tr); out.push_back(m);  out.push_back(mr);  // T1: right-boundary tr-mr
  out.push_back(tr); out.push_back(tc); out.push_back(m);   // T2: top-boundary tc-tr (reversed)
  out.push_back(tc); out.push_back(ml); out.push_back(m);   // T3
  out.push_back(tc); out.push_back(tl); out.push_back(ml);  // T4: top-boundary tl-tc
  out.push_back(mr); out.push_back(m);  out.push_back(br);  // T5: right-boundary mr-br
  out.push_back(br); out.push_back(m);  out.push_back(bm);  // T6
  out.push_back(bm); out.push_back(m);  out.push_back(bl);  // T7
  out.push_back(bl); out.push_back(m);  out.push_back(ml);  // T8
}

// Bottom-left corner: ls==2 AND bs==2
// Sub-grid: cols 0..2, rows N-3..N-1 (30..32).  Hub: m=vid(1,N-2).
// Covered cells: (0,C-2), (1,C-2), (0,C-1), (1,C-1).
// Fine-only verts: ml=vid(0,N-2) on left edge, bc=vid(1,N-1) on bottom edge.
void emitCornerBottomLeft(std::vector<u32>& out) {
  const u32 tl = vid(0, N - 3);
  const u32 tm = vid(1, N - 3);
  const u32 tr = vid(2, N - 3);
  const u32 ml = vid(0, N - 2);  // fine-only left edge vertex
  const u32 m  = vid(1, N - 2);  // hub
  const u32 mr = vid(2, N - 2);
  const u32 bl = vid(0, N - 1);
  const u32 bc = vid(1, N - 1);  // fine-only bottom edge vertex
  const u32 br = vid(2, N - 1);

  out.push_back(bl); out.push_back(bc); out.push_back(m);   // T1: bottom-boundary bl-bc
  out.push_back(bl); out.push_back(m);  out.push_back(ml);  // T2: left-boundary ml-bl (reversed)
  out.push_back(bc); out.push_back(br); out.push_back(m);   // T3: bottom-boundary bc-br
  out.push_back(ml); out.push_back(m);  out.push_back(tl);  // T4: left-boundary tl-ml (reversed)
  out.push_back(br); out.push_back(mr); out.push_back(m);   // T5
  out.push_back(tl); out.push_back(m);  out.push_back(tm);  // T6
  out.push_back(tm); out.push_back(m);  out.push_back(tr);  // T7
  out.push_back(tr); out.push_back(m);  out.push_back(mr);  // T8
}

// Bottom-right corner: rs==2 AND bs==2
// Sub-grid: cols N-3..N-1, rows N-3..N-1 (30..32 × 30..32).  Hub: m=vid(N-2,N-2).
// Covered cells: (C-2,C-2), (C-1,C-2), (C-2,C-1), (C-1,C-1).
// Fine-only verts: mr=vid(N-1,N-2) on right edge, bc=vid(N-2,N-1) on bottom edge.
void emitCornerBottomRight(std::vector<u32>& out) {
  const u32 tl = vid(N - 3, N - 3);
  const u32 tm = vid(N - 2, N - 3);
  const u32 tr = vid(N - 1, N - 3);
  const u32 ml = vid(N - 3, N - 2);
  const u32 m  = vid(N - 2, N - 2);  // hub
  const u32 mr = vid(N - 1, N - 2);  // fine-only right edge vertex
  const u32 bl = vid(N - 3, N - 1);
  const u32 bc = vid(N - 2, N - 1);  // fine-only bottom edge vertex
  const u32 br = vid(N - 1, N - 1);

  out.push_back(br); out.push_back(mr); out.push_back(m);   // T1: right-boundary mr-br (reversed)
  out.push_back(br); out.push_back(m);  out.push_back(bc);  // T2: bottom-boundary bc-br (reversed)
  out.push_back(mr); out.push_back(tr); out.push_back(m);   // T3: right-boundary tr-mr (reversed)
  out.push_back(bc); out.push_back(m);  out.push_back(bl);  // T4: bottom-boundary bl-bc (reversed)
  out.push_back(tr); out.push_back(tm); out.push_back(m);   // T5
  out.push_back(tm); out.push_back(tl); out.push_back(m);   // T6
  out.push_back(tl); out.push_back(ml); out.push_back(m);   // T7
  out.push_back(ml); out.push_back(bl); out.push_back(m);   // T8
}

}  // namespace

// ---------------------------------------------------------------------------
// Main entry point
//
// Partition of the 32×32 cell grid (for ls=2, rs=2, ts=2, bs=2 example):
//
//   Interior:  cols [2, 30) × rows [2, 30)  = 28×28 cells
//              (plus partial rows/cols not covered by corners or strips)
//   Corners:   4 × (2×2 cells) = 16 cells
//   Left strip:  col=0, rows [2, 30) = 14 pairs = 28 cells
//   Right strip: col=31, rows [2, 30) = 28 cells
//   Top strip:   row=0, cols [2, 30) = 28 cells
//   Bottom strip: row=31, cols [2, 30) = 28 cells
//   Total: 28×28 + 16 + 28×4 = 784 + 16 + 112 = 912... wait
//
//   Actually: interior covers cols [1,31) × rows [1,31) minus 4 corner inner
//   cells = 30×30 - 4 = 896 cells.  But col=0 and col=31 and row=0 and row=31
//   are excluded by the [1,31) range.  Strips: 4×14×2=112 cells.
//   Corners: 4×4=16 cells.  Interior inner: 30×30-4 = 896.
//   Total: 896+112+16 = 1024 = 32×32 ✓
// ---------------------------------------------------------------------------
std::vector<u32> generateStitchingIndices(i32 ls, i32 rs, i32 ts, i32 bs) {
  std::vector<u32> out;
  out.reserve(C * C * 6 + 256);

  const bool cl = (ls == 2), cr = (rs == 2), ct = (ts == 2), cb = (bs == 2);

  const bool corner_tl = cl && ct;
  const bool corner_tr = cr && ct;
  const bool corner_bl = cl && cb;
  const bool corner_br = cr && cb;

  // 1. Interior cells (rectangular grid minus edge strips, minus inner corner cells).
  emitInterior(out, ls, rs, ts, bs);

  // 2. Corner 2×2-cell blocks.
  if (corner_tl) emitCornerTopLeft(out);
  if (corner_tr) emitCornerTopRight(out);
  if (corner_bl) emitCornerBottomLeft(out);
  if (corner_br) emitCornerBottomRight(out);

  // 3. Coarse edge strips.
  //
  //    The strip range for each edge excludes the 2-row/col region at corners.
  //    When a corner block is active, the strip start/end is adjusted by 2
  //    to avoid the 2-row/col block already covered by the corner.
  //
  //    Left strip row range: [row_lo, row_hi)
  //      row_lo = 2 if corner_tl (corner covers rows 0..2)
  //      row_hi = C-2 if corner_bl (corner covers rows C-2..C)
  //      otherwise row_lo=0, row_hi=C
  if (cl) {
    const i32 row_lo = corner_tl ? 2 : 0;
    const i32 row_hi = corner_bl ? C - 2 : C;
    emitCoarseLeft(out, row_lo, row_hi);
  }

  if (cr) {
    const i32 row_lo = corner_tr ? 2 : 0;
    const i32 row_hi = corner_br ? C - 2 : C;
    emitCoarseRight(out, row_lo, row_hi);
  }

  if (ct) {
    const i32 col_lo = corner_tl ? 2 : 0;
    const i32 col_hi = corner_tr ? C - 2 : C;
    emitCoarseTop(out, col_lo, col_hi);
  }

  if (cb) {
    const i32 col_lo = corner_bl ? 2 : 0;
    const i32 col_hi = corner_br ? C - 2 : C;
    emitCoarseBottom(out, col_lo, col_hi);
  }

  return out;
}

bool IndexBufferTable::initialize(RHIDevice* device) {
  for (i32 ls = 1; ls <= 2; ++ls)
  for (i32 rs = 1; rs <= 2; ++rs)
  for (i32 ts = 1; ts <= 2; ++ts)
  for (i32 bs = 1; bs <= 2; ++bs) {
    const i32 idx = tableIndex(ls, rs, ts, bs);
    auto indices = generateStitchingIndices(ls, rs, ts, bs);

    BufferDesc desc;
    desc.size  = indices.size() * sizeof(u32);
    desc.usage = static_cast<BufferUsageFlags>(BufferUsageFlagBits::Index);
    desc.memoryProperties = MemoryPropertyFlagBits::HostVisible |
                            MemoryPropertyFlagBits::HostCoherent;
    auto buf = device->createBuffer(desc);
    if (!buf) return false;
    void* p = buf->map();
    if (!p) return false;
    std::memcpy(p, indices.data(), desc.size);
    buf->unmap();
    buffers_[static_cast<u32>(idx)] = buf;
    index_counts_[static_cast<u32>(idx)] = static_cast<u32>(indices.size());
  }
  return true;
}

}  // namespace hongtu::renderer
