#include "solver/ida.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <thread>
#include <vector>

#include "solver/coords.h"
#include "solver/move_tables.h"
#include "solver/pruning.h"

namespace cube::solver {

namespace {

constexpr int MAX_DEPTH_PHASE1 = 25;
constexpr int MAX_DEPTH_PHASE2 = 25;
constexpr int SEARCH_FAILED = std::numeric_limits<int>::max();

#ifdef ENABLE_NODE_COUNTS
thread_local uint64_t tls_phase1_nodes = 0;
thread_local uint64_t tls_phase2_nodes = 0;

struct CounterRegistry {
    std::mutex mu;
    std::vector<uint64_t*> phase1;
    std::vector<uint64_t*> phase2;
};

CounterRegistry& registry() {
    static CounterRegistry r;
    return r;
}

inline void ensure_registered() {
    thread_local bool registered = false;
    if (!registered) {
        auto& r = registry();
        std::lock_guard<std::mutex> lk(r.mu);
        r.phase1.push_back(&tls_phase1_nodes);
        r.phase2.push_back(&tls_phase2_nodes);
        registered = true;
    }
}

#define COUNT_PHASE1_NODE() do { ensure_registered(); ++tls_phase1_nodes; } while (0)
#define COUNT_PHASE2_NODE() do { ensure_registered(); ++tls_phase2_nodes; } while (0)
#else
#define COUNT_PHASE1_NODE() do {} while (0)
#define COUNT_PHASE2_NODE() do {} while (0)
#endif

inline int face_of(int move_index) { return move_index / 3; }

// pick one canonical order per pair to prune equivalent branches, U/D, R/L, F/B are parallel pairs
inline bool same_axis(int f1, int f2) { return (f1 / 2) == (f2 / 2); }

struct Phase1Node {
    Coord co, eo, sl;
};

// bundle each phase's tables into a Ctx so the DFS reads them via one
// stashed reference instead of hitting five static-local getters per node
struct Phase1Ctx {
    const CornerOriTable& co_moves;
    const EdgeOriTable& eo_moves;
    const SliceTable& sl_moves;
    const CornerOriSliceTable& co_sl_prune;
    const EdgeOriSliceTable& eo_sl_prune;
};

inline int phase1_h(const Phase1Ctx& c, Phase1Node n) {
    return std::max<int>(c.co_sl_prune[n.co][n.sl], c.eo_sl_prune[n.eo][n.sl]);
}

inline bool phase1_is_goal(Phase1Node n) {
    return n.co == 0 && n.eo == 0 && n.sl == SLICE_GOAL;
}

inline Phase1Node phase1_apply(const Phase1Ctx& c, Phase1Node n, int m) {
    return {
        c.co_moves[n.co][m],
        c.eo_moves[n.eo][m],
        c.sl_moves[n.sl][m],
    };
}

// phase 2 state packed into one uint64_t so it fits in a single register through
// the DFS recursion. layout: cp:16 | ep:16 | sp:5 | unused:27
using Phase2Node = uint64_t;

inline Phase2Node pack_phase2(Coord cp, Coord ep, Coord sp) {
    return uint64_t(cp) | (uint64_t(ep) << 16) | (uint64_t(sp) << 32);
}
inline Coord phase2_cp(Phase2Node n) { return Coord(n & 0xFFFF); }
inline Coord phase2_ep(Phase2Node n) { return Coord((n >> 16) & 0xFFFF); }
inline Coord phase2_sp(Phase2Node n) { return Coord((n >> 32) & 0x1F); }

struct Phase2Ctx {
    const CornerPermTable& cp_moves;
    const EdgePermUDTable& ep_moves;
    const SlicePermTable& sp_moves;
    const CornerPermSlicePermTable& cp_sp_prune;
    const EdgePermSlicePermTable& ep_sp_prune;
};

inline int phase2_h(const Phase2Ctx& c, Phase2Node n) {
    Coord sp = phase2_sp(n);
    return std::max<int>(c.cp_sp_prune[phase2_cp(n)][sp], c.ep_sp_prune[phase2_ep(n)][sp]);
}

inline bool phase2_is_goal(Phase2Node n) {
    return n == 0;
}

inline Phase2Node phase2_apply(const Phase2Ctx& c, Phase2Node n, int i) {
    return pack_phase2(
        c.cp_moves[phase2_cp(n)][i],
        c.ep_moves[phase2_ep(n)][i],
        c.sp_moves[phase2_sp(n)][i]);
}

// bool-returning DFS: pushes onto `path` descending, pops on backtrack.
// updates `next_bound` with the smallest f-value that exceeded `bound`.
bool phase1_dfs(const Phase1Ctx& c, Phase1Node n, int g, int bound, int& next_bound, int last_face, int last_axis_partner_face, std::vector<int>& path)
{
    COUNT_PHASE1_NODE();
    int f = g + phase1_h(c, n);
    if (f > bound) {
        if (f < next_bound) next_bound = f;
        return false;
    }
    if (phase1_is_goal(n)) return true;

    for (int m = 0; m < NUM_MOVES; ++m) {
        int face = face_of(m);
        if (face == last_face) continue;
        if (same_axis(face, last_face) && face < last_face) continue;
        if (face == last_axis_partner_face) continue;

        Phase1Node next = phase1_apply(c, n, m);
        int new_partner = same_axis(face, last_face) ? last_face : -1;
        path.push_back(m);
        if (phase1_dfs(c, next, g + 1, bound, next_bound, face, new_partner, path)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

bool phase2_dfs(const Phase2Ctx& c, Phase2Node n, int g, int bound, int& next_bound, int last_face, int last_axis_partner_face, std::vector<int>& path)
{
    COUNT_PHASE2_NODE();
    int f = g + phase2_h(c, n);
    if (f > bound) {
        if (f < next_bound) next_bound = f;
        return false;
    }
    if (phase2_is_goal(n)) return true;

    for (int i = 0; i < NUM_G1_MOVES; ++i) {
        int face = face_of(static_cast<int>(G1_MOVES[i]));
        if (face == last_face) continue;
        if (same_axis(face, last_face) && face < last_face) continue;
        if (face == last_axis_partner_face) continue;

        Phase2Node next = phase2_apply(c, n, i);
        int new_partner = same_axis(face, last_face) ? last_face : -1;
        path.push_back(i);
        if (phase2_dfs(c, next, g + 1, bound, next_bound, face, new_partner, path)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

// same DFS but bails out if the shared stop flag is set; checked per node with relaxed load
bool phase2_dfs_cancel(const Phase2Ctx& c, Phase2Node n, int g, int bound, int& next_bound,
                       int last_face, int last_axis_partner_face, std::vector<int>& path,
                       const std::atomic<bool>& stop)
{
    COUNT_PHASE2_NODE();
    if (stop.load(std::memory_order_relaxed)) return false;

    int f = g + phase2_h(c, n);
    if (f > bound) {
        if (f < next_bound) next_bound = f;
        return false;
    }
    if (phase2_is_goal(n)) return true;

    for (int i = 0; i < NUM_G1_MOVES; ++i) {
        int face = face_of(static_cast<int>(G1_MOVES[i]));
        if (face == last_face) continue;
        if (same_axis(face, last_face) && face < last_face) continue;
        if (face == last_axis_partner_face) continue;

        Phase2Node next = phase2_apply(c, n, i);
        int new_partner = same_axis(face, last_face) ? last_face : -1;
        path.push_back(i);
        if (phase2_dfs_cancel(c, next, g + 1, bound, next_bound, face, new_partner, path, stop)) {
            return true;
        }
        path.pop_back();
    }
    return false;
}

}  // namespace

std::vector<Move> ida_phase1(const CubeState& state) {
    const Phase1Ctx ctx = {
        corner_ori_move_table(),
        edge_ori_move_table(),
        slice_move_table(),
        corner_ori_slice_pruning(),
        edge_ori_slice_pruning(),
    };
    Phase1Node start = {
        encode_corner_ori(state),
        encode_edge_ori(state),
        encode_slice(state),
    };
    if (phase1_is_goal(start)) return {};

    std::array<std::pair<int, int>, NUM_MOVES> root_moves;
    for (int m = 0; m < NUM_MOVES; ++m) {
        root_moves[m] = {phase1_h(ctx, phase1_apply(ctx, start, m)), m};
    }
    std::sort(root_moves.begin(), root_moves.end());

    std::vector<int> path;
    int bound = phase1_h(ctx, start);
    while (bound <= MAX_DEPTH_PHASE1) {
        int next_bound = SEARCH_FAILED;
        for (auto [h, m] : root_moves) {
            int face = face_of(m);
            Phase1Node next = phase1_apply(ctx, start, m);
            path.clear();
            path.push_back(m);
            if (phase1_dfs(ctx, next, 1, bound, next_bound, face, -1, path)) {
                std::vector<Move> out;
                out.reserve(path.size());
                for (int mv : path) out.push_back(static_cast<Move>(mv));
                return out;
            }
        }
        if (next_bound == SEARCH_FAILED) return {};   // exhausted
        bound = next_bound;
    }
    return {};
}

std::vector<Move> ida_phase2(const CubeState& state) {
    const Phase2Ctx ctx = {
        corner_perm_move_table(),
        edge_perm_ud_move_table(),
        slice_perm_move_table(),
        corner_perm_slice_perm_pruning(),
        edge_perm_slice_perm_pruning(),
    };
    Phase2Node start = pack_phase2(
        encode_corner_perm(state),
        encode_edge_perm_ud(state),
        encode_slice_perm(state));
    if (phase2_is_goal(start)) return {};

    std::array<std::pair<int, int>, NUM_G1_MOVES> root_moves;
    for (int i = 0; i < NUM_G1_MOVES; ++i) {
        root_moves[i] = {phase2_h(ctx, phase2_apply(ctx, start, i)), i};
    }
    std::sort(root_moves.begin(), root_moves.end());

    std::vector<int> path;
    int bound = phase2_h(ctx, start);
    while (bound <= MAX_DEPTH_PHASE2) {
        int next_bound = SEARCH_FAILED;
        for (auto [h, i] : root_moves) {
            int face = face_of(static_cast<int>(G1_MOVES[i]));
            Phase2Node next = phase2_apply(ctx, start, i);
            path.clear();
            path.push_back(i);
            if (phase2_dfs(ctx, next, 1, bound, next_bound, face, -1, path)) {
                std::vector<Move> out;
                out.reserve(path.size());
                for (int idx : path) out.push_back(G1_MOVES[idx]);
                return out;
            }
        }
        if (next_bound == SEARCH_FAILED) return {};
        bound = next_bound;
    }
    return {};
}

std::vector<Move> solve(const CubeState& scramble) {
    std::vector<Move> solution = ida_phase1(scramble);

    CubeState intermediate = scramble;
    for (Move m : solution) apply_move(intermediate, m);

    std::vector<Move> p2 = ida_phase2(intermediate);
    solution.insert(solution.end(), p2.begin(), p2.end());
    return solution;
}

// parallel phase 2: iterative-deepening loop is unchanged, but for each bound fan out one worker per legal root move
std::vector<Move> ida_phase2_parallel(const CubeState& state, int num_threads) {
    if (num_threads <= 1) return ida_phase2(state);

    const Phase2Ctx ctx = {
        corner_perm_move_table(),
        edge_perm_ud_move_table(),
        slice_perm_move_table(),
        corner_perm_slice_perm_pruning(),
        edge_perm_slice_perm_pruning(),
    };
    Phase2Node start = pack_phase2(
        encode_corner_perm(state),
        encode_edge_perm_ud(state),
        encode_slice_perm(state));
    if (phase2_is_goal(start)) return {};

    // partition the NUM_G1_MOVES legal root moves round-robin across num_threads workers
    int workers = std::min<int>(num_threads, NUM_G1_MOVES);

    int bound = phase2_h(ctx, start);
    while (bound <= MAX_DEPTH_PHASE2) {
        std::atomic<bool> stop{false};
        std::atomic<int>  next_bound_atomic{SEARCH_FAILED};
        std::mutex        result_mu;
        std::vector<int>  best_path;   // guarded by result_mu

        auto worker = [&](int worker_id) {
            std::vector<int> local_path;
            local_path.reserve(32);
            int local_next_bound = SEARCH_FAILED;

            for (int i = worker_id; i < NUM_G1_MOVES; i += workers) {
                if (stop.load(std::memory_order_relaxed)) return;

                int face = face_of(static_cast<int>(G1_MOVES[i]));
                Phase2Node next = phase2_apply(ctx, start, i);
                local_path.clear();
                local_path.push_back(i);
                int new_partner = -1;   // last_face was -1 at true root

                if (phase2_dfs_cancel(ctx, next, 1, bound, local_next_bound,
                                      face, new_partner, local_path, stop)) {
                    // publish under lock; only the first arriving winner sticks
                    std::lock_guard<std::mutex> lk(result_mu);
                    if (best_path.empty()) {
                        best_path = local_path;
                        stop.store(true, std::memory_order_relaxed);
                    }
                    return;
                }
            }

            // fold this worker's next_bound suggestion into the shared one
            int prev = next_bound_atomic.load(std::memory_order_relaxed);
            while (local_next_bound < prev && !next_bound_atomic.compare_exchange_weak(prev, local_next_bound, std::memory_order_relaxed)) {
                // retry logic
            }
        };

        std::vector<std::thread> threads;
        threads.reserve(workers);
        for (int t = 0; t < workers; ++t) threads.emplace_back(worker, t);
        for (auto& th : threads) th.join();

        if (!best_path.empty()) {
            std::vector<Move> out;
            out.reserve(best_path.size());
            for (int i : best_path) out.push_back(G1_MOVES[i]);
            return out;
        }

        int nb = next_bound_atomic.load(std::memory_order_relaxed);
        if (nb == SEARCH_FAILED) return {};
        bound = nb;
    }
    return {};
}

std::vector<Move> solve_parallel(const CubeState& scramble, int num_threads) {
    std::vector<Move> solution = ida_phase1(scramble);

    CubeState intermediate = scramble;
    for (Move m : solution) apply_move(intermediate, m);

    std::vector<Move> p2 = ida_phase2_parallel(intermediate, num_threads);
    solution.insert(solution.end(), p2.begin(), p2.end());
    return solution;
}

#ifdef ENABLE_NODE_COUNTS
void reset_node_counts() {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    for (auto* p : r.phase1) *p = 0;
    for (auto* p : r.phase2) *p = 0;
}

NodeCounts read_node_counts() {
    auto& r = registry();
    std::lock_guard<std::mutex> lk(r.mu);
    NodeCounts out{0, 0};
    for (auto* p : r.phase1) out.phase1 += *p;
    for (auto* p : r.phase2) out.phase2 += *p;
    return out;
}
#else
// counting is a compile-time opt-in; leave the public entry points as no-ops
// so callers link cleanly against the default build
void reset_node_counts() {}
NodeCounts read_node_counts() { return NodeCounts{0, 0}; }
#endif

}
