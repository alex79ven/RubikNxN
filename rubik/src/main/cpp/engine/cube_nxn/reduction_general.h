#pragma once

#include <cstdint>
#include <vector>

#include "cube_nxn/cube_nxn.h"

// General-N reduction engine

namespace cube_nxn {

// which orbit a face-center piece lives in
enum class CenterFlavor : uint8_t {
    Fixed,     // odd N only, one per face
    XCenter,   // diagonal, 4 per face per ring
    PlusCenter,// axial, odd N only, 4 per face per ring
    Oblique,   // asymmetric (N >= 6), 8 per face per ring; comes in two chiralities
};

// which orbit an edge-piece lives in
enum class EdgeFlavor : uint8_t {
    Middle,    // odd N only, one per edge slot
    Wing,      // 2 per edge slot per wing-index
};

struct CenterSlot {
    Face  face;
    int   row;
    int   col;
    CenterFlavor flavor;
    int   ring;             // distance from center block's center
};

struct EdgeSlotG {
    int    edge_id;
    EdgeFlavor flavor;
    int    wing_index;
    int    wing_side;
};

std::vector<CenterSlot> enumerate_center_slots(int n);

std::vector<EdgeSlotG> enumerate_edge_slots(int n);

std::vector<MoveStep> solve_centers_general(NxNCube& cube);

// checked stage postconditions used by top-level pipeline
bool centers_reduced_general(const NxNCube& cube);
bool wings_reduced_general(const NxNCube& cube);

std::vector<MoveStep> solve_edges_general(NxNCube& cube);

std::vector<MoveStep> solve_middles_after_kociemba(NxNCube& cube);

// exposed commutator templates, mostly for tests/debugging
MoveStep xcenter_3cycle_alg(int d);
MoveStep wing_3cycle_alg(int d);
MoveStep plus_center_3cycle_alg(int d, int n);

MoveStep oblique_3cycle_alg(int n, int a, int b, int chir);
MoveStep middle_edge_3cycle_alg(int n);
MoveStep middle_edge_flip_alg(int n);

}