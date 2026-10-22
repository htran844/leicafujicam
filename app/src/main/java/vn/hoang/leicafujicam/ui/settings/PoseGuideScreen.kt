package vn.hoang.leicafujicam.ui.settings

import androidx.compose.foundation.Image
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.filled.ArrowBack
import androidx.compose.material3.Button
import androidx.compose.material3.ButtonDefaults
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.FilterChip
import androidx.compose.material3.FilterChipDefaults
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.material3.TopAppBar
import androidx.compose.material3.TopAppBarDefaults
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalUriHandler
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import vn.hoang.leicafujicam.R
import vn.hoang.leicafujicam.pose.PoseCatalog
import vn.hoang.leicafujicam.pose.PoseCatalogData
import vn.hoang.leicafujicam.pose.PoseEntry
import vn.hoang.leicafujicam.ui.camera.PoseFigureCanvas
import vn.hoang.leicafujicam.ui.camera.rememberAssetBitmap
import vn.hoang.leicafujicam.ui.theme.PremiumGold

/**
 * Pose-guide browser: pick a shooting environment (scene), browse the
 * ranked poses as stick figures, read the model tips, then send the pose
 * to the camera as a live overlay.
 */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun PoseGuideScreen(
    onBack: () -> Unit,
    onUsePose: (String) -> Unit
) {
    val context = LocalContext.current
    var catalog by remember { mutableStateOf<PoseCatalogData?>(null) }
    var selectedSceneId by remember { mutableStateOf<String?>(null) }
    var selectedGender by remember { mutableStateOf<String?>(null) }
    var selectedPoseId by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(Unit) {
        catalog = withContext(Dispatchers.IO) { PoseCatalog.load(context) }
    }

    val data = catalog
    val scenePoses: List<PoseEntry> = when {
        data == null -> emptyList()
        selectedSceneId == null -> data.poses
        else -> data.scenes.firstOrNull { it.id == selectedSceneId }
            ?.expectedPoses?.mapNotNull { data.posesById[it] } ?: emptyList()
    }
    val visiblePoses: List<PoseEntry> = scenePoses.filter { pose ->
        selectedGender == null || pose.gender == selectedGender || pose.gender == "unisex"
    }
    val selectedPose = selectedPoseId?.let { data?.posesById?.get(it) }

    Scaffold(
        topBar = {
            TopAppBar(
                title = { Text(stringResource(R.string.pose_guide_title)) },
                navigationIcon = {
                    IconButton(onClick = onBack) {
                        Icon(
                            Icons.AutoMirrored.Filled.ArrowBack,
                            contentDescription = stringResource(R.string.back)
                        )
                    }
                },
                colors = TopAppBarDefaults.topAppBarColors(
                    containerColor = MaterialTheme.colorScheme.background
                )
            )
        }
    ) { padding ->
        Column(
            modifier = Modifier
                .fillMaxSize()
                .padding(padding)
        ) {
            if (data == null) {
                Box(
                    modifier = Modifier.fillMaxSize(),
                    contentAlignment = androidx.compose.ui.Alignment.Center
                ) {
                    CircularProgressIndicator(color = PremiumGold)
                }
                return@Column
            }

            // Scene filter chips ("All" + one per shooting environment).
            LazyRow(
                contentPadding = PaddingValues(horizontal = 16.dp, vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                item {
                    FilterChip(
                        selected = selectedSceneId == null,
                        onClick = { selectedSceneId = null },
                        label = { Text(stringResource(R.string.pose_guide_all_scenes)) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = PremiumGold,
                            selectedLabelColor = Color.Black
                        )
                    )
                }
                items(data.scenes) { scene ->
                    FilterChip(
                        selected = selectedSceneId == scene.id,
                        onClick = { selectedSceneId = scene.id },
                        label = { Text(scene.getName()) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = PremiumGold,
                            selectedLabelColor = Color.Black
                        )
                    )
                }
            }

            // Gender filter chips (gendered poses plus shared unisex ones).
            LazyRow(
                contentPadding = PaddingValues(horizontal = 16.dp, vertical = 2.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                item {
                    FilterChip(
                        selected = selectedGender == null,
                        onClick = { selectedGender = null },
                        label = { Text(stringResource(R.string.pose_guide_gender_all)) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = PremiumGold,
                            selectedLabelColor = Color.Black
                        )
                    )
                }
                item {
                    FilterChip(
                        selected = selectedGender == "female",
                        onClick = { selectedGender = "female" },
                        label = { Text(stringResource(R.string.pose_guide_female)) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = PremiumGold,
                            selectedLabelColor = Color.Black
                        )
                    )
                }
                item {
                    FilterChip(
                        selected = selectedGender == "male",
                        onClick = { selectedGender = "male" },
                        label = { Text(stringResource(R.string.pose_guide_male)) },
                        colors = FilterChipDefaults.filterChipColors(
                            selectedContainerColor = PremiumGold,
                            selectedLabelColor = Color.Black
                        )
                    )
                }
            }

            LazyVerticalGrid(
                columns = GridCells.Fixed(3),
                modifier = Modifier
                    .fillMaxWidth()
                    .weight(1f),
                contentPadding = PaddingValues(12.dp),
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                items(visiblePoses, key = { it.id }) { pose ->
                    val selected = pose.id == selectedPoseId
                    Card(
                        onClick = { selectedPoseId = pose.id },
                        colors = CardDefaults.cardColors(
                            containerColor = if (selected) {
                                PremiumGold.copy(alpha = 0.16f)
                            } else {
                                MaterialTheme.colorScheme.surfaceVariant
                            }
                        )
                    ) {
                        Column(modifier = Modifier.padding(6.dp)) {
                            val cellPhoto = pose.photo?.let { photoPath ->
                                rememberAssetBitmap("poses/$photoPath")
                            }
                            if (cellPhoto != null) {
                                Image(
                                    bitmap = cellPhoto,
                                    contentDescription = pose.getName(),
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .height(104.dp),
                                    contentScale = ContentScale.Crop
                                )
                            } else {
                                PoseFigureCanvas(
                                    pose = pose,
                                    color = if (selected) PremiumGold else Color.White,
                                    modifier = Modifier
                                        .fillMaxWidth()
                                        .height(104.dp),
                                    strokeWidthPx = 4f
                                )
                            }
                            Text(
                                text = pose.getName(),
                                style = MaterialTheme.typography.labelSmall,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis,
                                modifier = Modifier.padding(top = 4.dp)
                            )
                        }
                    }
                }
            }

            // Detail panel for the selected pose.
            if (selectedPose != null) {
                Card(
                    modifier = Modifier
                        .fillMaxWidth()
                        .padding(horizontal = 12.dp, vertical = 8.dp),
                    colors = CardDefaults.cardColors(
                        containerColor = MaterialTheme.colorScheme.surfaceVariant
                    )
                ) {
                    Column(modifier = Modifier.padding(14.dp)) {
                        val detailPhoto = selectedPose.photo?.let { photoPath ->
                            rememberAssetBitmap("poses/$photoPath")
                        }
                        if (detailPhoto != null) {
                            Image(
                                bitmap = detailPhoto,
                                contentDescription = selectedPose.getName(),
                                modifier = Modifier
                                    .fillMaxWidth()
                                    .height(210.dp)
                                    .padding(bottom = 10.dp),
                                contentScale = ContentScale.Crop
                            )
                        }
                        Text(
                            text = selectedPose.getName(),
                            style = MaterialTheme.typography.titleMedium
                        )
                        val tips = selectedPose.getTips()
                        if (tips.isNotEmpty()) {
                            Spacer(Modifier.height(6.dp))
                            Text(
                                text = stringResource(R.string.pose_guide_tips),
                                style = MaterialTheme.typography.labelLarge,
                                color = PremiumGold
                            )
                            tips.forEach { tip ->
                                Text(
                                    text = "• $tip",
                                    style = MaterialTheme.typography.bodySmall
                                )
                            }
                        }
                        val cues = selectedPose.getCameraCues()
                        if (cues.isNotEmpty()) {
                            Spacer(Modifier.height(6.dp))
                            Text(
                                text = stringResource(R.string.pose_guide_camera_cues),
                                style = MaterialTheme.typography.labelLarge,
                                color = PremiumGold
                            )
                            cues.forEach { cue ->
                                Text(
                                    text = "• $cue",
                                    style = MaterialTheme.typography.bodySmall
                                )
                            }
                        }
                        if (selectedPose.sourceCreator != null) {
                            val uriHandler = LocalUriHandler.current
                            val sourceUrl = selectedPose.sourceUrl
                            Spacer(Modifier.height(8.dp))
                            Text(
                                text = stringResource(
                                    R.string.pose_guide_source,
                                    selectedPose.sourceCreator,
                                    selectedPose.sourcePlatform ?: ""
                                ),
                                style = MaterialTheme.typography.bodySmall,
                                color = PremiumGold,
                                modifier = if (sourceUrl != null) {
                                    Modifier.clickable { uriHandler.openUri(sourceUrl) }
                                } else {
                                    Modifier
                                }
                            )
                        }
                        Spacer(Modifier.height(10.dp))
                        Button(
                            onClick = { onUsePose(selectedPose.id) },
                            modifier = Modifier.fillMaxWidth(),
                            colors = ButtonDefaults.buttonColors(
                                containerColor = PremiumGold,
                                contentColor = Color.Black
                            )
                        ) {
                            Text(stringResource(R.string.pose_guide_use))
                        }
                    }
                }
            }
        }
    }
}
