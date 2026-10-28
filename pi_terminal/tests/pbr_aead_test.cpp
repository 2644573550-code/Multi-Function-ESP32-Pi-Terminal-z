// T-07：PBR AEAD 信封 + 拒绝顺序 + 重放的验收测试。
//
// 最关键的一条：用仓库里的官方测试向量逐字节验证，而不是自己
// 加密再自己解密 —— 后者两边同时写错也会通过。
#include "protocol/pbr_aead.h"

#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest>

class PbrAeadTest : public QObject {
    Q_OBJECT

private:
    QJsonObject vectors_;
    static QByteArray hex(const QJsonValue &v) { return QByteArray::fromHex(v.toString().toLatin1()); }
    static QByteArray bytes(const QJsonValue &v) {
        // 向量里有些字段是 ASCII 明文，有些是 hex。
        return v.toString().toUtf8();
    }

private slots:
    void initTestCase()
    {
        QFile file(QStringLiteral(VECTORS_PATH));
        QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(QStringLiteral(VECTORS_PATH)));
        vectors_ = QJsonDocument::fromJson(file.readAll()).object();
        QVERIFY(!vectors_.isEmpty());
    }

    // §5.1 / §5.3：信封与 AAD 必须与官方向量逐字节一致。
    void envelopeMatchesOfficialVector()
    {
        const QJsonObject envelope = vectors_.value("envelope").toObject();
        const QJsonObject keys = vectors_.value("keys").toObject();

        const QByteArray key = hex(keys.value("k_bridge_to_pi"));
        const QByteArray nonce = hex(envelope.value("nonce"));
        const quint64 authSeq = quint64(envelope.value("auth_seq").toDouble());
        const QByteArray plaintext = bytes(envelope.value("plaintext"));
        const QByteArray expectedAad = hex(envelope.value("aad"));
        const QByteArray expectedEnvelope = hex(envelope.value("envelope_hex"));

        // key_id 是**发送方**身份公钥的 SHA-256[0]。本条向量是桥板→Pi，
        // 所以它是桥板的 key_id，与 handshake.pi_key_id（Pi 侧）无关。
        const quint8 keyId = quint8(expectedEnvelope.at(1));
        QCOMPARE(int(keyId), expectedAad.toHex().mid(2, 2).toInt(nullptr, 16));
        const quint8 kind = quint8(expectedAad.at(2));
        const quint8 dir = quint8(expectedAad.at(3));

        PbrAead::Result result = PbrAead::Result::Malformed;
        const QByteArray noncePrefix = nonce.left(4);
        const QByteArray sealed = PbrAead::seal(plaintext, key, noncePrefix, authSeq,
                                                keyId, kind, dir, &result);
        QCOMPARE(result, PbrAead::Result::Ok);
        QCOMPARE(sealed.toHex(), expectedEnvelope.toHex());

        // 反向：用同一向量解密必须还原明文。
        PbrAead::Aad aad{PbrAead::kEnvVersion, keyId, kind, dir, authSeq,
                         quint16(plaintext.size())};
        const QByteArray opened = PbrAead::open(expectedEnvelope, key, aad, &result);
        QCOMPARE(result, PbrAead::Result::Ok);
        QCOMPARE(opened, plaintext);
    }

    // §5.1：密文/载荷是二进制，含 0x00 也必须能原样往返。
    void binaryPayloadWithNulBytesRoundTrips()
    {
        const QByteArray key(32, '\x11');
        const QByteArray noncePrefix = QByteArray::fromHex("01020304");
        QByteArray plaintext("a\0b\0c", 5);
        plaintext.append(QByteArray::fromHex("00ff00"));

        PbrAead::Result result = PbrAead::Result::Malformed;
        const QByteArray sealed = PbrAead::seal(plaintext, key, noncePrefix, 1, 0x09, 1, 0, &result);
        QCOMPARE(result, PbrAead::Result::Ok);
        QVERIFY(sealed.contains('\0'));
        const QByteArray opened = PbrAead::open(sealed, key,
            PbrAead::Aad{PbrAead::kEnvVersion, 0x09, 1, 0, 1, quint16(plaintext.size())}, &result);
        QCOMPARE(result, PbrAead::Result::Ok);
        QCOMPARE(opened, plaintext);
    }

    // §6.2 第 4 步：tag 必须先验，改任一字节都必须 AuthFailed。
    void tamperingFailsAuthentication()
    {
        const QByteArray key(32, '\x22');
        const QByteArray noncePrefix(4, '\x01');
        const QByteArray plaintext = QByteArrayLiteral("{\"event\":\"attach\"}");
        const QByteArray sealed = PbrAead::seal(plaintext, key, noncePrefix, 5, 0x09, 1, 0);
        QVERIFY(!sealed.isEmpty());

        for (const int index : QList<int>{1, 2, 6, sealed.size() - 20, sealed.size() - 1}) {
            QByteArray tampered = sealed;
            tampered[index] = char(tampered[index] ^ 0x01);
            PbrAead::Result result = PbrAead::Result::Ok;
            PbrAead::Aad aad{PbrAead::kEnvVersion, 0x09, 1, 0, 5, quint16(plaintext.size())};
            const QByteArray opened = PbrAead::open(tampered, key, aad, &result);
            QVERIFY2(opened.isEmpty(), "tampered envelope must not decrypt");
            QCOMPARE(result, PbrAead::Result::AuthFailed);
        }
    }

    // §5.3：AAD 里的 kind/dir 变了必须验不过 —— 防止把 Pi→桥的帧回放给 Pi。
    void aadDirectionIsAuthenticated()
    {
        const QByteArray key(32, '\x33');
        const QByteArray noncePrefix(4, '\x02');
        const QByteArray sealed = PbrAead::seal(QByteArrayLiteral("x"), key, noncePrefix, 7,
                                                0x09, 1, 0);
        PbrAead::Result result = PbrAead::Result::Ok;
        // 把 dir 从 0 改成 1（回放攻击）。
        PbrAead::Aad aad{PbrAead::kEnvVersion, 0x09, 1, 1, 7, 1};
        QVERIFY(PbrAead::open(sealed, key, aad, &result).isEmpty());
        QCOMPARE(result, PbrAead::Result::AuthFailed);
    }

    // §5.1：明文上限 2018，超限直接拒绝，不截断。
    void plaintextLimitIsEnforced()
    {
        const QByteArray key(32, '\x44');
        const QByteArray noncePrefix(4, '\x03');
        PbrAead::Result result = PbrAead::Result::Ok;
        QVERIFY(PbrAead::seal(QByteArray(2018, 'x'), key, noncePrefix, 1, 9, 1, 0, &result).isEmpty() == false);
        QCOMPARE(result, PbrAead::Result::Ok);
        result = PbrAead::Result::Ok;
        QVERIFY(PbrAead::seal(QByteArray(2019, 'x'), key, noncePrefix, 1, 9, 1, 0, &result).isEmpty());
        QCOMPARE(result, PbrAead::Result::TooLarge);
    }

    // §5.2：auth_seq 为 0 或到达上限时必须拒绝（禁止环绕）。
    void authSeqZeroAndWrapAreRejected()
    {
        const QByteArray key(32, '\x55');
        const QByteArray noncePrefix(4, '\x04');
        PbrAead::Result result = PbrAead::Result::Ok;
        QVERIFY(PbrAead::seal(QByteArrayLiteral("x"), key, noncePrefix, 0, 9, 1, 0, &result).isEmpty());
        QCOMPARE(result, PbrAead::Result::Malformed);
        result = PbrAead::Result::Ok;
        QVERIFY(PbrAead::seal(QByteArrayLiteral("x"), key, noncePrefix, 0xffffffffffffffffULL,
                              9, 1, 0, &result).isEmpty());
        QCOMPARE(result, PbrAead::Result::Malformed);
    }

    // §4：X25519 全零共享密钥必须判为不可用（低阶点）。
    void zeroSharedSecretIsRejected()
    {
        QVERIFY(!PbrAead::sharedIsUsable(QByteArray(32, '\0')));
        QVERIFY(PbrAead::sharedIsUsable(QByteArray(32, '\x01')));
        QVERIFY(!PbrAead::sharedIsUsable(QByteArray(31, '\x01')));
    }

    // §6.2 第 3 步：未知 env_ver 整帧丢弃。
    void unknownEnvelopeVersionIsRejected()
    {
        const QByteArray key(32, '\x66');
        QByteArray sealed = PbrAead::seal(QByteArrayLiteral("x"), key, QByteArray(4, '\x05'), 3,
                                          9, 1, 0);
        QVERIFY(!sealed.isEmpty());
        sealed[0] = char(2);
        PbrAead::Result result = PbrAead::Result::Ok;
        PbrAead::Aad aad{PbrAead::kEnvVersion, 9, 1, 0, 3, 1};
        QVERIFY(PbrAead::open(sealed, key, aad, &result).isEmpty());
        QCOMPARE(result, PbrAead::Result::UnknownVersion);
    }
};

QTEST_GUILESS_MAIN(PbrAeadTest)
#include "pbr_aead_test.moc"
