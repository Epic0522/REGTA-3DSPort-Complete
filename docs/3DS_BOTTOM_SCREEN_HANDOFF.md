# reVC 3DS 下屏方案与 re3 移植交接

更新日期：2026-08-25

> 本文保留为下屏专项实现说明。reVC 后续完成的粒子、渲染、音频、输入、视频与 re3 总迁移边界，统一记录在 `REVC_3DS_COMPLETE_HANDOFF.md`。本文第 10 节的旧 v13 版本边界仅用于追溯，不再代表当前构建。

## 1. 目标与后续范围

当前工作对象是 `re3ctr/miami`（reVC 3DS）。VC 完成后，把已经实机验证的下屏方案移植到 `re3ctr/re3`。

re3 本身已经是成熟、完整且性能良好的 3DS 移植，雷达、电台、人物、任务等均正常，实机帧率约 24–30 FPS。因此后续不要把 VC 的全部修改机械合并到 re3：

1. 必做：下屏动态地图、右侧 HUD、触控 L3/R3/Camera、加载阶段下屏生命周期。
2. 可选：先实测并分析 re3 加载时间，再决定是否移植加载优化。
3. 不做：VC 专属粒子删减、钻石人/动态顶点诊断、VC 雷达黑块探针等问题修复。
4. 离线工作：两个仓库都属于 takedown 后保留下来的离线历史，不连接远端、不 push。

## 2. 当前最终交互设计

### 2.1 游戏运行时

- 下屏分辨率为 320×240。
- 整个下屏绘制原生动态雷达地图，不使用圆形遮罩。
- 玩家中心固定在 `(120, 120)`，不是整个下屏中心 `(160, 120)`。
- 地图继续使用游戏原生旋转、缩放、雷达块和图标算法。
- 右侧 `x=240..320` 为 80 像素 HUD 栏；地图仍在半透明 HUD 背后继续变化。
- `x=240..242` 绘制粉色竖向分隔线。

### 2.2 右侧 HUD

- 武器图标：约 `(250,3)..(310,61)`，优先从武器模型 TXD 读取真实图标，模型不可用时回退到原 HUD sprite。
- 信息基线统一为每 32 像素一行：
  - 弹药：`y=64`
  - 时间：`y=96`
  - 金钱：`y=128`
  - 血量：`y=160`
  - 防弹衣：`y=192`
- 弹药、时间、血量、防弹衣数字使用约 `0.52×0.78`；金钱保留 `0.44×0.67`。
- 近战、空手和引爆器不显示弹药。判断基于 `weaponInfo->m_nWeaponSlot > 1`，并排除 `WEAPONTYPE_DETONATOR`，不能直接相信近战武器结构中的残留弹药值。
- 数字中心约 `x=288`。
- 心和盾图标当前源码中心为 `x=258`，尺寸约 `0.48×0.74`。
- 防弹衣数字保留原描边；盾图标单独关闭额外边缘/阴影层，以避免在小尺寸下形成一圈实心蓝边。
- 上屏常驻武器、弹药、时间、金钱、血量、防弹衣关闭。
- 通缉星仍在上屏右上角，但只有通缉等级大于 0 时显示；上边距与右边距一致，保留原版阴影。

### 2.3 外围图标

- 不再沿原版圆形轨迹限制远处任务点。
- 使用射线到矩形边界的限制方式，让外围图标自然落到整个下屏的四边。
- 18 像素图标中心轨迹向内缩约 10 像素，避免只显示半个图标。
- 当前归一化边界：左 `-11/12`、右 `19/12`、上下 `±11/12`；对应屏幕中心轨迹大约为 `x=10..310`、`y=10..230`。

## 3. 下屏渲染架构

### 3.1 独立 RenderWare Camera

在 `miami/src/core/main.cpp` 中创建独立的 320×240 下屏相机：

- camera raster：320×240
- z raster：320×240
- near clip：0.1
- far clip：100
- view window：`{0.7, 0.525}`

每帧流程：

1. 清理下屏 camera raster/z raster。
2. `RwCameraBeginUpdate(BottomRadarCamera)`。
3. 设置 `CRadar::m_bDrawingBottomScreen = true`。
4. 调用原生 `CRadar::DrawMap()` 和 `CRadar::DrawBlips()`。
5. 恢复 `m_bDrawingBottomScreen = false`。
6. 绘制右侧 HUD 与触控面板。
7. `RwCameraEndUpdate` 并向下屏 `RwCameraShowRaster`。

生命周期判断不能使用 `gameAlreadyInitialised`：该变量在这个 3DS 正常新游戏路径中不会被设置。应使用：

- `FrontEndMenuManager.m_bGameNotLoaded`
- `FrontEndMenuManager.m_bMenuActive`
- `CHud::m_Wants_To_Draw_Hud`
- `BottomLoadingActive`

退出 RenderWare 时必须销毁下屏 camera、raster 和预载触控纹理，当前入口是 `ShutdownBottomRadar()`。

### 3.2 Radar.cpp 的下屏分支

`CRadar::m_bDrawingBottomScreen` 是复用原生雷达算法的总开关。当前关键修改包括：

- 地图多边形使用 Sutherland–Hodgman 裁切到下屏非对称矩形：`x=-1..5/3`、`y=-1..1`。
- `TransformRadarPointToRealWorldSpace` 在下屏分支中使用 `(120,120)` 为中心，比例为 120。
- 下屏低速最小雷达范围提高到约 180，避免把低分辨率地图块放得过大而显得马赛克。
- 下屏图标尺寸固定约 18×18。
- `LimitRadarPoint` 使用矩形边界，而不是原版圆形边界。
- 上屏原生雷达绘制由 HUD 条件编译关闭。

注意：部分旧注释仍描述圆形/216 像素方案，移植时应以当前实际矩形代码和本文件为准。

## 4. 触控面板

素材来自用户提供的 `bottom.png`，当前作为 `miami/src/core/bottom_touch.bin` 嵌入执行程序。透明羽化必须保留。

### 4.1 显示规则

- 第一次点击下屏只显示控制面板，不触发按键。
- 必须先松开触控，面板才进入 armed 状态，避免第一次点击误触鸣笛或镜头。
- 5 秒没有触控后自动隐藏。
- 面板直接覆盖在地图/HUD 上，不替换地图画面。

### 4.2 区域与映射

- L3：`x=23..147`、`y=26..104`
- R3：`x=173..298`、`y=26..104`
- Camera：`x=28..292`、`y=119..221`

L3/R3 直接写入 `PCTempJoyState.LeftShock/RightShock`，实机已生效。

Camera 不能只写 PC mouse state；该路径在 3DS 游戏镜头中不生效。当前方案计算触控相邻帧位移，并写入已经验证可用的右摇杆通道：

- `RightStickX = clamp(dx * 12, -128, 128)`
- `RightStickY = clamp(dy * 12, -128, 128)`

### 4.3 纹理内存注意事项

- 3DS 纹理使用 2 的幂尺寸，不能把 320×240 PNG 当作单张普通纹理直接上传。
- 当前把面板的三个可见区域拆成四张小纹理：L3、R3、Camera 主体、Camera 右侧羽化边缘。
- 这些纹理必须在 `RsRwInitialize` 后、世界纹理大量占用 linear memory 前预创建。
- 曾经在第一次触控时才创建纹理，会让 3DS 纹理分配器在压力下缩小/破坏其他游戏纹理；不要恢复延迟创建方案。

## 5. 下屏加载生命周期

### 5.1 启动和读档

- 开机/主菜单阶段下屏保持黑色。
- 新游戏或读档开始时，下屏地图隐藏，改为黑底加载状态、阶段文字、百分比和进度条。
- 进度条范围约 `x=12..308`，可以完整走满。
- 加载结束后必须清理两个下屏 framebuffer，防止隔帧残留加载画面，然后恢复地图/HUD。

### 5.2 短促补载

最终决定：进入世界后的一帧级资源补载不显示任何加载图片。

`LoadingScreen()` 在游戏已经运行、菜单未打开且 `BottomLoadingActive == false` 时直接返回，从而保持上一帧游戏画面。不要再嵌入透明 `Loading…` PNG。

正常跨区仍走原版 `LoadingIslandScreen()`，保留 VC 原来的橙色/蓝色“欢迎来到罪恶都市”画面。

已否决的透明 PNG 方案存在三个问题：普通加载函数会先清黑屏，PNG 透明区域因此仍显示黑色；裁切可见区域时容易丢失文字；其他直接绘制 splash 的路径可能回退显示 `LOADSC0`。该素材已从源码移除。

## 6. 已否决方案和原因

### 6.1 上屏原雷达直接修补

- 原版 VC 雷达在 3DS 上始终是方形黑块。
- 把 radar texture 替换为白色后仍然是黑块，说明问题不只是雷达贴图内容，更可能在即时渲染、深度遮罩或 3DS TEV 状态。
- 当前目标是改善实际游玩，不继续在上屏黑块上消耗时间。

### 6.2 圆形下屏雷达和原版遮罩

- 独立下屏 camera 本身工作正常、位置正确且不崩溃。
- 一旦恢复原版深度遮罩，地图主体变黑，只剩边框和图标。
- 因此下屏最终方案永久取消圆形深度遮罩，改用全屏矩形地图。

### 6.3 固定整岛地图

- 整岛地图在 320×240 下屏上道路太小，并且与暂停菜单的大地图功能重复。
- 最终继续使用原生动态雷达/指南针式地图，只是扩大到整个下屏。

### 6.4 旧下屏占位 HUD

- 早期版本用 3DS 默认 8×8 字体和手动画条，只适合证明下屏可写。
- 最终 HUD 使用 VC 原版字体、符号和武器贴图。
- `DrawBottomRadarStatus()` 是保留在源码中的旧占位实现，移植到 re3 时不要把它当成最终方案；最终入口是 `DrawBottomOriginalHud()`。

## 7. VC 加载优化经验（re3 只按需移植）

VC 原先约 4 分 30 秒，主要卡在 `Load animations` 两三分钟。实机确认后的关键修复是：

- 一次性把 `ANIM/PED.IFP` 读入内存。
- 用 `rwSTREAMMEMORY` 在内存中解析，避免每个 20–44 字节关键帧都触发一次 SD 卡 `fread`。
- 解析完成后释放临时文件缓冲，仅保留正常动画结构。
- 此改动没有删除动画内容；实机加载时间已短于 GTA3 3DS 版。

其他加载相关修改包括：

- 约 200 ms 刷新节流，避免每个文件都重绘加载画面。
- 固定阶段文字和分段进度范围。
- `Streaming.cpp` 中双缓冲读取/转换流水线。
- O3 构建开关。

移植到 re3 前先掐表并分析：re3 原本约 1 分钟，不能因为“VC 有效”就默认全部合并。优先检查 re3 的 IFP 是否也存在大量微小文件读取；若没有明显瓶颈，仅移植下屏。

## 8. re3 后续移植顺序

1. 在 re3 建立独立离线工作分支/工作树，保留当前完善基线和实机帧率数据。
2. 对照 re3 自身 `Radar.cpp/Radar.h/Hud.cpp/main.cpp/Pad.cpp` API，不整文件覆盖 VC 文件。
3. 先加入编译开关与独立 320×240 下屏 camera，只绘制地图和图标，实机确认不崩溃。
4. 加入矩形裁切、固定 `(120,120)` 中心、矩形外围图标轨迹和 10 像素内缩。
5. 加入右侧原版 HUD，并关闭上屏重复 HUD；通缉星保留在上屏。
6. 加入下屏加载生命周期，验证主菜单、读档、游戏、暂停、跨区和短促补载。
7. 最后加入触控面板和预载纹理，分别验证 L3、R3、Camera 与 5 秒隐藏。
8. 对比移植前后帧率和内存；第二个 RW camera 与全屏地图不得明显破坏 re3 原有 24–30 FPS。
9. 只有 re3 加载实测仍值得优化时，才单独移植 PED.IFP 预读或流水线，并再次掐表。

## 9. 关键文件

VC 当前参考实现：

- `re3ctr/miami/build/GNUmakefile`：`LOADING_PIPELINE`、`BOTTOM_LOADING`、`BOTTOM_RADAR`、`OPTIMIZED_BUILD`
- `re3ctr/miami/src/core/main.cpp`：下屏 loading、独立 camera、地图/HUD、触控纹理、生命周期、短补载抑制
- `re3ctr/miami/src/core/main.h`：下屏销毁和加载进度接口
- `re3ctr/miami/src/core/Radar.cpp` / `Radar.h`：地图裁切、坐标、矩形外围图标、下屏状态
- `re3ctr/miami/src/render/Hud.cpp`：关闭上屏重复 HUD、移动并按需显示通缉星
- `re3ctr/miami/src/core/Pad.cpp` / `Pad.h`：触控面板、L3/R3、Camera 右摇杆映射
- `re3ctr/miami/src/core/Game.cpp`：加载阶段与下屏资源销毁
- `re3ctr/miami/src/animation/AnimManager.cpp`：可选 PED.IFP 一次性预读
- `re3ctr/miami/src/core/Streaming.cpp`：可选加载流水线
- `re3ctr/miami/src/core/bottom_touch.bin`：触控面板原始 PNG 数据

re3 对应源码已经存在于 `re3ctr/re3`，并且本身带有完整地图功能，应优先复用 re3 自己的实现与类型。

## 10. 当前版本边界

- 最近实机构建：`re3ctr/miami/artifacts/miami-r55-bottom-touch-hud-no-streaming-flash-o3-v13.3dsx`
- v13 SHA-256：`e0a1c5432b7405b8c72e965dfd331a52a49d6f692ce77e765c89001eaa67acaf`
- v13 已验证短促补载不再闪加载图。
- 当前源码比 v13 更新，但尚未编译/打包：防弹衣数字恢复描边，盾图标仍无蓝边，心与盾由 `x=252` 右移到 `x=258`。
- 下一次打包必须从当前源码继续，不能把 v13 误当作包含最后这组 HUD 微调。

推荐 VC 构建命令（使用已验证可运行的 r55 工具链）：

```sh
export DEVKITPRO=/Users/epicreds/Projects/revc3ds/toolchains
export DEVKITPRO="/Users/epicreds/Projects/revc3ds/toolchains"
export DEVKITARM="$DEVKITPRO/devkitARM"
export PATH="$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH"
make -f GNUmakefile -j6 LOADING_PIPELINE=1 OPTIMIZED_BUILD=1 BOTTOM_LOADING=1 BOTTOM_RADAR=1
```

这里必须使用项目锁定的 devkitARM release 55 / GCC 10.2.0。不要改回系统
`/opt/devkitpro/devkitARM`（当前为 GCC 15.2.0），也不要把两套工具链生成的对象文件混合链接。

构建目录：`/Users/epicreds/Projects/revc3ds/re3ctr/miami/build`

所有功能状态最终以实体 3DS 为准；编译成功、静态检查或模拟器画面不能替代实机确认。
