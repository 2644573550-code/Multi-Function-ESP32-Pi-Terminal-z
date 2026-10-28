// D1 / T-08：uidIndexKey 的生成、缺失失败关闭、轮换失效旧索引。
// 不碰用户真实密钥：setTestModeEnabled 把 AppConfigLocation 指到测试目录。
#include "security/uid_index_key.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMessageAuthenticationCode>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>



namespace {
QByteArray hmac(const QByteArray &key, const QByteArray &uid) {
    // f-day1 §3 的派生原文；此测试只用它验证"轮换后旧索引失配"，不是生产实现。
    QByteArray msg = QByteArrayLiteral("p0-rfid-index-v1");
    msg.append(static_cast<char>(uid.size()));
    msg.append(uid);
    return QMessageAuthenticationCode::hash(msg, key, QCryptographicHash::Sha256);
}

QString resolvedKeyPath() {
    return QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation)
        + QStringLiteral("/uid_index.key");
}
}  // namespace

class UidIndexKeyTest : public QObject {
    Q_OBJECT

private slots:
    void init() {
        QStandardPaths::setTestModeEnabled(true);
        const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        QDir(base).removeRecursively();
        QDir().mkpath(base);
    }

    void missingKeyReturnsFalse() {
        QByteArray key;
        // 失败关闭：缺失时必须 false，且不写出任何文件。
        QVERIFY(!uidIndexKey(&key));
        QVERIFY(!QFile::exists(resolvedKeyPath()));
        QVERIFY(key.isEmpty());
    }

    void generatedKeyIs32BytesAnd0600() {
        QVERIFY(ensureUidIndexKey());
        QByteArray key;
        QVERIFY(uidIndexKey(&key));
        QCOMPARE(key.size(), 32);

        const QFile::Permissions perms = QFileInfo(resolvedKeyPath()).permissions();
        QCOMPARE(perms & (QFile::ReadOwner | QFile::WriteOwner), QFile::ReadOwner | QFile::WriteOwner);
        // 组和其他用户一个权限位都不许有。
        QVERIFY(!(perms & (QFile::ReadGroup | QFile::WriteGroup | QFile::ExeGroup)));
        QVERIFY(!(perms & (QFile::ReadOther | QFile::WriteOther | QFile::ExeOther)));
    }

    void ensureIsIdempotent() {
        QVERIFY(ensureUidIndexKey());
        QByteArray first, second;
        QVERIFY(uidIndexKey(&first));
        QVERIFY(ensureUidIndexKey());   // 已存在，不得覆盖
        QVERIFY(uidIndexKey(&second));
        QCOMPARE(first, second);
    }

    void rotationInvalidatesOldIndex() {
        QVERIFY(ensureUidIndexKey());
        QByteArray oldKey;
        QVERIFY(uidIndexKey(&oldKey));

        const QByteArray rawUid = QByteArray::fromHex("a1b2c3d4");
        const QByteArray oldIndex = hmac(oldKey, rawUid);

        QVERIFY(rotateUidIndexKey());

        QByteArray newKey;
        QVERIFY(uidIndexKey(&newKey));
        QCOMPARE(newKey.size(), 32);
        QVERIFY(newKey != oldKey);
        // 旧索引必须失配 —— 轮换后须重新绑卡。
        QVERIFY(hmac(newKey, rawUid) != oldIndex);
    }

    void nullOutReturnsFalse() {
        QVERIFY(ensureUidIndexKey());
        // nullptr 不得崩，直接失败。
        QVERIFY(!uidIndexKey(nullptr));
    }
};

QTEST_GUILESS_MAIN(UidIndexKeyTest)
#include "uid_index_key_test.moc"

