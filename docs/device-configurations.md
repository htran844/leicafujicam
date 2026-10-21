# 设备覆盖配置

设备覆盖配置用于为已知机型提供相机、RAW、视频或厂商参数覆盖。配置文件随应用打包在 `app/src/main/assets/device_configurations/*.json`，应用启动时按设备信息自动选择并默认应用；不存在用户配置选择、导入或导出入口。JSON 是开发者维护的随包格式，不是面向用户的设置文件。

仓库内的完整示例：

- [全字段 demo](examples/device-configuration.full.json)：包含当前支持的全部 33 个覆盖字段、多型号、两种裁切坐标和完整的镜头关联。该文件只用于文档和开发参考，不会被打包，也不会自动加载。示例说明见下节。
- [OPPO X8 Ultra](../app/src/main/assets/device_configurations/oppo_x8_ultra.json)：`models` 为 `["PKJ110", "PKU110"]`，普通版与卫星通信版共用一份配置（[普通版官方参数](https://www.oppo.com/cn/smartphones/series-find-x/find-x8-ultra/specs/)、[卫星通信版官方参数](https://www.oppo.com/cn/smartphones/series-find-x/find-x8-ultra-satellite/specs/)）。
- [OPPO X9 Ultra](../app/src/main/assets/device_configurations/oppo_x9_ultra.json)：`models` 为 `["PMA110"]`。
- [OnePlus Ace 2](../app/src/main/assets/device_configurations/oneplus_ace2.json)：`manufacturer` 为 `OnePlus`，`models` 为 `["PHK110"]`（[设备信息](https://gitlab.com/fdroid/fdroidclient/-/issues/2837)）；默认 DCP 为 `OPPO Find X8 Ultra back camera 8.67mm f1.8 Adobe Standard.dcp`。

## 全字段 demo

[device-configuration.full.json](examples/device-configuration.full.json) 是不含注释的纯 JSON 示例，放在文档目录中，不会作为内置机型配置显示、打包或自动加载。它用于展示写法，参数组合不是任何真实机型的适配结果。开发者使用时应将其复制到 `assets/device_configurations/`，把 `manufacturer`、`models`、镜头 ID 和校准值换成目标设备的实际值，再删除不需要覆盖的字段。

示例包含以下关联和取值方式：

- **多型号与镜头发现**：`DEMO_MODEL_A`、`DEMO_MODEL_B` 共用全部设置。主摄为 `2`，微距为 `4`；`0/2` 表示逻辑镜头 `0` 绑定物理镜头 `2`。自动逻辑多摄发现关闭时，显式白名单仍生效。`99` 仅演示黑名单。
- **焦段**：`default_focal_length: -2` 表示默认 2×，`custom_focal_lengths` 中正数为 35mm 等效毫米数，负数绝对值为倍率；`hidden_focal_lengths` 使用正数毫米值。默认焦段也可用 `0` 表示不指定。
- **RAW 校正**：分别为物理镜头和 ISZ 虚拟镜头指定黑电平、白电平及 CFA。选择 `Custom` 时，同一镜头应在对应的 `raw_custom_*_levels` 中提供实际校准值；选择 `Default` 时使用原始元数据。
- **DCP 与降噪**：引用的是仓库中实际存在的内置资源 ID，仅演示引用格式，不代表适合 demo 的传感器。物理镜头和 ISZ 的模型映射分别填写；ISZ 不会自动继承物理镜头的降噪映射。`raw_dcp_ids_by_lens` 中镜头 `3` 的 `null` 表示关闭该镜头的 DCP，不是回退到全局 DCP。
- **两种 ISZ 裁切坐标**：镜头 `2` 演示竖屏左侧裁切 508，镜头 `4` 演示传感器底部裁切 508。两种写法位于不同镜头对象中；它们只有在传感器方向为 90° 时才表示相同的边。`is_macro: true` 演示将第二个虚拟镜头设为微距。
- **厂商参数关联**：两个 ISZ 的 `vendor_capture_profile_id` 与虚拟镜头 ID、`vendor_capture_settings` 一一对应。demo 同时列出 Qualcomm、vivo、MediaTek、OPPO 五种内置键；实际机型配置只保留该设备支持的键，不能把这组跨厂商示例当成通用参数一起启用。
- **自定义键**：`com.example.*` 是需要替换的示例键名，展示拍摄请求/会话参数、`INT32`/`U8`、全局/物理镜头/ISZ 作用域。同名拍摄键在全局值为 `1`，镜头 `2` 的同类型覆盖值为 `2`；条目的稳定 `id` 不同。
- **删除映射**：普通 map 中的 `legacy_lens: null` 用于移除已有条目；未出现该条目时无操作。`video_audio_input_id: "auto"` 使用系统自动选择的音频输入。

要重置整项设置，可将对应字段改为 `null`，例如：

```json
{
  "format": "photon_device_configuration",
  "version": 1,
  "name": "Reset selected device overrides",
  "manufacturer": "DEMO",
  "models": ["DEMO_MODEL_A", "DEMO_MODEL_B"],
  "overrides": {
    "preferred_macro_camera_id": null,
    "raw_dcp_ids_by_lens": null,
    "custom_vendor_key_settings": null
  }
}
```

只需保留当前值时，直接省略字段；`null` 会重置，不能用来表达“保持不变”。普通列表的 `[]` 会清空列表，而按 ID 合并的 ISZ/自定义厂商键数组的 `[]` 不会删除已有条目。

## 文件格式

文件必须是一个 JSON 对象，格式如下：

```json
{
  "format": "photon_device_configuration",
  "version": 1,
  "name": "配置名称",
  "manufacturer": "厂商",
  "models": ["PKJ110", "PKU110"],
  "overrides": {
    "字段名": "字段值"
  }
}
```

顶层只允许 `format`、`version`、`name`、`manufacturer`、`models`、旧字段 `model` 和 `overrides`。`format` 必须是 `photon_device_configuration`，`version` 当前必须是数字 `1`；`name` 必须是非空字符串，`manufacturer` 必须声明且为非空字符串。`overrides` 必须是包含至少一个字段的对象。

`models` 是必需的设备型号代码数组，一份配置中的全部覆盖设置共同适用于列出的型号。数组不能为空，每项必须是非空字符串、无首尾空格，且不可重复。`manufacturer` 与 `models` 必须同时声明，不能创建只有厂商或只有型号的通配配置。

已有开发者文件中的单型号字符串 `model` 仅为内部解析兼容字段，读取时转换为单元素 `models` 数组；不允许同时提供 `model` 和 `models`。新文件必须使用 `models`，应用不会生成或导出此格式。

内置文件最大为 1 MiB。未知顶层字段、未知覆盖字段、未知版本、JSON 语法错误或任何不符合下表类型/范围的值都会拒绝整个文件，不会部分应用。

### 自动匹配和应用

应用在首次读取设备配置 DataStore、发现镜头之前，读取 `Build.MANUFACTURER` 和 `Build.MODEL`，分别 trim 后按大小写不敏感规则与配置的 `manufacturer` 和 `models` 匹配。只有厂商和型号都声明且同时匹配的配置才算匹配。

全部内置文件中必须恰好有一份匹配：没有匹配时不修改任何现有设置；恰好一份匹配时，在一次事务内应用该文件声明的覆盖并写入“本设备已应用”标记；重复匹配属于配置错误，不能依赖文件顺序选择。后续启动只依据该标记跳过自动覆盖，不重写用户手动修改的设置。应用配置文件的内容发生变化，也不会自动重写已经标记为已应用的设备；当前没有新增版本或重新应用机制。

“本设备已应用”标记属于设备本地状态。跨设备恢复设置时必须保留目标设备自己的标记，不能用源设备的已应用状态替换它。

### 合并和重置

配置是覆盖集合，不要求包含所有设置。标量字段只覆盖自己；写入 `null` 会移除该字段当前保存的显式值，使其回到应用默认或其他基础设置。

普通列表字段（`custom_lens_ids`、`lens_id_blacklist`、`logical_camera_binding_whitelist`、`custom_focal_lengths`、`hidden_focal_lengths`）整体替换当前列表；列表元素不可重复。列表字段写入 `null` 会重置整个字段。

普通 map 字段按镜头 ID 合并：只更新文件中列出的镜头键，值为 `null` 的键会删除当前镜头条目。`raw_dcp_ids_by_lens` 是例外：它允许镜头值为 `null`，该 `null` 表示明确禁用该镜头的 DCP 映射；整个字段写入 `null` 才会重置整张 map。其他 map 字段中的 `null` 表示删除对应镜头键。

`isz_lens_configs` 和 `custom_vendor_key_settings` 是按稳定 ID 合并的数组：同一 ID 的条目更新原条目，未出现的条目保留；字段整体写入 `null` 会清空整个数组。`isz_lens_configs` 的稳定 ID 是由 `base_camera_id`、`isz_zoom_ratio` 和 `vendor_capture_profile_id` 生成的虚拟镜头 ID；自定义厂商键使用条目的 `id`。

`vendor_capture_settings` 按镜头 ID 合并，但每个镜头对应的对象是整体替换；镜头值为 `null` 会删除该镜头的厂商设置。其对象内部不能用 `null` 删除单个厂商键。字段整体写入 `null` 会重置全部镜头设置。

自动应用覆盖时会在同一事务内验证结果；如果 ISZ 镜头配置与对应镜头的厂商 profile 不一致，整次应用回滚。DCP、降噪 profile 等引用必须指向随应用打包的内置资源；维护机型配置时应同时提交所需资源，不能依赖用户导入或自定义模型。

## 支持的覆盖字段

下表中的覆盖字段字符串（以及 map 的镜头 ID）必须非空、首尾不能有空格，且不能包含逗号或控制字符；JSON 数字必须是有限值。顶层元数据仅适用上一节所述的顶层规则。

| 字段 | JSON 类型 | 取值和说明 |
| --- | --- | --- |
| `preferred_main_camera_id` | string | 主摄 Camera ID。 |
| `hdr_plus_frame_count` | integer | HDR+ 总帧数，范围 `1..20`；开启包围曝光时实际最少为 2 帧。X8 Ultra、X9 Ultra 内置为 8 帧。 |
| `preferred_macro_camera_id` | string | 微距 Camera ID。 |
| `custom_lens_ids` | array of string | 自定义镜头 ID 列表。 |
| `lens_id_blacklist` | array of string | 从所有发现来源中排除的镜头 ID 列表。 |
| `enable_logical_multi_camera_discovery` | boolean | 是否启用逻辑多摄发现。 |
| `logical_camera_binding_whitelist` | array of string | 每项必须是 `逻辑ID/物理ID`，两部分均非空且不能相同。 |
| `default_focal_length` | number | 有限数值；负数表示精确倍率，`0` 表示不指定，正数表示等效焦距。 |
| `custom_focal_lengths` | array of number | 自定义焦距；有限数值，范围 `[-Float.MAX_VALUE..Float.MAX_VALUE]`，不能为 `0`。 |
| `hidden_focal_lengths` | array of number | 要隐藏的焦距；有限正数，范围 `(0..Float.MAX_VALUE]`。 |
| `camera_orientation_offsets` | object&lt;string, integer&gt; | 按镜头 ID 的方向偏移；值只能是 `0`、`90`、`180` 或 `270` 度。 |
| `raw_black_level_modes` | object&lt;string, string&gt; | 每个镜头为 `Default` 或 `Custom`。 |
| `raw_custom_black_levels` | object&lt;string, number&gt; | 每个镜头的黑电平，范围 `0..65535`。 |
| `raw_white_level_modes` | object&lt;string, string&gt; | 每个镜头为 `Default`、`RAW10`、`RAW12`、`RAW14`、`RAW_SENSOR_65535` 或 `Custom`。 |
| `raw_custom_white_levels` | object&lt;string, number&gt; | 每个镜头的白电平，范围 `0..65535`。 |
| `raw_cfa_correction_modes` | object&lt;string, string&gt; | `Default`、`2x2_RGGB`、`2x2_GRBG`、`2x2_GBRG`、`2x2_BGGR`、`4x4_RGGB`、`4x4_GRBG`、`4x4_GBRG`、`4x4_BGGR`、`8x8_RGGB`、`8x8_GRBG`、`8x8_GBRG` 或 `8x8_BGGR`。 |
| `raw_lens_shading_correction_enabled` | boolean | 是否启用 RAW 镜头阴影校正。 |
| `raw_dcp_id` | string | 全局 DCP 配置 ID。 |
| `raw_dcp_ids_by_lens` | object&lt;string, string\|null&gt; | 按镜头的 DCP ID；值为 `null` 表示禁用该镜头映射。 |
| `raw_noise_profile_id` | string | 全局 RAW 噪声 profile ID。 |
| `raw_noise_profile_ids_by_lens` | object&lt;string, string&gt; | 按镜头的 RAW 噪声 profile ID。 |
| `raw_color_engine` | string | 当前支持的 `RawRenderingEngine` 名称。 |
| `use_p010` | boolean | 是否使用 P010。 |
| `use_p3_color_space` | boolean | 是否使用 P3 色彩空间。 |
| `oppo_super_stabilization_enabled` | boolean | 是否启用 OPPO 超级防抖。 |
| `video_audio_input_id` | string | 视频音频输入 ID。 |
| `nr_level` | integer | 照片降噪等级，范围 `0..4`。 |
| `edge_level` | integer | 照片锐化等级，范围 `0..3`。 |
| `video_nr_level` | integer | 视频降噪等级，范围 `0..4`。 |
| `video_edge_level` | integer | 视频锐化等级，范围 `0..3`。 |
| `isz_lens_configs` | array of object | 见下文。 |
| `vendor_capture_settings` | object&lt;string, object&gt; | 按镜头 ID 的内置厂商拍摄参数，见下文。 |
| `custom_vendor_key_settings` | array of object | 自定义的 Camera2 厂商键，见下文。 |

### ISZ 镜头对象

`isz_lens_configs` 的每个对象只允许以下字段：

```json
{
  "base_camera_id": "2",
  "isz_zoom_ratio": 2,
  "is_macro": false,
  "vendor_capture_profile_id": "oplus_agingtest_mode_select_22",
  "raw_black_border_crop_sensor_left_px": 508,
  "raw_black_border_crop_sensor_top_px": 0,
  "raw_black_border_crop_sensor_right_px": 0,
  "raw_black_border_crop_sensor_bottom_px": 0
}
```

`base_camera_id` 是非空字符串；`isz_zoom_ratio` 是 `1..100` 的有限数值；`is_macro` 是布尔值；`vendor_capture_profile_id`（如存在）是非空、经过允许字符清理的字符串。每个裁切值都是 `0..4096` 的整数。旧的 `raw_black_border_crop_left_px`、`top_px`、`right_px`、`bottom_px` 字段表示竖屏显示方向；新的 `raw_black_border_crop_sensor_left_px`、`top_px`、`right_px`、`bottom_px` 字段表示传感器方向。一个对象不能同时出现旧字段和新 sensor 字段；不要混用两套坐标。新配置应使用 sensor 字段。

上例的虚拟镜头 ID 为 `isz:2:2:oplus_agingtest_mode_select_22`。该 ID 对应的 `vendor_capture_settings` 必须配置为 `{"oplus_agingtest_mode_select":22}`（在同一文件中提供，或已存在于当前设置中）。完整关联示例见 OPPO X9 Ultra 配置文件。

### 内置厂商拍摄参数

`vendor_capture_settings` 的镜头 ID 对象只允许以下键，值必须是整数，并且处于该键可表示的范围（代码会拒绝需要归一化的值）：

| 键 | 类型/范围 |
| --- | --- |
| `insensor_zoom` | INT，任意 32 位整数 |
| `qcom_sensor_current_mode` | INT，任意 32 位整数 |
| `vivo_force_sensor_mode` | INT，任意 32 位整数 |
| `mtk_raw_bpp` | INT，任意 32 位整数 |
| `oplus_agingtest_mode_select` | BYTE，`-128..127` |

### 自定义厂商键对象

每个 `custom_vendor_key_settings` 条目只允许以下字段：

```json
{
  "id": "unique-id",
  "key_name": "vendor.key.name",
  "target": "CAPTURE_REQUEST",
  "value_type": "INT32",
  "value": 1,
  "lens_id": "2"
}
```

`id` 必须唯一且非空；合并后，如果同名键的 `target` 和作用域相同会冲突，作用域重叠且值类型不同也会冲突；`key_name` 必须非空，不能包含空白或控制字符；`target` 为 `CAPTURE_REQUEST` 或 `SESSION_PARAMETER`；`value_type` 为 `INT32` 或 `U8`；`value` 是整数，`INT32` 为任意 32 位整数，`U8` 为 `0..255`；`lens_id` 可省略或为 `null`（表示应用于所有镜头），存在时必须是非空字符串。

## 机型配置维护

新增或修改机型时，在 `app/src/main/assets/device_configurations/` 增加或更新 JSON，并确保 `manufacturer`、`models`、`overrides` 均符合上文规则。提交前应检查全部内置文件的匹配集合：同一设备若有匹配，必须恰好只有一份；重复匹配是配置错误，未匹配设备应保持现有设置不变。

DCP、RAW 噪声 profile 及其他被配置引用的资源必须随应用一起打包，并在配置中使用对应的内置资源 ID。机型配置不能要求用户导入模型或提供用户自定义资源。配置文件格式版本仍为数字 `1`；版本号表示 JSON 格式版本，不提供按版本重新应用已标记设备的机制。
