# EdgeLink v0.6.0 可靠性故障演练记录

## 演练目标

验证可靠数据链路在 MQTT Broker 断开、网络连接恢复、进程重启和本地存储重开时的行为。

## 自动化验证结果

执行命令：

```bash
cmake -S . -B build
cmake --build build -j4
cd build
ctest --output-on-failure
```

2026-10-19 的结果为 28 个测试完成、0 个失败；其中 `SocketCanTransportTest`
因测试机未创建 `vcan0` 按约定返回跳过，不属于本次可靠数据链路范围。

| 故障场景 | 验证方式 | 结果 |
| --- | --- | --- |
| Broker 连接被中断 | `MqttServiceTest` 强制关闭第一次连接 | 自动检测断线，并按退避策略重新连接成功 |
| Broker 不可用 | `MqttOfflineBufferTest` 在无 Broker 时连续发布 | 数据写入 SQLite outbox，重开数据库后仍存在 |
| 网络恢复 | `MqttDataReplayTest` 建立新连接并逐条返回 PUBACK | 按数据库 ID 顺序补传，收到 PUBACK 后才删除 |
| 补传期间产生新数据 | 首条 PUBACK 延迟期间发布第 4 条消息 | 新消息排在旧消息后，最终顺序为 1、2、3、4 |
| 数据库重启恢复 | `StorageServiceTest` 关闭并重新打开 SQLite | TagValue 类型、Quality 和时间戳均可还原 |
| systemd 通知 | `SystemdNotifierTest` 模拟 `NOTIFY_SOCKET` | READY、WATCHDOG、STOPPING 通知均正确 |
| 主程序启动与停止 | 临时数据库下启动 `edgelinkd`，发送 SIGTERM | TCP、控制端和 MQTT 正常启动，随后优雅退出 |
| 进程被强制终止 | 对 systemd 主进程发送 SIGKILL | 2 秒后自动重启，数据库与全部通信服务恢复 |

## 当前可靠性语义

离线补传采用 QoS 1。EdgeLink 只有收到 Broker 的 PUBACK 后才删除 outbox 记录，
因此可以避免“尚未确认就删除”造成的数据丢失。

该设计提供的是 **at-least-once（至少一次）** 语义：如果进程在收到 PUBACK 后、
删除 SQLite 记录前崩溃，该消息会在重启后再次发送。上游若要求严格去重，应使用
业务消息 ID 或 `(deviceId, tagName, timestampMs)` 作为幂等键。

## 真实 systemd 进程故障演练

这一项必须在完成服务安装后执行，不能只用单元测试代替。

先按 README 安装并启动服务，然后记录主进程 PID：

```bash
systemctl show edgelink -p MainPID
sudo systemctl kill --kill-who=main --signal=KILL edgelink
sleep 3
systemctl show edgelink -p ActiveState -p SubState -p MainPID -p NRestarts
```

通过标准：

- `ActiveState=active`、`SubState=running`；
- 故障后的 `MainPID` 与原 PID 不同；
- `NRestarts` 至少增加 1；
- `/var/lib/edgelink/edgelink.db` 仍存在；
- `/var/log/edgelink.log` 中能够看到新进程的启动记录。

实际演练结果：

- 2026-10-19 22:37:28，PID `9728` 被 SIGKILL 终止；
- systemd 记录 `code=killed, status=9/KILL`；
- 约 2 秒后新进程以 PID `9991` 启动；
- `NRestarts` 从 0 增加到 1；
- 状态恢复为 `ActiveState=active`、`SubState=running`；
- SQLite 数据库、MQTT、TCP 和 Unix Domain Socket 均重新打开成功。

结论：真实 `kill -9` 自动恢复测试通过。

## 日志轮转检查

配置语法可通过以下命令预演，`-d` 只显示判断过程，不会真的轮转文件：

```bash
sudo logrotate -d /etc/logrotate.d/edgelink
```

配置按天检查、保留 7 份，并在日志达到 10 MiB 时提前轮转。由于当前 Logger
不会主动重新打开日志文件，规则使用 `copytruncate` 保持进程继续写入原文件描述符。
