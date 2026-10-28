#include "core/ModulePageHost.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QFile>
#include <QQmlAbstractUrlInterceptor>
#include <QQmlEngine>
#include <QQuickItem>
#include <QDebug>

namespace {

// 只读用户上下文对象：QML 可读，不可写。
class UserContextObject : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString activeUserId READ activeUserId CONSTANT)
    Q_PROPERTY(bool locked READ locked CONSTANT)
    Q_PROPERTY(bool isAdmin READ isAdmin CONSTANT)
public:
    UserContextObject(ModulePageHost::UserContext context, QObject *parent = nullptr)
        : QObject(parent), context_(std::move(context)) {}
    QString activeUserId() const { return context_.activeUserId; }
    bool locked() const { return context_.locked; }
    bool isAdmin() const { return context_.isAdmin; }
private:
    ModulePageHost::UserContext context_;
};

}  // namespace

namespace {

// 扫描 import 语句，命中禁止清单则返回模块名，否则返回空。
QString blockedImportIn(const QString &sourceText)
{
    static const QStringList forbidden{
        QStringLiteral("Qt.labs.platform"), QStringLiteral("Qt.labs.settings"),
        QStringLiteral("Qt.labs.folderlistmodel"), QStringLiteral("QtQuick.LocalStorage"),
        QStringLiteral("QtWebView"), QStringLiteral("QtWebEngine"),
        QStringLiteral("QtQml.XmlListModel"), QStringLiteral("Qt.labs.qmlmodels")};

    // 逐行找 "import X" 与 "import \"..."（JS/QML 目录 import 也要看）。
    const QStringList lines = sourceText.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString trimmed = line.trimmed();
        if (!trimmed.startsWith(QLatin1String("import "))) continue;
        const QString rest = trimmed.mid(7).trimmed();
        for (const QString &bad : forbidden) {
            if (rest.startsWith(bad)) return bad;
        }
    }
    return {};
}

}  // namespace

QStringList ModulePageHost::allowedImports() {
    // 只允许纯 UI 模块。明确排除：Qt.labs.platform（原生菜单/文件对话框）、
    // Qt.labs.settings（读写宿主设置）、QtQuick.LocalStorage（本地存储）、
    // QtWebView / QtWebEngine（网络与脚本执行）、Qt.labs.folderlistmodel
    // （任意目录列目录）。
    return {
        QStringLiteral("QtQuick"),
        QStringLiteral("QtQuick.Layouts"),
        QStringLiteral("QtQuick.Controls"),
        QStringLiteral("QtQuick.Controls.Material"),
        QStringLiteral("QtQuick.Shapes"),
        QStringLiteral("QtQuick.Window"),      // 只允许内嵌，见下
        QStringLiteral("QtQml"),
        QStringLiteral("QtQml.Models"),
    };
}

ModulePageHost::ModulePageHost(QObject *parent) : QObject(parent) {
    // 独立引擎：与宿主 QQmlEngine 完全隔离，宿主 context 属性一个都看不到。
    engine_ = new QQmlEngine(this);
    // 注意：不能清空 importPathList —— QtQuick 等基础模块靠它解析，
    // 清空会让模块页连 Connections 都找不到。隔离靠的是：
    //   1) 独立 QQmlEngine + 独立 context（宿主对象不可见）
    //   2) load() 前扫描 import 语句，禁止 Qt.labs.* / WebEngine 等能
    //      访问文件、网络、设置的模块（见 blockedImportIn）
}

ModulePageHost::~ModulePageHost() {
    unload();
}

bool ModulePageHost::load(const QUrl &source, QObject *sessionModel, const QVariantMap &theme) {
    unload();
    if (source.isEmpty()) return false;

    // 全新 context：每次加载都不复用，避免上一页的注入项残留。
    auto *context = new QQmlContext(engine_, this);
    context->setContextProperty(QStringLiteral("sessionModel"), sessionModel);
    context->setContextProperty(QStringLiteral("userContext"),
                                new UserContextObject(userContext_, context));
    // 主题与 i18n 显式传入，不依赖宿主 context。
    context->setContextProperty(QStringLiteral("hostTheme"), theme);
    // §6.1：模块页访问用户数据的唯一入口。不是数据库句柄、没有 SQL，
    // 作用域写死在 ModuleDataApi 里（moduleId + instance + 当前用户）。
    if (moduleDataApi_)
        context->setContextProperty(QStringLiteral("data"), moduleDataApi_);

    lastError_.clear();

    // §8.4：模块页不得 import 能访问文件/网络/设置的模块。
    // 拦截点必须在加载**之前**——源码预扫描是最可靠的一层，
    // 因为 urlInterceptor 看不到本地文件里的 import 语句。
    if (source.isLocalFile()) {
        QFile pageFile(source.toLocalFile());
        if (pageFile.open(QIODevice::ReadOnly)) {
            const QString sourceText = QString::fromUtf8(pageFile.readAll());
            pageFile.close();
            const QString blocked = blockedImportIn(sourceText);
            if (!blocked.isEmpty()) {
                lastError_ = QStringLiteral("module page imports a forbidden module: ") + blocked;
                qWarning().noquote() << "module page rejected:" << lastError_;
                emit pageChanged();
                return false;
            }
        }
    }

    component_ = new QQmlComponent(engine_, source, QQmlComponent::PreferSynchronous, this);
    if (component_->isError()) {
        lastError_ = component_->errorString();
        qWarning().noquote() << "module page failed to load:" << lastError_;
        delete component_;
        component_ = nullptr;
        emit pageChanged();
        return false;
    }
    page_ = component_->create(context);
    if (page_) {
        // 独立引擎创建的根对象必须显式挂进宿主容器，否则不可见。
        if (auto *item = qobject_cast<QQuickItem *>(page_)) {
            if (container_) {
                item->setParentItem(container_);
                item->setWidth(container_->width());
                item->setHeight(container_->height());
            }
        }
    }
    if (!page_) {
        lastError_ = component_->errorString();
        qWarning().noquote() << "module page created nothing:" << lastError_;
        delete component_;
        component_ = nullptr;
        emit pageChanged();
        return false;
    }
    emit pageChanged();
    return true;
}

void ModulePageHost::unload() {
    if (page_) {
        delete page_;
        page_ = nullptr;
    }
    if (component_) {
        delete component_;
        component_ = nullptr;
    }
    emit pageChanged();
}

void ModulePageHost::setModuleDataApi(QObject *api) {
    moduleDataApi_ = api;
}

void ModulePageHost::setContainer(QObject *container) {
    container_ = qobject_cast<QQuickItem *>(container);
    if (!container_ || !page_) return;
    if (auto *item = qobject_cast<QQuickItem *>(page_)) {
        item->setParentItem(container_);
        item->setWidth(container_->width());
        item->setHeight(container_->height());
    }
}

void ModulePageHost::updateUserContext(const UserContext &context) {
    userContext_ = context;
    // 已加载的页面不重建；下次加载时生效。只读属性 CONSTANT 不可动态改。
}

bool ModulePageHost::hasHostObject(const QString &name) const {
    if (!engine_) return false;
    // 宿主注入的是 connectionManager/systemTools/musicService/desktopShell
    // 这类名字；独立引擎的根 context 不该有它们。
    return engine_->rootContext()->contextProperty(name).isValid();
}

#include "ModulePageHost.moc"
