# 官网 OTA 维护 / Official OTA maintenance

官网入口为 `https://kiikoread.com`；GitHub Release 保留正式包与历史版本。用户临时授权rc98的OTA应用使用既有GitHub Pages地址，绕开Pages独立证书对旧固件的不兼容；来源只接受固件原有的两个HTTPS前缀。浏览器能下载不代表旧固件能验证服务器证书。
The official site is `https://kiikoread.com`, with original packages archived in Releases. The user temporarily authorized the existing GitHub Pages rc98 OTA image to avoid the incompatible Pages certificate; retain the firmware's two exact HTTPS origins. Browser success does not establish legacy trust compatibility.

发布前及证书续期后运行（Python 需要 `cryptography`，curl 需要支持 `%{certs}`）：
Run before publication and after certificate renewal (Python `cryptography` and curl `%{certs}` support required):

```sh
python3 tools/ota_tls_host_test.py
python3 tools/check_official_ota.py --output build/ota-health.json
```

可用 `--trust-image /path/to/original-rc97.bin` 复用本地原正式版应用，脚本会检查固定 SHA；默认从官网获取原 rc97 应用。报告保存在被忽略的构建目录，不记录账户令牌。
Use `--trust-image /path/to/original-rc97.bin` to reuse a local original image; its fixed SHA is verified. Otherwise the original rc97 application is fetched from the official site. Reports stay in the ignored build directory and contain no account tokens.

检查 TLS 1.2/HTTP 1.1、证书有效期与签名、原 rc96/97/98 信任包的末端签发者兼容性、清单、实际应用版本/大小/SHA，以及 206 Range 续传。旧客户端未启用交叉签名链重建，不能因为链中间出现受信任的同名根就判为通过。此检查是宿主保守预检，发布验收仍需已发布固件真机完成“检查更新→下载→校验”。
Checks cover TLS 1.2/HTTP 1.1, validity/signatures, terminal-issuer compatibility with the original rc96/97/98 bundle, metadata, embedded app version/size/SHA and 206 Range resumes. Legacy clients lack cross-signed path rebuilding: a trusted root name elsewhere in the chain is insufficient. This conservative host precheck does not replace a released device completing discovery, download and verification.

证书切换先确认权限和免费/付费边界，保留当前有效 HTTPS 直至兼容证书生效。不要关闭证书验证、降低为 HTTP、禁用当前 Universal SSL、覆盖已发布二进制或未经授权改用其他来源。`check_official_ota.py`只检查官网来源；临时使用GitHub Pages不代表官网证书问题已修复，恢复官网来源前仍须通过该检查和设备实际下载。Wrangler 的 `ssl_certs:write` 只管理账户 mTLS，官网边缘证书需要本域名 `SSL and Certificates` 权限；临时令牌只在本机使用，完成后清理。
Confirm permissions and cost before certificate changes and retain working HTTPS. Never bypass verification, use HTTP, disable Universal SSL, replace published binaries or switch sources without authorization. The official-origin checker remains a prerequisite for restoring that origin; a working Pages alternative does not fix the official certificate. Wrangler's scope manages account mTLS; edge certificates require zone-scoped SSL permissions. Keep temporary tokens local and remove them after use.

参考 / References: [Espressif certificate bundle](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/api-reference/protocols/esp_crt_bundle.html), [Cloudflare Universal SSL API](https://developers.cloudflare.com/api/resources/ssl/subresources/universal/subresources/settings/methods/edit/).
