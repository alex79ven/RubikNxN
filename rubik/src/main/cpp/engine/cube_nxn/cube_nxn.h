#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "cube/cube.h"

namespace cube_nxn {

// wca face convention; solved cube has stickers[face_id * n*n + i] == face_id for every i
enum class Face : uint8_t {
    U = 0,
    R = 1,
    F = 2,
    D = 3,
    L = 4,
    B = 5,
};

constexpr int NUM_FACES = 6;

// index of the sticker at (face, row, col) is face*n*n + row*n + col
class NxNCube {
public:
    explicit NxNCube(int n);

    int n() const { return n_; }
    int face_size() const { return n_ * n_; }
    int num_stickers() const { return NUM_FACES * n_ * n_; }

    uint8_t sticker(int face, int row, int col) const;
    void set_sticker(int face, int row, int col, uint8_t color);

    uint8_t* face_data(int face);
    const uint8_t* face_data(int face) const;

    uint8_t* raw() { return stickers_.data(); }
    const uint8_t* raw() const { return stickers_.data(); }

    bool is_solved() const;

private:
    int n_;
    std::vector<uint8_t> stickers_;
};

enum class Turn : uint8_t {
    CW = 0,
    Half = 1,
    CCW = 2,
};

// rotate the stickers of one face in place by the given amount; does NOT touch any other face
void rotate_face(NxNCube& cube, int face, Turn t);

// apply a single outer-face move at the given turn amount; rotates the target face's own stickers AND cycles the neighboring strip on each of the four adjacent faces
void apply_outer_move(NxNCube& cube, Face f, Turn t);

// for wide or inner-slice move
void apply_wide_move(NxNCube& cube, Face f, int outer_depth, int inner_depth, Turn t);

struct Move {
    Face face;
    int  outer_depth;
    int  inner_depth;
    Turn turn;
};

void apply_move(NxNCube& cube, const Move& m);

std::optional<Move> parse_move(std::string_view s);

std::optional<std::vector<Move>> parse_scramble(std::string_view s);

std::string format_move(const Move& m);

std::optional<std::string> format_move_virtual_cube_net(const Move& m, int n);
std::optional<std::string> format_sequence_virtual_cube_net(
    const std::vector<Move>& moves, int n);

std::vector<Move> random_scramble(int n, int length, uint64_t seed);

// stage of the reduction pipeline being solved; determines the move set the BFS is allowed to explore
enum class Stage : uint8_t {
    Centers,   // no prior-stage constraints; all moves legal
    Edges,     // centers must remain solved
    Parity,    // centers + edges preserved; 3x3-equivalent move set
};

using MoveStep = std::vector<Move>;

std::vector<MoveStep> legal_move_steps_for_stage(int n, Stage stage);
void apply_move_step(NxNCube& cube, const MoveStep& step);

// result of a bounded BFS reduction search
struct BFSResult {
    bool found;
    std::vector<MoveStep> sequence;
    size_t nodes_explored;
};

// generic bounded BFS
BFSResult reduce_bfs(
    const NxNCube&                                  start,
    const std::function<bool(const NxNCube&)>&      is_goal,
    const std::function<uint64_t(const NxNCube&)>&  state_hash,
    const std::vector<MoveStep>&                    moves,
    int                                             max_depth,
    size_t                                          max_nodes = 5'000'000);

// parity detection + fix
enum class ParityState : uint8_t {
    Valid,   // no fix needed
    OLL,     // one wing flipped in place
    PLL,     // edge/corner perm sign mismatch
    Both,    // both problems present
};

ParityState detect_parity_general(const NxNCube& cube);

// post-process flat move sequence to collapse redundant moves
std::vector<Move> collapse_redundant_moves(const std::vector<Move>& moves);

// convert reduced NxN cube into CubeState used by the Kociemba solver
std::optional<cube::CubeState> to_cube_state_3x3(const NxNCube& cube);

// convert a 3x3 solver move into an NxN outer-face move
Move to_nxn_move(cube::Move m);

// finish reduced NxN cube by handing to Kociemba
std::vector<Move> finish_as_3x3(NxNCube& cube);

// per-stage move counts
struct SolveStageLengths {
    int centers;
    int edges;
    int parity;
    int kociemba;
};

struct SolveResult {
    bool ok;
    std::vector<Move> moves;
    SolveStageLengths stage_lengths;
};

// top-level solver, chains all four reduction stages + Kociemba
SolveResult solve_nxn(NxNCube& cube);
}
