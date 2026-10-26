package com.ledbadge.rubik.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.CenterAlignedTopAppBar
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.LinearProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Slider
import androidx.compose.material3.SnackbarHost
import androidx.compose.material3.SnackbarHostState
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.LifecycleEventObserver
import androidx.lifecycle.compose.LocalLifecycleOwner
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import androidx.lifecycle.viewmodel.compose.viewModel
import com.ledbadge.rubik.cube.CubeMove
import com.ledbadge.rubik.cube.Face
import com.ledbadge.rubik.cube.Turn
import kotlin.math.roundToInt

private const val MIN_SIZE = 2
private const val MAX_SIZE = 12

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun RubikApp(viewModel: CubeViewModel = viewModel()) {
    val state by viewModel.state.collectAsStateWithLifecycle()
    val snackbar = remember { SnackbarHostState() }
    var editorOpen by rememberSaveable { mutableStateOf(false) }

    // Save immediately when the app goes to the background, so a task kill or
    // battery death cannot lose more than the last autosave tick.
    val lifecycleOwner = LocalLifecycleOwner.current
    DisposableEffect(lifecycleOwner) {
        val observer = LifecycleEventObserver { _, event ->
            if (event == Lifecycle.Event.ON_STOP) viewModel.saveNow()
        }
        lifecycleOwner.lifecycle.addObserver(observer)
        onDispose { lifecycleOwner.lifecycle.removeObserver(observer) }
    }

    LaunchedEffect(state.message) {
        val message = state.message ?: return@LaunchedEffect
        snackbar.showSnackbar(message)
        viewModel.consumeMessage()
    }

    Scaffold(
        snackbarHost = { SnackbarHost(snackbar) },
        topBar = {
            CenterAlignedTopAppBar(
                title = { Text("Кубик ${state.n}×${state.n}") },
                actions = {
                    TextButton(onClick = { editorOpen = true }) { Text("Грани") }
                },
            )
        },
    ) { padding ->
        Column(
            modifier = Modifier
                .padding(padding)
                .fillMaxSize()
                .padding(horizontal = 16.dp),
            verticalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            SizeSelector(
                current = state.n,
                enabled = !state.solving,
                onSelect = viewModel::setSize,
            )

            Box(modifier = Modifier.fillMaxWidth().weight(1f)) {
                CubeCanvas(
                    cube = state.cube,
                    activeMove = state.activeMove,
                    progress = state.progress,
                    modifier = Modifier.fillMaxSize(),
                )
                if (!state.hasSolution && !state.solving) {
                    Text(
                        text = "Потяните, чтобы повернуть · щипок — масштаб · двойной тап — сброс",
                        style = MaterialTheme.typography.labelSmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant,
                        modifier = Modifier.align(Alignment.BottomCenter),
                    )
                }
            }

            Row(verticalAlignment = Alignment.CenterVertically) {
                Box(modifier = Modifier.weight(1f)) { StatusLine(state) }
                TextButton(
                    onClick = { viewModel.saveNow(notify = true) },
                    contentPadding = PaddingValues(horizontal = 6.dp, vertical = 0.dp),
                ) {
                    Text("Сохранить", style = MaterialTheme.typography.labelSmall)
                }
            }

            ActionRow(state, viewModel)

            if (state.solving) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
            }

            if (state.hasSolution) {
                PlaybackControls(state, viewModel)
            } else {
                ManualControls(
                    enabled = !state.solving,
                    canUndo = state.canUndo,
                    onMove = viewModel::manualMove,
                    onUndo = viewModel::undo,
                )
            }
        }
    }

    if (editorOpen) {
        CubeEditorSheet(
            n = state.n,
            initial = state.cube,
            onDismiss = { editorOpen = false },
            onApply = { cube ->
                viewModel.setCube(cube)
                editorOpen = false
            },
        )
    }
}

@Composable
private fun SizeSelector(current: Int, enabled: Boolean, onSelect: (Int) -> Unit) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(
            text = "Размер",
            style = MaterialTheme.typography.labelLarge,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Row(
            modifier = Modifier
                .weight(1f)
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            for (n in MIN_SIZE..MAX_SIZE) {
                FilterChip(
                    selected = n == current,
                    onClick = { onSelect(n) },
                    enabled = enabled,
                    label = { Text("$n×$n") },
                )
            }
        }
    }
}

@Composable
private fun StatusLine(state: CubeUiState) {
    val text = when {
        state.solving -> "Ищу решение…"
        state.hasSolution -> buildString {
            if (state.solutionIndex >= state.solution.size) {
                append("✓ Готово · ${state.solution.size} ходов")
            } else {
                append("Ход ${state.solutionIndex} из ${state.solution.size}")
                state.stages?.summary()?.takeIf { it.isNotEmpty() }?.let {
                    append("  ·  ")
                    append(it)
                }
            }
        }
        state.cube.isSolved() -> "Кубик собран"
        else -> "Собрано ${state.cube.correctCount()} из ${6 * state.n * state.n} наклеек"
    }

    Text(
        text = text,
        style = MaterialTheme.typography.bodySmall,
        fontFamily = FontFamily.Monospace,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
        maxLines = 2,
        overflow = TextOverflow.Ellipsis,
    )
}

@Composable
private fun ActionRow(state: CubeUiState, viewModel: CubeViewModel) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        horizontalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Button(
            onClick = viewModel::solve,
            enabled = !state.solving,
            modifier = Modifier.weight(1f),
        ) {
            Text(if (state.solving) "Решаю…" else "Собрать")
        }
        OutlinedButton(onClick = viewModel::scramble, enabled = !state.solving) {
            Text("Перемешать")
        }
        OutlinedButton(onClick = viewModel::reset, enabled = !state.solving) {
            Text("Сброс")
        }
    }
}

@Composable
private fun ManualControls(
    enabled: Boolean,
    canUndo: Boolean,
    onMove: (CubeMove) -> Unit,
    onUndo: () -> Unit,
) {
    Row(
        modifier = Modifier.fillMaxWidth(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Row(
            modifier = Modifier
                .weight(1f)
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            for (face in Face.entries) {
                for (turn in listOf(Turn.CW, Turn.CCW)) {
                    OutlinedButton(
                        onClick = { onMove(CubeMove(face, turn = turn)) },
                        enabled = enabled,
                        contentPadding = PaddingValues(horizontal = 10.dp, vertical = 2.dp),
                    ) {
                        Text(face.name + if (turn == Turn.CCW) "'" else "")
                    }
                }
            }
        }
        TextButton(onClick = onUndo, enabled = enabled && canUndo) {
            Text("↶")
        }
    }
}

@Composable
private fun PlaybackControls(state: CubeUiState, viewModel: CubeViewModel) {
    var sliderValue by remember(state.solution) {
        mutableFloatStateOf(state.solutionIndex.toFloat())
    }
    LaunchedEffect(state.solutionIndex, state.solution) {
        sliderValue = state.solutionIndex.toFloat()
    }

    Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
        FormulaStrip(state)
        Slider(
            value = sliderValue,
            onValueChange = { sliderValue = it },
            onValueChangeFinished = { viewModel.seekTo(sliderValue.roundToInt()) },
            valueRange = 0f..state.solution.size.toFloat(),
        )
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(8.dp),
        ) {
            FilledTonalButton(
                onClick = viewModel::stepBack,
                contentPadding = PaddingValues(horizontal = 14.dp),
            ) {
                Text("◀")
            }
            Button(
                onClick = { if (state.playing) viewModel.pause() else viewModel.play() },
                modifier = Modifier.weight(1f),
            ) {
                Text(if (state.playing) "Пауза" else "Играть")
            }
            FilledTonalButton(
                onClick = viewModel::stepForward,
                contentPadding = PaddingValues(horizontal = 14.dp),
            ) {
                Text("▶")
            }
        }
        SpeedRow(state.speed, viewModel::setSpeed)
    }
}

@Composable
private fun FormulaStrip(state: CubeUiState) {
    val listState = rememberLazyListState()
    val done = state.solutionIndex >= state.solution.size
    val active = state.activeMove
    val upcoming = state.solution.getOrNull(state.solutionIndex)

    LaunchedEffect(state.solutionIndex, state.solution) {
        if (state.solution.isNotEmpty()) {
            listState.scrollToItem(
                (state.solutionIndex - 2).coerceIn(0, state.solution.size - 1),
            )
        }
    }

    val caption = when {
        done -> "✓ выполнена"
        active != null -> "сейчас: ${active.notation(state.n)}"
        upcoming != null -> "следующий: ${upcoming.notation(state.n)}"
        else -> ""
    }

    Column(verticalArrangement = Arrangement.spacedBy(3.dp)) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = "Формула",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = caption,
                style = MaterialTheme.typography.titleSmall,
                fontFamily = FontFamily.Monospace,
                color = if (done) {
                    MaterialTheme.colorScheme.primary
                } else {
                    MaterialTheme.colorScheme.onSurface
                },
            )
        }
        LazyRow(
            state = listState,
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(4.dp),
        ) {
            items(state.solution.size) { index ->
                val current = index == state.solutionIndex
                val passed = index < state.solutionIndex
                Text(
                    text = "${index + 1} ${state.solution[index].notation(state.n)}",
                    style = MaterialTheme.typography.labelMedium,
                    fontFamily = FontFamily.Monospace,
                    maxLines = 1,
                    color = when {
                        current -> MaterialTheme.colorScheme.onPrimary
                        passed -> MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f)
                        else -> MaterialTheme.colorScheme.onSurface
                    },
                    modifier = Modifier
                        .background(
                            color = when {
                                current -> MaterialTheme.colorScheme.primary
                                passed -> Color(0x0AFFFFFF)
                                else -> Color(0x14FFFFFF)
                            },
                            shape = RoundedCornerShape(6.dp),
                        )
                        .padding(horizontal = 6.dp, vertical = 3.dp),
                )
            }
        }
    }
}

@Composable
private fun SpeedRow(speed: Int, onSpeed: (Int) -> Unit) {
    val options = listOf(
        0 to "Мгновенно",
        1 to "×1",
        2 to "×2",
        4 to "×4",
        8 to "×8",
        16 to "×16",
    )
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        for ((value, label) in options) {
            FilterChip(
                selected = speed == value,
                onClick = { onSpeed(value) },
                label = { Text(label) },
            )
        }
    }
}
