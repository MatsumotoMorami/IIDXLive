# IIDXLive

IIDXLive 为 OBS 提供「IIDX 游戏音画（原生音频）」来源，用于录制或直播游戏，并在 OBS 中叠加手元摄像头、麦克风和场景装饰。

项目包含游戏端 `IIDXLive.dll` 和 OBS 插件 `iidx-live-obs.dll`。游戏端从原生录像使用的音频分流点取得声音，通过共享内存传给 OBS；视频使用 OBS 游戏捕获，并提供 TDJ 主屏取图修正。游戏可继续使用 ASIO，无须切换 WASAPI。

IIDXLive 独立工作，不依赖 IIDXRecorder。需要同时保存原生 MP4 时，可另外挂载 IIDXRecorder；两者分别由 Spice 加载。直播平台、编码器和推流密钥在 OBS 中设置。

## 安装

适用环境：Windows x64。OBS 插件目前按 OBS Studio 32.2.2 接口构建。

1. 将 `IIDXLive.dll` 和 `IIDXLive.ini` 放到游戏目录，与 `bm2dx.dll` 同目录。
2. 在 Spice 的 DLL 注入列表（`-k`）中加入 `IIDXLive.dll`，保留其他所需 DLL。同一 DLL 只挂载一次。
3. 将 `iidx-live-obs.dll` 放到 `%ProgramData%\obs-studio\plugins\iidx-live-obs\bin\64bit\`，并创建 `%ProgramData%\obs-studio\plugins\iidx-live-obs\data\`。
4. 启动 OBS，在来源中添加 **IIDX 游戏音画（原生音频）**，保留自动选择游戏窗口。
5. 启动游戏，检查画面和该来源的音量表。分辨率、帧率、编码器及同步偏移由 OBS 设置。

更新 DLL 前请退出游戏和 OBS。安装脚本会备份已有 Live 文件，并保留已有 INI；不会修改 Spice 注入列表或安装 Recorder。

## 启动游戏前排版

游戏未运行时，来源保持 **1920×1080** 的固定尺寸，画面完全透明，不遮挡其他场景装饰。在 OBS 来源列表中选中它，通过「变换 → 编辑变换」设置位置和大小，再锁定来源。游戏启动后自动连接，无须切出游戏调整布局。

自动选择仅匹配桥接进程内标题以 `main` 结尾的 IIDX 主窗口，不回退到 `sub` 副屏窗口。捕获画面等比适配来源尺寸；非 16:9 的画面会留黑边。

## INI 配置

修改游戏目录中的 `IIDXLive.ini`，重启游戏后生效。

| 配置项（`[Live]`） | 默认值 | 说明和选项 |
| --- | --- | --- |
| `Enabled` | `1` | `1` 启用；`0` 禁用桥接。 |
| `MainScreenOnly` | `1` | `1` 启用 OBS D3D9 主屏取图修正；`0` 使用 OBS 默认取图行为。修正只作用于 OBS 发起的查询。 |
| `SampleRate` | `44100` | 原生音频采样率，可设置 `44100` 或 `48000`，必须与游戏内部音频一致。设置错误会变调。OBS 可自行重采样，不必为匹配 OBS 而修改。 |

Live 不负责 MP4 文件命名、保存路径或单曲录制开关。这些由单独的 Recorder 配置管理。同时录制和推流会增加编码负载。

## 排查

- 没有来源选项：检查 OBS 插件安装路径，并重启 OBS。
- 没有声音：查看游戏目录的 `IIDXLive.log` 是否出现 `Audio bridge installed`，以及 OBS 日志中的 `[iidx-live]` 信息。
- 黑屏或窗口不匹配：检查游戏端 DLL 是否加载、窗口选择和显卡设置；OBS 与游戏应使用同一显卡。游戏以管理员权限运行时，OBS 可能也需要相应权限。
- 音画不同步：在 OBS 中根据拍键声和手元画面调整同步偏移。

## 构建和本地安装

需要 Zig 0.14.1，以及 OBS Studio 源码（当前使用官方标签 `32.2.2`，包含 `libobs` 及其头文件依赖）。在项目目录执行：

```powershell
./build.ps1 -ZigPath C:/tools/zig/zig.exe -ObsSource C:/code/obs-studio
```

默认输出到 `build/`，包含两个 DLL 和默认 INI。退出游戏和 OBS 后，可以执行：

```powershell
./install.ps1 -GameDirectory C:/IIDX
```

如使用其他构建目录，安装时传入 `-BuildDirectory`。安装后仍需在 Spice 注入列表中独立加入 `IIDXLive.dll`。

## 源码结构

| 文件 | 用途 |
| --- | --- |
| `game.cpp` | 游戏端初始化、原生音频挂接和写入共享内存 |
| `audio-layout.h` | 通过类型信息与函数特征自动定位音频接入点 |
| `main-screen.h` | OBS D3D9 主屏取图修正 |
| `protocol.h` | 共享音频协议与固定大小环形缓冲区 |
| `obs-plugin.cpp` | OBS 来源、游戏捕获和音频接收 |
| `test-*.cpp` | 自动定位、缓冲区、OBS 音频及主屏取图验证程序 |

## 验证范围

已验证两份本机游戏 PE（时间戳 `69c347cc`、`69ddf5f8`）的音频自动定位和特征异常拒绝，以及共享缓冲区并发一致性。独立加载 Live、不加载 Recorder 的测试已通过：模拟 PCM 经原生音频函数挂接进入真实 OBS 音频接口。离线透明画面和固定尺寸也已验证。

主屏修正已通过真实 D3D9 设备测试：绑定副屏渲染目标时，模拟 OBS 调用取得主屏后台缓冲区，游戏自身查询不变。上述测试不等同于完整 TDJ 实机游玩验证；特殊全屏、长时间同步和性能仍需实际游玩确认。自动定位不保证兼容所有未来版本，校验失败时不安装音频挂接。

`vendor/minhook` 使用 MinHook v1.3.4，其许可证随源码保留。
