package com.ledbadge.rubik

import android.content.Context
import androidx.test.core.app.ApplicationProvider
import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ledbadge.rubik.cube.CubeMove
import com.ledbadge.rubik.cube.CubeState
import com.ledbadge.rubik.cube.CubeStateStore
import com.ledbadge.rubik.cube.Face
import com.ledbadge.rubik.cube.SavedSession
import com.ledbadge.rubik.cube.Scrambler
import com.ledbadge.rubik.cube.SolveStages
import com.ledbadge.rubik.cube.Turn
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith

/** Round-trip checks for the crash-safe session storage. */
@RunWith(AndroidJUnit4::class)
class CubeStateStoreTest {

    private val context: Context = ApplicationProvider.getApplicationContext()

    @Test
    fun roundTripsLargeSession() {
        val store = CubeStateStore(context)
        store.clear()

        val n = 9
        val cube = CubeState(n)
        val scramble = Scrambler.generate(n, 60)
        for (move in scramble) cube.apply(move)

        // Long solutions are the point of the storage: the 9×9 solver returns
        // ~12k moves, and users resume them days later.
        val moves = ArrayList<CubeMove>(5_000)
        repeat(5_000) { i ->
            val face = Face.entries[i % Face.entries.size]
            val depth = i % 5
            val turn = Turn.entries[i % Turn.entries.size]
            moves += CubeMove(
                face = face,
                outerDepth = depth,
                innerDepth = (depth + 2).coerceAtMost(n - 1),
                turn = turn,
            )
        }

        store.save(
            SavedSession(
                n = n,
                cube = cube,
                solution = moves,
                solutionIndex = 3_217,
                speed = 8,
                stages = SolveStages(centers = 9_000, edges = 3_000, parity = 0, kociemba = 21),
            ),
        )

        val loaded = store.load()
        assertNotNull(loaded)
        loaded!!
        assertEquals(n, loaded.n)
        assertArrayEquals(cube.stickers, loaded.cube.stickers)
        assertEquals(moves.size, loaded.solution.size)
        assertEquals(moves, loaded.solution)
        assertEquals(3_217, loaded.solutionIndex)
        assertEquals(8, loaded.speed)
        assertEquals(SolveStages(9_000, 3_000, 0, 21), loaded.stages)

        store.clear()
        assertNull(store.load())
    }

    @Test
    fun restoresSolvedCubeWithoutSolution() {
        val store = CubeStateStore(context)
        store.clear()

        val cube = CubeState(6)
        store.save(
            SavedSession(
                n = 6,
                cube = cube,
                solution = emptyList(),
                solutionIndex = 0,
                speed = 4,
                stages = null,
            ),
        )

        val loaded = store.load()
        assertNotNull(loaded)
        assertEquals(6, loaded!!.n)
        assertEquals(0, loaded.solution.size)
        assertNull(loaded.stages)
        // A solved cube must stay solved after the round trip.
        assertEquals(6 * 36, loaded.cube.correctCount())

        store.clear()
        assertNull(store.load())
    }
}
