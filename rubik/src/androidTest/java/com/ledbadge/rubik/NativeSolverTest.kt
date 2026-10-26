package com.ledbadge.rubik

import androidx.test.ext.junit.runners.AndroidJUnit4
import com.ledbadge.rubik.cube.CubeState
import com.ledbadge.rubik.cube.NativeSolver
import com.ledbadge.rubik.cube.Scrambler
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import kotlin.random.Random

/**
 * End-to-end check of the JNI solver on a device/emulator: scramble in Kotlin,
 * solve in native code, replay the returned moves in Kotlin and require the
 * cube to be solved.
 */
@RunWith(AndroidJUnit4::class)
class NativeSolverTest {

    @Test
    fun solvesTwoByTwo() = solveAndReplay(2)

    @Test
    fun solvesThreeByThree() = solveAndReplay(3)

    @Test
    fun solvesFourByFour() = solveAndReplay(4)

    @Test
    fun solvesSevenBySeven() = solveAndReplay(7)

    @Test
    fun solvesNineByNine() = solveAndReplay(9)

    @Test
    fun rejectsUnsolvableState() {
        val cube = CubeState(3)
        val a = cube.colorAt(0, 0, 0)
        val b = cube.colorAt(1, 0, 0)
        cube.setColor(0, 0, 0, b)
        cube.setColor(1, 0, 0, a)
        assertTrue("broken state must not solve", NativeSolver.solve(cube) == null)
    }

    private fun solveAndReplay(n: Int) {
        val scramble = Scrambler.generate(n, Scrambler.lengthFor(n), Random(n * 101))
        val cube = CubeState(n)
        for (move in scramble) cube.apply(move)
        assertTrue("scramble must not be solved", !cube.isSolved())

        val started = System.currentTimeMillis()
        val result = requireNotNull(NativeSolver.solve(cube)) { "no solution for n=$n" }
        val elapsed = System.currentTimeMillis() - started

        for (move in result.moves) cube.apply(move)
        assertTrue("n=$n not solved after ${result.moves.size} moves (${elapsed}ms)", cube.isSolved())
        assertTrue("n=$n empty solution", result.moves.isNotEmpty())
    }
}
