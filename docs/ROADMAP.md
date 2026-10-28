# 路线图 / Roadmap

本文件是**唯一**集中记录"做了什么、正在做什么、想做什么、已经否决什么"的地方。
`docs/CHANGELOG.md` 只写每个版本对用户可见的变化；进行中的易变状态（CI run、串口、验收清单）在
`docs/HANDOFF.local.md`（不入库）。

This is the single place listing what shipped, what is in flight, what is planned and what was rejected.
`docs/CHANGELOG.md` covers user-visible changes per release; volatile state lives in the untracked
`docs/HANDOFF.local.md`.

## 已发布 / Shipped (mod 线, 2026-10-26 → 10-28)

| 批次 | 版本 | 内容 |
| :--- | :--- | :--- |
| 网络与清理 | mod27–mod30 | 远程监看入口搬家 + 一次性 RAM 密钥、DNS 默认跟随路由器、自动连接新设备默认关、编译告警清零 |
| 字体 | mod31–mod32 | 修"阅读字体改完又弹回内建"；字体测试台不再写阅读字体 |
| 横屏锁屏第一批 | mod33 | 书架阅读票根横版 + 横版密码键盘 + 画布方向跟随 |
| 横屏锁屏第二批 | mod34 | 书架拼贴横版、深睡重启锁面方向、解锁不留键盘残影 |
| 壁纸双槽 | mod35/mod45 | 竖屏/横屏两个独立壁纸槽（网页端可分别裁剪上传） |
| 传书与网络语义 | mod36/mod37/mod48 | 传完即回空闲、离开即停会话并把网络接回来、慢网重连 |
| 壁纸预览 | mod38/mod42/mod43 | 只解码一次、加载提示、横图旋转取整图 |
| 更新页 | mod39/mod41/mod46/mod47/mod49 | 进度旁日志、只有下载中才让位、显示速率、允许重定向、90 秒慢网容忍、进入页不再自动检查 |
| 远程监看 | mod44/mod46 | 四位数进入密码（持久开关）；触摸接管 1.5 秒过期 |
| 文案与状态 | mod50 | 壁纸锁屏也随横屏、传输页文案与网络状态刷新 |

## 进行中 / In flight

- 无（mod50 待发版后进入验收）

## 待做 / Planned

1. **透明书封横屏适配**：`book_lock_bookmark.c` 的 684×1216 常量 → 画布尺寸；`seal_header_t` 缓存键加画布；预览框按画布比例；同步补 `book_lock_bookmark_host_test.py` 的 `epd_rotated_*` 桩。
2. **两指边缘内划手势（P4）**：六动作（关闭 / 关屏 / 切横竖 / 回主页 / 开目录 / 加书签）、默认全关、只在阅读页生效、横竖切换持久化（新增"上次横屏方向"键）。
3. **真机验收全过一遍**：mod27–mod50 一次刷机走查（清单在 HANDOFF）。
4. **OTA 链路**：观察"下载通道 N KB/s"读数，决定是否需要镜像源或分片。

## 已否决 / Rejected

- **"附着式启动"（进传书页不碰网络）**：收益小、风险大（组件掌管 netif/WiFi/httpd/事件循环，OTA/DNS/时同步都挂其状态机）。用户 2026-10-27 决定不做。
- **横屏主页**：无意义，主页/书架保持竖屏。
- **锁屏/预览继续区分横竖**：两槽位本质是两套图，预览不再加特化。
- **短口令降级为 4 位以下**：远程监看保留 32 位一次性 token，四位密码是可选弱路径。

## 验收状态 / Acceptance

**mod27–mod50 全部为"真机验收 pending"**；每次发布只承诺"编译零告警 + CI 全绿 + 线上 feed 校验通过"。
All mod27–mod50 are pending on-device acceptance; a release only claims a clean build, green CI and a verified feed.
