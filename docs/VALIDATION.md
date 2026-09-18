# 本次整理版本的验证记录

日期：2026-09-18。此次验证用于确认整理后的源码可以独立构建，随包程序可以从新目录运行；不属于产品出货稳定性认证。

## 验证环境

- 主机：DShanPi-A1 / RK3576，Ubuntu 24.04，aarch64。
- 内核：6.1.115-vendor-rk35xx。
- 算力卡：16GB AX8850，AXCL 3.16.0。
- 构建：GCC 13.3.0、OpenCV 4.6.0、CMake，Release，两个并行编译任务。
- 使用本项目内的 5 个模型、6 段 1080p 视频，第 3、4 路保留半速文件。

## 检查结果

| 检查 | 结果 |
|---|---|
| 模型和视频 SHA256 | 11 个资源文件全部一致 |
| 独立目录构建 | `bash build.sh` 成功，生成程序、视频 SDK 和三个检测插件 |
| 预编译目录运行 | 实际运行 `prebuilt/linux-aarch64/bin/six_app` |
| 库和资源路径 | 进程映射中未使用原 `ax-pipeline` 部署目录，插件及视频从本项目加载 |
| 六路独立 RTSP + 综合 RTSP | 七路均取得 H.264 / 1920×1080 信息，每路客户端解码 12 帧成功 |
| 本机 HTTP 预览 | `127.0.0.1:8850/overview.ts` 客户端解码 30 帧成功 |
| 综合录像 | 请求 5 秒，实际 4.700 秒，文件完整解码 141 帧 |
| 持续计数 | 最后一组统计到 120.23 秒，六路解码、推理、渲染、编码均前进 |
| 错误与队列 | 本次末尾统计：提交失败、封装失败、渲染替换、编码丢帧均为 0 |
| 正常停止 | 收到 `{"event":"exit","errors":0}` |
| 恢复原现场 | 原推流及预览服务均 active，VLC 已恢复 Playing |

码流直接复制录像需要从关键帧开始，本例编码 GOP 约 2 秒。最初采用“5 秒请求必须至少生成 4.9 秒文件”的检查过严；实际短片 4.733 秒且帧可解码。最终改为结合关键帧间隔检查时长，并完整解码录像验证，未因此修改推流程序。

## 本次测得的帧率

下表是单块卡本次运行的约 60 秒采样窗口；不是各模型的独占 NPU 性能，也不是长期最低帧率保证。

```text
Measured window: 60.1 seconds (video FPS and AI FPS are different)
Stream       Decode  New video       AI   Encode   Drops*   Errors
pcd           12.01      11.99    11.99    12.01        0        0
vehicle       25.08      25.08     9.98    25.04        0        0
seg           30.00      30.00     6.30    30.02        0        0
driving       30.00      29.98    10.21    29.98        0        0
depth         24.01      24.01    10.71    24.01        0        0
count         24.08      24.08     9.88    24.10        0        0
Overview encoder: 30.02 FPS
*Drops: render queue replacement + encoder queue drops, within this window.
```

RTSP / H.264 元数据中的第 4、5 路帧率可能显示为整数 29、23；上表按实际累积帧数及墙钟时间计算，配置中的源帧率分别为 29.970 和 23.976。

## 查看原始证据

- [本次测试结果](../provenance/validation/result.json)
- [构建日志](../provenance/validation/clean-build.txt)
- [应用 JSON 事件](../provenance/validation/application.jsonl)
- [应用完整日志](../provenance/validation/application.txt)
- [FPS 原始输出](../provenance/validation/fps.txt)
- [设备状态](../provenance/validation/axcl-smi.txt)
- [系统与编译环境](../provenance/validation/environment.json)
- [预编译文件 SHA256](../provenance/validation/runtime-manifest.json)
- [六段视频参数](../provenance/video-info.json)

构建日志包含上游代码的未使用参数等警告；本次无编译或链接失败。测试确认了启动、资源加载、数据流、输出解码和退出流程，未量化模型准确率、深度测距误差或计数准确率，也未替代长时间、多卡验证。
