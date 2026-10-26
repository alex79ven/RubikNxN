#include "solver/pruning.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>

#include "solver/symmetry.h"

namespace cube::solver {

namespace {

constexpr Distance UNVISITED = 0xFF;

// backward BFS over the (main coord, slice coord) product space
// applies each legal move via the two move tables and marks newly-reached pairs at depth+1
template <typename Table, size_t MainCount, size_t SliceCount, typename MainMoveTable, typename SliceMoveTable>
std::unique_ptr<Table> build_pair_table(
    Coord goal_main, Coord goal_slice,
    const MainMoveTable& main_moves,
    const SliceMoveTable& slice_moves,
    int legal_moves)
{
    auto t = std::make_unique<Table>();
    for (auto& row : *t) row.fill(UNVISITED);

    (*t)[goal_main][goal_slice] = 0;

    size_t total = MainCount * SliceCount;
    size_t filled = 1;
    Distance depth = 0;
    while (filled < total) {
        bool any_new = false;
        for (size_t mi = 0; mi < MainCount; ++mi) {
            for (size_t si = 0; si < SliceCount; ++si) {
                if ((*t)[mi][si] != depth) continue;
                for (int m = 0; m < legal_moves; ++m) {
                    Coord nm = main_moves[mi][m];
                    Coord ns = slice_moves[si][m];
                    if ((*t)[nm][ns] == UNVISITED) {
                        (*t)[nm][ns] = depth + 1;
                        ++filled;
                        any_new = true;
                    }
                }
            }
        }
        if (!any_new) break;
        ++depth;
    }
    return t;
}

std::unique_ptr<CornerOriSliceTable> build_corner_ori_slice() {
    return build_pair_table<CornerOriSliceTable, CORNER_ORI_COORDS, SLICE_COORDS>(
        0, SLICE_GOAL,
        corner_ori_move_table(),
        slice_move_table(),
        NUM_MOVES);
}

std::unique_ptr<EdgeOriSliceTable> build_edge_ori_slice() {
    return build_pair_table<EdgeOriSliceTable, EDGE_ORI_COORDS, SLICE_COORDS>(
        0, SLICE_GOAL,
        edge_ori_move_table(),
        slice_move_table(),
        NUM_MOVES);
}

std::unique_ptr<CornerPermSlicePermTable> build_corner_perm_slice_perm() {
    return build_pair_table<
        CornerPermSlicePermTable, CORNER_PERM_COORDS, SLICE_PERM_COORDS>(
        0, 0,
        corner_perm_move_table(),
        slice_perm_move_table(),
        NUM_G1_MOVES);
}

std::unique_ptr<EdgePermSlicePermTable> build_edge_perm_slice_perm() {
    return build_pair_table<
        EdgePermSlicePermTable, EDGE_PERM_UD_COORDS, SLICE_PERM_COORDS>(
        0, 0,
        edge_perm_ud_move_table(),
        slice_perm_move_table(),
        NUM_G1_MOVES);
}

}  // namespace

const CornerOriSliceTable& corner_ori_slice_pruning() {
    static const auto table = build_corner_ori_slice();
    return *table;
}

const EdgeOriSliceTable& edge_ori_slice_pruning() {
    static const auto table = build_edge_ori_slice();
    return *table;
}

const CornerPermSlicePermTable& corner_perm_slice_perm_pruning() {
    static const auto table = build_corner_perm_slice_perm();
    return *table;
}

const EdgePermSlicePermTable& edge_perm_slice_perm_pruning() {
    static const auto table = build_edge_perm_slice_perm();
    return *table;
}

namespace {

// canonical representation of a (main, sp) pair under the 8-element phase-2 symmetry group
// is the pair with the smallest key across all 8 symmetric variants; using
// (main * SLICE_PERM_COORDS + sp) as ordering key
template <size_t MainCount>
uint64_t canonical_key(Coord main, Coord sp,
                       const std::array<std::array<Coord, MainCount>, NUM_SYM>& sym_main,
                       const SymSlicePermTable& sym_sp)
{
    uint64_t best = std::numeric_limits<uint64_t>::max();
    for (int s = 0; s < NUM_SYM; ++s) {
        Coord m = sym_main[s][main];
        Coord p = sym_sp[s][sp];
        uint64_t key = uint64_t(m) * SLICE_PERM_COORDS + p;
        if (key < best) best = key;
    }
    return best;
}

// build the compressed pruning by walking every raw (main, sp) pair, computing
// its canonical rep, assigning class ids in order of first sighting, and
// looking up the distance in the raw table
template <typename SymTable, size_t MainCount, typename RawTable, typename SymMainTable>
std::unique_ptr<SymTable> build_sym_pruning(
    const RawTable& raw,
    const SymMainTable& sym_main,
    const SymSlicePermTable& sym_sp)
{
    auto out = std::make_unique<SymTable>();
    std::vector<uint64_t> canon_keys;
    canon_keys.reserve(MainCount * SLICE_PERM_COORDS / NUM_SYM);
    std::vector<std::vector<std::pair<uint64_t, SymClassIndex>>> buckets(MainCount);
    auto find_or_add = [&](uint64_t key, Distance d) -> SymClassIndex {
        // bucket by the canonical main coord (key / SLICE_PERM_COORDS)
        size_t bucket = key / SLICE_PERM_COORDS;
        auto& b = buckets[bucket];
        for (auto& kv : b) if (kv.first == key) return kv.second;
        SymClassIndex id = static_cast<SymClassIndex>(canon_keys.size());
        canon_keys.push_back(key);
        out->dist.push_back(d);
        b.push_back({key, id});
        return id;
    };

    for (size_t mi = 0; mi < MainCount; ++mi) {
        for (size_t si = 0; si < SLICE_PERM_COORDS; ++si) {
            uint64_t key = canonical_key<MainCount>(
                static_cast<Coord>(mi), static_cast<Coord>(si), sym_main, sym_sp);
            Distance d = raw[mi][si];
            SymClassIndex id = find_or_add(key, d);
            out->class_of[mi][si] = id;
        }
    }
    return out;
}

std::unique_ptr<SymPhase2CornerPruning> build_sym_corner_perm_slice_perm() {
    return build_sym_pruning<SymPhase2CornerPruning, CORNER_PERM_COORDS>(
        corner_perm_slice_perm_pruning(),
        sym_corner_perm(),
        sym_slice_perm());
}

std::unique_ptr<SymPhase2EdgePruning> build_sym_edge_perm_slice_perm() {
    return build_sym_pruning<SymPhase2EdgePruning, EDGE_PERM_UD_COORDS>(
        edge_perm_slice_perm_pruning(),
        sym_edge_perm_ud(),
        sym_slice_perm());
}

}  // namespace

const SymPhase2CornerPruning& sym_corner_perm_slice_perm_pruning() {
    static const auto t = build_sym_corner_perm_slice_perm();
    return *t;
}
const SymPhase2EdgePruning& sym_edge_perm_slice_perm_pruning() {
    static const auto t = build_sym_edge_perm_slice_perm();
    return *t;
}

}
