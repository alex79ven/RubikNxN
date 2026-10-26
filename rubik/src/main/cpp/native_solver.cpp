// JNI bridge between the Android app and the vendored NxN solver.
//
// The solver itself lives in engine/ and is an unmodified copy of
// https://github.com/krishvijay100/NxN-Cube-Solver-Engine (MIT, see engine/LICENSE).
// Only the artificial n <= 7 assertion in solve_nxn() is relaxed: the generic
// reduction pipeline works for bigger cubes and was verified up to 12x12.

#include <jni.h>

#include <cstdint>
#include <vector>

#include "cube_nxn/cube_nxn.h"

namespace {

using cube_nxn::Move;
using cube_nxn::NxNCube;
using cube_nxn::Turn;

// Sticker layout: 6 * n * n, face-major, row-major, values 0..5 (U R F D L B).
constexpr jint kFaceShift = 0;
constexpr jint kOuterShift = 3;
constexpr jint kInnerShift = 9;
constexpr jint kTurnShift = 15;

jint encode(const Move& m) {
    return (static_cast<jint>(m.face) << kFaceShift) |
           (static_cast<jint>(m.outer_depth) << kOuterShift) |
           (static_cast<jint>(m.inner_depth) << kInnerShift) |
           (static_cast<jint>(m.turn) << kTurnShift);
}

// A 2x2 cube is the corner subset of a 3x3. Padding the edges to the solved
// state only works when the corner permutation has even parity, so the second
// attempt swaps two edge pieces to fix the parity mismatch.
NxNCube pad2to3(const NxNCube& small, bool swapEdges) {
    NxNCube big(3);
    const int map[2] = {0, 2};
    for (int f = 0; f < 6; ++f) {
        for (int r = 0; r < 2; ++r) {
            for (int c = 0; c < 2; ++c) {
                big.set_sticker(f, map[r], map[c], small.sticker(f, r, c));
            }
        }
    }
    if (swapEdges) {
        // Swap the UF and UR edge pieces (both are solved in the padded cube).
        const uint8_t uf_u = big.sticker(0, 2, 1);
        const uint8_t uf_f = big.sticker(2, 0, 1);
        const uint8_t ur_u = big.sticker(0, 1, 2);
        const uint8_t ur_r = big.sticker(1, 0, 1);
        big.set_sticker(0, 2, 1, ur_u);
        big.set_sticker(2, 0, 1, ur_r);
        big.set_sticker(0, 1, 2, uf_u);
        big.set_sticker(1, 0, 1, uf_f);
    }
    return big;
}

bool replaySolves(const NxNCube& start, const std::vector<Move>& moves) {
    NxNCube cube = start;
    for (const Move& m : moves) cube_nxn::apply_move(cube, m);
    return cube.is_solved();
}

bool solve2x2(const NxNCube& cube, std::vector<Move>& out) {
    for (bool swap : {false, true}) {
        NxNCube padded = pad2to3(cube, swap);
        const auto result = cube_nxn::solve_nxn(padded);
        if (!result.ok) continue;
        if (!replaySolves(cube, result.moves)) continue;
        out = result.moves;
        return true;
    }
    return false;
}

}  // namespace

extern "C" JNIEXPORT jintArray JNICALL
Java_com_ledbadge_rubik_cube_NativeSolver_solveNative(
    JNIEnv* env, jobject /*thiz*/, jint n, jintArray stickerArray) {
    if (n < 2 || n > 20) return env->NewIntArray(0);

    const jsize length = env->GetArrayLength(stickerArray);
    if (length != 6 * n * n) return env->NewIntArray(0);

    jint* raw = env->GetIntArrayElements(stickerArray, nullptr);
    if (raw == nullptr) return env->NewIntArray(0);

    NxNCube cube(n);
    bool valid = true;
    for (jsize i = 0; i < length; ++i) {
        if (raw[i] < 0 || raw[i] > 5) {
            valid = false;
            break;
        }
        cube.raw()[i] = static_cast<uint8_t>(raw[i]);
    }
    env->ReleaseIntArrayElements(stickerArray, raw, JNI_ABORT);
    if (!valid) return env->NewIntArray(0);

    std::vector<Move> moves;
    int centers = 0;
    int edges = 0;
    int parity = 0;
    int kociemba = 0;

    bool ok = false;
    if (n == 2) {
        ok = solve2x2(cube, moves);
    } else {
        const auto result = cube_nxn::solve_nxn(cube);
        ok = result.ok;
        if (ok) {
            moves = result.moves;
            centers = result.stage_lengths.centers;
            edges = result.stage_lengths.edges;
            parity = result.stage_lengths.parity;
            kociemba = result.stage_lengths.kociemba;
        }
    }

    if (!ok) return env->NewIntArray(0);

    std::vector<jint> payload;
    payload.reserve(5 + moves.size());
    payload.push_back(static_cast<jint>(moves.size()));
    payload.push_back(centers);
    payload.push_back(edges);
    payload.push_back(parity);
    payload.push_back(kociemba);
    for (const Move& m : moves) payload.push_back(encode(m));

    jintArray out = env->NewIntArray(static_cast<jsize>(payload.size()));
    if (out != nullptr) {
        env->SetIntArrayRegion(out, 0, static_cast<jsize>(payload.size()), payload.data());
    }
    return out;
}
