#pragma once

#include <QByteArray>
#include <QString>

#include <memory>

// Pi 侧静态身份（`pbr-secure-v1` §2：「Pi 身份 (host_id)」）。
//
// 私钥存储定级：**仅测试级，不构成生产保障。** P0 以 0600 文件保存
// 原始 32 字节私钥标量；NVS 加密 / eFuse 直存列 P1。该措辞与规范 §2 一致。
//
// 只做签名与验签，**不参与 ECDH**（前向保密来自临时 X25519，§4）。
// 签名算法 ECDSA P-256 + SHA-256，编码固定 `r‖s` 64 字节，**不是 DER**
// （§3.4：DER 长度可变，两端边界易不一致）。
//
// mbedtls 细节刻意藏在 pimpl 里：本头文件不包含任何 mbedtls 头，调用方
// 不需要 mbedtls 的包含路径。
class HostIdentity
{
public:
    static constexpr int kPublicKeyBytes = 65;   // 未压缩点 0x04||X||Y
    static constexpr int kSignatureBytes = 64;   // r(32) ‖ s(32)
    static constexpr int kHashBytes = 32;        // SHA-256

    // path 为空时用 AppConfigLocation/host_identity.pem
    explicit HostIdentity(QString path = QString());
    ~HostIdentity();

    HostIdentity(const HostIdentity &) = delete;
    HostIdentity &operator=(const HostIdentity &) = delete;

    QString path() const;

    bool exists() const;
    bool hasKey() const;

    // 从磁盘载入私有标量。文件不存在返回 false 并给原因。
    bool load(QString *error = nullptr);

    // 生成新身份并原子落盘（0600）。已存在时**默认拒绝覆盖**：
    // 换密钥意味着所有桥板要重新配对，必须显式 overwrite。
    bool generate(QString *error = nullptr, bool overwrite = false);

    // 对**摘要**签名（调用方先算 SHA-256(T3)，见 §3.4）。输出固定 64 字节。
    bool sign(const QByteArray &hash, QByteArray *signature, QString *error = nullptr) const;

    QByteArray publicKey() const;       // 65 字节；无密钥时为空
    QByteArray fingerprint() const;     // SHA-256(publicKey) 前 8 字节
    quint8 keyId() const;               // 线上 1 字节选钥值

    // 清除内存中的私钥并零化。
    void reset();

    // 用完整公钥验 `r‖s` 签名。静态，不需要本对象的密钥。
    static bool verify(const QByteArray &publicKey, const QByteArray &hash,
                       const QByteArray &signature);
    // DER ↔ r‖s 转换（§3.4 要求固定 r‖s；mbedtls 的 write_signature 给的是 DER）。
    static bool derToRaw(const QByteArray &der, QByteArray *raw);
    static bool rawToDer(const QByteArray &raw, QByteArray *der);

private:
    struct Impl;
    QString path_;
    std::unique_ptr<Impl> d_;
};
