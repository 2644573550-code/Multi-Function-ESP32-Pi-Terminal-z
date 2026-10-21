#pragma once
#include <QObject>
#include <QVariantList>
#include <QTimer>
#include <functional>

class RemovableStorage : public QObject {
    Q_OBJECT
public:
    explicit RemovableStorage(QObject *parent = nullptr, const QString &sysRoot = "/sys/class/block",
                              std::function<QVariantList()> volumes = {});
    QVariantList devices() const { return devices_; }
    void refresh();
    QString resolve(const QString &location, bool creating = false, bool allowRoot = false) const;
    void open(const QString &id);
signals:
    void changed();
    void inserted(QString name);
    void ready(QString location);
    void error(QString message);
private:
    QString sysRoot_;
    std::function<QVariantList()> volumes_;
    QVariantList devices_;
    QTimer timer_;
    bool initialized_ = false;
    bool mounting_ = false;
};
