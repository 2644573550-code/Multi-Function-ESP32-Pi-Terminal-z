import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: root
    required property var editor
    required property var formatter
    property bool rich: false
    property bool searching: false
    property string feedback: ""
    property bool allowRichSave: false
    signal saveRequested()
    signal saveRichRequested()
    spacing: 4
    function apply(action, value) {
        formatter.format(editor.textDocument, editor.selectionStart, editor.selectionEnd, action, value || 0)
        editor.forceActiveFocus()
    }
    function findNext() {
        const pos = formatter.find(editor.textDocument, query.text, editor.selectionEnd)
        if (pos >= 0) { editor.select(pos, pos + query.text.length); feedback = "" }
        else feedback = I18n.t("未找到")
    }
    RowLayout {
        Layout.fillWidth: true; spacing: 4
        IconButton {iconName:"save";label:I18n.t("保存文件");onClicked:root.saveRequested()}
        IconButton {iconName:"undo";label:I18n.t("撤销");enabled:root.editor.canUndo;onClicked:root.editor.undo()}
        IconButton {iconName:"redo";label:I18n.t("重做");enabled:root.editor.canRedo;onClicked:root.editor.redo()}
        Button {text:I18n.t("剪切");implicitHeight:48;enabled:root.editor.selectedText.length>0;onClicked:root.editor.cut()}
        Button {text:I18n.t("复制");implicitHeight:48;enabled:root.editor.selectedText.length>0;onClicked:root.editor.copy()}
        Button {text:I18n.t("粘贴");implicitHeight:48;enabled:root.editor.canPaste;onClicked:root.editor.paste()}
        Item {Layout.fillWidth:true}
        Button {visible:root.allowRichSave;text:I18n.t("另存 HTML");implicitHeight:48;onClicked:root.saveRichRequested()}
        IconButton {iconName:"search";label:I18n.t("查找与替换");onClicked:root.searching=!root.searching}
    }
    Flickable {
        Layout.fillWidth:true;Layout.preferredHeight:52;clip:true
        contentWidth:formats.implicitWidth;contentHeight:height
        flickableDirection:Flickable.HorizontalFlick;boundsBehavior:Flickable.StopAtBounds
        RowLayout {id:formats;spacing:4
            ComboBox {model:["Sans","Mono"];implicitWidth:110;implicitHeight:48;enabled:root.rich;Accessible.name:I18n.t("字体");onActivated:root.apply("family",currentIndex)}
            ComboBox {model:[12,14,16,18,24,32];currentIndex:2;implicitWidth:80;implicitHeight:48;Accessible.name:I18n.t("字号");enabled:root.rich;onActivated:root.apply("size",Number(currentText))}
            Repeater {model:[{text:"B",name:"粗体",action:"bold"},{text:"I",name:"斜体",action:"italic"},{text:"U",name:"下划线",action:"underline"}]
                Button {required property var modelData; text:modelData.text;implicitWidth:48;implicitHeight:48;enabled:root.rich;font.bold:modelData.action==="bold";font.italic:modelData.action==="italic";font.underline:modelData.action==="underline";Accessible.name:I18n.t(modelData.name);onClicked:root.apply(modelData.action)}
            }
            ComboBox {model:[I18n.t("左对齐"),I18n.t("居中"),I18n.t("右对齐")];implicitWidth:120;implicitHeight:48;enabled:root.rich;Accessible.name:I18n.t("段落对齐");onActivated:root.apply("align",[1,4,2][currentIndex])}
            Button {text:"• ≡";implicitHeight:48;implicitWidth:48;enabled:root.rich;Accessible.name:I18n.t("项目符号");onClicked:root.apply("list")}
            ComboBox {model:[I18n.t("墨色"),I18n.t("红色")];implicitWidth:100;implicitHeight:48;enabled:root.rich;Accessible.name:I18n.t("文字颜色");onActivated:root.apply("color",currentIndex)}
        }
    }
    RowLayout {
        visible:root.searching;Layout.fillWidth:true
        TextField {id:query;Layout.fillWidth:true;placeholderText:I18n.t("查找");Accessible.name:placeholderText;onAccepted:root.findNext()}
        TextField {id:replacement;Layout.fillWidth:true;placeholderText:I18n.t("替换为");Accessible.name:placeholderText}
        Button {text:I18n.t("查找");implicitHeight:48;onClicked:root.findNext()}
        Button {text:I18n.t("替换");implicitHeight:48;enabled:query.text.length>0;onClicked:{if(root.editor.selectedText===query.text){const start=root.editor.selectionStart;root.editor.remove(start,root.editor.selectionEnd);root.editor.insert(start,replacement.text)}root.findNext()}}
        CarText {text:root.feedback;visible:text!==""}
    }
}
