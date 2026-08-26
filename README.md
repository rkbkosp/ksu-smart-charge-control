# KSU Smart Charge Control

面向 **PJE110 / OPlus 充电后端**的轻量级 KernelSU 智能充电模块。模块通过原生 C 守护进程 `charged`，根据 Wi-Fi、时间和电池状态动态调整充电上限。

> [!WARNING]
> 本项目会写入 OPlus 专有的 `/proc/oplus-votable/CHG_DISABLE/*` 节点，只在 PJE110 / kalama / crDroid 环境验证过。请勿在节点或语义不一致的设备上直接使用。

## 特性

- 单线程、事件驱动，空闲时阻塞在 `poll(2)`；
- 不依赖 JVM、常驻 Shell 轮询、工作线程或 wakelock；
- 根据办公室、家庭和未知 Wi-Fi 分别应用充电策略；
- 支持办公室指定时间后恢复 100% 充电；
- 仅在策略状态变化时写入充电节点；
- `SIGHUP` 热加载配置；
- 正常退出和卸载时主动恢复充电，默认失效安全；
- Git tag 自动测试、交叉编译、打包并创建 GitHub Release。

## 兼容性

- KernelSU；
- Android arm64（`aarch64`）；
- Android API 28 或更高版本；
- 具有以下设备节点的 OPlus 内核：
  - `/proc/oplus-votable/CHG_DISABLE/force_active`
  - `/proc/oplus-votable/CHG_DISABLE/force_val`
- 系统提供 `/system/bin/cmd wifi status`。

充电后端的完整约定见 [`charge-policy/docs/charge-backend.md`](charge-policy/docs/charge-backend.md)。

## 配置

编辑 [`charge-policy/config.conf`](charge-policy/config.conf)：

```ini
office_ssid=OfficeWiFi
home_ssid=HomeWiFi

office_limit=79
office_release_time=18:30
home_limit=100
default_limit=79
charging_reconcile_seconds=1800
```

SSID 为区分大小写的精确 UTF-8 字符串。仓库中的名称仅为占位示例，使用前必须替换。

| 配置项 | 说明 |
| --- | --- |
| `office_ssid` | 办公室 Wi-Fi SSID |
| `home_ssid` | 家庭 Wi-Fi SSID |
| `office_limit` | 办公室在释放时间前的充电上限 |
| `office_release_time` | 每天恢复 100% 充电的本地时间，格式为 `HH:MM` |
| `home_limit` | 家庭环境的充电上限 |
| `default_limit` | Wi-Fi 未知或断开时的充电上限 |
| `charging_reconcile_seconds` | 接通外部电源后的低频策略校准周期；设为 `0` 可关闭 |

安装后也可编辑 `/data/adb/modules/charge-policy/config.conf`，再向进程发送 `SIGHUP` 或重启设备：

```sh
adb shell "su -c 'kill -HUP \$(pidof charged)'"
```

## 本地构建

依赖：C11 编译器、GNU Make、`zip` 和 Android NDK。

```sh
# 运行主机端单元测试（ASan + UBSan）
make host-test

# 编译 Android arm64 二进制
ANDROID_NDK_HOME=/path/to/android-ndk ./scripts/build-android.sh

# 生成 KernelSU 模块 ZIP
./scripts/package.sh
```

输出文件为 `dist/charge-policy.zip`。默认目标 API 为 28，可通过 `ANDROID_API` 覆盖：

```sh
ANDROID_API=30 ANDROID_NDK_HOME=/path/to/android-ndk ./scripts/build-android.sh
```

开发机已连接并授权 ADB 时，可使用 `./scripts/install-adb.sh` 将模块暂存到 `/data/adb/modules/charge-policy`。

## 安装

1. 从 GitHub Releases 下载最新 ZIP，或按上节自行构建；
2. 在 KernelSU 管理器中选择“从本地安装”；
3. 选择模块 ZIP 并重启；
4. 按需修改模块目录中的 `config.conf`。

## 发布新版本

仓库名建议使用 **`ksu-smart-charge-control`**。首次推送示例：

```sh
git remote add origin git@github.com:<your-account>/ksu-smart-charge-control.git
git push -u origin main
```

推送符合 `vX.Y.Z` 格式的 tag 会触发 [GitHub Actions 发布工作流](.github/workflows/release.yml)：

```sh
git tag -a v1.0.0 -m "Release v1.0.0"
git push origin v1.0.0
```

工作流会自动运行主机测试、安装固定版本的 Android NDK、同步 `module.prop` 版本、编译模块、生成 SHA-256 校验文件并创建 GitHub Release。

## 项目结构

```text
native/          charged 守护进程源码
charge-policy/   KernelSU 模块模板与配置
scripts/         构建、打包、ADB 安装和资源测量脚本
tests/           主机端单元测试
.github/         GitHub Actions 工作流
```

## 风险提示

修改充电控制节点存在设备损坏、无法充电或数据丢失风险。请确认设备节点及写入语义完全一致，并自行承担使用风险。本项目不探测或写入其他备用充电节点。

## License

本项目基于 [MIT License](LICENSE) 开源。
