package com.ledbadge.rubik.cube

import android.content.Context
import android.util.AtomicFile
import java.io.BufferedInputStream
import java.io.BufferedOutputStream
import java.io.DataInputStream
import java.io.DataOutputStream
import java.io.File
import java.io.FileOutputStream

/** Everything needed to continue a solve after the app was closed. */
data class SavedSession(
    val n: Int,
    val cube: CubeState,
    val solution: List<CubeMove>,
    val solutionIndex: Int,
    val speed: Int,
    val stages: SolveStages?,
)

/**
 * Stores the session in app-private storage. Writes go through [AtomicFile],
 * so a crash, a dead battery or a force stop in the middle of a write leaves
 * the previous good snapshot intact instead of a truncated file.
 */
class CubeStateStore(context: Context) {

    private val file = AtomicFile(File(context.filesDir, FILE_NAME))

    fun load(): SavedSession? = try {
        file.openRead().use { stream ->
            val input = DataInputStream(BufferedInputStream(stream))
            if (input.readInt() != MAGIC || input.readInt() != VERSION) {
                return null
            }
            val n = input.readInt()
            if (n !in MIN_SIZE..MAX_SIZE) return null

            val stickers = ByteArray(6 * n * n)
            input.readFully(stickers)
            if (stickers.any { it.toInt() !in 0..5 }) return null

            val count = input.readInt()
            if (count < 0 || count > MAX_MOVES) return null
            val moves = ArrayList<CubeMove>(count)
            for (i in 0 until count) {
                moves += decodeMove(input.readInt()) ?: return null
            }

            val index = input.readInt().coerceIn(0, count)
            val speed = input.readInt().coerceIn(0, 64)
            val stages = if (input.readBoolean()) {
                SolveStages(
                    centers = input.readInt(),
                    edges = input.readInt(),
                    parity = input.readInt(),
                    kociemba = input.readInt(),
                )
            } else {
                null
            }

            SavedSession(
                n = n,
                cube = CubeState(n, stickers),
                solution = moves,
                solutionIndex = index,
                speed = speed,
                stages = stages,
            )
        }
    } catch (_: Throwable) {
        // Corrupt or unreadable snapshot: start from a fresh cube.
        null
    }

    fun save(session: SavedSession) {
        var raw: FileOutputStream? = null
        try {
            val stream = file.startWrite()
            raw = stream
            val out = DataOutputStream(BufferedOutputStream(stream))
            out.writeInt(MAGIC)
            out.writeInt(VERSION)
            out.writeInt(session.n)
            out.write(session.cube.stickers)
            out.writeInt(session.solution.size)
            for (move in session.solution) out.writeInt(encodeMove(move))
            out.writeInt(session.solutionIndex)
            out.writeInt(session.speed)
            out.writeBoolean(session.stages != null)
            session.stages?.let {
                out.writeInt(it.centers)
                out.writeInt(it.edges)
                out.writeInt(it.parity)
                out.writeInt(it.kociemba)
            }
            out.flush()
            file.finishWrite(stream)
        } catch (_: Throwable) {
            try {
                raw?.let { file.failWrite(it) }
            } catch (_: Throwable) {
                // Nothing else we can do; the old snapshot stays in place.
            }
        }
    }

    fun clear() {
        file.delete()
    }

    private companion object {
        const val FILE_NAME = "rubik_session.bin"
        const val MAGIC = 0x5255424B // "RUBK"
        const val VERSION = 1
        const val MIN_SIZE = 2
        const val MAX_SIZE = 20
        const val MAX_MOVES = 5_000_000

        fun encodeMove(move: CubeMove): Int =
            move.face.ordinal or
                (move.outerDepth shl 3) or
                (move.innerDepth shl 11) or
                (move.turn.ordinal shl 19)

        fun decodeMove(code: Int): CubeMove? {
            val face = code and 0x7
            val outer = (code shr 3) and 0xFF
            val inner = (code shr 11) and 0xFF
            val turn = (code shr 19) and 0x3
            if (face !in 0..5 || turn !in 0..2) return null
            if (outer > 19 || inner > 19) return null
            return CubeMove(Face.entries[face], outer, inner, Turn.entries[turn])
        }
    }
}
