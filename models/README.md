# 模型说明

本目录保存当前六路配置使用的 **5 个模型文件**。第二路和第六路共用 `vehicle.axmodel`，分别完成检测与跟踪计数。

| 文件 | 用途 | 上游仓库 / 原文件 | 模型仓库许可标记 |
|---|---|---|---|
| `pcd.axmodel` | 第一路 PCD | 原开发板已有模型；参考 [Person_car-axera](https://huggingface.co/AXERA-TECH/Person_car-axera) | 参考仓库为 AGPL-3.0；本文件的上游对应关系未独立核验 |
| `vehicle.axmodel` | 第二、六路 YOLOv8s | [YOLOv8](https://huggingface.co/AXERA-TECH/YOLOv8)，`AX650/yolov8s_640x640_npu3.axmodel` | MIT |
| `seg.axmodel` | 第三路 YOLO26n-Seg | [yolo26-seg](https://huggingface.co/AXERA-TECH/yolo26-seg)，`ax650/yolo26n-seg_npu3.axmodel` | AGPL-3.0 |
| `yolo26n.axmodel` | 第四路 YOLO26n | [yolo26](https://huggingface.co/AXERA-TECH/yolo26)，`ax650/yolo26n.axmodel` | AGPL-3.0 |
| `depth.axmodel` | 第五路 YOLO26n-Depth | [Yolo26-Depth](https://huggingface.co/AXERA-TECH/Yolo26-Depth)，`ax8850n/yolo26n-depth_w8a8_mix.axmodel` | BSD-3-Clause |

上表许可标记来自 2026-09-18 查询到的模型仓库元数据；具体适用范围以对应来源的许可文件为准。编译后模型、训练代码和原始权重可能有各自的许可约定。

`pcd.axmodel` 是之前已部署的原始模型，本次原样收集，未重新转换。PCD 后处理类别顺序为 `person`、`car`、`person_cycle`。

其余四个模型的原始下载记录保存在 [models-v2.json](../provenance/models-v2.json)。所有随包文件的 SHA256 和大小保存在 [assets-manifest.json](../provenance/assets-manifest.json)，在项目根目录运行 `python3 tools/verify_assets.py` 可统一核验。

模型均是 AXERA 编译后的 `.axmodel` 文件。本项目未包含训练数据、训练工程、原始权重或模型转换工具链。更换模型须同时核对输入布局、量化方式、张量输出及后处理，不能仅凭模型名称相近直接替换。
