package com.example.jokerprotocol

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.foundation.layout.*
import androidx.compose.material3.Button
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.unit.dp
import com.example.jokerprotocol.theme.JokerProtocolTheme

class MainActivity : ComponentActivity() {
    
    // Load the JNI library
    init {
        System.loadLibrary("joker_jni")
    }

    external fun stringFromJNI(): String

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()
        setContent {
            JokerProtocolTheme { 
                Surface(
                    modifier = Modifier.fillMaxSize(), 
                    color = MaterialTheme.colorScheme.background
                ) { 
                    JokerScreen(::stringFromJNI)
                } 
            }
        }
    }
}

@Composable
fun JokerScreen(getJniString: () -> String) {
    var jniText by remember { mutableStateOf("Press the button to test JNI") }

    Column(
        modifier = Modifier.fillMaxSize(),
        verticalArrangement = Arrangement.Center,
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text(text = jniText)
        Spacer(modifier = Modifier.height(16.dp))
        Button(onClick = { jniText = getJniString() }) {
            Text("Start Protocol (Test JNI)")
        }
    }
}
