# 第三方组件与资源来源

本文件记录本次整理的源码和资源来源。根目录已有的 MIT `LICENSE` 保持不变；该文件不覆盖具有独立许可的第三方材料。

| 内容 | 来源与记录 |
|---|---|
| `vendor/ax-pipeline/` | [AXERA-TECH/ax-pipeline](https://github.com/AXERA-TECH/ax-pipeline)，BSD-3-Clause，见目录内 `LICENSE` |
| `vendor/ax-pipeline/deps/ax-video-sdk/` | 原项目实际使用的依赖快照，版本及 remote 见 `provenance/source-versions.json` |
| SDK 内的 `third-party/rtsp-sdk/` | 原部署的嵌套依赖快照，保留源码及原有许可声明 |
| ByteTrack、JSON、线性代数等第三方源码 | 随上游源码收集，保留其原目录中的许可文件和代码声明 |
| `models/*.axmodel` | 见 `models/README.md`，包括上游路径、许可元数据与文件校验记录 |
| `videos/*.mp4` | 用户提供的道路视频及其 1080p / 半速处理版本，未附原始素材的独立许可证明 |
| `prebuilt/linux-aarch64/` | 本项目及所收集依赖源码的配套编译产物 |

收集到的 ax-video-sdk 和 rtsp-sdk 快照未发现独立的根目录 `LICENSE` 文件；已有文件头和依赖内声明原样保留。公开分发时，应保留上游声明，并补齐适用于这些组件以及视频素材的授权依据。

AXCL 驱动和运行库、PAC / AXP、Linux 内核 headers 为外部系统依赖，未复制进本项目。模型的训练源码和原始权重也未包含在此仓库。
