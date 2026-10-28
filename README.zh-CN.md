# kiikoread 二改 · 小纸 Pico（RDP-G01-W）

> **这是下游二改，不是 kiikoread 官方版。** 本仓库由 **ismoss** 维护，基于上游 [kiikoread](https://github.com/wegooo-cell/read-pico-reader) **0.3.3-rc100**；kiikoread 又基于 [MindReset 官方示例固件](https://github.com/MindReset/read_pico_firmware)。本二改与 MindReset、kiikoread 上游无隶属或背书关系；上游的官网、更新日志、商店、支持渠道和 issue 区不覆盖本二改。

> **许可提醒。** 上游 rc93 及以前为 Apache-2.0；上游 rc94 起明确标注的新增原创内容适用[个人学习许可](licenses/kiikoread-Personal-Study-1.0.txt)（禁止商用）。本二改新增文件按 Apache-2.0 发布，但组合固件仍含上游非商用材料——仅供个人学习与自用，不要商用或售卖。完整范围见 [LICENSE](LICENSE)。

**当前二改版本**：`0.3.3-rc100-mod52` · **更新源**：[update.json](https://ismoss.github.io/read-pico-reader/update.json) · **完整二改说明**：[中文](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.zh-CN.md) · [English](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.md)

**默认分支 `main` 是上游源码镜像**；二改代码在 **`mod/glass-dock-lockoff`** 分支。下面介绍的是那个分支，不是 `main` 里的代码。

## 这个仓库是什么

| 定位 | 说明 |
| :--- | :--- |
| 不是重写 | 同一套 kiikoread 界面、书籍格式、分区表、配置备份和 NVS 键值 |
| 不是官方 | 没有背书；更新源自建；问题请找本仓库，不要找上游 |
| 不是一次性魔改 | CI 宿主回归 + 真机时序编译、按版本记录的更新日志与路线图 |

保留 kiikoread 的底子，把“用起来会皱眉”的地方改顺：阅读能横过来、网络像手机一样讲道理、每个等待都有反馈、锁屏和刷屏按真因收干净。

## 二改做了什么

| 主题 | 亮点 |
| :--- | :--- |
| 阅读 | 书内横屏（正文、目录、15 个二级面板）、首翻预渲染、字体选择不再被预览打回内建 |
| 锁屏 | 票根/壁纸/拼贴/透明书封适配横屏；竖/横两张壁纸；GC16 定稿；密码宽限期 |
| 书架 | 分片扫描保持界面可操作；索引持久化，冷启动/深睡唤醒秒开 |
| 网络 | 自动换网真修复（旧路径是死代码）、自动连接开关、真实链路显示、DNS 跟随路由器 + 在线列表 |
| 更新 | 自建 OTA feed、下载速率、跟随重定向、90 秒慢网容忍、取消不阻塞、进入页不自动检查 |
| 传书 | 离开即停会话但接回 WiFi；“已传输 N 个文件”；网页可设竖/横壁纸，预览只解码一次 |
| 远程监看 | 局域网看屏 + 触摸注入；每次开机默认关；一次性 RAM 密钥；可选四位密码；触摸 1.5 秒过期；十分钟无人访问自动关 |
| 工程 | CI 宿主回归、真机时序编译、发布后线上校验、每个版本更新日志 + 路线图 |

### 代表性根因修复

| 现象 | 真因 | 修法 |
| :--- | :--- | :--- |
| 到公司只连家里热点 | 选网前活动凭据被清零，选网函数直接返回（死代码） | 选网放到重连路径，活动网络不在场才换网 |
| 监看网页一直 waiting + 破图 | BMP 头 16 位字段只写了低字节，高字节是堆脏数据 | 54 字节头整块清零再逐字段写；整头金标准测试 |
| 字体改完闪回思源黑体 | 预览面板还原正文字体失败后静默退回内建 | 绘制不改正文状态；点选必落库 |
| 回到锁屏留密码圈圈 | `present()` 固定 GL16，忽略整屏定稿请求 | 回锁屏面 GC16 定稿；深睡前清白定稿 |
| 壁纸预览白屏很久 | 每画一帧都重新解码大图 | 只解码一次留 PSRAM，缓存键带 mtime |

## 分支

| 分支 | 用途 |
| :--- | :--- |
| `main` | 上游源码镜像 + 本门面页；**不含**二改代码 |
| `mod/glass-dock-lockoff` | 二改源码、CI、OTA feed 发布 |
| `pr/*` | 对上游同样有价值的修复，单独整理成可提交上游的 PR |

## 安装与回退

二改只发布**应用镜像**，没有网页全量刷机包。

1. **从 kiikoread rc72+ 升级**：打开 [update.json](https://ismoss.github.io/read-pico-reader/update.json) 下载 Bin，改名为 `Pico-update.bin` 放 TF 卡根目录；设备上进入 **设置 → 升级和恢复 → 系统升级 → TF 卡升级**。
2. **从官方演示或其他系统**：先用[官方网页刷机](https://kiikoread.com/#flash-action)装一次 kiikoread rc72+，再做第 1 步。
3. **回退**：用官方网页完整刷机。二改设备只认自建 feed，不会提示官方更新，所以回退必须走 USB。

TF 卡应用升级保留内部设置与阅读记录，TF 卡内容不动；升级前建议做一份配置备份（备份含明文 WiFi 密码，请保管好卡）。官方完整刷机是否清内部数据，取决于是否勾选“擦除设备”。

## 验证状态

| 项目 | 方式 | 状态 |
| :--- | :--- | :--- |
| 编译 | GitHub Actions，ESP-IDF v6.1，真机时序 | 通过；mod30 起零告警 |
| 宿主回归 | 显示/阅读/锁屏/书架/EPUB/设置/OTA TLS/DNS/监看 BMP | 每次 push 通过 |
| 发布 feed | 发布后校验版本、大小、SHA-256、线上可取 | 每次 dispatch 通过 |
| 真机走查 | mod1–mod52 人工验收 | **进行中** |

CI 绿不等于每项都过了实机验收；哪些修复有真机证据，以更新日志的标注为准。

## 继续阅读

| 用途 | 链接 |
| :--- | :--- |
| 完整二改说明（中文） | [mod 分支 README.zh-CN.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.zh-CN.md) |
| 完整二改说明（English） | [mod 分支 README.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/README.md) |
| 更新日志 | [docs/CHANGELOG.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/docs/CHANGELOG.md) |
| 路线图 | [docs/ROADMAP.md](https://github.com/ismoss/read-pico-reader/blob/mod/glass-dock-lockoff/docs/ROADMAP.md) |
| 更新源 | <https://ismoss.github.io/read-pico-reader/update.json> |
| DNS 在线列表 | <https://ismoss.github.io/read-pico-reader/dns.json> |
| 上游 kiikoread | <https://github.com/wegooo-cell/read-pico-reader> |
| 官方设备文档 | <https://dot.mindreset.tech/docs/read_0> |
| 官方网页刷机 | <https://kiikoread.com/#flash-action> |

## 许可与致谢

- [MindReset](https://dot.mindreset.tech/docs/read_0)：硬件、官方示例固件与文档。
- [Kiiko厂长 / wegooo-cell](https://github.com/wegooo-cell/read-pico-reader) 与 kiikoread 贡献者：本二改的全部基础。
- 上游第三方组件：Lucide（ISC）、epdiy（LGPL-3.0-or-later）、pypinyin（MIT）、Noto（SIL OFL 1.1）。见 [第三方许可说明](THIRD_PARTY_NOTICES.md)。

不要把二改包当官方包转发，不要去掉版权/许可声明再分发，更不要售卖。上游 issue 区留给上游；二改问题请提在本仓库。
