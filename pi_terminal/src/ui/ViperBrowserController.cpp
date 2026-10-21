#include "ViperBrowserController.h"

#include "BrowserApplication.h"
#include "BookmarkManager.h"
#include "BookmarkNode.h"
#include "AdBlockManager.h"
#include "HistoryManager.h"
#include "URLRecord.h"
#include "URL.h"
#include "AdBlockModel.h"
#include "SearchEngineManager.h"

#include <QtWebEngineQuick/qquickwebengineprofile.h>
#include <QWebEngineCookieStore>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QDir>

namespace {
const QMap<QString, BrowserSetting> preferenceKeys{
    {"homePage", BrowserSetting::HomePage}, {"startupMode", BrowserSetting::StartupMode},
    {"newTabPage", BrowserSetting::NewTabPage}, {"javascript", BrowserSetting::EnableJavascript},
    {"popups", BrowserSetting::EnableJavascriptPopups}, {"images", BrowserSetting::AutoLoadImages},
    {"cookies", BrowserSetting::EnableCookies}, {"thirdPartyCookies", BrowserSetting::EnableThirdPartyCookies},
    {"sessionCookies", BrowserSetting::CookiesDeleteWithSession},
    {"doNotTrack", BrowserSetting::SendDoNotTrack},
    {"adBlock", BrowserSetting::AdBlockPlusEnabled},
    {"backgroundTabs", BrowserSetting::OpenAllTabsInBackground}, {"smoothScroll", BrowserSetting::ScrollAnimatorEnabled},
    {"historyPolicy", BrowserSetting::HistoryStoragePolicy}, {"downloadDirectory", BrowserSetting::DownloadDir},
    {"askDownload", BrowserSetting::AskWhereToSaveDownloads}, {"standardFont", BrowserSetting::StandardFont},
    {"fixedFont", BrowserSetting::FixedFont}, {"fontSize", BrowserSetting::StandardFontSize},
    {"fixedFontSize", BrowserSetting::FixedFontSize}
};
}
ViperBrowserController::ViperBrowserController(QObject *parent)
    : QObject(parent), browser_(new BrowserApplication(nullptr, this))
{
    bookmarks_ = qobject_cast<BookmarkManager *>(browser_->getService(QStringLiteral("BookmarkManager")));
    if (bookmarks_)
        connect(bookmarks_, &BookmarkManager::bookmarksChanged, this, &ViperBrowserController::bookmarksChanged);
    if (auto *history=qobject_cast<HistoryManager *>(browser_->getService(QStringLiteral("HistoryManager")))) {
        connect(history,&HistoryManager::pageVisited,this,[this]{emit historyChanged();});
        connect(history,&HistoryManager::historyCleared,this,&ViperBrowserController::historyChanged);
    }
    connect(browser_->getSettings(),&Settings::settingChanged,this,[this](BrowserSetting setting,const QVariant &){
        Q_UNUSED(setting);
        configureProfile(profile());
        configureProfile(privateProfile_);
        emit browserSettingsChanged();
    });
    auto *filterModel = browser_->getAdBlockManager()->getModel();
    auto filterChanged = [this] { emit subscriptionsChanged(); };
    connect(filterModel, &QAbstractItemModel::dataChanged, this, filterChanged);
    connect(filterModel, &QAbstractItemModel::rowsInserted, this, filterChanged);
    connect(filterModel, &QAbstractItemModel::rowsRemoved, this, filterChanged);
    connect(filterModel, &QAbstractItemModel::modelReset, this, filterChanged);
    auto &search = SearchEngineManager::instance();
    connect(&search, &SearchEngineManager::engineAdded, this, [this] { emit searchEnginesChanged(); });
    connect(&search, &SearchEngineManager::engineRemoved, this, [this] { emit searchEnginesChanged(); });
    connect(&search, &SearchEngineManager::defaultEngineChanged, this, [this] { emit searchEnginesChanged(); });
}

ViperBrowserController::~ViperBrowserController() = default;

QQuickWebEngineProfile *ViperBrowserController::profile() const
{
    return browser_->quickProfile();
}

bool ViperBrowserController::profileReady() const
{
    return browser_->quickProfile()!=nullptr;
}

int ViperBrowserController::bookmarkCount() const
{
    return bookmarks().size();
}

void ViperBrowserController::appendBookmarks(QVariantList &items, const BookmarkNode *node) const
{
    if (!node)
        return;
    if (node->getType() == BookmarkNode::Bookmark) {
        items.append(QVariantMap{{QStringLiteral("title"), node->getName()},
                                 {QStringLiteral("url"), node->getURL().toString()}});
        return;
    }
    for (int i = 0; i < node->getNumChildren(); ++i)
        appendBookmarks(items, node->getNode(i));
}

QVariantList ViperBrowserController::bookmarks() const
{
    QVariantList items;
    if (bookmarks_)
        appendBookmarks(items, bookmarks_->getRoot());
    return items;
}

QString ViperBrowserController::downloadDirectory() const
{
    const QString configured = browser_->getSettings()->getValue(BrowserSetting::DownloadDir).toString();
    const QString configuredPath = configured.isEmpty()
        ? QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) : configured;
    if (configuredPath.isEmpty())
        return {};
    const QString path = QDir::cleanPath(QFileInfo(configuredPath).absoluteFilePath());
    if (!QDir(path).exists() && !QDir().mkpath(path))
        return {};
    const QFileInfo info(path);
    return info.isDir() && info.isWritable() ? info.absoluteFilePath() : QString{};
}

QString ViperBrowserController::profileStoragePath() const
{
    const QString path=QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)+QStringLiteral("/viper-browser");
    QDir().mkpath(path);
    return path;
}

QString ViperBrowserController::profileCachePath() const
{
    const QString path=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+QStringLiteral("/viper-browser");
    QDir().mkpath(path);
    return path;
}

bool ViperBrowserController::attachProfile(QObject *object)
{
    auto *quickProfile=qobject_cast<QQuickWebEngineProfile *>(object);
    if(!quickProfile)return false;
    const bool wasReady=profileReady();
    browser_->attachQuickProfile(quickProfile);
    if (!wasReady) {
        auto *store = quickProfile->cookieStore();
        connect(store, &QWebEngineCookieStore::cookieAdded, this, [this](const QNetworkCookie &cookie) {
            for (auto &existing : cookies_) {
                if (existing.hasSameIdentifier(cookie)) { existing = cookie; emit cookiesChanged(); return; }
            }
            cookies_.append(cookie);
            emit cookiesChanged();
        });
        connect(store, &QWebEngineCookieStore::cookieRemoved, this, [this](const QNetworkCookie &cookie) {
            for (qsizetype i = cookies_.size(); i-- > 0;)
                if (cookies_[i].hasSameIdentifier(cookie)) cookies_.removeAt(i);
            emit cookiesChanged();
        });
        store->loadAllCookies();
    }
    configureProfile(quickProfile);
    emit browserSettingsChanged();
    if(!wasReady)emit profileReadyChanged();
    return browser_->quickProfile()==quickProfile;
}

QVariantMap ViperBrowserController::preferences() const
{
    QVariantMap result;
    for (auto it = preferenceKeys.cbegin(); it != preferenceKeys.cend(); ++it)
        result.insert(it.key(), browser_->getSettings()->getValue(it.value()));
    return result;
}

bool ViperBrowserController::setPreference(const QString &key, const QVariant &value)
{
    const auto it = preferenceKeys.constFind(key);
    if (it == preferenceKeys.cend()) return false;
    if ((key == "startupMode" && (value.toInt() < 0 || value.toInt() > 3)) ||
        (key == "newTabPage" && (value.toInt() < 0 || value.toInt() > 2)) ||
        ((key == "fontSize" || key == "fixedFontSize") && (value.toInt() < 6 || value.toInt() > 72))) return false;
    browser_->getSettings()->setValue(it.value(), value);
    return true;
}

void ViperBrowserController::configureProfile(QQuickWebEngineProfile *target)
{
    if (!target) return;
    const auto prefs = preferences();
    target->setPersistentCookiesPolicy(prefs.value("sessionCookies").toBool() ?
        QQuickWebEngineProfile::NoPersistentCookies : QQuickWebEngineProfile::AllowPersistentCookies);
    target->setDownloadPath(downloadDirectory());
    // Cookie filters execute on Chromium's IO thread: capture an immutable snapshot,
    // never read QSettings or a UI QObject from the callback.
    const bool enabled = prefs.value("cookies").toBool();
    const bool thirdParty = prefs.value("thirdPartyCookies").toBool();
    target->cookieStore()->setCookieFilter([enabled, thirdParty](const QWebEngineCookieStore::FilterRequest &request) {
        return enabled && (thirdParty || !request.thirdParty);
    });
}

QVariantList ViperBrowserController::cookies() const
{
    QVariantList result;
    for (const auto &cookie : cookies_)
        result.append(QVariantMap{{"name", QString::fromUtf8(cookie.name())}, {"domain", cookie.domain()},
            {"path", cookie.path()}, {"secure", cookie.isSecure()}, {"httpOnly", cookie.isHttpOnly()},
            {"expires", cookie.expirationDate()}, {"raw", QString::fromUtf8(cookie.toRawForm())}});
    return result;
}

bool ViperBrowserController::setCookie(const QString &rawCookie, const QUrl &origin)
{
    if (!profile() || (origin.scheme() != "https" && origin.scheme() != "http")) return false;
    const auto parsed = QNetworkCookie::parseCookies(rawCookie.toUtf8());
    if (parsed.size() != 1 || parsed.first().name().isEmpty()) return false;
    profile()->cookieStore()->setCookie(parsed.first(), origin);
    return true;
}

void ViperBrowserController::deleteCookie(int index)
{
    if (profile() && index >= 0 && index < cookies_.size()) profile()->cookieStore()->deleteCookie(cookies_[index]);
}

void ViperBrowserController::clearCookies() { if (profile()) profile()->cookieStore()->deleteAllCookies(); }
void ViperBrowserController::clearCache() { if (profile()) profile()->clearHttpCache(); }

QVariantList ViperBrowserController::subscriptions() const
{
    QVariantList result;
    auto *model = browser_->getAdBlockManager()->getModel();
    for (int row = 0; row < model->rowCount(); ++row)
        result.append(QVariantMap{{"name", model->data(model->index(row, 1))},
            {"enabled", model->data(model->index(row, 0), Qt::CheckStateRole).toInt() == Qt::Checked},
            {"updated", model->data(model->index(row, 2))}, {"url", model->data(model->index(row, 3))}});
    return result;
}

void ViperBrowserController::installSubscription(const QUrl &url)
{
    if (url.scheme() == "https" || url.scheme() == "http") browser_->getAdBlockManager()->installSubscription(url);
}
void ViperBrowserController::updateSubscriptions() { browser_->getAdBlockManager()->updateSubscriptions(); }
void ViperBrowserController::toggleSubscription(int row)
{
    auto *model = browser_->getAdBlockManager()->getModel();
    if (row >= 0 && row < model->rowCount()) model->setData(model->index(row, 0), {}, Qt::CheckStateRole);
}
void ViperBrowserController::removeSubscription(int row)
{
    auto *model = browser_->getAdBlockManager()->getModel();
    if (row >= 0 && row < model->rowCount()) model->removeRows(row, 1);
}
bool ViperBrowserController::attachPrivateProfile(QObject *object)
{
    auto *target = qobject_cast<QQuickWebEngineProfile *>(object);
    if (!target || !target->isOffTheRecord()) return false;
    privateProfile_ = target;
    // The public and private pages share filtering, but never storage or cookies.
    browser_->attachQuickPrivateProfile(target);
    configureProfile(target);
    return true;
}

QString ViperBrowserController::newTabUrl() const
{
    switch (browser_->getSettings()->getValue(BrowserSetting::NewTabPage).toInt()) {
    case 0: return browser_->getSettings()->getValue(BrowserSetting::HomePage).toString();
    // The Viper new-tab cards rely on a removed WebChannel integration; use
    // the configured search home as a practical new-tab destination instead.
    case 2: return browser_->getSettings()->getValue(BrowserSetting::HomePage).toString();
    default: return "about:blank";
    }
}

QString ViperBrowserController::resolveAddress(const QString &text) const
{
    const QString input = text.trimmed();
    if (input.isEmpty()) return {};
    if (input.contains("://") || input.startsWith("about:") || input.startsWith("view-source:") ||
        (!input.contains(' ') && (input.contains('.') || input.startsWith("localhost"))))
        return QUrl::fromUserInput(input).toString();
    const auto request = SearchEngineManager::instance().getSearchRequest(input);
    return request.getUrl().toString();
}

QVariantMap ViperBrowserController::savedSession() const
{
    QFile file(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation) + "/viper-browser/quick-session.json");
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
}

bool ViperBrowserController::saveSession(const QVariantList &tabs, int currentIndex)
{
    QVariantList publicTabs;
    int publicIndex = 0;
    for (const auto &entry : tabs) {
        const auto tab = entry.toMap();
        if (!tab.value("privateMode").toBool()) {
            if (publicTabs.size() == currentIndex) publicIndex = publicTabs.size();
            publicTabs.append(tab);
        }
    }
    QSaveFile file(profileStoragePath() + "/quick-session.json");
    const auto bytes = QJsonDocument::fromVariant(QVariantMap{{"tabs", publicTabs}, {"currentIndex", publicIndex}}).toJson();
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}

QVariantList ViperBrowserController::searchEngines() const
{
    QVariantList result;
    const auto &manager = SearchEngineManager::instance();
    for (const auto &name : manager.getSearchEngineNames())
        result.append(QVariantMap{{"name", name}, {"query", manager.getQueryString(name)},
                                  {"default", name == manager.getDefaultSearchEngine()}});
    return result;
}

QString ViperBrowserController::defaultSearchEngine() const
{
    return SearchEngineManager::instance().getDefaultSearchEngine();
}

bool ViperBrowserController::addSearchEngine(const QString &name, const QString &queryTemplate)
{
    const QString cleanName = name.trimmed();
    const QString cleanQuery = queryTemplate.trimmed();
    if (cleanName.isEmpty() || cleanName.size() > 48 || cleanQuery.size() > 2048 ||
        cleanQuery.count("%s") != 1 || cleanQuery.contains('\n') || cleanQuery.contains('\r')) return false;
    QString testQuery = cleanQuery;
    testQuery.replace("%s", "test");
    const QUrl url(testQuery);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https")) return false;
    SearchEngineManager::instance().addSearchEngine(cleanName, cleanQuery);
    emit searchEnginesChanged();
    return true;
}

void ViperBrowserController::removeSearchEngine(const QString &name)
{
    SearchEngineManager::instance().removeSearchEngine(name);
    emit searchEnginesChanged();
}

void ViperBrowserController::setDefaultSearchEngine(const QString &name)
{
    SearchEngineManager::instance().setDefaultSearchEngine(name);
    emit searchEnginesChanged();
}

bool ViperBrowserController::javascriptEnabled() const
{
    return browser_->getSettings()->getValue(BrowserSetting::EnableJavascript).toBool();
}

bool ViperBrowserController::javascriptPopupsEnabled() const
{
    return browser_->getSettings()->getValue(BrowserSetting::EnableJavascriptPopups).toBool();
}

void ViperBrowserController::setJavascriptPopupsEnabled(bool enabled)
{
    browser_->getSettings()->setValue(BrowserSetting::EnableJavascriptPopups,enabled);
}

bool ViperBrowserController::imagesEnabled() const
{
    return browser_->getSettings()->getValue(BrowserSetting::AutoLoadImages).toBool();
}

void ViperBrowserController::recordVisit(const QUrl &url, const QString &title)
{
    if (!url.isValid() || (url.scheme() != QStringLiteral("http") && url.scheme() != QStringLiteral("https")))
        return;
    auto *history = qobject_cast<HistoryManager *>(browser_->getService(QStringLiteral("HistoryManager")));
    if (history)
        history->addVisit(url, title, QDateTime::currentDateTime(), url, false);
}

QVariantList ViperBrowserController::recentHistory() const
{
    QVariantList items;
    auto *history=qobject_cast<HistoryManager *>(browser_->getService(QStringLiteral("HistoryManager")));
    if(!history)return items;
    const auto &recent=history->getRecentItems();
    for(auto it=recent.begin();it!=recent.end()&&items.size()<100;++it)
        items.append(QVariantMap{{QStringLiteral("title"),it->Title},
                                 {QStringLiteral("url"),it->URL.toString()},
                                 {QStringLiteral("lastVisit"),it->LastVisit}});
    return items;
}

void ViperBrowserController::clearHistory()
{
    if(auto *history=qobject_cast<HistoryManager *>(browser_->getService(QStringLiteral("HistoryManager"))))
        history->clearAllHistory();
    emit historyChanged();
}

void ViperBrowserController::requestAdBlockData(const QUrl &url, int token)
{
    auto *manager = qobject_cast<adblock::AdBlockManager *>(browser_->getService(QStringLiteral("AdBlockManager")));
    if (!manager) {
        emit adBlockDataReady(token, {}, {});
        return;
    }
    emit adBlockDataReady(token, manager->getStylesheet(URL(url)), manager->getDomainJavaScript(URL(url)));
}

bool ViperBrowserController::requestFilteringReady() const
{
    return browser_->requestFilteringReady();
}

bool ViperBrowserController::customSchemesReady() const
{
    return browser_->viperSchemeHandler() && browser_->blockedSchemeHandler();
}

bool ViperBrowserController::isBookmarked(const QUrl &url) const
{
    return bookmarks_ && bookmarks_->isBookmarked(url);
}

bool ViperBrowserController::addBookmark(const QUrl &url, const QString &title)
{
    if (!bookmarks_ || !url.isValid() || url.isEmpty() || isBookmarked(url))
        return false;
    bookmarks_->appendBookmark(title.isEmpty() ? url.host() : title, url);
    emit bookmarksChanged();
    return true;
}

bool ViperBrowserController::removeBookmark(const QUrl &url)
{
    if (!bookmarks_ || !isBookmarked(url))
        return false;
    bookmarks_->removeBookmark(url);
    emit bookmarksChanged();
    return true;
}
