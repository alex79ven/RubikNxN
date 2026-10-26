package com.ledbadge.rubik.cube

/** How the native solver split the work. Sizes are numbers of quarter turns. */
data class SolveStages(
    val centers: Int,
    val edges: Int,
    val parity: Int,
    val kociemba: Int,
) {
    val total: Int get() = centers + edges + parity + kociemba

    fun summary(): String {
        val parts = buildList {
            if (centers > 0) add("центры $centers")
            if (edges > 0) add("рёбра $edges")
            if (parity > 0) add("паритет $parity")
            if (kociemba > 0) add("3×3 $kociemba")
        }
        return parts.joinToString(" · ")
    }
}

data class SolveResult(val moves: List<CubeMove>, val stages: SolveStages)

/**
 * Calls the vendored C++ solver. The native side validates the state and
 * replays the solution on its own copy before reporting success, so a non-null
 * result is known to solve the cube.
 */
object NativeSolver {
    init {
        System.loadLibrary("rubik_solver")
    }

    private external fun solveNative(n: Int, stickers: IntArray): IntArray

    fun solve(state: CubeState): SolveResult? {
        val raw = solveNative(state.n, state.toIntArray())
        if (raw.isEmpty()) return null

        val count = raw[0]
        if (count < 0 || raw.size < 5 + count) return null

        val moves = ArrayList<CubeMove>(count)
        for (i in 0 until count) {
            val code = raw[5 + i]
            val face = code and 0x7
            val outer = (code shr 3) and 0x3F
            val inner = (code shr 9) and 0x3F
            val turn = (code shr 15) and 0x3
            if (face !in 0..5 || turn !in 0..2) return null
            moves += CubeMove(Face.entries[face], outer, inner, Turn.entries[turn])
        }

        return SolveResult(
            moves = moves,
            stages = SolveStages(raw[1], raw[2], raw[3], raw[4]),
        )
    }
}
