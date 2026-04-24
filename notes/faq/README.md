# FAQ

常见问题与排错索引。按出现顺序追加；解决路径可外链博客 / 对应 openspec change / notes 子系统文档。

## 程序运行时总是产生奇怪的二进制日志

参见博客：[vulkan程序运行时总是产生奇怪的二进制日志的问题](https://crackhopper.github.io/2025/11/17/%E8%AE%B0%E5%BD%95%E8%B0%83%E8%AF%95vulkan%E7%A8%8B%E5%BA%8F%E6%89%93%E5%8D%B0%E5%A5%87%E6%80%AA%E6%97%A5%E5%BF%97%E7%9A%84%E9%97%AE%E9%A2%98/)

相关实现：`LX_core::expSetEnvVK()`（`src/core/utils/env.*`）在可执行 `main()` 启动期抑制 Vulkan implicit validation layer 产生的 `.log` 文件。所有可能触发 Vulkan loader 初始化的可执行程序必须在 `main()` 一开始调用，早于 window / renderer / Vulkan instance 初始化。
