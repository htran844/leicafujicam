package vn.hoang.leicafujicam.ui.settings

import android.content.Context
import android.content.Intent
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.Matrix
import android.graphics.PointF
import android.media.ExifInterface
import android.net.Uri
import android.os.Build
import androidx.activity.compose.BackHandler
import androidx.activity.compose.rememberLauncherForActivityResult
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.gestures.detectDragGestures
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.Checkbox
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedButton
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.material3.TopAppBar
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.core.content.FileProvider
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONArray
import org.json.JSONObject
import vn.hoang.leicafujicam.R
import vn.hoang.leicafujicam.data.CustomImportManager
import vn.hoang.leicafujicam.lut.LutInfo
import vn.hoang.leicafujicam.lut.LutParser
import vn.hoang.leicafujicam.lut.calibration.CalibrationPackStore
import vn.hoang.leicafujicam.lut.calibration.ChartCalibration
import vn.hoang.leicafujicam.lut.calibration.PlutIO
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import kotlin.math.max

/** Built-in looks calibrated by default, in favorites order. */
private val DEFAULT_STYLE_IDS = listOf(
    "leica", "cc", "Hasselblad", "tomosaki",
    "kodak_portra_400", "kodak_gold_200", "nc", "teal_orange"
)
private const val CALIBRATION_CATEGORY = "Device Calibration"

private enum class CalibStep { Guide, Corners, Result, Done, VerifyResult }
private enum class CornersMode { Calibrate, Verify }

private fun defaultCorners(): List<Offset> = listOf(
    Offset(0.08f, 0.08f), Offset(0.92f, 0.08f),
    Offset(0.92f, 0.92f), Offset(0.08f, 0.92f)
)

private fun formatDe(value: Float?): String =
    if (value == null) "-" else String.format(Locale.US, "%.2f", value)

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun DeviceCalibrationScreen(
    onBack: () -> Unit,
    onCalibrationChanged: () -> Unit
) {
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    val customImportManager = remember { CustomImportManager(context) }

    var step by remember { mutableStateOf(CalibStep.Guide) }
    var manifest by remember { mutableStateOf<JSONObject?>(null) }
    var builtIns by remember { mutableStateOf<List<LutInfo>>(emptyList()) }
    var bitmap by remember { mutableStateOf<Bitmap?>(null) }
    var cornersMode by remember { mutableStateOf(CornersMode.Calibrate) }
    var corners by remember { mutableStateOf(defaultCorners()) }
    var measured by remember { mutableStateOf<Array<FloatArray>?>(null) }
    var detectedRotation by remember { mutableIntStateOf(0) }
    var correctionData by remember { mutableStateOf<FloatArray?>(null) }
    var deBefore by remember { mutableStateOf<Pair<Float, Float>?>(null) }
    var deAfter by remember { mutableStateOf<Pair<Float, Float>?>(null) }
    var selectedStyles by remember { mutableStateOf(DEFAULT_STYLE_IDS.toSet()) }
    var busy by remember { mutableStateOf(false) }
    var errorText by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        withContext(Dispatchers.IO) {
            manifest = CalibrationPackStore.loadManifest(context)
            val all = LutParser.listAvailableLuts(context, "luts")
            builtIns = DEFAULT_STYLE_IDS.mapNotNull { id -> all.firstOrNull { it.id == id } }
        }
    }

    val pickPhoto = rememberLauncherForActivityResult(
        ActivityResultContracts.GetContent()
    ) { uri ->
        if (uri != null) {
            busy = true
            scope.launch {
                val decoded = withContext(Dispatchers.IO) { decodeChartBitmap(context, uri) }
                busy = false
                if (decoded != null) {
                    bitmap = decoded
                    corners = defaultCorners()
                    step = CalibStep.Corners
                } else {
                    errorText = context.getString(R.string.calibration_error_photo)
                }
            }
        }
    }

    fun runExtraction() {
        val bmp = bitmap ?: return
        busy = true
        scope.launch {
            val extracted = withContext(Dispatchers.Default) {
                val pts = Array(4) { i ->
                    PointF(corners[i].x * bmp.width, corners[i].y * bmp.height)
                }
                ChartCalibration.extractBestRotation(bmp, pts)
            }
            if (extracted == null) {
                busy = false
                errorText = context.getString(R.string.calibration_error_extract)
                return@launch
            }
            detectedRotation = extracted.first
            measured = extracted.second
            if (cornersMode == CornersMode.Calibrate) {
                val fitted = withContext(Dispatchers.Default) {
                    val correction = ChartCalibration.fitCorrection(extracted.second)
                    val data = ChartCalibration.buildCorrectionData(correction)
                    val corrected = ChartCalibration.applyCorrectionData(data, extracted.second)
                    Triple(
                        data,
                        ChartCalibration.meanMaxDeltaE(extracted.second),
                        ChartCalibration.meanMaxDeltaE(corrected)
                    )
                }
                correctionData = fitted.first
                deBefore = fitted.second
                deAfter = fitted.third
                busy = false
                step = CalibStep.Result
            } else {
                val savedCorrection = withContext(Dispatchers.IO) {
                    CalibrationPackStore.loadCorrectionData(context)
                }
                if (savedCorrection == null) {
                    busy = false
                    errorText = context.getString(R.string.calibration_error_extract)
                    return@launch
                }
                val corrected = ChartCalibration.applyCorrectionData(
                    savedCorrection, extracted.second
                )
                deBefore = ChartCalibration.meanMaxDeltaE(extracted.second)
                deAfter = ChartCalibration.meanMaxDeltaE(corrected)
                busy = false
                step = CalibStep.VerifyResult
            }
        }
    }

    fun saveCalibration() {
        val data = correctionData ?: return
        if (selectedStyles.isEmpty()) return
        busy = true
        scope.launch {
            val result = withContext(Dispatchers.IO) {
                runCatching {
                    val oldCustomIds = manifest
                        ?.let { CalibrationPackStore.savedStyles(it) }
                        ?.map { it.customLutId }
                        ?: emptyList()

                    val configText = context.assets.open("luts/config.json")
                        .bufferedReader().readText()
                    val allEntries = JSONObject(configText).getJSONArray("luts")
                    val entryById = (0 until allEntries.length())
                        .map { allEntries.getJSONObject(it) }
                        .associateBy { it.getString("id") }

                    val plutByFile = linkedMapOf<String, ByteArray>()
                    val entries = JSONArray()
                    val savedStyles = mutableListOf<CalibrationPackStore.SavedStyle>()
                    for (style in builtIns.filter { it.id in selectedStyles }) {
                        val styleBytes = context.assets.open(style.fileName)
                            .use { stream -> stream.readBytes() }
                        val grid = PlutIO.readPlut(styleBytes) ?: continue
                        val plutBytes = PlutIO.writePlutV1(PlutIO.compose(data, grid))
                        val bareName = style.fileName.substringAfterLast('/')
                        plutByFile[bareName] = plutBytes
                        entryById[style.id]?.let { entries.put(it) }
                        val nameMap = style.nameMap.mapValues { (lang, name) ->
                            name + if (lang == "vi") " (Đã hiệu chuẩn)" else " (Calibrated)"
                        }.toMutableMap()
                        if (!nameMap.containsKey("en")) {
                            nameMap["en"] = style.getName(Locale.ENGLISH) + " (Calibrated)"
                        }
                        val newId = customImportManager.importPresetLut(
                            plutBytes, nameMap, CALIBRATION_CATEGORY, style.isFavorite
                        )
                        if (newId != null) {
                            savedStyles += CalibrationPackStore.SavedStyle(
                                style.id, bareName, newId
                            )
                        }
                    }
                    check(savedStyles.isNotEmpty()) { "No calibrated LUT could be saved" }

                    val report = JSONObject()
                        .put("deviceKey", CalibrationPackStore.deviceKey())
                        .put("manufacturer", Build.MANUFACTURER ?: "")
                        .put("model", Build.MODEL ?: "")
                        .put("createdAt", System.currentTimeMillis())
                        .put("meanBefore", deBefore?.first?.toDouble() ?: 0.0)
                        .put("maxBefore", deBefore?.second?.toDouble() ?: 0.0)
                        .put("meanAfter", deAfter?.first?.toDouble() ?: 0.0)
                        .put("maxAfter", deAfter?.second?.toDouble() ?: 0.0)
                    CalibrationPackStore.savePack(
                        context = context,
                        plutByFileName = plutByFile,
                        configEntries = entries,
                        report = report,
                        correctionBytes = PlutIO.writePlutV1(data),
                        savedStyles = savedStyles
                    )
                    // Replace any previous calibration only after the new one is stored.
                    oldCustomIds.forEach { customImportManager.deleteCustomLut(it) }
                }
            }
            busy = false
            result.onSuccess {
                manifest = CalibrationPackStore.loadManifest(context)
                onCalibrationChanged()
                step = CalibStep.Done
            }.onFailure {
                errorText = context.getString(R.string.calibration_error_save)
            }
        }
    }

    fun exportPack() {
        busy = true
        scope.launch {
            val zip = withContext(Dispatchers.IO) {
                CalibrationPackStore.buildExportZip(context)
            }
            busy = false
            if (zip != null) {
                val uri = FileProvider.getUriForFile(
                    context, "${context.packageName}.fileprovider", zip
                )
                val intent = Intent(Intent.ACTION_SEND).apply {
                    type = "application/zip"
                    putExtra(Intent.EXTRA_STREAM, uri)
                    addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION)
                }
                context.startActivity(
                    Intent.createChooser(
                        intent, context.getString(R.string.calibration_share_title)
                    )
                )
            } else {
                errorText = context.getString(R.string.calibration_error_export)
            }
        }
    }

    fun deleteCalibration() {
        busy = true
        scope.launch {
            withContext(Dispatchers.IO) {
                manifest?.let { current ->
                    CalibrationPackStore.savedStyles(current).forEach {
                        customImportManager.deleteCustomLut(it.customLutId)
                    }
                }
                CalibrationPackStore.clear(context)
            }
            busy = false
            manifest = null
            onCalibrationChanged()
            step = CalibStep.Guide
        }
    }

    BackHandler(enabled = step != CalibStep.Guide && step != CalibStep.Done) {
        step = CalibStep.Guide
    }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.calibration_title)) },
                navigationIcon = {
                    IconButton(onClick = {
                        if (step == CalibStep.Guide || step == CalibStep.Done) onBack()
                        else step = CalibStep.Guide
                    }) {
                        Icon(
                            Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = null
                        )
                    }
                }
            )
        }
    ) { padding ->
        Box(
            Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            when (step) {
                CalibStep.Guide -> GuideStep(
                    manifest = manifest,
                    busy = busy,
                    onCalibrate = {
                        cornersMode = CornersMode.Calibrate
                        pickPhoto.launch("image/*")
                    },
                    onVerify = {
                        cornersMode = CornersMode.Verify
                        pickPhoto.launch("image/*")
                    },
                    onExport = { exportPack() },
                    onDelete = { deleteCalibration() }
                )

                CalibStep.Corners -> Column(
                    Modifier
                        .fillMaxSize()
                        .padding(16.dp)
                ) {
                    Text(
                        stringResource(R.string.calibration_corners_hint),
                        style = MaterialTheme.typography.bodyMedium
                    )
                    Spacer(Modifier.height(12.dp))
                    bitmap?.let { bmp ->
                        CornerEditor(
                            bitmap = bmp,
                            corners = corners,
                            onCornersChange = { corners = it }
                        )
                    }
                    Spacer(Modifier.height(16.dp))
                    Button(
                        onClick = { runExtraction() },
                        enabled = !busy,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.calibration_extract))
                    }
                }

                CalibStep.Result -> Column(
                    Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(16.dp)
                ) {
                    Text(
                        stringResource(R.string.calibration_result_title),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        stringResource(
                            R.string.calibration_de_before,
                            formatDe(deBefore?.first), formatDe(deBefore?.second)
                        )
                    )
                    Text(
                        stringResource(
                            R.string.calibration_de_after,
                            formatDe(deAfter?.first), formatDe(deAfter?.second)
                        )
                    )
                    Spacer(Modifier.height(16.dp))
                    Text(
                        stringResource(R.string.calibration_styles_title),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold
                    )
                    builtIns.forEach { style ->
                        Row(
                            verticalAlignment = Alignment.CenterVertically,
                            modifier = Modifier.fillMaxWidth()
                        ) {
                            Checkbox(
                                checked = style.id in selectedStyles,
                                onCheckedChange = { checked ->
                                    selectedStyles = if (checked) {
                                        selectedStyles + style.id
                                    } else {
                                        selectedStyles - style.id
                                    }
                                }
                            )
                            Text(style.getName())
                        }
                    }
                    Spacer(Modifier.height(16.dp))
                    Button(
                        onClick = { saveCalibration() },
                        enabled = !busy && selectedStyles.isNotEmpty(),
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.calibration_save))
                    }
                }

                CalibStep.Done -> Column(
                    Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(16.dp)
                ) {
                    Text(
                        stringResource(R.string.calibration_done_title),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(stringResource(R.string.calibration_done_body))
                    Spacer(Modifier.height(16.dp))
                    Button(
                        onClick = { exportPack() },
                        enabled = !busy,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.calibration_export))
                    }
                    Spacer(Modifier.height(8.dp))
                    OutlinedButton(
                        onClick = onBack,
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.calibration_back))
                    }
                }

                CalibStep.VerifyResult -> Column(
                    Modifier
                        .fillMaxSize()
                        .verticalScroll(rememberScrollState())
                        .padding(16.dp)
                ) {
                    Text(
                        stringResource(R.string.calibration_verify_title),
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.SemiBold
                    )
                    Spacer(Modifier.height(8.dp))
                    Text(
                        stringResource(
                            R.string.calibration_de_before,
                            formatDe(deBefore?.first), formatDe(deBefore?.second)
                        )
                    )
                    Text(
                        stringResource(
                            R.string.calibration_de_after,
                            formatDe(deAfter?.first), formatDe(deAfter?.second)
                        )
                    )
                    Spacer(Modifier.height(16.dp))
                    Button(
                        onClick = { step = CalibStep.Guide },
                        modifier = Modifier.fillMaxWidth()
                    ) {
                        Text(stringResource(R.string.calibration_back))
                    }
                }
            }

            if (busy) {
                Box(
                    Modifier
                        .fillMaxSize()
                        .background(Color.Black.copy(alpha = 0.35f)),
                    contentAlignment = Alignment.Center
                ) {
                    Column(horizontalAlignment = Alignment.CenterHorizontally) {
                        CircularProgressIndicator()
                        Spacer(Modifier.height(12.dp))
                        Text(
                            stringResource(R.string.calibration_working),
                            color = Color.White
                        )
                    }
                }
            }
        }
    }

    errorText?.let { message ->
        AlertDialog(
            onDismissRequest = { errorText = null },
            confirmButton = {
                TextButton(onClick = { errorText = null }) {
                    Text(stringResource(R.string.calibration_ok))
                }
            },
            text = { Text(message) }
        )
    }
}

@Composable
private fun GuideStep(
    manifest: JSONObject?,
    busy: Boolean,
    onCalibrate: () -> Unit,
    onVerify: () -> Unit,
    onExport: () -> Unit,
    onDelete: () -> Unit
) {
    Column(
        Modifier
            .fillMaxSize()
            .verticalScroll(rememberScrollState())
            .padding(16.dp)
    ) {
        if (manifest != null) {
            Card(Modifier.fillMaxWidth()) {
                Column(Modifier.padding(16.dp)) {
                    Text(
                        stringResource(R.string.calibration_saved_title),
                        style = MaterialTheme.typography.titleSmall,
                        fontWeight = FontWeight.SemiBold
                    )
                    Spacer(Modifier.height(6.dp))
                    val dateText = SimpleDateFormat("dd/MM/yyyy HH:mm", Locale.getDefault())
                        .format(Date(manifest.optLong("createdAt", 0L)))
                    Text(
                        stringResource(
                            R.string.calibration_saved_info,
                            "${manifest.optString("manufacturer")} ${manifest.optString("model")}",
                            dateText,
                            formatDe(manifest.optDouble("meanBefore", 0.0).toFloat()),
                            formatDe(manifest.optDouble("meanAfter", 0.0).toFloat()),
                            formatDe(manifest.optDouble("maxBefore", 0.0).toFloat()),
                            formatDe(manifest.optDouble("maxAfter", 0.0).toFloat())
                        )
                    )
                }
            }
            Spacer(Modifier.height(16.dp))
            Button(
                onClick = onExport,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(stringResource(R.string.calibration_export))
            }
            Spacer(Modifier.height(8.dp))
            OutlinedButton(
                onClick = onVerify,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(stringResource(R.string.calibration_verify))
            }
            Spacer(Modifier.height(8.dp))
            OutlinedButton(
                onClick = onCalibrate,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(stringResource(R.string.calibration_recalibrate))
            }
            Spacer(Modifier.height(8.dp))
            TextButton(
                onClick = onDelete,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(
                    stringResource(R.string.calibration_delete),
                    color = MaterialTheme.colorScheme.error
                )
            }
        } else {
            Text(stringResource(R.string.calibration_guide_body))
            Spacer(Modifier.height(12.dp))
            Text(stringResource(R.string.calibration_guide_steps))
            Spacer(Modifier.height(20.dp))
            Button(
                onClick = onCalibrate,
                enabled = !busy,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(stringResource(R.string.calibration_pick_photo))
            }
        }
    }
}

@Composable
private fun CornerEditor(
    bitmap: Bitmap,
    corners: List<Offset>,
    onCornersChange: (List<Offset>) -> Unit
) {
    val currentCorners by rememberUpdatedState(corners)
    val currentOnChange by rememberUpdatedState(onCornersChange)
    BoxWithConstraints(
        Modifier
            .fillMaxWidth()
            .height(430.dp)
    ) {
        val scale = minOf(maxWidth.value / bitmap.width, maxHeight.value / bitmap.height)
        val dispW = (bitmap.width * scale).dp
        val dispH = (bitmap.height * scale).dp
        val originX = (maxWidth - dispW) / 2
        val originY = (maxHeight - dispH) / 2
        Image(
            bitmap = bitmap.asImageBitmap(),
            contentDescription = null,
            modifier = Modifier
                .offset(originX, originY)
                .size(dispW, dispH),
            contentScale = ContentScale.FillBounds
        )
        Canvas(
            Modifier
                .offset(originX, originY)
                .size(dispW, dispH)
        ) {
            val pts = currentCorners.map {
                Offset(it.x * size.width, it.y * size.height)
            }
            for (i in 0..3) {
                drawLine(
                    Color(0xFFFFC107),
                    pts[i],
                    pts[(i + 1) % 4],
                    strokeWidth = 2.dp.toPx()
                )
            }
        }
        val density = LocalDensity.current
        corners.forEachIndexed { index, frac ->
            val handleSize = 34.dp
            Box(
                Modifier
                    .offset(
                        x = originX + dispW * frac.x - handleSize / 2,
                        y = originY + dispH * frac.y - handleSize / 2
                    )
                    .size(handleSize)
                    .clip(CircleShape)
                    .background(Color(0xFFFFC107).copy(alpha = 0.9f))
                    .border(2.dp, Color.Black, CircleShape)
                    .pointerInput(index) {
                        val dispWPx = with(density) { dispW.toPx() }
                        val dispHPx = with(density) { dispH.toPx() }
                        detectDragGestures { change, dragAmount ->
                            change.consume()
                            val cur = currentCorners[index]
                            val updated = currentCorners.toMutableList()
                            updated[index] = Offset(
                                (cur.x + dragAmount.x / dispWPx).coerceIn(0f, 1f),
                                (cur.y + dragAmount.y / dispHPx).coerceIn(0f, 1f)
                            )
                            currentOnChange(updated)
                        }
                    }
            )
        }
    }
}

/** Decode a picked photo, cap its size and apply its EXIF orientation. */
private fun decodeChartBitmap(context: Context, uri: Uri): Bitmap? {
    val resolver = context.contentResolver
    val orientation = resolver.openInputStream(uri)?.use { stream ->
        ExifInterface(stream).getAttributeInt(
            ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL
        )
    } ?: ExifInterface.ORIENTATION_NORMAL

    val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
    resolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
    if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
    var sample = 1
    while (max(bounds.outWidth, bounds.outHeight) / sample > 1600) sample *= 2
    val options = BitmapFactory.Options().apply { inSampleSize = sample }
    var decoded = resolver.openInputStream(uri)?.use {
        BitmapFactory.decodeStream(it, null, options)
    } ?: return null

    val matrix = Matrix()
    when (orientation) {
        ExifInterface.ORIENTATION_ROTATE_90 -> matrix.postRotate(90f)
        ExifInterface.ORIENTATION_ROTATE_180 -> matrix.postRotate(180f)
        ExifInterface.ORIENTATION_ROTATE_270 -> matrix.postRotate(270f)
        ExifInterface.ORIENTATION_FLIP_HORIZONTAL -> matrix.postScale(-1f, 1f)
        ExifInterface.ORIENTATION_FLIP_VERTICAL -> matrix.postScale(1f, -1f)
    }
    if (!matrix.isIdentity) {
        decoded = Bitmap.createBitmap(
            decoded, 0, 0, decoded.width, decoded.height, matrix, true
        )
    }
    return decoded
}
