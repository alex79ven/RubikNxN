package com.ledbadge.rubik.ui

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.ledbadge.rubik.cube.CubeMove
import com.ledbadge.rubik.cube.CubeState
import com.ledbadge.rubik.cube.CubeStateStore
import com.ledbadge.rubik.cube.NativeSolver
import com.ledbadge.rubik.cube.SavedSession
import com.ledbadge.rubik.cube.Scrambler
import com.ledbadge.rubik.cube.SolveStages
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext

data class CubeUiState(
    val n: Int = 3,
    val cube: CubeState = CubeState(3),
    val scramble: List<CubeMove> = emptyList(),
    val solution: List<CubeMove> = emptyList(),
    val solutionIndex: Int = 0,
    val stages: SolveStages? = null,
    /** Move currently being animated, if any. */
    val activeMove: CubeMove? = null,
    val progress: Float = 0f,
    val playing: Boolean = false,
    /** Moves per second; 0 means "apply everything at once". */
    val speed: Int = 4,
    val solving: Boolean = false,
    val message: String? = null,
    val canUndo: Boolean = false,
) {
    val hasSolution: Boolean get() = solution.isNotEmpty()
}

class CubeViewModel(application: Application) : AndroidViewModel(application) {

    private val _state = MutableStateFlow(CubeUiState())
    val state: StateFlow<CubeUiState> = _state.asStateFlow()

    private val store = CubeStateStore(application)
    private val storeMutex = Mutex()
    private var lastSaveKey: String? = null

    private var animationJob: Job? = null
    private var playbackJob: Job? = null
    private var solveJob: Job? = null
    private var seekJob: Job? = null
    private var seekTicket = 0
    private val manualHistory = ArrayDeque<CubeMove>()
    private var solveTicket = 0

    init {
        restore()
        startAutosave()
    }

    /**
     * Restores the previous session: cube colours, the solution that was
     * already calculated and the current step. Solving a big cube by hand can
     * take days, so the app must survive reboots and crashes mid-solve.
     */
    private fun restore() {
        val saved = store.load() ?: return
        lastSaveKey = saveKey(
            CubeUiState(
                n = saved.n,
                cube = saved.cube,
                solution = saved.solution,
                solutionIndex = saved.solutionIndex,
                speed = saved.speed,
            ),
        )
        _state.value = CubeUiState(
            n = saved.n,
            cube = saved.cube,
            solution = saved.solution,
            solutionIndex = saved.solutionIndex,
            stages = saved.stages,
            speed = saved.speed,
            message = if (saved.solution.isNotEmpty()) {
                "Состояние восстановлено: ход ${saved.solutionIndex} из ${saved.solution.size}"
            } else {
                null
            },
        )
    }

    private fun startAutosave() {
        viewModelScope.launch(Dispatchers.IO) {
            while (isActive) {
                delay(AUTOSAVE_INTERVAL_MS)
                persist(_state.value)
            }
        }
    }

    /** Writes the current snapshot now; used by the save button and onStop. */
    fun saveNow(notify: Boolean = false) {
        val snapshot = _state.value
        viewModelScope.launch(Dispatchers.IO) {
            val saved = persist(snapshot, force = true)
            if (saved && notify) {
                _state.update { it.copy(message = "Состояние сохранено") }
            }
        }
    }

    private suspend fun persist(snapshot: CubeUiState, force: Boolean = false): Boolean {
        val key = saveKey(snapshot)
        if (!force && key == lastSaveKey) return false
        return storeMutex.withLock {
            if (!force && key == lastSaveKey) {
                false
            } else {
                store.save(
                    SavedSession(
                        n = snapshot.n,
                        cube = snapshot.cube,
                        solution = snapshot.solution,
                        solutionIndex = snapshot.solutionIndex,
                        speed = snapshot.speed,
                        stages = snapshot.stages,
                    ),
                )
                lastSaveKey = key
                true
            }
        }
    }

    private fun saveKey(state: CubeUiState): String = buildString {
        append(state.n)
        append(':')
        append(state.solutionIndex)
        append(':')
        append(state.solution.size)
        append(':')
        append(System.identityHashCode(state.cube))
        append(':')
        append(System.identityHashCode(state.solution))
        append(':')
        append(state.speed)
    }

    fun setSize(n: Int) {
        if (n == _state.value.n) return
        stopAll()
        solveTicket++
        manualHistory.clear()
        _state.update { CubeUiState(n = n, cube = CubeState(n), speed = it.speed) }
    }

    fun scramble() {
        stopAll()
        solveTicket++
        manualHistory.clear()
        val n = _state.value.n
        val moves = Scrambler.generate(n, Scrambler.lengthFor(n))
        val cube = CubeState(n)
        for (move in moves) cube.apply(move)
        _state.update {
            it.copy(
                cube = cube,
                scramble = moves,
                solution = emptyList(),
                solutionIndex = 0,
                stages = null,
                message = null,
                canUndo = false,
                playing = false,
                activeMove = null,
                progress = 0f,
            )
        }
    }

    fun reset() {
        stopAll()
        solveTicket++
        manualHistory.clear()
        _state.update {
            it.copy(
                cube = CubeState(it.n),
                scramble = emptyList(),
                solution = emptyList(),
                solutionIndex = 0,
                stages = null,
                message = null,
                canUndo = false,
                playing = false,
                activeMove = null,
                progress = 0f,
            )
        }
    }

    fun setCube(cube: CubeState) {
        stopAll()
        solveTicket++
        manualHistory.clear()
        _state.update {
            CubeUiState(n = cube.n, cube = cube, speed = it.speed)
        }
    }

    fun solve() {
        if (_state.value.solving) return
        stopAll()
        val snapshot = _state.value.cube.copy()
        if (snapshot.isSolved()) {
            _state.update { it.copy(message = "Кубик уже собран") }
            return
        }

        val ticket = ++solveTicket
        _state.update {
            it.copy(
                solving = true,
                message = null,
                solution = emptyList(),
                solutionIndex = 0,
                stages = null,
                canUndo = manualHistory.isNotEmpty(),
            )
        }

        solveJob = viewModelScope.launch {
            val result = withContext(Dispatchers.Default) {
                val found = runCatching { NativeSolver.solve(snapshot) }.getOrNull() ?: return@withContext null
                // Replay in Kotlin: protects against any model drift between
                // the native engine and the renderer.
                val check = snapshot.copy()
                for (move in found.moves) check.apply(move)
                if (check.isSolved()) found else null
            }
            if (ticket != solveTicket) return@launch

            if (result == null) {
                _state.update {
                    it.copy(
                        solving = false,
                        message = "Не удалось собрать: проверьте цвета. Состояние должно быть разрешимым.",
                    )
                }
            } else {
                _state.update {
                    it.copy(
                        solving = false,
                        solution = result.moves,
                        solutionIndex = 0,
                        stages = result.stages,
                        message = if (result.moves.isEmpty()) "Кубик уже собран" else null,
                    )
                }
            }
        }
    }

    fun manualMove(move: CubeMove) {
        if (_state.value.solving) return
        stopAll()
        _state.update {
            it.copy(
                solution = emptyList(),
                solutionIndex = 0,
                stages = null,
                message = null,
            )
        }
        animationJob = viewModelScope.launch {
            animateCube(move, 150)
            manualHistory.addLast(move)
            _state.update { it.copy(canUndo = true) }
        }
    }

    fun undo() {
        if (_state.value.solving) return
        stopAll()
        val current = _state.value
        val move = when {
            current.solution.isNotEmpty() && current.solutionIndex > 0 ->
                current.solution[current.solutionIndex - 1].inverse

            current.solution.isEmpty() && manualHistory.isNotEmpty() ->
                manualHistory.removeLast().inverse

            else -> null
        } ?: return

        animationJob = viewModelScope.launch {
            animateCube(move, 130)
            _state.update {
                if (it.solution.isNotEmpty()) {
                    it.copy(solutionIndex = (it.solutionIndex - 1).coerceAtLeast(0))
                } else {
                    it.copy(canUndo = manualHistory.isNotEmpty())
                }
            }
        }
    }

    fun play() {
        val current = _state.value
        if (current.solution.isEmpty() || current.solutionIndex >= current.solution.size) return
        stopAll()

        if (current.speed == 0) {
            seekTo(current.solution.size)
            return
        }

        playbackJob = viewModelScope.launch {
            _state.update { it.copy(playing = true) }
            try {
                while (isActive) {
                    val snapshot = _state.value
                    if (snapshot.solutionIndex >= snapshot.solution.size) break
                    val speed = snapshot.speed
                    if (speed == 0) {
                        // Very long solutions would block the UI thread, so
                        // "instant" playback walks the cube in chunks.
                        applyChunk(2000)
                        continue
                    }
                    val duration: Long = if (speed <= 8) (1000 / speed).coerceIn(60, 400).toLong() else 0L
                    applyNextSolutionMove(duration)
                    if (duration == 0L) delay((1000 / speed).toLong().coerceAtLeast(1))
                }
            } finally {
                _state.update { it.copy(playing = false, activeMove = null, progress = 0f) }
            }
        }
    }

    fun pause() {
        stopPlayback()
        _state.update { it.copy(activeMove = null, progress = 0f) }
    }

    fun stepForward() {
        if (_state.value.solving) return
        stopAll()
        animationJob = viewModelScope.launch {
            try {
                applyNextSolutionMove(160)
            } finally {
                _state.update { it.copy(activeMove = null, progress = 0f) }
            }
        }
    }

    fun stepBack() {
        if (_state.value.solving) return
        stopAll()
        val current = _state.value
        if (current.solution.isEmpty() || current.solutionIndex == 0) {
            undo()
            return
        }
        val move = current.solution[current.solutionIndex - 1].inverse
        animationJob = viewModelScope.launch {
            try {
                animateCube(move, 140)
                _state.update { it.copy(solutionIndex = (it.solutionIndex - 1).coerceAtLeast(0)) }
            } finally {
                _state.update { it.copy(activeMove = null, progress = 0f) }
            }
        }
    }

    fun seekTo(target: Int) {
        stopAll()
        val current = _state.value
        if (current.solution.isEmpty()) return
        val bounded = target.coerceIn(0, current.solution.size)
        if (bounded == current.solutionIndex) return

        // Seeking into the middle of a huge solution applies a lot of moves,
        // so the walk happens off the main thread in one shot per call.
        val ticket = ++seekTicket
        val from = current.solutionIndex
        val solution = current.solution
        val startCube = current.cube.copy()
        seekJob?.cancel()
        animationJob?.cancel()
        seekJob = viewModelScope.launch(Dispatchers.Default) {
            val cube = startCube
            if (bounded > from) {
                for (i in from until bounded) {
                    if (ticket != seekTicket) return@launch
                    cube.apply(solution[i])
                }
            } else {
                for (i in from - 1 downTo bounded) {
                    if (ticket != seekTicket) return@launch
                    cube.apply(solution[i].inverse)
                }
            }
            if (ticket != seekTicket) return@launch
            _state.update { it.copy(cube = cube, solutionIndex = bounded) }
        }
    }

    fun setSpeed(speed: Int) {
        _state.update { it.copy(speed = speed) }
    }

    fun consumeMessage() {
        _state.update { it.copy(message = null) }
    }

    /** Applies up to [chunkSize] remaining moves without animation. */
    private fun applyChunk(chunkSize: Int) {
        val current = _state.value
        val to = (current.solutionIndex + chunkSize).coerceAtMost(current.solution.size)
        if (to == current.solutionIndex) return
        val cube = current.cube.copy()
        for (i in current.solutionIndex until to) cube.apply(current.solution[i])
        _state.update { it.copy(cube = cube, solutionIndex = to) }
    }

    private suspend fun applyNextSolutionMove(durationMs: Long) {
        val current = _state.value
        if (current.solutionIndex >= current.solution.size) return
        val move = current.solution[current.solutionIndex]

        if (durationMs <= 0) {
            _state.update {
                it.copy(
                    cube = it.cube.applied(move),
                    solutionIndex = it.solutionIndex + 1,
                )
            }
            return
        }

        _state.update { it.copy(activeMove = move, progress = 0f) }
        val steps = 10
        val stepDelay = (durationMs / steps).coerceAtLeast(1)
        for (i in 1..steps) {
            delay(stepDelay)
            _state.update { it.copy(progress = i / steps.toFloat()) }
        }
        _state.update {
            it.copy(
                cube = it.cube.applied(move),
                activeMove = null,
                progress = 0f,
                solutionIndex = it.solutionIndex + 1,
            )
        }
    }

    /** Animates [move] and commits it to the cube without touching the solution index. */
    private suspend fun animateCube(move: CubeMove, durationMs: Long) {
        _state.update { it.copy(activeMove = move, progress = 0f) }
        val steps = 10
        val stepDelay = (durationMs / steps).coerceAtLeast(1)
        for (i in 1..steps) {
            delay(stepDelay)
            _state.update { it.copy(progress = i / steps.toFloat()) }
        }
        _state.update { it.copy(cube = it.cube.applied(move), activeMove = null, progress = 0f) }
    }

    private fun stopAnimation() {
        animationJob?.cancel()
        animationJob = null
        if (_state.value.activeMove != null) {
            _state.update { it.copy(activeMove = null, progress = 0f) }
        }
    }

    private fun stopPlayback() {
        playbackJob?.cancel()
        playbackJob = null
        if (_state.value.playing) {
            _state.update { it.copy(playing = false) }
        }
    }

    private fun stopSeek() {
        seekTicket++
        seekJob?.cancel()
        seekJob = null
    }

    private fun stopAll() {
        stopAnimation()
        stopPlayback()
        stopSeek()
    }

    override fun onCleared() {
        super.onCleared()
        solveTicket++
        stopAll()
        // Final best-effort snapshot when the screen is really finished (not
        // on rotation: the ViewModel survives that).
        try {
            kotlinx.coroutines.runBlocking(Dispatchers.IO) {
                persist(_state.value, force = true)
            }
        } catch (_: Throwable) {
            // Ignore: autosave already has a recent snapshot.
        }
    }

    private companion object {
        const val AUTOSAVE_INTERVAL_MS = 1_500L
    }
}
