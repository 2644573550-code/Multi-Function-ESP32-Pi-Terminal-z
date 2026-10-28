// T-07 握手密钥派生：用官方向量逐字节验证 §4 的 X25519 + HKDF + 切片。
#include "protocol/pbr_handshake.h"

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class PbrHandshakeTest : public QObject {
    Q_OBJECT

private:
    QJsonObject vectors_;
    static QByteArray hex(const QJsonValue &v) { return QByteArray::fromHex(v.toString().toLatin1()); }

private slots:
    void initTestCase()
    {
        QFile file(QStringLiteral(VECTORS_PATH));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(QStringLiteral(VECTORS_PATH)));
        vectors_ = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!vectors_.isEmpty());
    }

    // §4：X25519 双向必须得到同一个 shared，且与向量一致。
    void x25519MatchesOfficialVector()
    {
        const QJsonObject h = vectors_.value("handshake").toObject();
        const QByteArray piPriv = hex(h.value("pi_eph_priv"));
        const QByteArray piPub = hex(h.value("pi_eph_pub"));
        const QByteArray bridgePriv = hex(h.value("bridge_eph_priv"));
        const QByteArray bridgePub = hex(h.value("bridge_eph_pub"));
        const QByteArray expectedShared = hex(h.value("x25519_shared"));
        QCOMPARE(expectedShared.size(), 32);

        // 公钥推导必须与向量一致。
        QCOMPARE(PbrHandshake::publicKeyOf(piPriv).toHex(), piPub.toHex());
        QCOMPARE(PbrHandshake::publicKeyOf(bridgePriv).toHex(), bridgePub.toHex());

        // 两个方向必须得出同一个共享密钥，且等于向量。
        const QByteArray a = PbrHandshake::sharedSecret(piPriv, bridgePub);
        const QByteArray b = PbrHandshake::sharedSecret(bridgePriv, piPub);
        QCOMPARE(a.toHex(), expectedShared.toHex());
        QCOMPARE(b.toHex(), expectedShared.toHex());
    }

    // §4：HKDF 派生 96 字节并切成三个方向密钥 + session_id + nonce_prefix。
    void hkdfDerivationMatchesOfficialVector()
    {
        const QJsonObject h = vectors_.value("handshake").toObject();
        const QJsonObject k = vectors_.value("keys").toObject();
        const QByteArray shared = hex(h.value("x25519_shared"));

        // 向量直接给了 t3 与 transcript_hash。
        const QByteArray t3 = hex(h.value("t3"));
        QVERIFY2(!t3.isEmpty(), "vector must carry t3 for HKDF salt");
        QCOMPARE(QCryptographicHash::hash(t3, QCryptographicHash::Sha256).toHex(),
                 hex(h.value("transcript_hash")).toHex());

        const PbrHandshake::Keys keys = PbrHandshake::deriveKeys(shared, t3);
        QCOMPARE(keys.handshake.toHex(), hex(k.value("k_handshake")).toHex());
        QCOMPARE(keys.piToBridge.toHex(), hex(k.value("k_pi_to_bridge")).toHex());
        QCOMPARE(keys.bridgeToPi.toHex(), hex(k.value("k_bridge_to_pi")).toHex());
    }

    // §5.2：session_id 与 nonce_prefix 必须来自 transcript_hash 的不同区段，
    // 不能像修订 1 那样用同一个 4 字节值兼作两者。
    void sessionIdAndNoncePrefixAreDistinctFields()
    {
        const QJsonObject h = vectors_.value("handshake").toObject();
        const QByteArray shared = hex(h.value("x25519_shared"));
        const QByteArray t3 = hex(h.value("t3"));
        const PbrHandshake::Keys keys = PbrHandshake::deriveKeys(shared, t3);

        QCOMPARE(keys.sessionId.size(), 16);
        QCOMPARE(keys.noncePrefix.size(), 4);
        // session_id 前 4 字节 != nonce_prefix（若相等说明又混用了）。
        QVERIFY(keys.sessionId.left(4) != keys.noncePrefix);
        // 但 nonce_prefix 必须等于 transcript_hash 的第 16..20 字节，
        // 即 session_id 之后紧邻的 4 字节。
        QCOMPARE(keys.noncePrefix, QCryptographicHash::hash(t3, QCryptographicHash::Sha256).mid(16, 4));
        QCOMPARE(keys.sessionId.toHex(), hex(h.value("session_id")).toHex());
        QCOMPARE(keys.noncePrefix.toHex(), hex(h.value("nonce_prefix")).toHex());
    }

    // §4：低阶点公钥必须让 sharedSecret 返回空（全零共享密钥）。
    void lowOrderPointIsRejected()
    {
        const QJsonObject h = vectors_.value("handshake").toObject();
        const QByteArray piPriv = hex(h.value("pi_eph_priv"));
        // 全零公钥是低阶点，X25519 输出全零。
        QVERIFY(PbrHandshake::sharedSecret(piPriv, QByteArray(32, '\0')).isEmpty());
        // 长度不对也必须拒绝。
        QVERIFY(PbrHandshake::sharedSecret(piPriv, QByteArray(31, '\x01')).isEmpty());
        QVERIFY(PbrHandshake::sharedSecret(QByteArray(16, '\x01'), QByteArray(32, '\x01')).isEmpty());
    }

    // §6.7：比完整公钥用常量时间，长度不等直接 false。
    void constantTimeEqualsRejectsMismatch()
    {
        const QByteArray a(65, '\x04');
        QVERIFY(PbrHandshake::constantTimeEquals(a, a));
        QByteArray b = a;
        b[64] = char(b[64] ^ 0x01);
        QVERIFY(!PbrHandshake::constantTimeEquals(a, b));
        QVERIFY(!PbrHandshake::constantTimeEquals(a, a.left(64)));
    }
};

QTEST_GUILESS_MAIN(PbrHandshakeTest)
#include "pbr_handshake_test.moc"
