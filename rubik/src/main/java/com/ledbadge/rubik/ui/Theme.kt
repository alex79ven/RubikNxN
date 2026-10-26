package com.ledbadge.rubik.ui

import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color

private val RubikColors = darkColorScheme(
    primary = Color(0xFF8AB4F8),
    onPrimary = Color(0xFF08111F),
    primaryContainer = Color(0xFF1E3557),
    onPrimaryContainer = Color(0xFFD8E6FF),
    secondary = Color(0xFF9AD49A),
    onSecondary = Color(0xFF0B1B0B),
    secondaryContainer = Color(0xFF203A22),
    onSecondaryContainer = Color(0xFFD5F0D4),
    tertiary = Color(0xFFF2C230),
    background = Color(0xFF0E0F14),
    onBackground = Color(0xFFE7E8EE),
    surface = Color(0xFF15161C),
    onSurface = Color(0xFFE7E8EE),
    surfaceVariant = Color(0xFF23242C),
    onSurfaceVariant = Color(0xFFB8BAC6),
    outline = Color(0xFF3A3C48),
    error = Color(0xFFFF9A94),
)

@Composable
fun RubikTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = RubikColors, content = content)
}
