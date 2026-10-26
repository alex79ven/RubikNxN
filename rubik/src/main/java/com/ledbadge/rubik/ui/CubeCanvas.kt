package com.ledbadge.rubik.ui

import androidx.compose.foundation.Canvas
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.gestures.detectTransformGestures
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.Path
import androidx.compose.ui.graphics.drawscope.DrawScope
import androidx.compose.ui.input.pointer.pointerInput
import com.ledbadge.rubik.cube.CubeMove
import com.ledbadge.rubik.cube.CubeState
import kotlin.math.PI
import kotlin.math.cos
import kotlin.math.floor
import kotlin.math.max
import kotlin.math.min
import kotlin.math.sin

/** Sticker palette in U R F D L B order, the face order the solver uses. */
val FaceColors: List<Color> = listOf(
    Color(0xFFF2F2ED), // U
    Color(0xFFD8382C), // R
    Color(0xFF2E9E4F), // F
    Color(0xFFF3C32F), // D
    Color(0xFFF0781F), // L
    Color(0xFF2A6FD6), // B
)

private data class Vec3(val x: Float, val y: Float, val z: Float) {
    fun dot(o: Vec3): Float = x * o.x + y * o.y + z * o.z
}

/** Outward normals in U R F D L B order. */
private val FaceNormals = arrayOf(
    Vec3(0f, 1f, 0f),
    Vec3(1f, 0f, 0f),
    Vec3(0f, 0f, 1f),
    Vec3(0f, -1f, 0f),
    Vec3(-1f, 0f, 0f),
    Vec3(0f, 0f, -1f),
)

private val LightDir = Vec3(-0.381f, 0.762f, 0.524f)

/**
 * Centre of a sticker cell in cube coordinates.
 *
 * The cube spans `-n/2 .. n/2`; face stickers sit on the outer surface while
 * side face rows sit on layer planes. `r` and `c` are floats so that cell
 * corners can be evaluated by passing `r ± 0.5`.
 */
private fun cellCenter(face: Int, r: Float, c: Float, n: Int): Vec3 {
    val m = (n - 1) * 0.5f
    val h = n * 0.5f
    return when (face) {
        0 -> Vec3(c - m, h, r - m) // U
        1 -> Vec3(h, m - r, m - c) // R
        2 -> Vec3(c - m, m - r, h) // F
        3 -> Vec3(c - m, -h, m - r) // D
        4 -> Vec3(-h, m - r, c - m) // L
        else -> Vec3(m - c, m - r, -h) // B
    }
}

/** Rodrigues rotation of [p] about a unit [axis] by [angle] radians. */
private fun rotateAbout(p: Vec3, axis: Vec3, angle: Float): Vec3 {
    val c = cos(angle)
    val s = sin(angle)
    val d = p.dot(axis)
    return Vec3(
        p.x * c + (axis.y * p.z - axis.z * p.y) * s + axis.x * d * (1f - c),
        p.y * c + (axis.z * p.x - axis.x * p.z) * s + axis.y * d * (1f - c),
        p.z * c + (axis.x * p.y - axis.y * p.x) * s + axis.z * d * (1f - c),
    )
}

/** Yaw around Y, then pitch around X. The camera sits on +Z. */
private fun toView(p: Vec3, yawDeg: Float, pitchDeg: Float): Vec3 {
    val yaw = yawDeg * (PI.toFloat() / 180f)
    val pitch = pitchDeg * (PI.toFloat() / 180f)
    val cy = cos(yaw)
    val sy = sin(yaw)
    val x1 = p.x * cy + p.z * sy
    val z1 = -p.x * sy + p.z * cy
    val cp = cos(pitch)
    val sp = sin(pitch)
    return Vec3(
        x1,
        p.y * cp - z1 * sp,
        p.y * sp + z1 * cp,
    )
}

private fun Color.shaded(factor: Float): Color = Color(
    red = (red * factor).coerceIn(0f, 1f),
    green = (green * factor).coerceIn(0f, 1f),
    blue = (blue * factor).coerceIn(0f, 1f),
    alpha = alpha,
)

private fun quadPath(pts: Array<Offset>, scale: Float): Path {
    var cx = 0f
    var cy = 0f
    for (p in pts) {
        cx += p.x
        cy += p.y
    }
    cx /= pts.size
    cy /= pts.size
    val path = Path()
    for (i in pts.indices) {
        val x = cx + (pts[i].x - cx) * scale
        val y = cy + (pts[i].y - cy) * scale
        if (i == 0) path.moveTo(x, y) else path.lineTo(x, y)
    }
    path.close()
    return path
}

private class StickerQuad(val depth: Float, val corners: Array<Offset>, val color: Color)

/**
 * Renders the cube from three visible sides. Drag to orbit the camera, double
 * tap to reset the view. The layer being turned by [activeMove] is rotated by
 * [progress] (0..1) about its axis, matching the committed state at 1.
 */
@Composable
fun CubeCanvas(
    cube: CubeState,
    activeMove: CubeMove?,
    progress: Float,
    modifier: Modifier = Modifier,
) {
    var yaw by rememberSaveable { mutableFloatStateOf(-32f) }
    var pitch by rememberSaveable { mutableFloatStateOf(22f) }
    var zoom by rememberSaveable { mutableFloatStateOf(1f) }

    Canvas(
        modifier = modifier
            .pointerInput(Unit) {
                // One finger orbits, two fingers pinch to zoom the cube.
                detectTransformGestures(panZoomLock = true) { _, pan, factor, _ ->
                    yaw += pan.x * 0.45f
                    pitch = (pitch + pan.y * 0.45f).coerceIn(-86f, 86f)
                    if (factor != 1f) zoom = (zoom * factor).coerceIn(0.6f, 3f)
                }
            }
            .pointerInput(Unit) {
                detectTapGestures(onDoubleTap = {
                    yaw = -32f
                    pitch = 22f
                    zoom = 1f
                })
            },
    ) {
        drawCube(cube, activeMove, progress, yaw, pitch, zoom)
    }
}

private fun DrawScope.drawCube(
    cube: CubeState,
    move: CubeMove?,
    progress: Float,
    yaw: Float,
    pitch: Float,
    zoom: Float,
) {
    val n = cube.n
    if (n < 2) return

    val h = n * 0.5f
    val camZ = 3.4f
    val focal = min(size.width, size.height) * 1.5f * zoom
    val cx = size.width / 2f
    val cy = size.height / 2f
    val inv = 1f / n

    val moveAxis = move?.let { FaceNormals[it.face.ordinal] }
    val p = progress.coerceIn(0f, 1f)
    val angle = if (move != null && p > 0f) {
        (-PI.toFloat() / 2f) * move.turn.quarterTurns * p
    } else {
        0f
    }
    val animating = move != null && angle != 0f

    fun project(v: Vec3): Offset {
        val depth = camZ - v.z
        val s = focal / depth
        return Offset(cx + v.x * s, cy - v.y * s)
    }

    fun layerAlong(axis: Vec3, face: Int, r: Float, c: Float): Int =
        floor(h - cellCenter(face, r, c, n).dot(axis) + 1e-3f).toInt()

    fun inMovingSlab(face: Int, r: Float, c: Float): Boolean {
        if (!animating) return false
        val m = move ?: return false
        val axis = moveAxis ?: return false
        val layer = layerAlong(axis, face, r, c)
        return layer >= m.outerDepth && layer <= m.innerDepth
    }

    val quads = ArrayList<StickerQuad>(n * n * 3)

    for (face in 0 until 6) {
        val normal = FaceNormals[face]
        if (toView(normal, yaw, pitch).z <= 0.01f) continue

        for (r in 0 until n) {
            for (c in 0 until n) {
                val rotating = inMovingSlab(face, r.toFloat(), c.toFloat())
                val shadeNormal = if (rotating) {
                    rotateAbout(normal, moveAxis!!, angle)
                } else {
                    normal
                }
                val viewNormal = toView(shadeNormal, yaw, pitch)
                val light = 0.62f + 0.38f * max(0f, viewNormal.dot(LightDir))
                val color = FaceColors[cube.colorAt(face, r, c).coerceIn(0, 5)].shaded(light)

                val corners = arrayOf(
                    cellCenter(face, r - 0.5f, c - 0.5f, n),
                    cellCenter(face, r - 0.5f, c + 0.5f, n),
                    cellCenter(face, r + 0.5f, c + 0.5f, n),
                    cellCenter(face, r + 0.5f, c - 0.5f, n),
                )

                var zSum = 0f
                val pts = Array(4) { i ->
                    val base = corners[i]
                    val world = if (rotating) rotateAbout(base, moveAxis!!, angle) else base
                    val v = toView(Vec3(world.x * inv, world.y * inv, world.z * inv), yaw, pitch)
                    zSum += v.z
                    project(v)
                }
                quads += StickerQuad(camZ - zSum / 4f, pts, color)
            }
        }
    }

    // Painter's algorithm: farthest quads first.
    quads.sortByDescending { it.depth }

    for (quad in quads) {
        drawPath(quadPath(quad.corners, 1f), Color(0xFF0B0C10))
        drawPath(quadPath(quad.corners, 0.86f), quad.color)
    }
}
