# Photon 核心成像参数

`PhotonCoreImagingTuning` 保持为本地默认值单例，不提供核心参数的持久化读写。
设置 → 专业设置 → HDR+ 画质中的「HDR+ 画质调优」恢复 1.27.2.2 的传感器面积调优算法，
默认关闭；开启后仅对 HDR+（RAWmax）拍摄应用。融合/降噪随传感器面积拟合，锐化使用统一固定配方。
传感器面积缺失或无效时，融合/降噪使用默认参数，固定锐化仍按开关生效。

## 本地默认值

| 参数 | 默认值 |
| --- | --- |
| Sabre `mergeGradientThreshold` | `-1` |
| 参考信号缺失时的替代值 | `0.18` |
| 噪声谱缩放 `noiseCorrelationScale` | `1` |
| luma/chroma 五层强度倍率 | 全部 `1` |
| revert/outlier 五层倍率 | 全部 `1` |
| 频率响应 `(responseOffset-cos²)+(cos+cosineOffset)²` | offset=`1`，cosineOffset=`-1` |
| Sabre strength/revert/outlier 绝对节点覆盖 | 全部不覆盖，使用原始资产 |
| 新建噪声谱初值 `noiseSpectrumSeed` | `1`；已有测量谱保持自身幅度 |

移除此前 AGC p0 的固定默认调参：strength `[2.5,2.5,2.5,2.5,1.25]`、revert `2`、outlier `0.75`、
responseOffset `0.9375`、噪声谱初值 `0.5`，以及 dl/dm/dh 的 strength/revert/outlier 绝对覆盖。
历史 AGC 参数仅保留在显式测试样本中，用于验证原版插值语义，不参与运行期默认值。

融合阈值 `-1` 对应 AGC `lib_sabre_denoise_control_key=-1.000`，原样进入
`covariance_parameters1.z`。正常有限非负局部特征下，
`clamp(1-(feature-threshold)/transition,0,1)` 为零，取消弱纹理区域向宽各向同性核的额外混合。
画质调优开关的两种状态均保留这一默认值。显式 `null` 可使用原始 SNR 自适应阈值；归一化范围
为 `-32..32`，允许负阈值通过。既有拍摄日志记录 `mergeGradientThreshold` 与最终 kernel 参数。

## 1.27.2.2 画质调优

`PhotonSensorSizeTuning` 将物理传感器面积限制在 `20.48..128 mm²`，令
`t=log2(area/32)`，沿用历史拟合系数：

| 字段 | 启用后的值 |
| --- | --- |
| Sabre 噪声谱缩放 | `0.767513962 - 0.164396329*t` |
| 五层亮度 strength 倍率 | `0.252164225 + 0.059759506*t` |
| 五层亮度 revert 倍率 | `0.3125` |
| 五层亮度 outlier 倍率 | `0.8125` |
| 五层色度 strength 倍率 | `0.3125` |
| 频率响应 responseOffset | `13.499369928 - 4.878369857*t` |
| SNR20 的第一层亮度 strength 节点 | `0.608876213 + 0.033127858*t` |

Sabre 亮度 strength 在 SNR 插值前使用下列节点：

| SNR | level 1..5 |
| --- | --- |
| 5 | `[0.8,2.2,0.5,1.65,0.7]` |
| 20 | `[拟合值,2.1,0.4,0.8,0.2]` |
| 40 | `[0.85,0.4,0.3,0.457,0.1]` |

revert/outlier 不覆盖资产节点，只应用上述倍率。用户 RAW luma/chroma 滑杆继续控制外层总强度。
噪声谱缩放在 Sabre 输出噪声进入 denoise 金字塔之前应用；Spatial 保持自己的测量谱。

参数恢复不回退融合噪声传播、CFA 映射或数值域转换等算法修复。
默认噪声模型为 Pixel5。以下三个模型通过通用 `.c` 解析器加载，作为可选模型：

| 编号 | 文件 | 模拟增益 ISO 分界 |
| --- | --- | --- |
| 26 | `noise_profiles/GC02M1_LMIPRO.c` | 600 |
| 48 | `noise_profiles/LGV50_IMX363.c` | 800 |
| 70 | `noise_profiles/GalaxyM51_SLSI_GC5035_54.c` | 800 |

26 号复用已有条目，系数与用户提供的 `.c` 文件完全一致。70 号的测试循环上限 1600 不代表
模拟增益分界，实际使用文件函数中的 800。

空域降噪的原版 `Scale(2)` 数值域转换保持原实现。

## 拍摄与重处理

- 开关由 `UserPreferencesRepository` 保存；拍摄时取当前镜头的物理传感器面积。
- 每张照片保存 `photonCoreTuningModel=photon-sensor-area-v1` 表示启用；有效面积另存
  `photonSensorPhysicalAreaMm2`。面积缺失也保留启用标记，关闭时不写入这两项。
- 拍摄默认降噪通过 `RawMetadata.rawMaxQualityTuningSensorAreaMm2` 传入；DNG 重处理和回退路径
  从该照片的属性恢复面积，重新计算参数，不读取当前全局开关，也不读取旧的逐字段核心参数覆盖。
- `MgcFullResolutionDenoise` 统一解析面积调优，日志记录面积、五层倍率和频率响应。
- 原有有效融合模型、噪声谱传播和默认降噪烘焙边界保持不变。

## 锐化与除雾

画质调优开启时使用 `PhotonQualitySharpenTuning.FIXED`：参考 AGC LM v1.1 p0 镜头槽0，
所有传感器使用同一表，不参与面积拟合；仍按参考帧 SNR 在 2/8/16 节点间插值。
关闭时使用原通用表。照片重处理恢复拍摄时的开关，不读取当前全局开关。
具备参考帧 SNR 的 RAW 使用 MGC 9.6 原版
`SharpenTo16BitHalide`。`PhotonCoreImagingTuning.sharpen` 提供本地 SNR 节点、三频段增强倍率
及显式对比度控制点，关闭画质调优时默认值等效于原 `sharpen_default.binarypb` 曲线；参数含义、完整默认表及
公式见 [Photon 锐化参数表](photon-sharpen-tuning.md)。用户 sharpening 滑杆与融合 attenuation
继续控制内核外层强度。锐化选择曲线使用参考帧 SNR，降噪使用融合后 SNR，两者不可混用。
没有参考帧统计且无法访问原始 RAW 的其他 GPU 来源保留 GLES USM，并记录原因。
HDRNet Dehaze/DHA 继续使用本地默认值，独立于面积拟合。完整链路见
[Photon HDRNet Dehaze + DHA 链路](photon-dehaze-pipeline.md)。

## 验证

恢复历史 `PhotonSensorSizeTuningTest` 的拟合系数、面积趋势和边界校验，并覆盖开关状态的照片属性传递。
默认值校验确认单位倍率、无节点覆盖、频率响应与噪声谱初值为 `1`；融合参数校验确认 `-1` 经归一化
进入 covariance uniform。原 AGC 插值回归使用显式历史样本，不再依赖生产默认参数。
