package vn.hoang.leicafujicam.ui.settings

import android.widget.Toast
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import vn.hoang.leicafujicam.R
import vn.hoang.leicafujicam.data.CaptureSoundRepository
import vn.hoang.leicafujicam.utils.ShutterSoundPlayer
import vn.hoang.leicafujicam.viewmodel.CameraViewModel
import kotlinx.coroutines.launch

@Composable
internal fun CaptureSoundSetting(viewModel: CameraViewModel) {
    val preferences by viewModel.userPreferences.collectAsState()
    val importing by viewModel.captureSoundImporting.collectAsState()
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    var pendingBurst by rememberSaveable { mutableStateOf<Boolean?>(null) }
    var previewing by remember { mutableStateOf(false) }
    val previewPlayer = remember(context) { ShutterSoundPlayer(context.applicationContext) }

    DisposableEffect(previewPlayer) {
        onDispose { previewPlayer.release() }
    }
    LaunchedEffect(previewPlayer, preferences.shutterSoundFileName, preferences.burstSoundFileName) {
        previewPlayer.configure(
            preferences.shutterSoundFileName,
            preferences.burstSoundFileName,
        )
    }
    LaunchedEffect(viewModel, context) {
        viewModel.captureSoundErrors.collect { message ->
            Toast.makeText(context, message, Toast.LENGTH_LONG).show()
        }
    }
    val launcher = rememberLauncherForActivityResult(ActivityResultContracts.GetContent()) { uri ->
        val isBurst = pendingBurst
        pendingBurst = null
        if (uri != null && isBurst != null) viewModel.importCaptureSound(isBurst, uri)
    }

    Column(
        modifier = Modifier.fillMaxWidth().padding(top = 12.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp),
    ) {
        Text(
            stringResource(R.string.settings_shutter_sound_custom_description),
            color = Color.White.copy(alpha = 0.6f),
            fontSize = 13.sp,
        )
        listOf(false, true).forEach { isBurst ->
            val fileName = if (isBurst) preferences.burstSoundFileName else preferences.shutterSoundFileName
            CaptureSoundRow(
                title = stringResource(
                    if (isBurst) R.string.settings_shutter_sound_burst else R.string.settings_shutter_sound_single
                ),
                fileName = fileName,
                enabled = !importing && !previewing && pendingBurst == null,
                onChoose = {
                    previewPlayer.stopBurst()
                    pendingBurst = isBurst
                    launcher.launch("audio/*")
                },
                onPreview = {
                    previewing = true
                    scope.launch {
                        try {
                            if (!previewPlayer.preview(isBurst)) {
                                Toast.makeText(context, R.string.settings_shutter_sound_preview_failed, Toast.LENGTH_LONG).show()
                            }
                        } finally {
                            previewing = false
                        }
                    }
                },
                onReset = {
                    previewPlayer.stopBurst()
                    viewModel.resetCaptureSound(isBurst)
                },
            )
        }
        if (importing) {
            Text(
                stringResource(R.string.settings_shutter_sound_importing),
                color = Color.White.copy(alpha = 0.6f),
                fontSize = 13.sp,
            )
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun CaptureSoundRow(
    title: String,
    fileName: String?,
    enabled: Boolean,
    onChoose: () -> Unit,
    onPreview: () -> Unit,
    onReset: () -> Unit,
) {
    Column(Modifier.fillMaxWidth()) {
        Text(title, color = Color.White, fontSize = 14.sp)
        Text(
            text = fileName?.let(CaptureSoundRepository::displayName)
                ?: stringResource(R.string.settings_shutter_sound_default),
            color = Color.White.copy(alpha = 0.6f),
            fontSize = 13.sp,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
        FlowRow(horizontalArrangement = Arrangement.spacedBy(4.dp)) {
            TextButton(onClick = onChoose, enabled = enabled) {
                Text(stringResource(R.string.settings_shutter_sound_choose))
            }
            TextButton(onClick = onPreview, enabled = enabled) {
                Text(stringResource(R.string.settings_shutter_sound_preview))
            }
            if (fileName != null) {
                TextButton(onClick = onReset, enabled = enabled) {
                    Text(stringResource(R.string.settings_shutter_sound_reset))
                }
            }
        }
    }
}
