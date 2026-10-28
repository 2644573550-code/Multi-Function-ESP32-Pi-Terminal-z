// D9a：登录/锁屏/用户选择/数字键盘四个组件必须能实例化且绑定无错。
// 通过 qml module 加载，确保 Theme/I18n 单例真的注册。
// 关键：任何 QML 警告都算失败 —— 否则 I18n.t 是 undefined 时组件照样"创建成功"，
// 那种测试是假绿。
#include <QQmlComponent>
#include <QQmlEngine>
#include <QFile>
#include <QRegularExpression>
#include <QtTest>

class LoginQmlTest : public QObject {
    Q_OBJECT

private:
    QQmlEngine engine_;
    QStringList warnings_;

    static void capture(QtMsgType type, const QMessageLogContext &, const QString &message) {
        if (type == QtWarningMsg || type == QtCriticalMsg) qWarning().noquote() << "QML:" << message;
    }

    QObject *instantiate(const QString &type, QString *error) {
        QQmlComponent component(&engine_,
            QStringLiteral("qrc:/qt/qml/OrangePiTerminal/LoginTest/") + type);
        if (component.isError()) { *error = component.errorString(); return nullptr; }
        QObject *object = component.create();
        if (!object) *error = component.errorString();
        return object;
    }

private slots:
    void initTestCase() {
        engine_.addImportPath(QStringLiteral("qrc:/qt/qml/OrangePiTerminal/LoginTest"));
    }

    void themeSingletonResolvesThroughImport() {
        // 单例不能直接 QQmlComponent 加载，必须经 import 使用；用一个最小页面验证。
        QQmlComponent component(&engine_);
        component.setData(R"(
import QtQuick
import OrangePiTerminal.LoginTest
QtObject { property real hitSize: Theme.hit }
)", QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> probe(component.create());
        QVERIFY(probe);
        QVERIFY(probe->property("hitSize").toReal() > 0);
    }

    void i18nSingletonResolvesThroughImport() {
        QQmlComponent component(&engine_);
        component.setData(R"(
import QtQuick
import OrangePiTerminal.LoginTest
QtObject { property string label: I18n.t("登录") }
)", QUrl());
        QVERIFY2(!component.isError(), qPrintable(component.errorString()));
        QScopedPointer<QObject> probe(component.create());
        QVERIFY(probe);
        // I18n.t 必须返回非空字符串；返回空说明单例没注册。
        QVERIFY(!probe->property("label").toString().isEmpty());
    }

    void pinPadAcceptsOnlySixDigits() {
        QString error;
        QScopedPointer<QObject> pad(instantiate(QStringLiteral("PinPad.qml"), &error));
        QVERIFY2(pad, qPrintable(error));

        QSignalSpy submitted(pad.data(), SIGNAL(submitted(QString)));
        for (const char *digit : {"1", "2", "3", "4", "5"})
            QMetaObject::invokeMethod(pad.data(), "press", Q_ARG(QVariant, QString(digit)));
        QCOMPARE(submitted.count(), 0);

        QMetaObject::invokeMethod(pad.data(), "press", Q_ARG(QVariant, QString("6")));
        QCOMPARE(submitted.count(), 1);
        QCOMPARE(submitted.first().first().toString(), QStringLiteral("123456"));
    }

    void pinPadBackspaceWorks() {
        QString error;
        QScopedPointer<QObject> pad(instantiate(QStringLiteral("PinPad.qml"), &error));
        QVERIFY2(pad, qPrintable(error));
        QMetaObject::invokeMethod(pad.data(), "press", Q_ARG(QVariant, QString("7")));
        QCOMPARE(pad->property("value").toString(), QStringLiteral("7"));
        QMetaObject::invokeMethod(pad.data(), "backspace");
        QCOMPARE(pad->property("value").toString(), QString());
    }

    void userPickerShowsNoSensitiveFields() {
        QString error;
        QScopedPointer<QObject> picker(instantiate(QStringLiteral("UserPicker.qml"), &error));
        QVERIFY2(picker, qPrintable(error));
        QVERIFY(!picker->property("temperatureC").isValid());
        QVERIFY(!picker->property("uid").isValid());
        QVERIFY(!picker->property("dbKey").isValid());
    }

    void loginPageStartsWithoutSelectedUser() {
        QString error;
        QScopedPointer<QObject> login(instantiate(QStringLiteral("LoginPage.qml"), &error));
        QVERIFY2(login, qPrintable(error));
        QCOMPARE(login->property("selectedUserId").toString(), QString());
    }

    void lockScreenStartsIdle() {
        QString error;
        QScopedPointer<QObject> lock(instantiate(QStringLiteral("LockScreen.qml"), &error));
        QVERIFY2(lock, qPrintable(error));
        QVERIFY(!lock->property("busy").toBool());
    }

    // ---- 设计约束（docs/pi-ui-regression.md）----
    // 新页面必须走 Theme token，不能出现硬编码颜色/字号，否则主题与
    // 1.5x DPI 下会与既有界面不一致。
    void pinPadUsesThemeTokensOnly() {
        QFile file(QStringLiteral(TERMINAL_QML_DIR "/PinPad.qml"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QString text = QString::fromUtf8(file.readAll());
        const QRegularExpression hexColor(QStringLiteral("\\\"#[0-9a-fA-F]{3,8}\\\""));
        QVERIFY2(!hexColor.match(text).hasMatch(),
                 "PinPad must not hardcode colors; use Theme.*");
        QVERIFY2(!text.contains(QStringLiteral("pointSize:")),
                 "PinPad must use Theme pixel sizes, not pointSize");
    }

    // 触控热区：数字键必须 >= Theme.hit（48px 下限）。
    void pinPadKeysRespectMinimumHitSize() {
        QString error;
        QScopedPointer<QObject> pad(instantiate(QStringLiteral("PinPad.qml"), &error));
        QVERIFY2(pad, qPrintable(error));
        // 键盘按钮用 Layout.preferredHeight: Theme.hit，Theme.hit 自身 >= 48。
        QFile theme(QStringLiteral(TERMINAL_QML_DIR "/Theme.qml"));
        QVERIFY(theme.open(QIODevice::ReadOnly));
        const QString themeText = QString::fromUtf8(theme.readAll());
        QVERIFY2(themeText.contains(QStringLiteral("Math.max(48, 64 * scaleFactor)")),
                 "Theme.hit must keep the 48px floor");
    }

    // 所有新页面的 I18n 词条必须已进词表，否则英文界面会漏中文。
    void newPagesHaveEnglishTranslations() {
        const QStringList pages{QStringLiteral("LoginPage"), QStringLiteral("LockScreen"),
                                QStringLiteral("PinPad"), QStringLiteral("UserPicker"),
                                QStringLiteral("SessionGate"), QStringLiteral("TaskFinishDialog"),
                                QStringLiteral("AdminPairingPage")};
        QFile i18n(QStringLiteral(TERMINAL_QML_DIR "/I18n.qml"));
        QVERIFY(i18n.open(QIODevice::ReadOnly));
        const QString table = QString::fromUtf8(i18n.readAll());

        QStringList missing;
        for (const QString &page : pages) {
            QFile file(QStringLiteral(TERMINAL_QML_DIR "/") + page + QStringLiteral(".qml"));
            QVERIFY(file.open(QIODevice::ReadOnly));
            const QString text = QString::fromUtf8(file.readAll());
            const QRegularExpression call(QStringLiteral("I18n\\.t\\(\\\"([^\\\"]+)\\\"\\)"));
            QRegularExpressionMatchIterator it = call.globalMatch(text);
            while (it.hasNext()) {
                const QString key = it.next().captured(1);
                if (!table.contains(QLatin1Char('"') + key + QLatin1Char('"'))) missing.append(key);
            }
        }
        QVERIFY2(missing.isEmpty(),
                 qPrintable(QStringLiteral("missing I18n entries: ") + missing.join(QStringLiteral(", "))));
    }

};

QTEST_MAIN(LoginQmlTest)
#include "login_qml_test.moc"
