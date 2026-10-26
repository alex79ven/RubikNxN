#pragma once

#include <cstdint>
#include <vector>

#include "cube/cube.h"

namespace cube::solver {

struct NodeCounts {
    uint64_t phase1;
    uint64_t phase2;
};

void reset_node_counts();
NodeCounts read_node_counts();

// returns the move sequence that transforms scramble into the solved cube; returns empty on malformed
// input or if the internal depth cap is hit (never expected for valid states)
std::vector<Move> solve(const CubeState& scramble);

// same contract as solve() but fans out the phase 2 search across num_threads
// worker threads at each ID iteration's root
std::vector<Move> solve_parallel(const CubeState& scramble, int num_threads);

// individual phase solvers, exposed so tests can exercise them in isolation
std::vector<Move> ida_phase1(const CubeState& state);
std::vector<Move> ida_phase2(const CubeState& state);
std::vector<Move> ida_phase2_parallel(const CubeState& state, int num_threads);

}
