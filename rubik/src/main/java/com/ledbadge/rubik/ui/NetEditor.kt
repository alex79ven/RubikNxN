package com.ledbadge.rubik.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.foundation.layout.Spacer
import androidx.compose.material3.FilterChip
import androidx.compose.material3.Button
import androidx.compose.material3.TextButton
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Tab
import androidx.compose.material3.TabRow
import androidx.compose.material3.Text
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.onSizeChanged
import androidx.compose.ui.unit.IntSize
import androidx.compose.ui.unit.dp
import com.ledbadge.rubik.cube.CubeState

internal val FaceLetters = listOf("U", "R", "F", "D", "L", "B")
internal val FaceNames = listOf("верх", "право", "перед", "низ", "лево", "зад")

private val NetFaceOrder = listOf(4, 2, 1, 5)

/**
 * Flat sticker editor with two ways of entering the cube state: a series of
 * camera photos (one per face) and the plain N×N net editor.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun CubeEditorSheet(
    n: Int,
    initial: CubeState,
    onDismiss: () -> Unit,
    onApply: (CubeState) -> Unit,
) {
    val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
    var cube by remember(n) { mutableStateOf(initial.copy()) }
    var selected by remember { mutableIntStateOf(0) }
    var captured by remember(n) { mutableStateOf(emptySet<Int>()) }
    var tab by rememberSaveable { mutableIntStateOf(0) }
    var zoomedFace by rememberSaveable { mutableIntStateOf(-1) }

    ModalBottomSheet(onDismissRequest = onDismiss, sheetState = sheetState) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .verticalScroll(rememberScrollState())
                .padding(horizontal = 16.dp)
                .padding(bottom = 24.dp),
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            TabRow(selectedTabIndex = tab) {
                Tab(selected = tab == 0, onClick = { tab = 0 }, text = { Text("Фото") })
                Tab(selected = tab == 1, onClick = { tab = 1 }, text = { Text("Сетка") })
            }

            if (tab == 0) {
                PhotoFaceEditor(
                    n = n,
                    captured = captured,
                    onSampled = { face, colors ->
                        val updated = cube.copy()
                        for ((index, color) in colors.withIndex()) {
                            updated.setColor(face, index / n, index % n, color)
                        }
                        cube = updated
                        captured = captured + face
                    },
                )
            } else if (zoomedFace >= 0) {
                SingleFaceEditor(
                    face = zoomedFace,
                    n = n,
                    cube = cube,
                    selected = selected,
                    onSelect = { selected = it },
                    onPaint = { row, col ->
                        cube = cube.copy().also { it.setColor(zoomedFace, row, col, selected) }
                    },
                    onCollapse = { zoomedFace = -1 },
                    onSwitch = { zoomedFace = it },
                )
            } else {
                Text("Раскраска граней", style = MaterialTheme.typography.titleMedium)
                Text(
                    text = "Выберите цвет и нажмите на клетку. Цвета идут в порядке U R F D L B.",
                    style = MaterialTheme.typography.bodySmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant,
                )

                PaletteRow(selected = selected, onSelect = { selected = it })

                Net(
                    cube = cube,
                    n = n,
                    onPaint = { face, row, col ->
                        cube = cube.copy().also { it.setColor(face, row, col, selected) }
                    },
                    onExpand = { zoomedFace = it },
                )
            }

            Text(
                text = FaceLetters.indices.joinToString(" · ") {
                    "${FaceLetters[it]} — ${FaceNames[it]}"
                },
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )

            val counts = cube.colorCounts()
            val perFace = n * n
            val balanced = counts.all { it == perFace }
            Text(
                text = if (balanced) {
                    "По $perFace наклеек каждого цвета"
                } else {
                    "Наклеек по цветам: " + counts.joinToString(" ")
                },
                style = MaterialTheme.typography.bodySmall,
                color = if (balanced) {
                    MaterialTheme.colorScheme.onSurfaceVariant
                } else {
                    MaterialTheme.colorScheme.error
                },
            )

            Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                OutlinedButton(
                    onClick = { cube = CubeState(n); captured = emptySet() },
                    modifier = Modifier.weight(1f),
                ) {
                    Text("Сброс")
                }
                Button(
                    onClick = { onApply(cube) },
                    modifier = Modifier.weight(1f),
                ) {
                    Text("Применить")
                }
            }
        }
    }
}

/**
 * Full-screen editor of a single face: the N×N grid is as big as the screen
 * allows, which keeps big cubes (9×9 and up) comfortably tappable. Palette is
 * always visible and neighbouring faces are one chip tap away.
 */
@Composable
private fun SingleFaceEditor(
    face: Int,
    n: Int,
    cube: CubeState,
    selected: Int,
    onSelect: (Int) -> Unit,
    onPaint: (Int, Int) -> Unit,
    onCollapse: () -> Unit,
    onSwitch: (Int) -> Unit,
) {
    val zoom = remember(face, n) { FaceZoom() }

    Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = onCollapse) { Text("⨯ Вся сетка") }
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = "${FaceLetters[face]} — ${FaceNames[face]}",
                style = MaterialTheme.typography.titleSmall,
            )
        }

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .horizontalScroll(rememberScrollState()),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            for (f in 0..5) {
                FilterChip(
                    selected = f == face,
                    onClick = { onSwitch(f) },
                    label = { Text(FaceLetters[f]) },
                )
            }
        }

        ZoomableFaceGrid(
            face = face,
            cube = cube,
            n = n,
            zoom = zoom,
            onPaint = { row, col -> onPaint(row, col) },
        )

        Row(verticalAlignment = Alignment.CenterVertically) {
            Text(
                text = "Масштаб ×" + "%.1f".format(java.util.Locale.US, zoom.scale),
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(modifier = Modifier.weight(1f))
            TextButton(
                onClick = { zoom.reset() },
                enabled = zoom.scale > 1.01f,
            ) {
                Text("1:1")
            }
        }

        PaletteRow(selected = selected, onSelect = onSelect)

        Text(
            text = "Выберите цвет и нажмите на клетку. Щипок двумя пальцами — " +
                "приблизить, двойной тап — вернуть 1:1.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
    }
}

@Composable
private fun PaletteRow(selected: Int, onSelect: (Int) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(12.dp),
    ) {
        for ((index, color) in FaceColors.withIndex()) {
            Column(
                horizontalAlignment = Alignment.CenterHorizontally,
                modifier = Modifier.widthIn(min = 44.dp),
            ) {
                Box(
                    modifier = Modifier
                        .size(38.dp)
                        .clip(CircleShape)
                        .background(color)
                        .border(
                            width = if (selected == index) 3.dp else 1.dp,
                            color = if (selected == index) {
                                MaterialTheme.colorScheme.onSurface
                            } else {
                                Color(0x33FFFFFF)
                            },
                            shape = CircleShape,
                        )
                        .clickable { onSelect(index) },
                )
                Text(FaceLetters[index], style = MaterialTheme.typography.labelSmall)
            }
        }
    }
}

@Composable
private fun Net(
    cube: CubeState,
    n: Int,
    onPaint: (Int, Int, Int) -> Unit,
    onExpand: (Int) -> Unit = {},
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        FaceCell(0, cube, n, onPaint, onExpand, Modifier.fillMaxWidth(0.25f))
        Row(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(6.dp),
        ) {
            for (face in NetFaceOrder) {
                FaceCell(face, cube, n, onPaint, onExpand, Modifier.weight(1f))
            }
        }
        FaceCell(3, cube, n, onPaint, onExpand, Modifier.fillMaxWidth(0.25f))
    }
    Text(
        text = "Нажмите на букву, чтобы открыть грань во весь экран",
        style = MaterialTheme.typography.labelSmall,
        color = MaterialTheme.colorScheme.onSurfaceVariant,
    )
}

@Composable
private fun FaceCell(
    face: Int,
    cube: CubeState,
    n: Int,
    onPaint: (Int, Int, Int) -> Unit,
    onExpand: (Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier) {
        Row(
            modifier = Modifier
                .fillMaxWidth()
                .clickable { onExpand(face) },
            verticalAlignment = Alignment.CenterVertically,
        ) {
            Text(
                text = FaceLetters[face],
                style = MaterialTheme.typography.labelSmall,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
            Spacer(modifier = Modifier.weight(1f))
            Text(
                text = "⤢",
                style = MaterialTheme.typography.labelMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
            )
        }
        FaceGrid(face, cube, n, onPaint)
    }
}

/** Sticker grid of a single face; painting happens on tap. */
@Composable
private fun FaceGrid(
    face: Int,
    cube: CubeState,
    n: Int,
    onPaint: ((face: Int, row: Int, col: Int) -> Unit)? = null,
    modifier: Modifier = Modifier,
) {
    Canvas(
        modifier = modifier
            .fillMaxWidth()
            .aspectRatio(1f)
            .padding(top = 2.dp)
            .clip(RoundedCornerShape(4.dp))
            .background(Color(0xFF0B0C10))
            .then(
                if (onPaint == null) {
                    Modifier
                } else {
                    Modifier.pointerInput(n) {
                        detectTapGestures { offset ->
                            val cellSize = size.width / n.toFloat()
                            val col = (offset.x / cellSize).toInt().coerceIn(0, n - 1)
                            val row = (offset.y / cellSize).toInt().coerceIn(0, n - 1)
                            onPaint(face, row, col)
                        }
                    }
                },
            ),
    ) {
        val cell = size.width / n
        for (row in 0 until n) {
            for (col in 0 until n) {
                drawRect(
                    color = FaceColors[cube.colorAt(face, row, col).coerceIn(0, 5)],
                    topLeft = Offset(col * cell, row * cell),
                    size = Size(cell, cell),
                )
            }
        }
        for (i in 0..n) {
            val line = Color(0x40000000)
            drawLine(line, Offset(i * cell, 0f), Offset(i * cell, size.height), 1f)
            drawLine(line, Offset(0f, i * cell), Offset(size.width, i * cell), 1f)
        }
    }
}

/**
 * Zoom state of the single-face editor. Pan is stored in screen pixels after
 * scaling about the box centre, so taps can be mapped back to a cell exactly.
 */
private class FaceZoom {
    var scale by mutableFloatStateOf(1f)
    var offset by mutableStateOf(Offset.Zero)
    var box by mutableStateOf(IntSize.Zero)

    fun applyGesture(factor: Float, pan: Offset) {
        val next = (scale * factor).coerceIn(MinZoom, MaxZoom)
        scale = next
        offset = if (next <= 1.001f) Offset.Zero else clamp(offset + pan, next)
    }

    /** Double tap: zoom in around the tap or go back to 1:1. */
    fun toggle(anchor: Offset, target: Float = 2.5f) {
        if (scale > 1.01f) {
            reset()
            return
        }
        val s = target.coerceIn(MinZoom, MaxZoom)
        scale = s
        offset = clamp(
            Offset(
                (1f - s) * (anchor.x - box.width / 2f),
                (1f - s) * (anchor.y - box.height / 2f),
            ),
            s,
        )
    }

    fun reset() {
        scale = 1f
        offset = Offset.Zero
    }

    /** Converts a touch position in the zoomed view back to a cell. */
    fun cellAt(anchor: Offset, n: Int): Pair<Int, Int>? {
        if (box.width <= 0 || box.height <= 0) return null
        val cx = box.width / 2f
        val cy = box.height / 2f
        val x = cx + (anchor.x - cx - offset.x) / scale
        val y = cy + (anchor.y - cy - offset.y) / scale
        if (x < 0f || y < 0f || x >= box.width || y >= box.height) return null
        val cell = box.width / n.toFloat()
        val col = (x / cell).toInt().coerceIn(0, n - 1)
        val row = (y / cell).toInt().coerceIn(0, n - 1)
        return row to col
    }

    private fun clamp(raw: Offset, s: Float): Offset {
        val maxX = (s - 1f) * box.width / 2f
        val maxY = (s - 1f) * box.height / 2f
        return Offset(raw.x.coerceIn(-maxX, maxX), raw.y.coerceIn(-maxY, maxY))
    }

    private companion object {
        const val MinZoom = 1f
        const val MaxZoom = 8f
    }
}

/**
 * Pinch-zoomable face grid. Big cubes used to have ~10 px cells even in the
 * full-screen editor; pinching up to 8× makes a single sticker easy to hit.
 * One finger pans while zoomed, double tap toggles 1:1 / 2.5×.
 */
@Composable
private fun ZoomableFaceGrid(
    face: Int,
    cube: CubeState,
    n: Int,
    zoom: FaceZoom,
    onPaint: (row: Int, col: Int) -> Unit,
    modifier: Modifier = Modifier,
) {
    Box(
        modifier = modifier
            .fillMaxWidth()
            .aspectRatio(1f)
            .clip(RoundedCornerShape(4.dp))
            .background(Color(0xFF0B0C10))
            .onSizeChanged { zoom.box = it }
            .pointerInput(face, n) {
                detectTransformGestures(panZoomLock = true) { _, pan, factor, _ ->
                    zoom.applyGesture(factor, pan)
                }
            }
            .pointerInput(face, n) {
                detectTapGestures(onDoubleTap = { zoom.toggle(it) }) { tap ->
                    zoom.cellAt(tap, n)?.let { (row, col) -> onPaint(row, col) }
                }
            },
    ) {
        FaceGrid(
            face = face,
            cube = cube,
            n = n,
            modifier = Modifier
                .fillMaxSize()
                .graphicsLayer {
                    scaleX = zoom.scale
                    scaleY = zoom.scale
                    translationX = zoom.offset.x
                    translationY = zoom.offset.y
                },
        )
    }
}
