# Privacy / 隐私

## 中文

本仓库是 `kiikoread` 的独立下游二改，不代表 MindReset 或上游 `wegooo-cell/read-pico-reader`。固件不收集遥测、使用统计或账号数据，也不把书籍、阅读进度、书签、字体、图片或设置上传到本项目控制的服务器。

联网只用于用户可见的功能：检查/下载更新、联网对时、可选 DNS、用户主动发起的文件传输与热点，以及用户手动开启的局域网远程监看。远程监看默认关闭，只应在可信局域网内短时开启；不使用时请关闭。

设备上的 WiFi 凭据、阅读记录和配置备份属于敏感数据。配置文件备份包含 WiFi 密码；串口日志在开发构建中可能包含网络名、局域网地址或访问 URL。向公开 issue 提交日志、截图或备份前，请删除凭据、完整设备标识、书籍内容和其他个人信息。

第三方站点、上游固件和用户自行配置的服务有自己的隐私规则，不在本项目的控制范围内。

## English

This repository is an independent downstream mod of `kiikoread` and is not endorsed by MindReset or the upstream `wegooo-cell/read-pico-reader` project. The firmware contains no telemetry, usage analytics or account service, and it does not upload books, reading progress, bookmarks, fonts, pictures or settings to a server controlled by this project.

Network access is limited to visible user features: update checks/downloads, network time, optional DNS, user-initiated file transfer/hotspot mode, and the LAN remote-monitor feature after the user explicitly enables it. Remote monitor is off by default and should be used only briefly on a trusted local network.

WiFi credentials, reading history and configuration backups are sensitive. Configuration backups contain the WiFi password, and development serial logs can include network names, LAN addresses or access URLs. Redact credentials, full device identifiers, book content and other personal data before posting logs, screenshots or backups in a public issue.

Third-party sites, upstream firmware and services configured by the user have their own privacy practices and are outside this project's control.
Remote-monitor session tokens are generated in RAM when you enable the feature. They are not restored after reboot, are cleared when the service is disabled, and expire after ten minutes without requests. The transport is plain HTTP; use only on a trusted LAN.
