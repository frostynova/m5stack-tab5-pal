# M5Stack Tab5 版 SDLPal

[English](README.md)

这是面向 ESP32-P4 M5Stack Tab5 的 [SDLPal](https://github.com/sdlpal/sdlpal)
ESP-IDF 移植版本。当前固件能够以横屏方式运行 Windows 95 版《仙剑奇侠传》，
支持屏幕触摸控制、SD 卡存档、音效以及 RIX/OPL 音乐。

本仓库不包含受版权保护的游戏数据。请使用你合法持有的 Windows 95 版游戏资源文件。

## 当前状态

- 已在 M5Stack Tab5 硬件版本 2、ESP32-P4 芯片版本 1.3 上完成测试
- 将仙剑的 320x200 帧缓冲缩放并旋转至 1280x720 屏幕
- 提供触摸方向键以及确认、取消、物品/重复、强制和自动战斗按键
- 从 FAT32 SD 卡加载 Windows 95 版游戏资源
- 支持在同一 SD 卡目录中保存和读取存档
- ES8388 以 44.1 kHz 单声道输出音频
- 通过 DOSBox 整数 OPL 核心播放 Windows 版音效和 RIX 音乐
- SDL 线程栈分配在 PSRAM 中，音频任务固定在 CPU 核心 1

## 工具链

已经验证的工具链版本是 ESP-IDF 6.0.1。在本项目使用的开发 Mac 上，每次打开
新的终端后通过下面的命令激活环境：

```sh
source /Users/flex/.espressif/tools/activate_idf_v6.0.1.sh
```

在其他电脑上，请安装 ESP-IDF 6.0.1，并使用对应安装目录中的常规 `export.sh`
或环境激活脚本。

## SD 卡

使用 MBR 分区表，并将 SD 卡格式化为 FAT32。把游戏文件放入卡上的 `/pal`
目录：

```text
/pal/
├── abc.mkf
├── ball.mkf
├── data.mkf
├── f.mkf
├── fbp.mkf
├── fire.mkf
├── gop.mkf
├── map.mkf
├── mgo.mkf
├── pat.mkf
├── rgm.mkf
├── rng.mkf
├── sss.mkf
├── word.dat
├── sounds.mkf
└── mus.mkf
```

前 14 个文件组成 Windows 95 版的核心资源集。`sounds.mkf` 提供音效，
`mus.mkf` 提供 RIX 音乐。存档文件和 SDLPal 配置也会写入 `/pal`。

启动游戏引擎前，固件会检查所有必需文件，并执行一次 SD 卡写入、同步和回读测试。
如果资源不完整，设备会停留在诊断界面，而不会启动游戏。

## 操作方式

| 屏幕按键 | 功能 |
| --- | --- |
| 方向键 | 行走或移动当前选项 |
| A | 确认、调查或互动 |
| B | 取消或打开菜单 |
| X | 非战斗状态下使用物品；战斗中重复上一条指令 |
| Y | SDLPal 支持的强制/防御操作 |
| AUTO | 开启或关闭自动战斗 |

方向键使用了扩大的触摸判定区域。手指保持按下时，可以直接滑向另一个方向。

## 构建和刷机

激活 ESP-IDF 环境后执行：

```sh
idf.py set-target esp32p4
idf.py build
idf.py -p /dev/cu.usbmodem101 flash monitor
```

请把串口设备名替换为系统分配给你的 Tab5 的设备名。按 `Ctrl+]` 退出串口监视器。

如需验证项目是否能够从干净环境重新构建：

```sh
idf.py fullclean
idf.py build
```

`sdkconfig.defaults` 和 `dependencies.lock` 会纳入 Git。自动生成的 `sdkconfig`、
`build` 和 `managed_components` 目录会被忽略。

## 针对硬件的稳定性设置

官方 `espressif/m5stack_tab5_noglib` 1.2.0~1 组件保持原样，没有直接修改。
针对当前硬件的稳定性策略均位于项目自身代码中：

- `main/resource_check.c` 将 SDMMC 限制为 20 MHz，避免测试所用 SD 卡偶发
  MKF 文件短读。
- SDL Tab5 适配层会在显示启动前重试偶发失败的 PI4IOE5V6408 初始化。
- 适配层自行实现仙剑的 PPA 显示路径；LCD DMA 读取前，会显式同步 PSRAM 中
  未被 PPA 改写的黑边区域。
- 项目触摸桥接层会标准化 Tab5 坐标并生成有效的 SDL3 手指事件，以规避当前
  固定版本 SDL 组件中 ESP-IDF 触摸后端的兼容性问题。

## 仓库结构

```text
components/georgik__sdl_bsp/       Tab5 专用 SDL 显示和触摸适配层
components/sdlpal/                 SDLPal 引擎、平台移植层和触摸界面
main/                              启动流程和 SD 游戏资源诊断
docs/porting-plan.md                已完成的里程碑和剩余验证项目
```

## 剩余验证

当前固件是一个可玩的基线版本。在将其定义为稳定版本前，建议进行一次时间更长的
连续游戏测试，覆盖场景行走、菜单、战斗、场景切换、保存、重启和读取存档，并同时
观察串口日志中是否出现重启、SD 卡错误或音频写入失败。
