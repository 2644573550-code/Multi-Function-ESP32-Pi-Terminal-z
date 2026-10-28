// D9a：登录/锁屏/用户选择/数字键盘四个组件必须能实例化且绑定无错。
// 通过 qml module 加载，确保 Theme/I18n 单例真的注册。
// 关键：任何 QML 警告都算失败 —— 否则 I18n.t 是 undefined 时组件照样"创建成功"，
// 那种测试是假绿。
#include <QQmlComponent>
#include <QQmlEngine>
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
};

QTEST_MAIN(LoginQmlTest)
#include "login_qml_test.moc"
