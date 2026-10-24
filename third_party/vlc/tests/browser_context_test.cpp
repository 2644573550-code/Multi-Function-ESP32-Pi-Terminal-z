#include <QFile>
#include <QGuiApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJSEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QtTest>
#include <QtWebEngineQuick/qtwebenginequickglobal.h>

class BrowserContextTests : public QObject {
    Q_OBJECT
    QQmlEngine engine_;
    QScopedPointer<QObject> root_;
    QVariant evaluate(const QString &script) {
        const int previous = root_->property("done").toInt();
        if (!QMetaObject::invokeMethod(root_.data(), "evaluate", Q_ARG(QVariant, script))) {
            QTest::qFail("Unable to invoke WebEngine JS", __FILE__, __LINE__); return {};
        }
        QElapsedTimer timer; timer.start();
        while (root_->property("done").toInt() == previous && timer.elapsed() < 10000) QTest::qWait(10);
        if (root_->property("done").toInt() == previous) {
            QTest::qFail("WebEngine JS callback timed out", __FILE__, __LINE__); return {};
        }
        const QByteArray json = "[" + root_->property("result").toString().toUtf8() + "]";
        return QJsonDocument::fromJson(json).array().at(0).toVariant();
    }
private slots:
    void qmlGeneratedPageScriptsAreValidJavaScript() {
        QFile script(QStringLiteral(BROWSER_SCROLL_JS)); QVERIFY(script.open(QIODevice::ReadOnly));
        QJSEngine moduleEngine;
        const auto module = moduleEngine.evaluate(QString::fromUtf8(script.readAll()) +
            "\n[installationScript(), scrollScript(12,-7)]");
        QVERIFY2(!module.isError(), qPrintable(module.toString()));
        const auto scripts = module.toVariant().toList();
        QCOMPARE(scripts.size(), 2);
        for (const auto &scriptValue : scripts) {
            const auto source = scriptValue.toString();
            QVERIFY(!source.contains(QStringLiteral("[code]")));
            QJsonArray encoded;
            encoded.append(source);
            const auto args = QJsonDocument(encoded).toJson(QJsonDocument::Compact);
            const auto stringLiteral = QString::fromUtf8(args.mid(1, args.size() - 2));
            QJSEngine pageEngine;
            const auto syntax = pageEngine.evaluate(QStringLiteral("new Function(%1)").arg(stringLiteral));
            QVERIFY2(!syntax.isError(), qPrintable(syntax.toString()));
        }
    }

    void initTestCase() {
        QQmlComponent component(&engine_);
        component.setData(R"(
            import QtQuick
            import QtQuick.Window
            import QtWebEngine
            Window {
                width: 800; height: 600; visible: true
                property bool ready: false
                property int done: 0
                property string result: "null"
                function evaluate(script) {
                    web.runJavaScript(script, function(value) {
                        result = JSON.stringify(value === undefined ? null : value); done++
                    })
                }
                function load(html) { ready = false; web.loadHtml(html) }
                WebEngineView {
                    id: web; anchors.fill: parent
                    onLoadingChanged: function(request) { if (request.status === WebEngineView.LoadSucceededStatus) ready = true }
                }
            })", QUrl("file:///browser-context-test.qml"));
        root_.reset(component.create());
        QVERIFY2(root_, qPrintable(component.errorString()));
    }
    void contextsAndBounds() {
        const QString html = QStringLiteral(R"(
            <!doctype html><style>body{margin:0} #box{width:300px;height:150px;overflow:auto}
            #content{width:1200px;height:1000px;background:coral} #tail{height:2000px}</style>
            <div id="box" tabindex="0"><div id="content">Nested content</div></div>
            <textarea id="text" style="width:200px;height:50px">one
            two
            three
            four
            five
            six
            seven</textarea>
            <iframe id="frame" srcdoc="<div id='nested' style='height:1200px'>Frame content</div>"></iframe>
            <div id="tail">Page content</div>)");
        QVERIFY(QMetaObject::invokeMethod(root_.data(), "load", Q_ARG(QVariant, html)));
        QTRY_VERIFY_WITH_TIMEOUT(root_->property("ready").toBool(), 15000);
        QFile script(QStringLiteral(BROWSER_SCROLL_JS)); QVERIFY(script.open(QIODevice::ReadOnly));
        evaluate(QString::fromUtf8(script.readAll()) + "\ninstallContextTracking();");
        evaluate("document.getElementById('content').dispatchEvent(new PointerEvent('pointermove',{bubbles:true,composed:true}));");
        evaluate("scrollContext(80,120)");
        QCOMPARE(evaluate("document.getElementById('box').scrollTop").toInt(), 120);
        QCOMPARE(evaluate("document.getElementById('box').scrollLeft").toInt(), 80);
        QCOMPARE(evaluate("window.scrollY").toInt(), 0);
        evaluate("scrollContext(-50,-70)");
        QCOMPARE(evaluate("document.getElementById('box').scrollTop").toInt(), 50);
        QCOMPARE(evaluate("document.getElementById('box').scrollLeft").toInt(), 30);
        evaluate("document.getElementById('box').scrollTop=100000; scrollContext(0,100)");
        QVERIFY(evaluate("window.scrollY").toInt() > 0); // Chain at the inner boundary.
        evaluate("window.scrollTo(0,0); document.getElementById('box').style.overscrollBehavior='contain'; scrollContext(0,100)");
        QCOMPARE(evaluate("window.scrollY").toInt(), 0);
        evaluate("document.getElementById('text').focus(); scrollContext(0,40)");
        QVERIFY(evaluate("document.getElementById('text').scrollTop").toInt() > 0);
        // The frame is same-origin srcdoc; inspect it only under that policy.
        evaluate("document.getElementById('frame').dispatchEvent(new PointerEvent('pointermove',{bubbles:true})); scrollContext(0,100)");
        QVERIFY(evaluate("document.getElementById('frame').contentWindow.scrollY").toInt() > 0);
        evaluate("document.__terminalJoystickTarget=document.createElement('div'); document.getElementById('text').blur();");
        QVERIFY(evaluate("scrollContext(0,40) !== undefined").toBool()); // Detached context is safe.
        evaluate("installContextTracking(); installContextTracking();"); // Idempotent.
    }
};

int main(int argc, char **argv) {
    QCoreApplication::setAttribute(Qt::AA_ShareOpenGLContexts);
    QtWebEngineQuick::initialize();
    QGuiApplication app(argc, argv);
    BrowserContextTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "browser_context_test.moc"
