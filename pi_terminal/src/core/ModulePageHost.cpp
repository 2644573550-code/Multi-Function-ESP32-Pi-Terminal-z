#include "core/ModulePageHost.h"

#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
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

ModulePageHost::ModulePageHost(QObject *parent) : QObject(parent) {
    // 独立引擎：与宿主 QQmlEngine 完全隔离，宿主 context 属性一个都看不到。
    engine_ = new QQmlEngine(this);
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

    component_ = new QQmlComponent(engine_, source, QQmlComponent::PreferSynchronous, this);
    if (component_->isError()) {
        qWarning().noquote() << "module page failed to load:" << component_->errorString();
        delete component_;
        component_ = nullptr;
        emit pageChanged();
        return false;
    }
    page_ = component_->create(context);
    if (!page_) {
        qWarning().noquote() << "module page created nothing:" << component_->errorString();
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
