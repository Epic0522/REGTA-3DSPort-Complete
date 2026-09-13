# reVC 3DS 完整优化、美化与 re3 迁移交接

更新日期：2026-08-25

## 1. 文档用途与可信度

这份文档记录 `re3ctr/miami` 从“完整可通关但存在严重 3DS 问题”的 reVC 版本，整理到当前实机可用版本期间的全部主要优化、美化、修复、失败实验和 re3 迁移边界。

状态标记：

- **已实机确认**：在实体 New Nintendo 3DS 上观察到目标问题消失或功能正常。
- **已接受，有硬件上限**：功能明显改善，剩余问题来自高负载下的性能瓶颈。
- **候选保护**：代码已加入，但对应任务/极端场景没有完成独立复现验证。
- **VC 专属**：不能机械迁移到 GTA3。

本项目是 takedown 后保留下来的离线源码。不要连接远端或 push；不要把整个脏工作树当成一份可直接合并的补丁。迁移时按本文件列出的功能和文件逐项提取。

下屏的逐像素布局、生命周期和失败方案另见 `3DS_BOTTOM_SCREEN_HANDOFF.md`。本文是总索引，并覆盖该文档中已经过时的“当前版本边界”。

## 2. 当前 reVC 基线

- 源码仓库：`re3ctr/miami`
- 当前离线分支：`codex/revc3ds-radar-diagnostics`
- 历史 HEAD：`e604be65`；当前大量修改尚未形成上游提交
- 当前运行产物：`miami/miami.3dsx`
- 同步副本：
  - `re3ctr/miami/build/miami.3dsx`
  - `re3ctr/miami/miami.3dsx`
  - `re3ctr/release/miami.3dsx`
  - `miami/miami.3dsx`
- 四份当前产物 SHA-256：`76d3fce2074e99e02c39ad64a97a2e4d3029fd0f9a9c8e5b091967f32413f79d`
- 当前只交付 3DSX；CIA 尚未重做。正式 CIA 以后需要新的 banner，并确认 `mvd:STD` 服务与 New3DS 视频解码要求。

当前版本的整体结论：完整游戏可通关；加载已经短于旧 re3 3DS 版；粒子、人物/载具黑钻石、白反光、透明材质、车牌、输入和主要下屏体验均已修复。水边车辆破碎仍未解决；此前只改变 vehicle/boat pass 的补丁没有命中实机根因。高压力即时演算过场的性能正在继续优化。

## 3. 加载与流式传输

### 3.1 PED.IFP 一次性预读（已实机确认）

原版约 4 分 30 秒的加载中，`Load animations` 可占两三分钟。根因不是动画数据量，而是用文件流解析 `ANIM/PED.IFP` 时，每个 20–44 字节关键帧都会触发一次 SD 卡小读。

最终方案位于 `src/animation/AnimManager.cpp`：

1. 一次性把 PED.IFP 读入临时内存。
2. 通过 `rwSTREAMMEMORY` 解析原有完整动画数据。
3. 解析结束后释放临时文件缓冲。
4. 不删除动画、关键帧或关联组。

实机确认总加载时间已短于 GTA3 3DS 旧移植版。这是 VC 最大的加载收益。

### 3.2 Collision 解析优化（已保留）

`src/core/FileLoader.cpp` 为模型名建立 8192 槽开放寻址哈希索引，约 16 KiB，将每条 COL 记录最多线性扫描 6500 个模型名改为哈希查询，并保留迟注册模型的线性回退。

流式 COL 直接从读盘缓冲解析，取消逐模型复制到工作缓冲。功能保持完整；收益小于 PED.IFP，但解决了 `Loading collision` 中明显的纯 CPU 查找浪费。

### 3.3 双缓冲加载流水线与进度

`src/core/Streaming.cpp` 使用两个流通道重叠下一批读取与当前批转换；`src/core/Game.cpp`、`src/core/main.cpp`、`src/animation/AnimManager.cpp` 提供分阶段进度。加载画面约 200 ms 节流，避免每加载一个素材都重绘。

下屏进度条可完整走满；已去掉 `Loading the Game` 和 `ViceCityFor3DS` 等拥挤文本，只保留阶段、百分比和进度条。进入世界后的单帧短促补载不再闪任何 splash；正常跨岛仍保留原版 VC 加载图。

### 3.4 构建配置

`re3ctr/miami/build/GNUmakefile`：

- 工具链固定为 `/Users/epicreds/Projects/revc3ds/toolchains/devkitARM`，即 devkitARM release 55 / GCC 10.2.0；`DEVKITPRO` 必须指向 `/Users/epicreds/Projects/revc3ds/toolchains`。禁止使用系统 `/opt/devkitpro/devkitARM` 的 GCC 15.2.0，也禁止混合两套工具链的增量对象。

- `LOADING_PIPELINE=1`：进度、双缓冲流式加载、动画预读
- `BOTTOM_LOADING=1`：下屏加载生命周期
- `BOTTOM_RADAR=1`：下屏地图/HUD/触控
- `OPTIMIZED_BUILD=1`：O3 优化构建

re3 原版加载约一分钟，迁移前必须单独掐表。优先分析它是否也在 IFP 解析中产生大量小读；不能默认把 VC 的整个加载路径覆盖过去。

## 4. 下屏地图、HUD、菜单与触控（已实机确认）

### 4.1 游戏内布局

- 独立 320×240 RenderWare camera 绘制下屏。
- 原生动态雷达扩展到整个下屏，不用圆形遮罩。
- 玩家中心固定 `(120,120)`，保持在左侧地图区域中心，不移动到整屏中心。
- 地图继续在右侧 80px 半透明状态栏下面变化。
- 外围图标使用矩形射线边界，中心轨迹向内缩约 10px，避免图标被屏幕裁掉一半。
- 任务标点和常规图标针对下屏放大；新存档也强制使用矩形轨迹，不能混回原版圆轨迹。

### 4.2 右侧状态栏

- 使用原版武器贴图、字体、心和盾符号，不用 3DS 系统占位字体。
- 武器、弹药、时间、金钱、血量、防弹衣统一纵向间距，整体上移后的上下余量一致。
- 弹药单独放大；时间、血量、防弹衣使用接近原版可读尺寸；钱数保持较小。
- 心和盾与数字留出距离；数字和盾牌均恢复黑色投影/描边。
- 近战、空手和引爆器不显示弹药。
- 上屏常驻武器、弹药、时间、金钱、血量、防弹衣关闭。
- 通缉星保留在上屏右上角，只在通缉等级大于 0 时出现，上/右边距一致且保留阴影。

### 4.3 生命周期与美化

- 冷启动主菜单：上屏原主视觉，下屏显示游戏内完整世界地图素材，背景色与海面素材一致。
- 读档/新游戏：下屏地图移除，显示加载进度。
- 正常游戏：动态地图与状态栏。
- 游戏内暂停：保留暂停前的下屏游戏画面，不误当冷启动菜单。
- 即时演算过场：下屏主动提交黑色，不能穿帮显示地图/HUD。
- 一帧级补载：保留当前画面，不闪加载图。

### 4.4 触控面板

触摸下屏时覆盖半透明 `src/core/bottom_touch.bin`：

- 首次点击只唤出，不触发动作；松开后才 armed。
- L3、R3 写入 `LeftShock` / `RightShock`。
- Camera 拖动写入右摇杆通道，比例约 `dx/dy × 12`。
- 5 秒无触控后消失。
- 三个可见区拆为四张 2 的幂纹理，并在世界纹理大量分配前预创建；禁止恢复首次触控时延迟创建，否则会挤压/破坏运行中纹理。

## 5. 粒子系统（已实机确认，VC 专属为主）

### 5.1 根因与配置读取

旧 3DS `particle.cfg` 被删行后，加载器仍按行号分配类型，导致后续每一项整体错位；同时精简版 38 列配置与 VC 原版 41 列字段不兼容，stretch/create range/flags 落入错误字段。于是白烟、黑烟、尾气、扬尘、枪烟、喷泉等变成巨大纯色矩形。

`src/render/ParticleMgr.cpp` 现在：

- 按粒子名称查固定枚举槽，不再依赖行号。
- 使用完整 VC 41 列合同。
- ReloadConfig 时保留运行时 raster 指针，避免丢失后回退白纹理。
- 缺少的名字只禁用自身，不会错位污染后续类型。

运行目录使用恢复后的 `miami/data/particle.cfg`，SHA-256：`f1c54149bb4ee91339916b67eff9688544fdc562a09cc6a4c35570e2b3309e0d`。

### 5.2 Alpha 合并与数量限制

PC 粒子 TXD 常把颜色与透明遮罩拆成 `name` + `namem`。3DS 路径原先只取得颜色图，产生黑底或矩形。`src/render/Particle.cpp` 在初始化时把颜色与 mask 合成 ETC1A4，统一走与正常火焰相同的内建 alpha 格式；覆盖烟雾、轮胎烟、碎屑、树叶、纸屑、云、血、碰撞烟、弹着烟、弹壳、热浪和雨滴等。

- 活跃粒子总数上限：256。
- 恢复原版粒子种类，不再靠删配置保证不崩溃。
- 爆炸和火焰不做额外削减。

### 5.3 枪械专项性能

- 同时可见弹道最多 4 条。
- 弹道寿命最多 300 ms。
- 每条只绘制中央主体，去掉两个仅作端部渐隐的额外提交，单条 3 次提交降为 1 次。
- 每帧枪口火焰最多 3 个、枪烟 2 个、弹壳 2 个。
- 不影响枪械模拟、命中、爆炸和火焰。

实机确认开枪拖慢问题改善。

## 6. 人物、载具、透明材质与纹理缓存

### 6.1 人物黑钻石（已实机确认，VC 专属）

VC 是整体蒙皮人物；re3 是类似早期游戏的分段刚体人物。黑色褶皱三角不是 LOD、骨骼数量、权重、UV 或贴图错误，而是 3DS 蒙皮路径中的逐顶点光照/材质颜色进入 TEV 后产生错误三角面。

共享 `re3ctr/librw/src/3ds/3dsskin.cpp` 使用独立 skin texture shader：

- RGB 取 `texture0 × material RGB`，绕过损坏的逐顶点 lighting RGB。
- Alpha 保留 `texture alpha × material/vertex alpha`。
- 人物黑钻石消失，正常颜色保持；代价是人物不再精确随暗处逐顶点变暗。

曾测试过 GX 队列等待/同步，视觉无改善且会让游戏主线程冻结（车辆声音循环、Rosalina 仍可打开）；永久否决，不得恢复。

### 6.2 任务人物动画隔离（候选保护）

多人任务中，强制保留的任务人物可能共用临时蒙皮顶点缓冲，导致活人继承死者姿势，并在互动状态转换时崩溃。共享 3DS skin 路径改为每帧有界独立缓冲：192 个槽，总量不超过约 3 MiB，帧末统一复用。普通路人常被激进卸载，任务人物才是重点复现场景。

该保护已进入当前代码，但“暴动”等对应任务未完成最终独立实机回归，所以不能写成已彻底验证。

### 6.3 载具黑钻石与白反光（已实机确认）

`re3ctr/librw/src/3ds/3dsmatfx.cpp`、`3dsshader.cpp`、`3dsrender.cpp` 与 `miami/src/modelinfo/VehicleModelInfo.cpp`：

- 汽车、坦克、船、飞机的所有 atomic 统一走安全 MatFX 管线，覆盖没有显式 MatFX 材质的漏网部件。
- 基础颜色使用贴图与材质色，不使用损坏的逐顶点光照 RGB，消除黑色三角褶皱。
- 环境反射按原 coefficient 混合，并乘轻量环境色；不做昂贵逐像素场景取色。
- 反射强度约按 coefficient 的 0.5 倍进入 TEV，避免整面吹成纯白。
- 含 texture/material/vertex alpha 的车窗和灯罩绕过不透明环境反射，保留原版偏暗透明，不再乳白或纯白。

这些共享 librw 修改也会被 re3 链接；迁移时不要再复制一套不同实现。

### 6.4 纹理缓存、Alpha 与 LOD（已实机确认）

- 纹理 alpha 是每次绘制的 render state；即使纹理绑定命中缓存，也必须重新恢复 alpha 状态，解决树木等贴图偶发变成不透明矩形。
- 纹理内存回收改为 LRU。
- 载具纹理禁止参与破坏性 mip 裁剪；远处质量交给原生载具 LOD、GPU mip 采样和 TXD 流式卸载/重载。
- 这样车辆靠近时能恢复细节，不会跑远一圈回来后尾灯消失、车身永久扁平。

### 6.5 水边载具破碎（仍未解决）

症状是普通汽车靠近水边时立刻出现多边形/材质破碎，离开水边又立刻恢复。`src/render/Renderer.cpp` 已尝试只让真正的船进入延迟 boat/water pass、让普通汽车留在正常 vehicle pass，但实机确认症状仍在。因此不能再把 `bTouchingWater` 的 pass 分类当作根因；后续应继续检查水面 mask/MatFX 与车辆 MatFX 之间的 3DS GPU 状态或缓冲污染。

### 6.6 车牌 Z-fighting（已实机确认）

- 通用 `plates*` 纹理在默认/MatFX 两条 3DS 管线使用一致的小 depth bias。
- Admiral 的车牌不是普通 plate 材质，而是 `admiral868bit128` 图集里离车尾仅 0.004 的四顶点片。加载 `chassis_hi` 时精确识别四个顶点并向外移动 0.012；若模型版本不恰好匹配四点则拒绝修改。
- Admiral 是开局第一辆常见车，实机已确认该专项修复成功。

## 7. 音频

### 7.1 电台和流音频（已实机确认）

- 3DS 输出本身是单声道，MP3/ADF 在 mpg123 解码阶段直接 `MONO_MIX`，不再解码无用的第二声道。
- 处理一次 `MPG123_NEW_FORMAT` 通知，避免误判死轨并反复重开。
- 电台队列使用非阻塞的最小已处理缓冲数，取消双声道严格相等的死循环；解决低概率切台后极端卡顿且关闭电台才恢复的问题。
- 上车时使用较短初始缓冲，减少无电台车辆也会发生的卡顿。

### 7.2 任务/过场对白（已接受，有硬件上限）

所有 1120 个游戏对白 WAV 均为 mono。当前 3DS 路径：

1. 任务/过场 WAV 一次性完整解码到 PCM；最长原始对白低于约 0.4 MiB，安全上限 1 MiB。
2. 不经过逐帧 OpenAL 流队列；任务 slot 1/2 分别直接使用 NDSP channel 1/2，游戏 OpenAL 混音保留 channel 0。
3. 完整硬件 wave buffer 支持音量、pan、rate、pause、播放位置和 provider reset。

实机结果：

- 较低压力的 CG/即时演算过场可以达到约 30fps，语音与口型对齐。
- 较高压力场景，例如开幕直升机交易段，帧率只有个位数，仍会出现音频截断和卡壳。
- 这说明重复音/积累漂移已基本压下；剩余问题与重场景 CPU/GPU 饱和共同出现，属于性能瓶颈。不要再恢复双源严格同步、短块重启或 GX 等待。

## 8. 输入、提示文本、镜头与作弊码

### 8.1 3DS 原生键位（已实机确认）

- 在输入源头按 Nintendo 实体标签直接映射 ABXY，最终行为为 B 刹车、X 攻击，避免 Xbox/Nintendo 字母位置混乱。
- 29 类动态 `~k~` 提示使用固定 3DS ASCII 名称，不依赖小到无法编辑的桌面控制器菜单。
- 摇杆图标乱码改成 `CIRCLE PAD`、`C-STICK`、`STICK` 等英文文本。
- `HELP35/HELP36` 等转向提示已改为纯文本；外部 `american.gxt` 与 `utils/gxt/american.txt` 同步。

### 8.2 摇杆与镜头（已实机确认）

- Circle Pad 径向死区 0.14。
- C-stick 径向死区 0.18。
- C-stick 经过死区重映射后乘 1.65，解决视角必须大力掰动才转的问题。
- 场景切换/下车时清理残留输入，减少视角偶发仰飘。

### 8.3 系统键盘作弊码（已实机确认）

- 游戏运行中同时按 L+R+ZL+ZR，弹出 libctru QWERTY 系统键盘。
- 不进入游戏自身暂停菜单；系统键盘本身按 3DS 系统行为暂停进程。
- 输入转大写并逐字符送入原有 `AddToPCCheatString`。
- 仅接受字母和数字；组合键有 latch，长按不会反复弹出。
- re3 没有 VC 的暂停界面作弊识别，因此迁移时同样直接送字符，不调用 frontend pause。

## 9. 启动视频与加载美化

### 9.1 Logo 与 OP（已实机确认）

- `src/skel/3ds/3dsmovie.cpp` 使用 New3DS `mvd:STD` 硬件 H.264 解码。
- 视频为自定义逐 NAL `R3MVD01`/`.3mv`，音频为 32 kHz mono PCM sidecar。
- Logo 与 GTAtitles 均播放，任意主要按键可跳过；缺失或不支持时安全跳过，不阻止启动。
- 4:3 视频放在 320×240 下屏；上屏保留 `LOADSC0` 主视觉。
- 只交换下屏 framebuffer，避免播放时上屏主视觉闪动。
- 资源位于 `miami/movies/{Logo,GTAtitles}.{3mv,pcm}`，共约 14.6 MiB。

### 9.2 CIA 边界

旧 CIA 不包含这组正式视频/服务权限和未来正式 banner，不要继续覆盖它。最终发布阶段单独重做 CIA，并在实体 New3DS 上验证 HOME 启动、服务权限、视频播放与跳过。

## 10. 其他稳定性与小修

- 常用随机区间调用的整数重载歧义已修正，保证旧工具链可编译。
- 3DS 高精度 timer 走 system tick，供低帧率下时间推进和音视频定位使用。
- 编译兼容、文件路径和 OpenAL 3DS backend 有若干基础改动；迁移时只取当前构建所需部分，不能把整个旧共享库工作树覆盖到干净仓库。
- 当前完整源码差异横跨 `miami` 约 48 个文件，包含历史端口兼容改动；“文件出现在 git diff 中”不等于它属于本轮优化。

## 11. re3 迁移矩阵

### 11.1 已共享或已经进入 re3 候选

以下不应重复另写一套：

- 共享 librw 的车辆 MatFX、透明车窗、环境反射、车牌通用 depth bias、纹理 alpha cache、LRU/载具纹理保护。
- re3 源码中已经存在的单声道流音频、摇杆死区、1.65× C-stick、启动视频和系统键盘作弊码候选。
- re3 当前候选仍需按自己的实体机测试结果锁定，不能拿 VC 真机结果替代 re3 真机确认。

### 11.2 必迁移

1. 独立下屏 320×240 camera。
2. 原生动态雷达全屏化、固定 `(120,120)`、矩形裁切和外围图标轨迹。
3. 右侧原版 GTA3 HUD，按 GTA3 自身武器、血量、防弹衣和字体接口适配。
4. 冷启动菜单地图、加载、游戏、暂停、过场黑屏的完整下屏生命周期。
5. 触控 L3/R3/Camera 与纹理预创建。
6. 上屏重复 HUD 的关闭与仅通缉时显示星星。
7. 实体机对比迁移前后的 24–30fps 和线性内存。

### 11.3 先测再迁移

- PED.IFP 内存预读、Collision 哈希和双缓冲 streaming：先分阶段计时；re3 没有同等瓶颈时不要增加复杂度。
- 枪械粒子预算：只有 re3 自动武器/弹道也明显拖慢时才加。
- 任务对白 direct NDSP：re3 当前对白无重复音或音画漂移时不要动它成熟的音频路径。
- 启动视频/UI/作弊码虽已有候选，仍按 re3 资源名和启动顺序逐项确认。

### 11.4 不迁移

- VC `particle.cfg` 41 列和 VC 粒子枚举修复；re3 粒子本来正常。
- VC 蒙皮人物 texture-only shader 作为“人物修复”；GTA3 人物是分段刚体，不走同一 skin 问题。共享库代码可以保留，但不要为 GTA3 人物强制改管线。
- Admiral 图集四顶点偏移；车型/模型资源是 VC 专属。
- VC 普通汽车水状态过早进入 boat/water pass 的判断，除非 re3 实机出现同样症状。
- VC 上屏雷达黑块的旧探针和圆形深度遮罩实验。

## 12. re3 建议迁移顺序

1. 锁定 re3 当前可玩 3DSX、源码状态、SHA-256 和实机帧率。
2. 确认共享 librw 只存在一份实际链接目标，避免修改 symlink 旁的错误副本。
3. 实机验证已经进入 re3 候选的车辆、透明、车牌、视频、音频、死区、C-stick 和作弊码。
4. 只移植下屏 camera 与地图，先确认不崩溃和地图坐标正确。
5. 加矩形裁切、外围图标、任务点尺寸与右 HUD。
6. 加冷启动/加载/暂停/过场生命周期。
7. 最后加入触控覆盖层并测 L3、R3、Camera、5 秒隐藏。
8. 对比迁移前后帧率、内存和加载时间；再决定加载优化是否值得移植。
9. 每次代码修改后构建 3DSX、校验同步副本与 SHA-256；最终结论只由实体机确认。

## 13. 关键文件索引

### reVC 游戏层

- `re3ctr/miami/src/core/main.cpp`：下屏 camera、菜单地图、加载、HUD、触控纹理、过场黑屏
- `re3ctr/miami/src/core/Radar.cpp` / `Radar.h`：矩形动态地图、坐标、外围图标、任务点
- `re3ctr/miami/src/render/Hud.cpp`：上屏 HUD 与通缉星
- `re3ctr/miami/src/core/Pad.cpp` / `Pad.h`：3DS 输入、死区、C-stick、触控、作弊键盘
- `re3ctr/miami/src/core/ControllerConfig.cpp`：3DS ASCII 按键提示
- `re3ctr/miami/utils/gxt/american.txt`、`gamefiles/TEXT/american.gxt`：固定操作文本
- `re3ctr/miami/src/animation/AnimManager.cpp`：PED.IFP 预读
- `re3ctr/miami/src/core/FileLoader.cpp`：COL 名称哈希和直接解析
- `re3ctr/miami/src/core/Streaming.cpp`：双通道加载流水线
- `re3ctr/miami/src/render/ParticleMgr.cpp`：VC 粒子名称/41 列配置
- `re3ctr/miami/src/render/Particle.cpp`：粒子 alpha 合并、256 上限、枪口预算
- `re3ctr/miami/src/render/SpecialFX.cpp`：弹道限制
- `re3ctr/miami/src/modelinfo/VehicleModelInfo.cpp`：全部载具 MatFX、Admiral 车牌
- `re3ctr/miami/src/render/Renderer.cpp`：水边汽车正常 pass
- `re3ctr/miami/src/audio/oal/stream.cpp` / `stream.h`：mono、完整对白、direct NDSP
- `re3ctr/miami/src/audio/sampman_oal.cpp`：任务 stream 参数
- `re3ctr/miami/src/skel/3ds/3dsmovie.cpp` / `.h`：Logo/OP 播放

### 共享 3DS RenderWare

- `re3ctr/librw/src/3ds/3dsskin.cpp`：蒙皮人物安全颜色/alpha、逐帧独立缓冲
- `re3ctr/librw/src/3ds/3dsmatfx.cpp`：载具 texture/material、透明、环境反射、车牌
- `re3ctr/librw/src/3ds/3dsshader.cpp`：TEV 基础色与反射强度
- `re3ctr/librw/src/3ds/3dsrender.cpp`：默认管线 alpha 和 plate depth bias
- `re3ctr/librw/src/3ds/3dsdevice.cpp`：纹理 alpha 状态、下屏 camera 支持、帧末 skin buffer reset
- `re3ctr/librw/src/3ds/memory.cpp`：LRU 与载具纹理保护

### 外部运行资源

- `miami/data/particle.cfg`
- `miami/movies/Logo.3mv`
- `miami/movies/Logo.pcm`
- `miami/movies/GTAtitles.3mv`
- `miami/movies/GTAtitles.pcm`

## 14. 最终保留的经验

- 先用实机症状分类，再定位是素材、配置、RenderWare 状态还是性能瓶颈。
- 3DS 上“矩形粒子”不等于贴图拿错；可能是配置错位、mask 未合并、raster 指针丢失或 alpha cache 未恢复。
- 黑钻石不是 LOD 的同义词；VC 的根因是逐顶点光照颜色，正确修法是隔离 RGB 并保留材质/alpha。
- 低负载音频正常、高负载个位数帧率卡壳时，应承认整机瓶颈，避免继续加入会冻结主线程的同步等待。
- re3 是成熟基线，只迁移已证明有价值的体验层，不把 VC 的病灶和补丁一起搬过去。
