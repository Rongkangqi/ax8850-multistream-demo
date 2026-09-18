# aarch64 预编译程序

本目录为项目源码在 Ubuntu 24.04 aarch64 / GCC 13.3.0 / AXCL 3.16.0 / OpenCV 4.6 环境下生成的 Release 产物。

```text
bin/six_app
lib/libax_video_sdk.so
lib/plugins/libax_plugin_pcd.so
lib/plugins/libax_plugin_yolo26.so
lib/plugins/libax_plugin_yolov8_split.so
```

程序通过 `$ORIGIN` 相对路径及启动脚本加载本目录的库，不依赖原开发板的源码路径。AXCL 和 OpenCV 仍由主机系统提供。

从 Windows 复制到 Linux 后，在项目根目录执行：

```bash
chmod +x prebuilt/linux-aarch64/bin/six_app
bash start.sh
```

如果当前系统的动态库版本不同，执行 `bash build.sh` 重新构建。根目录 `run.sh` 优先使用 `build/install/`，其次使用此目录；不要混用两套程序和插件。

完整使用方法见 [项目 README](../../README.md)。
