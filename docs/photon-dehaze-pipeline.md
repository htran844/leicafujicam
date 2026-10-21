# Photon HDRNet、Dehaze 与曝光匹配

HDRNet 和 Dehaze/DHA 的结果共同烘焙到 DNG `ProfileGainTableMap2`。运行期只查一次
PGTM，不另加全分辨率 Dehaze，也不在生成后追加曝光匹配增益。

## 曝光与处理顺序

物理 AE 的 `sourceToShortGain=s0`、`hdrRatio=r` 与渲染输入曝光 `e` 分开保存。
`e` 调整短、长曝光的共同输入尺度，不改变物理 TET 和长短曝光比例：

```text
RAW camera RGB
  -> s0 * 2^e -> working-space CCM
  -> per-pixel min(RGB, 1) -> box average -> clamp(mean, 0, 1)
  -> HDRNet tensor [short R, G, B, min(short BT601 luma * r, 12)]
  -> inference -> HDRNet RGB -> Dehaze/DHA
  -> viewfinder brightness score
  -> bake this same candidate into PGTM -> profile / film rendering
```

曝光必须参与非线性响应的求解。旧链在固定 HDRNet/Dehaze 输出后再乘匹配增益；当匹配为
负 EV 时，已经被压缩或裁剪的白色高光会变成灰色平台，原有差异无法通过后乘增益恢复。
仅延后 RGB 裁剪或扩大 PGTM 范围不足以修复这类分块。现在每个候选都从未裁剪源纹理
重新准备模型输入、推理并重算 Dehaze，最终表使用选中候选的全部数据。不能缩放旧 tensor、
复用上一候选的系数或去雾曲线。

模型准备的裁剪顺序对应原 MGC `apply_lsc_and_ccm.frag` 和最终 box pass：逐像素保留负值、
仅限制正上界，完成平均后再限制到 `[0,1]`。HDRNet guide 是原 protobuf 的 16 项 hinge 和，
两侧仿射均为 `[1,0]`。原 renderer 的输入也经过 CCM 上裁剪；不能把原 apply shader 没有
显式 luma clamp 理解为模型支持任意未归一化高光外推。

## 匹配

沿用原 8×6 全图参考、空间权重、可靠暗/亮端排除与人像优先。HDRNet 单独启用
`1 + 0.25*(1-smoothstep(0.02,0.25,referenceLuma))` 阴影权重，Classic 不变。
每个候选按 RGB 逐像素完成最终通道裁剪、计算线性 Rec.709 亮度，再取每格均值。
不能先平均 RGB 再裁剪，也不能给候选结果追加 EV。

使用既有原生候选搜索和评分：优先最大化 ±0.1 EV 内的加权匹配率，其次比较平均 EV 误差
和 robust loss。候选范围为用户支持的 `-4..4 EV`，搜索次数有界。GPU 输入缓冲和推理器
复用，内存只保留最近一个完整候选；如果最终选择了较早候选，就重新计算它再烘焙。

## Dehaze 契约

Dehaze 不进入模型 tensor。统计来自当前候选唯一一次 HDRNet 推理后的完整 256×192 图像：

- 877-bin haze histogram；
- 5251-bin highlight histogram；
- 20 个低百分位样本决定 atmospheric haze point；
- 5 个高百分位样本决定 DHA highlight scale，限制到 `0.78..1.7`；
- 低端二次段和高端线性段保持数值及斜率连续。

保留原有有界 HDRNet/Dehaze 响应。新链不再用负的匹配增益压暗已经裁剪的结果。
Dehaze 的本地开关与强度仍来自 `PhotonCoreImagingTuning.dehaze`；每个曝光候选重新计算曲线，
P99 只用于诊断，不限制曝光结果。

## PGTM 范围与曝光计数

内部 plan 使用有效 short gain `s=s0*2^e`。N 权重同时消除渲染 BaselineExposure 并带入
这个有效增益；native 烘焙分母仍还原为实际 `RAW*BaselineGain`，因此输入曝光仅烘焙一次。
不修改物理 `sourceToShortGain` 的持久化值，不将用户编辑 EV 混入输入配方。

全分辨率 RGB 可能超过模型输入的上界。DNG 按 `tableIndex=N*pointCount` 查表，越过末端
后只返回最后 gain；原 RGB 继续增大会导致高光重新线性变亮。GPU 在逐像素裁剪和 box
平均之前，用 PXL 五权重计算最大短曝光强度 `M`。范围统计单独将负 RGB 归零，以同时覆盖
保留或清除负值的渲染器，不改变模型 tensor。

```text
pointCount = max(257, ceil(M*257)+1)
rangeScale = 257/pointCount
storedWeights = PXLWeights * s / BaselineGain * rangeScale
```

原有强度节点间距保持为 `1/257`。节点上限为 `min(4096,GL_MAX_TEXTURE_SIZE)`，达到上限
时使用 `rangeScale=(pointCount-1)/(pointCount*M)` 保证完整覆盖。上传纹理高度固定为
`64*48`，生成前检查设备能力。极暗 cell 的中性轴判定使用按权重和归一的阈值，不随范围
缩放改变。DNG 读取端无需 Photon 特殊查表规则。

## 持久化与旧文件

新配方保存物理 `s0/r`、`hdrNetInputExposureEv` 和合约 `hdrnet_input_viewfinder_v1`。
新写入清除旧 post EV 字段；完整配方也进入 DNG XMP SummaryText，独立导入有效 Photon
PGTM 时恢复，不能仅依赖相册 metadata.json。HDR 场景参考采用 `s0*2^e*r`，后续用户编辑
EV 仍单独应用一次。

- 普通打开、开关动态范围优化：复用有效内嵌 Photon PGTM。
- 新配方显式重生成：恢复 `s0/r/e`，只计算这一候选，不重新匹配。
- 旧配方显式重生成：用旧 HDRNet/Dehaze 和旧 SLM 响应重建亮度参考，再求解新的输入 EV。
  旧 post EV 不能直接解释为 input EV；目标迁移只约束亮度，不要求继续保留旧高光压缩失真。
- 没有参考和有效配方的 RAW：采用 `e=0`。
- 参考没有可靠计量区域（如全黑或全白）：记录 `UNUSABLE_REFERENCE`，采用同样的 `e=0`
  策略；候选准备、推理或原生提交失败仍终止生成，不静默降级。

显式刷新延续当前处理结果重生成的既有语义，不覆写原始 DNG。原内嵌表与原配方仍是旧文件
再次显式重生成的参考。新拍摄 DNG 保存的则是选中候选的完整表与新配方。

## 日志

`HDRNET_PGTM_RANGE` 记录每次准备的最大强度、节点数和范围比例。
`HDRNET_MATCH stage=SELECTED` 记录物理与有效 short gain、ratio、input EV、来源、Dehaze
参数和节点数。原生 matcher 记录候选评分，用于区分匹配变化、配方恢复和模型输入变化。
