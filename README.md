# EdgeLink

EdgeLink 是一个基于 C++17 的 Linux 工业边缘网关工程。当前 CMake 项目版本为 `0.6.0`；仓库保留网关运行代码、协议插件、配置与部署文件。

## 当前内容

- `edgelinkd`：运行 `epoll` 事件循环、TCP 服务（端口 9000）、Unix Domain 控制通道、MQTT 发布与离线重连、SQLite 持久化、系统资源监控和 systemd 通知。
- `libedgelink`：提供设备/Tag 模型、设备管理、调度与重连状态机、消息总线、共享内存 IPC 等可复用组件。
- `libmodbus_plugin.so`：Modbus TCP 多 Tag 采集及 RTU 传输实现。
- `libsocketcan_plugin.so`：SocketCAN 插件。

Modbus 和 SocketCAN 插件会被编译，但当前 `edgelinkd` 的入口代码**没有自动加载它们**；它们属于可接入的独立模块，不代表启动网关后就会自动采集现场设备。V4L2/RKNN 扩展也尚未实现，RK3568 真机未完成验证。

## 工程结构

```text
include/edgelink/         公共接口
src/                      网关核心与 edgelinkd 入口
plugins/modbus_plugin/    Modbus 插件
plugins/socketcan_plugin/ SocketCAN 插件
config/                   默认 YAML 配置
deploy/                   systemd 服务与日志轮转配置
CMakeLists.txt             构建入口
```

## 构建

Ubuntu 环境需要 CMake、C++17 编译器、pkg-config，以及 yaml-cpp、libmodbus、libmosquitto 和 SQLite3 开发包：

```bash
sudo apt-get install -y build-essential cmake pkg-config \
    libyaml-cpp-dev libmodbus-dev libmosquitto-dev libsqlite3-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
```

主要产物位于 `build/`：`edgelinkd`、`libmodbus_plugin.so`、`libsocketcan_plugin.so`。构建目录已被 Git 忽略。

## 本地运行

从仓库根目录执行：

```bash
./build/edgelinkd
```

程序默认读取 `config/edgelink.yaml`，使用 `/tmp/edgelink.sock` 作为本地控制套接字，并将 SQLite 数据保存到配置指定的路径。按 `Ctrl+C` 可正常退出。MQTT Broker 暂时离线时，网关仍可启动并保留待发送数据；需要实际 MQTT 联调时可自行准备 Broker。

配置中可调整 MQTT 连接参数、SQLite 路径及系统监控采样周期。也可用 `EDGELINK_CONFIG`、`EDGELINK_STORAGE_PATH`、`EDGELINK_CONTROL_SOCKET` 指定部署路径。

## systemd 部署

仓库提供 `deploy/edgelink.service` 和 `deploy/edgelink.logrotate`。在目标机上安装已编译的程序和配置后，可启用服务：

```bash
sudo install -m 0755 build/edgelinkd /usr/local/bin/edgelinkd
sudo install -d -m 0755 /etc/edgelink
sudo install -m 0644 config/edgelink.yaml /etc/edgelink/edgelink.yaml
sudo install -m 0644 deploy/edgelink.service /etc/systemd/system/edgelink.service
sudo install -m 0644 deploy/edgelink.logrotate /etc/logrotate.d/edgelink
sudo systemctl daemon-reload
sudo systemctl enable --now edgelink
systemctl status edgelink --no-pager
```

服务使用 systemd 的 `READY=1` 与 watchdog 通知；部署配置将数据库放在 `/var/lib/edgelink/`，控制套接字放在 `/run/edgelink/`。

## 仓库范围

此版本按精简源码仓库的目标移除了测试程序、Mock 插件、性能基准和教学实验。它们可从 Git 历史中的旧提交恢复。源码可构建不等于已完成设备现场或 RK3568 真机验收；发布前仍应在目标环境进行独立验证。
