package com.ledbadge.rubik.ui

import android.Manifest
import android.content.Context
import android.content.pm.PackageManager
import android.graphics.Bitmap
import android.graphics.Matrix
import android.view.Surface
import android.view.ViewGroup
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageCapture
import androidx.camera.core.ImageCaptureException
import androidx.camera.core.ImageProxy
import androidx.camera.core.Preview
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.camera.view.PreviewView
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.horizontalScroll
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxScope
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.FilterChip
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalLifecycleOwner
import androidx.compose.ui.unit.dp
import androidx.core.content.ContextCompat
import kotlin.math.max
import kotlin.math.min

/**
 * Sampling resolution of a captured face photo: the centred square crop is
 * scaled down to this side before the sticker colours are picked.
 */
private const val SAMPLE_SIZE = 512

/** Normalised reference sticker colours: max-channel normalisation — squared distance picks a color. */
private val ReferenceStickers = listOf(
    Triple(0.955f, 0.955f, 0.930f), // U
    Triple(0.760f, 0.196f, 0.152f), // R
    Triple(0.161f, 0.554f, 0.275f), // F
    Triple(0.916f, 0.737f, 0.178f), // D
    Triple(0.825f, 0.420f, 0.108f), // L
    Triple(0.141f, 0.384f, 0.741f), // B
)

private fun nearestFaceColor(r: Int, g: Int, b: Int): Int {
    val mx = maxOf(r, g, b).coerceAtLeast(1).toFloat()
    val fr = r / mx
    val fg = g / mx
    val fb = b / mx
    var best = 0
    var bestDist = Float.MAX_VALUE
    for (index in 0 until ReferenceStickers.size) {
        val (cr, cg, cb) = ReferenceStickers[index]
        val dist = (fr - cr) * (fr - cr) + (fg - cg) * (fg - cg) + (fb - cb) * (fb - cb)
        if (dist < bestDist) {
            bestDist = dist
            best = index
        }
    }
    return best
}

/**
 * Photo editor for one cube face. Shows the camera with an N×N guide grid,
 * captures a still and turns the colours of every cell into sticker indices
 * which the caller merges into the flat net.
 *
 * The reviewed snapshot can be discarded ("Заново") and the target face is
 * advanced automatically after each accepted capture.
 */
@Composable
internal fun PhotoFaceEditor(
    n: Int,
    captured: Set<Int>,
    onSampled: (face: Int, colors: IntArray) -> Unit,
) {
    val context = LocalContext.current
    val lifecycleOwner = LocalLifecycleOwner.current
    val mainExecutor = remember(context) { ContextCompat.getMainExecutor(context) }

    var granted by remember {
        mutableStateOf(
            ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) ==
                PackageManager.PERMISSION_GRANTED,
        )
    }
    var requested by remember { mutableStateOf(false) }
    val permissionLauncher = rememberLauncherForActivityResult(
        ActivityResultContracts.RequestPermission(),
    ) { granted = it }
    LaunchedEffect(requested) {
        if (!requested) {
            requested = true
            if (!granted) permissionLauncher.launch(Manifest.permission.CAMERA)
        }
    }

    if (!granted) {
        Column(
            modifier = Modifier
                .fillMaxWidth()
                .padding(vertical = 20.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(10.dp),
        ) {
            Text(
                text = "Чтобы снять грань кубика, нужен доступ к камере.",
                style = MaterialTheme.typography.bodyMedium,
            )
            Button(onClick = { permissionLauncher.launch(Manifest.permission.CAMERA) }) {
                Text("Разрешить камеру")
            }
        }
        return
    }

    var targetFace by remember(n) {
        mutableIntStateOf(((0..5).firstOrNull { it !in captured }) ?: 0)
    }
    var reviewBitmap by remember(n) { mutableStateOf<Bitmap?>(null) }
    var reviewColors by remember(n) { mutableStateOf<IntArray?>(null) }
    var cameraError by remember { mutableStateOf<String?>(null) }

    val imageCapture = remember { ImageCapture.Builder().build() }
    var onDisposeExtras by remember { mutableStateOf<(() -> Unit)?>(null) }
    DisposableEffect(Unit) {
        onDispose { onDisposeExtras?.invoke() }
    }

    val photo = reviewBitmap
    val colors = reviewColors
    if (photo != null && colors != null) {
        PhotoReviewGrid(
            bitmap = photo,
            colors = colors,
            n = n,
            face = targetFace,
            alreadyCaptured = targetFace in captured,
            onRetake = {
                reviewColors = null
                reviewBitmap = null
            },
            onAccept = {
                onSampled(targetFace, colors)
                val usedSet = captured + targetFace
                val next = (0..5).firstOrNull { it !in usedSet }
                if (next != null) targetFace = next
                reviewColors = null
                reviewBitmap = null
            },
        )
        return
    }

    Column(
        modifier = Modifier.fillMaxWidth(),
        horizontalAlignment = Alignment.CenterHorizontally,
        verticalArrangement = Arrangement.spacedBy(10.dp),
    ) {
        Text(
            text = "Снимаем «${FaceLetters[targetFace]}» — ${FaceNames[targetFace]}. " +
                "Впиши грань в квадрат и держи камеру параллельно грани.",
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )

        FaceTargetChips(captured = captured, selected = targetFace, onSelect = { targetFace = it })

        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(RoundedCornerShape(12.dp)),
        ) {
            AndroidCameraPreview(
                imageCapture = imageCapture,
                lifecycleOwner = lifecycleOwner,
                mainExecutor = mainExecutor,
                onError = { cameraError = it },
                onDisposeHook = { onDisposeExtras = it },
                modifier = Modifier.fillMaxSize(),
            )
            GuideGridOverlay(n = n, modifier = Modifier.fillMaxSize())
        }

        cameraError?.let { message ->
            Text(
                text = message,
                style = MaterialTheme.typography.bodySmall,
                color = MaterialTheme.colorScheme.error,
            )
        }

        Button(
            onClick = {
                imageCapture.takePicture(
                    mainExecutor,
                    object : ImageCapture.OnImageCapturedCallback() {
                        override fun onCaptureSuccess(image: ImageProxy) {
                            val analysis = analyseFacePhoto(image, n)
                            image.close()
                            if (analysis == null) {
                                cameraError = "Не удалось обработать снимок"
                            } else {
                                cameraError = null
                                reviewBitmap = analysis.first
                                reviewColors = analysis.second
                            }
                        }

                        override fun onError(exception: ImageCaptureException) {
                            cameraError = "Ошибка камеры: " +
                                (exception.message ?: exception.javaClass.simpleName)
                        }
                    },
                )
            },
            modifier = Modifier.fillMaxWidth(),
        ) {
            Text("Снять грань")
        }
    }
}

@Composable
private fun AndroidCameraPreview(
    imageCapture: ImageCapture,
    lifecycleOwner: androidx.lifecycle.LifecycleOwner,
    mainExecutor: java.util.concurrent.Executor,
    onError: (String) -> Unit,
    onDisposeHook: (() -> Unit) -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    androidx.compose.ui.viewinterop.AndroidView(
        factory = { ctx ->
            val view = PreviewView(ctx)
            view.layoutParams = ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT,
            )
            val future = ProcessCameraProvider.getInstance(ctx)
            future.addListener(
                {
                    try {
                        val provider = future.get()
                        val preview = Preview.Builder().build()
                        preview.setSurfaceProvider(view.surfaceProvider)
                        provider.unbindAll()
                        provider.bindToLifecycle(
                            lifecycleOwner,
                            CameraSelector.DEFAULT_BACK_CAMERA,
                            preview,
                            imageCapture,
                        )
                        imageCapture.targetRotation =
                            view.display?.rotation ?: Surface.ROTATION_0
                        onDisposeHook {
                            try {
                                provider.unbindAll()
                            } catch (_: Exception) {
                                // camera is already released
                            }
                        }
                    } catch (t: Throwable) {
                        onError("Камера недоступна: ${t.message ?: t.javaClass.simpleName}")
                    }
                },
                mainExecutor,
            )
            view
        },
        modifier = modifier,
    )
}

/** Removes guide grid over the live preview so the user can align the face. */
@Composable
private fun BoxScope.GuideGridOverlay(n: Int, modifier: Modifier = Modifier) {
    Canvas(modifier) {
        val cell = size.width / n
        val line = StrokeWidth
        val color = Color(0xB3FFFFFF)
        for (i in 0..n) {
            val x = i * cell
            drawLine(color, Offset(x, 0f), Offset(x, size.height), line)
            drawLine(color, Offset(0f, i * cell), Offset(size.width, i * cell), line)
        }
    }
}

@Composable
private fun FaceTargetChips(
    captured: Set<Int>,
    selected: Int,
    onSelect: (Int) -> Unit,
) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .horizontalScroll(rememberScrollState()),
        horizontalArrangement = Arrangement.spacedBy(6.dp),
    ) {
        for (face in 0..5) {
            FilterChip(
                selected = face == selected,
                onClick = { onSelect(face) },
                label = {
                    Text(FaceLetters[face] + if (face in captured) " ✓" else "")
                },
            )
        }
    }
}

@Composable
private fun PhotoReviewGrid(
    bitmap: Bitmap,
    colors: IntArray,
    n: Int,
    face: Int,
    alreadyCaptured: Boolean,
    onRetake: () -> Unit,
    onAccept: () -> Unit,
) {
    Column(
        modifier = Modifier.fillMaxWidth(),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Box(
            modifier = Modifier
                .fillMaxWidth()
                .aspectRatio(1f)
                .clip(RoundedCornerShape(12.dp)),
        ) {
            Image(
                bitmap = bitmap.asImageBitmap(),
                contentDescription = null,
                contentScale = ContentScale.FillBounds,
                modifier = Modifier.fillMaxSize(),
            )
            Canvas(modifier = Modifier.fillMaxSize()) {
                val cell = size.width / n
                for (row in 0 until n) {
                    for (col in 0 until n) {
                        drawRect(
                            color = FaceColors[
                                colors[row * n + col].coerceIn(0, 5),
                            ].copy(alpha = 0.55f),
                            topLeft = Offset(col * cell, row * cell),
                            size = Size(cell, cell),
                        )
                    }
                }
                val line = StrokeWidth
                val gridColor = Color(0xB3FFFFFF)
                for (i in 0..n) {
                    val x = i * cell
                    drawLine(gridColor, Offset(x, 0f), Offset(x, size.height), line)
                    drawLine(gridColor, Offset(0f, i * cell), Offset(size.width, i * cell), line)
                }
            }
        }
        Text(
            text = if (alreadyCaptured) {
                "Грань «${FaceLetters[face]}» уже снята — снимок перепишет её."
            } else {
                "Если какой-то цвет считался неверно — поправьте его на вкладке «Сетка»."
            },
            style = MaterialTheme.typography.bodySmall,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
        )
        Row(verticalAlignment = Alignment.CenterVertically) {
            FilledTonalButton(onClick = onRetake) {
                Text("Заново")
            }
            Spacer(modifier = Modifier.weight(1f))
            Button(onClick = onAccept) {
                Text("OK · ${FaceLetters[face]}")
            }
        }
    }
}

/**
 * Rotates the capture to display orientation, crops the centred square (the
 * part the FILL_CENTER preview actually showed) and samples one sticker
 * colour per grid cell.
 */
private fun analyseFacePhoto(image: ImageProxy, n: Int): Pair<Bitmap, IntArray>? {
    val base = try {
        image.toBitmap()
    } catch (_: Throwable) {
        null
    } ?: return null

    val matrix = Matrix().apply { postRotate(image.imageInfo.rotationDegrees.toFloat()) }
    val rotated = try {
        Bitmap.createBitmap(base, 0, 0, base.width, base.height, matrix, true)
    } catch (_: Throwable) {
        base
    }

    val side = min(rotated.width, rotated.height)
    val square = Bitmap.createBitmap(
        rotated,
        (rotated.width - side) / 2,
        (rotated.height - side) / 2,
        side,
        side,
    )

    val small = if (side > SAMPLE_SIZE) {
        Bitmap.createScaledBitmap(square, SAMPLE_SIZE, SAMPLE_SIZE, true)
    } else {
        square
    }
    if (small != square && square != rotated) square.recycle()
    if (rotated != base) base.recycle()

    val colors = IntArray(n * n)
    val cell = SAMPLE_SIZE / n.toFloat()
    val radius = max(1, (cell / 3f).toInt())
    for (row in 0 until n) {
        for (col in 0 until n) {
            val cx = ((col + 0.5f) * cell).toInt().coerceIn(0, SAMPLE_SIZE - 1)
            val cy = ((row + 0.5f) * cell).toInt().coerceIn(0, SAMPLE_SIZE - 1)
            var sr = 0
            var sg = 0
            var sb = 0
            var count = 0
            for (dy in -radius..radius) {
                for (dx in -radius..radius) {
                    val x = (cx + dx).coerceIn(0, SAMPLE_SIZE - 1)
                    val y = (cy + dy).coerceIn(0, SAMPLE_SIZE - 1)
                    val pixel = small.getPixel(x, y)
                    sr += (pixel shr 16) and 0xFF
                    sg += (pixel shr 8) and 0xFF
                    sb += pixel and 0xFF
                    count++
                }
            }
            colors[row * n + col] =
                nearestFaceColor(sr / count, sg / count, sb / count)
        }
    }
    return small to colors
}

private const val StrokeWidth = 2f
