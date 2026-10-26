# 固件

完整可构建源码已从以下基线导入，`v0` 标签指向导入合并提交 `243c0c1`：

- 上游：<https://github.com/txp666/xiaozhi-esp32>
- 固定提交：`fb7f8152628f07a3f1bca91c222d3bc8853b1f70`
- 目标芯片：ESP32-S3
- 板型：`otto-robot`，无摄像头、6 舵机
- 工具链：ESP-IDF 6.1

`firmware/xiaozhi-esp32/` 与上游 `fb7f815` 逐文件一致；ESP-IDF 6.1 构建通过，证据见 [ARCHITECTURE.md](../docs/ARCHITECTURE.md)「实现状态」。

板型配置按实物核对：可进入烧录模式，屏幕与音频正常；舵机引脚映射见 [hardware/BOM.md](../hardware/BOM.md)。

`v0` 之后的灵矽适配、安全控制和文档更新都通过独立 PR 提交。上游 LICENSE、第三方许可和来源信息见 [RESEARCH.md](../docs/RESEARCH.md)「许可与来源」。
