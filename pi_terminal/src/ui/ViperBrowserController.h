#pragma once

#include <QObject>
#include <QPointer>
#include <QVariantList>
#include <QNetworkCookie>
#include <QtWebEngineQuick/qquickwebengineprofile.h>
#include <QWebEngineUrlSchemeHandler>

class BookmarkManager;
class BrowserApplication;
class ViperBrowserController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QQuickWebEngineProfile *profile READ profile NOTIFY profileReadyChanged)
    Q_PROPERTY(bool profileReady READ profileReady NOTIFY profileReadyChanged)
    Q_PROPERTY(int bookmarkCount READ bookmarkCount NOTIFY bookmarksChanged)
    Q_PROPERTY(bool javascriptPopupsEnabled READ javascriptPopupsEnabled NOTIFY browserSettingsChanged)
    Q_PROPERTY(QVariantMap preferences READ preferences NOTIFY browserSettingsChanged)
    Q_PROPERTY(QVariantList cookies READ cookies NOTIFY cookiesChanged)
    Q_PROPERTY(QVariantList subscriptions READ subscriptions NOTIFY subscriptionsChanged)
    Q_PROPERTY(QVariantList searchEngines READ searchEngines NOTIFY searchEnginesChanged)
    Q_PROPERTY(QString defaultSearchEngine READ defaultSearchEngine NOTIFY searchEnginesChanged)

public:
    explicit ViperBrowserController(QObject *parent = nullptr);
    ~ViperBrowserController() override;
    QQuickWebEngineProfile *profile() const;
    bool profileReady() const;
    int bookmarkCount() const;

    Q_INVOKABLE QVariantList bookmarks() const;
    Q_INVOKABLE QString downloadDirectory() const;
    Q_INVOKABLE QString profileStoragePath() const;
    Q_INVOKABLE QString profileCachePath() const;
    Q_INVOKABLE bool attachProfile(QObject *profile);
    Q_INVOKABLE bool javascriptEnabled() const;
    Q_INVOKABLE bool javascriptPopupsEnabled() const;
    Q_INVOKABLE void setJavascriptPopupsEnabled(bool enabled);
    Q_INVOKABLE bool imagesEnabled() const;
    Q_INVOKABLE void recordVisit(const QUrl &url, const QString &title);
    Q_INVOKABLE QVariantList recentHistory() const;
    Q_INVOKABLE void clearHistory();
    Q_INVOKABLE void requestAdBlockData(const QUrl &url, int token);
    Q_INVOKABLE bool requestFilteringReady() const;
    Q_INVOKABLE bool customSchemesReady() const;
    Q_INVOKABLE bool isBookmarked(const QUrl &url) const;
    Q_INVOKABLE bool addBookmark(const QUrl &url, const QString &title);
    Q_INVOKABLE bool removeBookmark(const QUrl &url);
    QVariantMap preferences() const;
    Q_INVOKABLE bool setPreference(const QString &key, const QVariant &value);
    QVariantList cookies() const;
    Q_INVOKABLE bool setCookie(const QString &rawCookie, const QUrl &origin);
    Q_INVOKABLE void deleteCookie(int index);
    Q_INVOKABLE void clearCookies();
    Q_INVOKABLE void clearCache();
    QVariantList subscriptions() const;
    Q_INVOKABLE void installSubscription(const QUrl &url);
    Q_INVOKABLE void updateSubscriptions();
    Q_INVOKABLE void toggleSubscription(int row);
    Q_INVOKABLE void removeSubscription(int row);
    Q_INVOKABLE bool attachPrivateProfile(QObject *profile);
    Q_INVOKABLE QString newTabUrl() const;
    Q_INVOKABLE QString resolveAddress(const QString &text) const;
    Q_INVOKABLE QVariantMap savedSession() const;
    Q_INVOKABLE bool saveSession(const QVariantList &tabs, int currentIndex);
    QVariantList searchEngines() const;
    QString defaultSearchEngine() const;
    Q_INVOKABLE bool addSearchEngine(const QString &name, const QString &queryTemplate);
    Q_INVOKABLE void removeSearchEngine(const QString &name);
    Q_INVOKABLE void setDefaultSearchEngine(const QString &name);

signals:
    void profileReadyChanged();
    void bookmarksChanged();
    void adBlockDataReady(int token, const QString &css, const QString &script);
    void historyChanged();
    void browserSettingsChanged();
    void cookiesChanged();
    void subscriptionsChanged();
    void searchEnginesChanged();

private:
    void appendBookmarks(QVariantList &items, const class BookmarkNode *node) const;

    BrowserApplication *browser_ = nullptr;
    BookmarkManager *bookmarks_ = nullptr;
    QList<QNetworkCookie> cookies_;
    QPointer<QQuickWebEngineProfile> privateProfile_;
    void configureProfile(QQuickWebEngineProfile *profile);
};
