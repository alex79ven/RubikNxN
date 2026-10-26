package com.ledbadge.rubik

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import com.ledbadge.rubik.ui.RubikApp
import com.ledbadge.rubik.ui.RubikTheme

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            RubikTheme {
                RubikApp()
            }
        }
    }
}
