# robot_voice：Agent 音乐使用 OpenVela Media

当前音乐修复和安装说明见 [`README_AGENT_NATIVE_MEDIA.md`](README_AGENT_NATIVE_MEDIA.md)。

包内 `robot_voice/` 是应用模块，`src/` 是本轮上传的板级源码。Agent 音乐只调用 OpenVela Media 的 open、prepare、start；本地 HTTP/MP3 回退已移除。

这是源码模块包，不含完整 OpenVela SDK、Agent 外部源码或固件。安装前清理旧对象并完整重编译，尤其要重编译 Agent 的 `tool_media.c`。主机桥接、原生路由、构建策略和 Media 生命周期回归已通过；未连接板卡验收。
