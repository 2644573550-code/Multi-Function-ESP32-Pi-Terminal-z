#include <QtTest>
#include <QQmlEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QTextDocument>
#include "ui/TextFormatter.h"

class TextFormatterTest : public QObject {
    Q_OBJECT
private slots:
    void formattingRoundTrip() {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick; TextEdit { textFormat: TextEdit.RichText }", QUrl());
        QScopedPointer<QObject> editor(component.create());
        QVERIFY2(editor, qPrintable(component.errorString()));
        TextFormatter formatter;
        editor->setProperty("text", formatter.richText("Hello <Pi>\nSecond line", false));
        auto *document = editor->property("textDocument").value<QQuickTextDocument *>();
        QVERIFY(document);
        QCOMPARE(formatter.serialize(document, false), QString("Hello <Pi>\nSecond line"));
        formatter.format(document, 0, 5, "bold");
        QTextCursor cursor(document->textDocument());
        cursor.setPosition(0); cursor.setPosition(5, QTextCursor::KeepAnchor);
        QCOMPARE(cursor.charFormat().fontWeight(), int(QFont::Bold));
        formatter.format(document, 0, 5, "italic");
        formatter.format(document, 0, 5, "underline");
        formatter.format(document, 0, 5, "size", 24);
        QTextDocument restored;
        restored.setHtml(formatter.serialize(document, true));
        QCOMPARE(restored.toPlainText(), QString("Hello <Pi>\nSecond line"));
        QTextCursor restoredCursor(&restored);
        restoredCursor.setPosition(0); restoredCursor.setPosition(5, QTextCursor::KeepAnchor);
        const auto format = restoredCursor.charFormat();
        QVERIFY(format.fontItalic()); QVERIFY(format.fontUnderline());
        QCOMPARE(format.fontPointSize(), 24.0);
        QCOMPARE(formatter.find(document, "Second", 0), 11);
        QCOMPARE(formatter.find(document, "Hello", 10), 0); // Wrap around.
        QCOMPARE(formatter.find(document, "missing", 0), -1);
        formatter.format(document, 0, 5, "align", Qt::AlignHCenter);
        QCOMPARE(document->textDocument()->begin().blockFormat().alignment(), Qt::AlignHCenter);
        formatter.format(document, 0, 5, "list");
        QVERIFY(document->textDocument()->begin().textList());
        formatter.format(document, 0, 5, "list");
        QVERIFY(!document->textDocument()->begin().textList());
    }
};
QTEST_MAIN(TextFormatterTest)
#include "text_formatter_test.moc"
