#pragma once
#include <QByteArray>
#include <QJsonObject>
#include <QString>

// worker IPC：schema="p0-worker-v1"（f-day1 §5 冻结）。
//
// 线上格式：u32le 长度 + UTF-8 JSON。单消息 ≤65536、深度 ≤8。
// 字段：请求 schema/request_id/op/body；响应 schema/request_id/code/body。
// 操作白名单只有 publishSample/readOwnRecords/reportState/stop。
//
// 传输：**Unix domain socket**，不是 stdin/stdout。
// 原因：worker 必须由 systemd 以 DynamicUser 启动才有隔离，而
// `systemd-run --pipe` 在非特权用户下不可用（exit 217），宿主又不得提权。
// 因此 worker 在单元专属 RuntimeDirectory 里监听，宿主作为普通用户连接；
// 目录 0755 可穿越、socket 0666 可连，其余权限仍由 ProtectHome/ProtectSystem 收紧。
namespace WorkerIpc {

constexpr auto Schema = "p0-worker-v1";
constexpr int kMaxMessageBytes = 65536;
constexpr int kMaxDepth = 8;
constexpr int kMaxInFlight = 16;
constexpr int kTimeoutMs = 5000;

// socket 文件名（放在单元的 RuntimeDirectory 下）。
constexpr auto SocketName = "ipc.sock";

// 由 session 名推导 unit 名与 socket 路径。两处必须一致，
// 否则 worker 监听的位置和宿主连接的位置会分叉。
QString safeToken(const QString &session);
QString unitName(const QString &session);
QString socketPath(const QString &session);

// 编码：u32le 长度前缀 + JSON。
QByteArray encode(const QJsonObject &message);

// 解码：长度前缀与声明一致、schema 匹配、深度不超限才接受。
// 失败返回空对象，调用方必须当拒绝处理，不得猜测解析。
QJsonObject decode(const QByteArray &frame);

// 白名单校验。未知 op / 版本不符一律拒绝。
bool opAllowed(const QString &op);

QJsonObject request(quint64 requestId, const QString &op, const QJsonObject &body);
QJsonObject response(quint64 requestId, const QString &code, const QJsonObject &body);

}  // namespace WorkerIpc
