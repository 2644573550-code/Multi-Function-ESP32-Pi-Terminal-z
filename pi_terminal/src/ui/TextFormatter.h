#pragma once
#include <QObject>
#include <QQuickTextDocument>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QTextBlockFormat>
#include <QTextDocument>
#include <QTextList>

class TextFormatter : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE QString serialize(QQuickTextDocument *document, bool rich) {
        if (!document || !document->textDocument()) return {};
        return rich ? document->textDocument()->toHtml() : document->textDocument()->toPlainText();
    }
    Q_INVOKABLE QString richText(const QString &text, bool rich) {
        if (rich) return text;
        QTextDocument document; document.setPlainText(text); return document.toHtml();
    }
    Q_INVOKABLE int find(QQuickTextDocument *document, const QString &text, int from) {
        if (!document || !document->textDocument() || text.isEmpty()) return -1;
        auto cursor = document->textDocument()->find(text, from);
        if (cursor.isNull()) cursor = document->textDocument()->find(text);
        return cursor.isNull() ? -1 : cursor.selectionStart();
    }
    Q_INVOKABLE void format(QQuickTextDocument *document, int start, int end, const QString &action, int value = 0) {
        if (!document || !document->textDocument()) return;
        QTextCursor cursor(document->textDocument());
        cursor.setPosition(qBound(0, start, document->textDocument()->characterCount()-1));
        cursor.setPosition(qBound(0, end, document->textDocument()->characterCount()-1), QTextCursor::KeepAnchor);
        if (!cursor.hasSelection()) cursor.select(QTextCursor::WordUnderCursor);
        QTextCharFormat format;
        const auto current = cursor.charFormat();
        if (action == "bold") format.setFontWeight(current.fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
        else if (action == "italic") format.setFontItalic(!current.fontItalic());
        else if (action == "underline") format.setFontUnderline(!current.fontUnderline());
        else if (action == "size") format.setFontPointSize(value);
        else if (action == "align") { QTextBlockFormat block; block.setAlignment(static_cast<Qt::Alignment>(value)); cursor.mergeBlockFormat(block); return; }
        else if (action == "list") {
            if (auto *list = cursor.currentList()) list->remove(cursor.block());
            else { QTextListFormat listFormat; listFormat.setStyle(QTextListFormat::ListDisc); cursor.createList(listFormat); }
            return;
        }
        else if (action == "family") format.setFontFamilies({value == 1 ? QStringLiteral("monospace") : QStringLiteral("sans-serif")});
        else if (action == "color") format.setForeground(value == 1 ? QColor("#b3261e") : QColor("#1f1f1f"));
        cursor.mergeCharFormat(format);
    }
};
