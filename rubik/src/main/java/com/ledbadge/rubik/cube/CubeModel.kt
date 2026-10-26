package com.ledbadge.rubik.cube

/**
 * Faces in the same order the native solver uses: U, R, F, D, L, B.
 *
 * Sticker colours are stored as face indices, i.e. colour 0 is the U colour,
 * colour 1 the R colour and so on. A solved cube has sticker(f, r, c) == f.
 */
enum class Face { U, R, F, D, L, B }

/** Quarter/half/three-quarter turn, clockwise as seen from outside the face. */
enum class Turn {
    CW,
    HALF,
    CCW;

    val quarterTurns: Int
        get() = when (this) {
            CW -> 1
            HALF -> 2
            CCW -> 3
        }

    fun inverse(): Turn = when (this) {
        CW -> CCW
        CCW -> CW
        HALF -> HALF
    }
}

/**
 * One turn of a block of layers around a face axis.
 *
 * [outerDepth] and [innerDepth] are 0-based layer indices counted from the
 * face itself, so an ordinary face turn is (0, 0), a two-layer wide turn is
 * (0, 1) and a single inner slice is (d, d).
 */
data class CubeMove(
    val face: Face,
    val outerDepth: Int = 0,
    val innerDepth: Int = 0,
    val turn: Turn = Turn.CW,
) {
    val inverse: CubeMove get() = copy(turn = turn.inverse())

    /** Standard WCA-ish notation, e.g. R, Uw2, 3F', 2L. */
    fun notation(n: Int): String {
        val suffix = when (turn) {
            Turn.CW -> ""
            Turn.HALF -> "2"
            Turn.CCW -> "'"
        }
        val name = face.name
        return when {
            outerDepth == 0 && innerDepth == 0 -> "$name$suffix"
            outerDepth == 0 && innerDepth == 1 -> "${name}w$suffix"
            outerDepth == 0 -> "${innerDepth + 1}${name}w$suffix"
            outerDepth == innerDepth -> "${innerDepth + 1}$name$suffix"
            else -> "${outerDepth + 1}-${innerDepth + 1}$name$suffix"
        }
    }
}

private enum class StripDir { ROW_LR, ROW_RL, COL_TB, COL_BT }
private enum class StripSide { FIRST, LAST }

private class Strip(val face: Face, val dir: StripDir, val side: StripSide)

/**
 * The four side strips each face turn cycles. This is a direct port of the
 * native cube's `outer_table`; keeping both in sync is what lets the solver's
 * move list be applied to the Kotlin state.
 */
private val MOVE_STRIPS: Array<Array<Strip>> = arrayOf(
    // U
    arrayOf(
        Strip(Face.F, StripDir.ROW_LR, StripSide.FIRST),
        Strip(Face.L, StripDir.ROW_LR, StripSide.FIRST),
        Strip(Face.B, StripDir.ROW_LR, StripSide.FIRST),
        Strip(Face.R, StripDir.ROW_LR, StripSide.FIRST),
    ),
    // R
    arrayOf(
        Strip(Face.F, StripDir.COL_TB, StripSide.LAST),
        Strip(Face.U, StripDir.COL_TB, StripSide.LAST),
        Strip(Face.B, StripDir.COL_BT, StripSide.FIRST),
        Strip(Face.D, StripDir.COL_TB, StripSide.LAST),
    ),
    // F
    arrayOf(
        Strip(Face.U, StripDir.ROW_LR, StripSide.LAST),
        Strip(Face.R, StripDir.COL_TB, StripSide.FIRST),
        Strip(Face.D, StripDir.ROW_RL, StripSide.FIRST),
        Strip(Face.L, StripDir.COL_BT, StripSide.LAST),
    ),
    // D
    arrayOf(
        Strip(Face.F, StripDir.ROW_LR, StripSide.LAST),
        Strip(Face.R, StripDir.ROW_LR, StripSide.LAST),
        Strip(Face.B, StripDir.ROW_LR, StripSide.LAST),
        Strip(Face.L, StripDir.ROW_LR, StripSide.LAST),
    ),
    // L
    arrayOf(
        Strip(Face.F, StripDir.COL_TB, StripSide.FIRST),
        Strip(Face.D, StripDir.COL_TB, StripSide.FIRST),
        Strip(Face.B, StripDir.COL_BT, StripSide.LAST),
        Strip(Face.U, StripDir.COL_TB, StripSide.FIRST),
    ),
    // B
    arrayOf(
        Strip(Face.U, StripDir.ROW_RL, StripSide.FIRST),
        Strip(Face.L, StripDir.COL_TB, StripSide.FIRST),
        Strip(Face.D, StripDir.ROW_LR, StripSide.LAST),
        Strip(Face.R, StripDir.COL_BT, StripSide.LAST),
    ),
)

/**
 * Sticker state of an n×n×n cube.
 *
 * Layout matches the native engine: index = face * n * n + row * n + col.
 */
class CubeState(
    val n: Int,
    val stickers: ByteArray = solvedStickers(n),
) {
    init {
        require(n in 2..16) { "unsupported cube size $n" }
        require(stickers.size == 6 * n * n) { "expected ${6 * n * n} stickers" }
    }

    fun copy(): CubeState = CubeState(n, stickers.copyOf())

    fun colorAt(face: Face, row: Int, col: Int): Int =
        stickers[face.ordinal * n * n + row * n + col].toInt()

    fun colorAt(faceIndex: Int, row: Int, col: Int): Int =
        stickers[faceIndex * n * n + row * n + col].toInt()

    fun setColor(face: Face, row: Int, col: Int, color: Int) {
        stickers[face.ordinal * n * n + row * n + col] = color.toByte()
    }

    fun setColor(faceIndex: Int, row: Int, col: Int, color: Int) {
        stickers[faceIndex * n * n + row * n + col] = color.toByte()
    }

    /** Applies [move] in place. Layer indices are validated here. */
    fun apply(move: CubeMove) {
        require(move.outerDepth in 0 until n) { "outerDepth out of range" }
        require(move.innerDepth in move.outerDepth until n) { "innerDepth out of range" }

        if (move.outerDepth == 0) rotateFace(move.face.ordinal, move.turn)

        val strips = MOVE_STRIPS[move.face.ordinal]
        val shifts = move.turn.quarterTurns
        for (depth in move.outerDepth..move.innerDepth) {
            repeat(shifts) { cycleStrips(strips, depth) }
        }
    }

    /** Returns a new state with [move] applied. */
    fun applied(move: CubeMove): CubeState = copy().also { it.apply(move) }

    fun isSolved(): Boolean {
        val faceSize = n * n
        for (f in 0 until 6) {
            val expected = f.toByte()
            val base = f * faceSize
            for (i in base until base + faceSize) {
                if (stickers[i] != expected) return false
            }
        }
        return true
    }

    /** Number of stickers that are already on their home face. */
    fun correctCount(): Int {
        var correct = 0
        val faceSize = n * n
        for (f in 0 until 6) {
            val base = f * faceSize
            for (i in base until base + faceSize) {
                if (stickers[i].toInt() == f) correct++
            }
        }
        return correct
    }

    fun colorCounts(): IntArray {
        val counts = IntArray(6)
        for (s in stickers) {
            val v = s.toInt()
            if (v in 0..5) counts[v]++
        }
        return counts
    }

    fun toIntArray(): IntArray = IntArray(stickers.size) { stickers[it].toInt() }

    fun withStickers(newStickers: ByteArray): CubeState = CubeState(n, newStickers)

    private fun rotateFace(face: Int, turn: Turn) {
        val src = stickers
        val base = face * n * n
        val scratch = ByteArray(n * n)
        for (r in 0 until n) {
            for (c in 0 until n) {
                val (nr, nc) = when (turn) {
                    Turn.CW -> c to (n - 1 - r)
                    Turn.CCW -> (n - 1 - c) to r
                    Turn.HALF -> (n - 1 - r) to (n - 1 - c)
                }
                scratch[nr * n + nc] = src[base + r * n + c]
            }
        }
        System.arraycopy(scratch, 0, stickers, base, n * n)
    }

    private fun cycleStrips(strips: Array<Strip>, depth: Int) {
        val a = readStrip(strips[0], depth)
        val b = readStrip(strips[1], depth)
        val c = readStrip(strips[2], depth)
        val d = readStrip(strips[3], depth)
        writeStrip(strips[1], depth, a)
        writeStrip(strips[2], depth, b)
        writeStrip(strips[3], depth, c)
        writeStrip(strips[0], depth, d)
    }

    private fun lineFor(side: StripSide, depth: Int): Int =
        if (side == StripSide.FIRST) depth else n - 1 - depth

    private fun readStrip(strip: Strip, depth: Int): ByteArray {
        val line = lineFor(strip.side, depth)
        val base = strip.face.ordinal * n * n
        val out = ByteArray(n)
        when (strip.dir) {
            StripDir.ROW_LR -> for (i in 0 until n) out[i] = stickers[base + line * n + i]
            StripDir.ROW_RL -> for (i in 0 until n) out[i] = stickers[base + line * n + (n - 1 - i)]
            StripDir.COL_TB -> for (i in 0 until n) out[i] = stickers[base + i * n + line]
            StripDir.COL_BT -> for (i in 0 until n) out[i] = stickers[base + (n - 1 - i) * n + line]
        }
        return out
    }

    private fun writeStrip(strip: Strip, depth: Int, values: ByteArray) {
        val line = lineFor(strip.side, depth)
        val base = strip.face.ordinal * n * n
        when (strip.dir) {
            StripDir.ROW_LR -> for (i in 0 until n) stickers[base + line * n + i] = values[i]
            StripDir.ROW_RL -> for (i in 0 until n) stickers[base + line * n + (n - 1 - i)] = values[i]
            StripDir.COL_TB -> for (i in 0 until n) stickers[base + i * n + line] = values[i]
            StripDir.COL_BT -> for (i in 0 until n) stickers[base + (n - 1 - i) * n + line] = values[i]
        }
    }

    companion object {
        fun solvedStickers(n: Int): ByteArray {
            val size = 6 * n * n
            return ByteArray(size) { (it / (n * n)).toByte() }
        }
    }
}

/** Scramble and free-play moves never touch the middle slice of an odd cube. */
object Scrambler {
    fun generate(n: Int, length: Int, random: kotlin.random.Random = kotlin.random.Random.Default): List<CubeMove> {
        val maxWide = (n / 2).coerceAtLeast(1)
        val moves = ArrayList<CubeMove>(length)
        var lastFace = -1
        while (moves.size < length) {
            val face = Face.entries[random.nextInt(6)]
            if (face.ordinal == lastFace) continue
            val depth = if (maxWide > 1 && random.nextInt(4) == 0) {
                2 + random.nextInt(maxWide - 1)
            } else {
                1
            }
            val turn = Turn.entries[random.nextInt(3)]
            moves += CubeMove(face, 0, depth - 1, turn)
            lastFace = face.ordinal
        }
        return moves
    }

    fun lengthFor(n: Int): Int = maxOf(20, n * 8)
}
