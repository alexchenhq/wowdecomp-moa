# Font SDF patch — 改动说明

给 wowdecomp 的字体管线接入单通道 SDF（路线 B）。**未实机验证**：本机没有 3.3.5a 的 `Data` 目录，只能保证编译通过与字节码入包，画质需进游戏看。

## 改了什么

### 新增

| 文件 | 作用 |
|---|---|
| `Engine/Source/Gxu/GxuFontSDF.h/.cpp` | EDT 距离场生成器（Felzenszwalb 两遍可分离 1D 变换），无第三方依赖 |
| `Engine/Source/Gxu/Shaders/FontSDF.hlsl` | ps_2_0 像素着色器源码 |
| `Engine/Source/Gxu/Shaders/FontSDF.ps` | fxc 编译产物（352 字节） |
| `Engine/Source/Gxu/GxuFontSDFShader.h` | 由 .ps 生成的 uint32 字节码数组（88 个字） |

### 修改

| 文件 | 改动 |
|---|---|
| `GxuFontTextureCache.cpp` | 图集格式 `GxTex_Argb4444` → `GxTex_Argb8888`；四张贴图函数改为写 32 位，统一走 SDF；行跨距 512 → 1024 |
| `GxuFontMiscClasses.h` | 四个 PasteGlyph* 的 `dest` 参数 `uint16_t*` → `uint32_t*` |
| `GxuFontMiscClasses.cpp` | 新增 `GxuGetFontSdfShader()` 用内嵌字节码创建像素着色器；`RenderBatch()` 内 `RsPush` + 绑定 + 常量 + `RsPop` |

改动量：**新增约 250 行，修改 123 行 / 删除 72 行**。

## 2026-10-25 06:21 修正（第二版反馈：描边字体太粗、堆积）

第二版实机确认「清楚了非常多」，但带 OUTLINE 的文字过粗。

原因：第一版把 `state[]`（字形本体 + 描边环）并成一个掩膜，描边环被当成实体笔画，等于把 1~2 texel 的 rim 焊进了字身。

改法：**距离场只存字形本体，描边在着色器里解析式扩张**。
- `PasteGlyphOutlinedAA` 改用 `coverage[]`（本体灰度）而不是 `state[]`
- `PasteGlyphOutlinedMonochrome` 只把 value 1/4 当墨，排除 value 2（rim）
- 着色器新增 c1/c2：
  - c1 = `(描边宽度 texel, 描边 alpha)`，宽度按 `fontFlags & 1`（有描边）与 `& 8`（粗描边）取 1.0 或 2.0 texel，alpha 取 0.47（对应原版 0x7000 的 7/15）
  - c2 = 描边颜色（黑）
- 合成：`rgb = lerp(描边色, 文字色, body)`，`alpha = max(body * 文字alpha, rim * 描边alpha)`

可调参数：描边 alpha（0.47）、描边宽度（1/2 texel）。嫌细可提 alpha 到 0.55，嫌粗可降到 0.35。

## 2026-10-25 修正（第一版实机反馈：更模糊）

三个原因，全部已改：

**1. 缩放常数写死（主因）**
第一版 ps_2_0 假设 1 texel = 1 屏幕像素，把锐度常数写死为 8.0。实际客户端 `gxResolution 3840x2160` + `uiScale 0.64`，文字被缩小到 64%，正确系数应是 8 ÷ 1.5625 = 5.12。用 8.0 导致过渡带被拉宽 56%。
→ 改成 **ps_3_0 + `ddx/ddy`**，用屏幕导数实时测出「一个屏幕像素覆盖多少 texel」，任何 uiScale 下都正确。
→ 常量 c0 改为 `(1/256, 1/256, 256, 256)`。

**2. 图集用最近邻过滤（同样致命）**
`CreateTexture(this->m_fontFlags & 4)` —— 原本多数情况是 0，即 `GxTex_Nearest`。覆盖度位图 1:1 采样时无所谓，但距离场用最近邻缩放会直接碎掉。
→ 强制 `GxTex_Linear`。

**3. 距离场从二值掩膜算，丢失亚像素**
阈值 128 后再做 EDT，边缘只剩整像素精度，小字号下笔画圆钝。
→ 对部分覆盖的像素（0 < coverage < 255）直接用 `sd = coverage - 0.5` 覆盖 EDT 结果，恢复亚像素轮廓。

**诊断输出**：像素着色器创建成功/失败会打印到 WowClient 的控制台窗口：
```
[DragonUI-SDF] font SDF pixel shader active (ps_3_0)
[DragonUI-SDF] font SDF pixel shader FAILED to create (ps_3_0 unsupported?)
```

## 原理

原管线：FreeType 光栅化 → 8 位覆盖度 → 量化进 4 位 alpha → 固定功能纹理阶段采样。
问题：4 位 = 16 级灰阶，且缩放时位图被拉伸采样。

新管线：
1. FreeType 光栅化照旧（8 位灰度）
2. 在 50% 覆盖度处阈值化得到二值掩膜
3. 对该掩膜做精确欧氏距离变换，得到每个像素到最近边界的距离
4. 编码 `value = 0.5 + signedDistance / (2 * 4.0)` 写进 8 位 alpha
5. 像素着色器反解：`coverage = saturate((dist - 0.5) * 8 + 0.5)`

距离场与分辨率解耦，所以放大不再是位图拉伸，边缘由着色器按屏幕尺寸重建。

## 怎么测

1. 把 3.3.5a 客户端的 `Data` 目录复制到 `bin/Win/Data`
2. 改 `Data/<locale>/realmlist.wtf` 指向你的服务器
3. 跑 `WowClient64_sdf.exe`（带崩溃日志版）
4. 重点看：金币数字、伤害数字、聊天文字、动作条快捷键

对比基线：`git stash` 后重编的原版 exe（同目录，需要时我可另编译）。

## 已知风险

1. **描边变粗**：原版描边是「几何偏移 + 独立 alpha rim」两层；SDF 版把 body 和 rim 合并成一个掩膜，所以带 OUTLINE 的文字会比原版略粗约 1 像素。
2. **其他文字绘制路径未覆盖**：只改了 `BATCHEDRENDERFONTDESC::RenderBatch`（FrameXML 文本主路径）。若还有别的地方直接画字体图集（如 3D 名字板、某些 Glue 界面），会走固定功能路径，SDF 值会被当成 alpha 显示成灰雾。**这是本次最需要实机确认的点。**
3. **顶点寄存器假设**：着色器按 `TEXCOORD0` + `COLOR0` 取输入，与原版 UI 顶点着色器的输出约定一致。若不一致，文字会整体错色或变黑。
4. **设备丢失后不重建**：自建的 `CGxShader` 不在 `m_shaderList` 里，D3D 设备重置后不会自动重建（切分辨率/全屏切换可能触发）。出现文字消失，重启客户端即可。
5. 破坏 `// OFFSET:` 对账纪律，新增代码无原始地址可对。

## 回退

```bash
git checkout -- Engine/Source/Gxu/GxuFontMiscClasses.cpp \
                Engine/Source/Gxu/GxuFontMiscClasses.h \
                Engine/Source/Gxu/GxuFontTextureCache.cpp
git clean -fd Engine/Source/Gxu/Shaders Engine/Source/Gxu/GxuFontSDF.* Engine/Source/Gxu/GxuFontSDFShader.h
```
然后重编，约 1 分半。

## 下一步（未做）

- 把 EDT 换成 msdfgen 真 MSDF（尖角锐利，需解决 FreeType 2.0 兼容）
- 中文：`CGxString::WriteGeometry` 已带 `texturePage`，每字面最多 8 页；中文常用字需要压测页数上限
- 分析式描边：把 OUTLINE 从几何偏移改成着色器内 `smoothstep` 双阈值，可顺带解决风险 1
