#include "DiagnosticRunner.h"

#include <QCommandLineParser>
#include <QCursor>
#include <QMetaObject>
#include <QObject>
#include <QQuickWindow>
#include <QTimer>
#include <QVariant>
#include <QJsonDocument>
#include <QJsonObject>
#ifdef TERMINAL_REPAIR_AUDIT
#include <QTest>
#include <QQuickItem>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonArray>
#include <algorithm>
#include <memory>

namespace {
// Identical frame demand before/after: measure transition windows, not idle FPS.
class BlurBenchmark final : public QObject {
    QObject *root_;
    QQuickWindow *window_;
    QTimer pulse_;
    QElapsedTimer clock_;
    QList<double> gaps_;
    double last_ = 0;
    bool measuring_ = false;
    int mode_ = 0, sample_ = 0;
    QJsonArray results_;
    QJsonObject before_;
    const QStringList actions_{"maximize","restore","panel-open","panel-change","panel-close"};
    QJsonObject stats() {
        QVariant result;
        QMetaObject::invokeMethod(root_,"blurPerformanceStats",Q_RETURN_ARG(QVariant,result));
        return QJsonDocument::fromJson(result.toString().toUtf8()).object();
    }
    void setup() {
        QMetaObject::invokeMethod(root_,"blurPerformanceSetup",Q_ARG(QVariant,QVariant(mode_==0)));
        QTimer::singleShot(3000,this,[this]{ sample(); });
    }
    void sample() {
        before_=stats();gaps_.clear();last_=0;clock_.restart();measuring_=true;
        pulse_.start();
        QMetaObject::invokeMethod(root_,"blurPerformanceAction",Q_ARG(QVariant,QVariant(actions_[sample_%5])));
        QTimer::singleShot(500,this,[this] {
            measuring_=false;pulse_.stop();
            const double elapsed=clock_.nsecsElapsed()/1e6;
            auto sorted=gaps_;std::sort(sorted.begin(),sorted.end());
            const auto after=stats();QJsonObject delta;
            for(const auto &key:after.keys())delta[key]=after[key].toDouble()-before_[key].toDouble();
            results_.append(QJsonObject{{"energySaving",mode_==0},{"action",actions_[sample_%5]},
                {"repeat",sample_/5},{"frames",gaps_.size()},{"windowMs",elapsed},
                {"fps",gaps_.size()*1000.0/elapsed},{"firstFrameMs",gaps_.isEmpty()?elapsed:gaps_.first()},
                {"p95Ms",sorted.isEmpty()?elapsed:sorted[int((sorted.size()-1)*.95)]},
                {"worstMs",sorted.isEmpty()?elapsed:sorted.last()},
                {"over50Ms",int(std::count_if(gaps_.begin(),gaps_.end(),[](double x){return x>50;}))},
                {"blurWork",delta}});
            if(++sample_==10) {
                sample_=0;
                if(++mode_==2) {
                    qInfo().noquote()<<"BLUR_BENCHMARK"<<QJsonDocument(results_).toJson(QJsonDocument::Compact);
                    QCoreApplication::quit();return;
                }
                QTimer::singleShot(750,this,[this]{setup();});
            } else QTimer::singleShot(750,this,[this]{sample();});
        });
    }
public:
    BlurBenchmark(QObject *root,QQuickWindow *window):QObject(root),root_(root),window_(window) {
        pulse_.setInterval(16);
        connect(&pulse_,&QTimer::timeout,this,[this]{window_->update();});
        connect(window_,&QQuickWindow::frameSwapped,this,[this] {
            if(!measuring_)return;
            const double now=clock_.nsecsElapsed()/1e6;gaps_.append(now-last_);last_=now;
        });
        QTimer::singleShot(2000,this,[this]{setup();});
    }
};
}
#endif

#include "ui/SystemTools.h"

namespace DiagnosticRunner {

void schedule(const QCommandLineParser &options, QObject *root, QQuickWindow *window,
              SystemTools &systemTools) {
#ifdef TERMINAL_REPAIR_AUDIT
    const auto repairSpec=qEnvironmentVariable("PI_REPAIR_SPEC");
    if(!repairSpec.isEmpty()) {
        const auto spec=QJsonDocument::fromJson(repairSpec.toUtf8()).object().toVariantMap();
        if(spec.value("startup").toBool() && window) {
            systemTools.setUiLanguage(spec.value("language").toString());
            systemTools.setEnergySaving(spec.value("energy").toBool());
            auto firstFrame=std::make_shared<QMetaObject::Connection>();
            auto captured=std::make_shared<bool>(false);
            *firstFrame=QObject::connect(window,&QQuickWindow::frameSwapped,root,[root,window,firstFrame,captured] {
                if(*captured)return;
                *captured=true;
                QObject::disconnect(*firstFrame);
                QVariant snapshot;
                QMetaObject::invokeMethod(root,"startupSnapshot",Q_RETURN_ARG(QVariant,snapshot));
                qInfo().noquote()<<"STARTUP_AUDIT"<<snapshot.toString();
                // Do not synchronously re-enter rendering from frameSwapped:
                // Qt 6.4/VC4 can stall the render/GUI handshake here.
                QMetaObject::invokeMethod(root,"repairCaptureStartup",
                    Q_ARG(QVariant,QVariant(qEnvironmentVariable("PI_REPAIR_IMAGE")+".startup.png")));
            },Qt::QueuedConnection);
        }
        if(spec.value("blurBenchmark").toBool()) {
            QTimer::singleShot(800,root,[root,window,spec] {
                QMetaObject::invokeMethod(root,"repairSetup",Q_ARG(QVariant,QVariant(spec)));
                new BlurBenchmark(root,window);
            });
            return;
        }
        const int settleMs=qBound(1400,qEnvironmentVariableIntValue("PI_REPAIR_SETTLE_MS"),60000);
        QTimer::singleShot(800,root,[root,window,spec,settleMs,&systemTools] {
            qInfo("REPAIR_PHASE setup-begin");
            QMetaObject::invokeMethod(root,"repairSetup",Q_ARG(QVariant,QVariant(spec)));
            qInfo("REPAIR_PHASE setup-end");
            if(spec.value("cacheTransitions").toBool()) {
                const auto output=qEnvironmentVariable("PI_REPAIR_IMAGE");
                auto capture=[root,window,output](const QString &label) {
                    auto *timer=new QTimer(root);auto index=std::make_shared<int>(0);
                    timer->setInterval(75);
                    QObject::connect(timer,&QTimer::timeout,root,[timer,index,window,output,label] {
                        window->grabWindow().save(output+"."+label+QString::number((*index)++)+".png");
                        if(*index==4){timer->stop();timer->deleteLater();}
                    });timer->start();
                };
                for(const auto &step:QList<QPair<int,QString>>{{3000,"maximize"},{5000,"restore"},{7000,"panel-open"},{10000,"panel-change"}}) {
                    QTimer::singleShot(step.first,root,[root,capture,action=step.second] {
                        QMetaObject::invokeMethod(root,"blurPerformanceAction",Q_ARG(QVariant,QVariant(action)));
                        if(action!="restore")capture(action);
                    });
                }
            }
            QTimer::singleShot(settleMs,root,[root,window,spec,&systemTools] {
                if(spec.value("petEasterEgg").toBool()) {
                    auto *group=root->findChild<QQuickItem *>("personalizationGroup");
                    auto *title=group?group->findChild<QQuickItem *>("settingsGroupTitle"):nullptr;
                    auto *menu=root->findChild<QObject *>("petEasterEggMenu");
                    QJsonObject result;
                    result.insert("titleFound",title!=nullptr);
                    if(title && menu) {
                        const QPoint point=title->mapToScene(QPointF(title->width()/2,title->height()/2)).toPoint();
                        result.insert("x",point.x());result.insert("y",point.y());
                        result.insert("titleWidth",title->width());result.insert("titleVisible",title->isVisible());
                        result.insert("enabled",group->property("titleTapEnabled").toBool());
                        QJsonArray counts;
                        for(int i=0;i<5;++i) {
                            if(i==0) {
                                auto clock=QElapsedTimer();clock.start();
                                while(!root->property("startupComplete").toBool() && clock.elapsed()<15000)QTest::qWait(50);
                            }
                            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point,80);
                            QTest::qWait(80);
                            counts.append(group->property("titleTapCount").toInt());
                        }
                        result.insert("counts",counts);
                        result.insert("tapCount",group->property("titleTapCount").toInt());
                        result.insert("menuVisible",menu->property("visible").toBool());
                        auto clickItem=[&](const char *name) {
                            auto *item=menu->findChild<QQuickItem *>(QString::fromLatin1(name));
                            if(!item)return false;
                            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
                            QTest::qWait(250);return true;
                        };
                        auto reopen=[&] {
                            for(int i=0;i<5;++i){QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,point,30);QTest::qWait(60);}
                            QTest::qWait(150);
                        };
                        auto *loader=root->findChild<QObject *>("desktopPetLoader");
                        result.insert("initiallyOff",!root->property("petEasterEggEnabled").toBool() && loader && !loader->property("active").toBool());
                        clickItem("petEasterEggManage");
                        auto *manager=root->findChild<QObject *>("petManagerDialog");
                        result.insert("managementOnly",manager && manager->property("opened").toBool() && !root->property("petEasterEggEnabled").toBool());
                        if(manager)QMetaObject::invokeMethod(manager,"close");
                        QTest::qWait(250);reopen();clickItem("petEasterEggToggle");
                        result.insert("started",root->property("petEasterEggEnabled").toBool() && loader && loader->property("item").value<QObject *>());
                        reopen();clickItem("petEasterEggToggle");
                        result.insert("stopped",!root->property("petEasterEggEnabled").toBool() && loader && !loader->property("item").value<QObject *>());
                    }
                    qInfo().noquote()<<"PET_EASTER_EGG"<<QJsonDocument(result).toJson(QJsonDocument::Compact);
                }
                if (spec.contains("duckPreview")) {
                    systemTools.setDuckMediaForSpeech(spec.value("duckPreview").toBool());
                    systemTools.speak(QStringLiteral("正在测试系统语音播报，音乐和视频应根据设置自动降低音量，播报结束后恢复。"));
                    // Change the user's desired state DURING speech. Recovery
                    // must not restore a stale volume or restart paused media.
                    QTimer::singleShot(1200,root,[root] {
                        for (const auto &name : {"musicPlayerPage", "videoPlayerPage"}) {
                            if (auto *page = root->findChild<QObject *>(QString::fromLatin1(name))) {
                                page->setProperty("volume", 0.45);
                                if(QString::fromLatin1(name)=="videoPlayerPage")page->setProperty("muted",true);
                                QMetaObject::invokeMethod(page,"pauseMedia");
                            }
                        }
                    });
                    auto recoveryConnection=std::make_shared<QMetaObject::Connection>();
                    *recoveryConnection=QObject::connect(&systemTools,&SystemTools::audioFocusChanged,root,[root,&systemTools,recoveryConnection] {
                        if(systemTools.speechActive())return;
                        QObject::disconnect(*recoveryConnection);
                        QTimer::singleShot(250,root,[root,&systemTools] {
                            QJsonObject recovery{{"active",systemTools.speechActive()},{"gain",systemTools.mediaGain()}};
                            for (const auto &kind : {"music", "video"}) {
                                const auto prefix = QString::fromLatin1(kind);
                                if (auto *page = root->findChild<QObject *>(prefix+"PlayerPage"))
                                    recovery.insert(prefix+"Playing",page->property("mediaPlaying").toBool());
                                if (auto *output = root->findChild<QObject *>(prefix+"AudioOutput")) {
                                    recovery.insert(prefix+"Output",output->property("volume").toDouble());
                                    recovery.insert(prefix+"Muted",output->property("muted").toBool());
                                }
                            }
                            qInfo().noquote()<<"AUDIO_RECOVERY"<<QString::fromUtf8(QJsonDocument(recovery).toJson(QJsonDocument::Compact));
                        });
                    });
                }
                if(spec.value("paint").toBool()) {
                    QVariant snapshot;
                    QMetaObject::invokeMethod(root,"repairSnapshot",Q_RETURN_ARG(QVariant,snapshot));
                    const auto paint=QJsonDocument::fromJson(snapshot.toString().toUtf8()).object()["paint"].toObject();
                    const int x=qRound(paint["x"].toDouble()),y=qRound(paint["y"].toDouble());
                    const int width=qRound(paint["width"].toDouble()),height=qRound(paint["height"].toDouble());
                    auto *device=QTest::createTouchDevice();
                    for(int stroke=0;stroke<30;++stroke) {
                        const QPoint start(x+20,y+15+(stroke%10)*qMax(1,(height-30)/10));
                        const QPoint end(x+width-20,start.y());
                        if(stroke%2==0) {
                            QTest::touchEvent(window,device).press(0,start,window);
                            for(int step=1;step<8;++step)QTest::touchEvent(window,device).move(0,start+(end-start)*step/8,window);
                            QTest::touchEvent(window,device).release(0,end,window);
                        } else {
                            QTest::mousePress(window,Qt::LeftButton,Qt::NoModifier,start);
                            for(int step=1;step<8;++step)QTest::mouseMove(window,start+(end-start)*step/8);
                            QTest::mouseRelease(window,Qt::LeftButton,Qt::NoModifier,end);
                        }
                    }
                    // Offscreen x86 has no compositor input device. Exercise
                    // the exact shared Canvas sampler directly after trying
                    // Qt mouse/touch delivery; Pi hardware always uses real
                    // Xorg input events and never takes this fallback.
                    if(qEnvironmentVariableIntValue("PI_REPAIR_HOST_TEST")==1
                       && root->property("paintStrokes").toList().isEmpty()) {
                        if(auto *canvas=root->findChild<QObject *>(QStringLiteral("paintCanvas"))) {
                            for(int stroke=0;stroke<30;++stroke) {
                                const double y=15+(stroke%10)*qMax(1,(height-30)/10);
                                for(int step=0;step<8;++step)
                                    QMetaObject::invokeMethod(canvas,"sample",Q_ARG(QVariant,QVariant(20+(width-40)*step/7.0)),Q_ARG(QVariant,QVariant(y)));
                                QMetaObject::invokeMethod(canvas,"finishStroke");
                            }
                        }
                    }
                }
                QTimer::singleShot(900,root,[root,window,spec] {
                    qInfo("REPAIR_PHASE snapshot-begin");
                    QVariant snapshot;
                    QMetaObject::invokeMethod(root,"repairSnapshot",Q_RETURN_ARG(QVariant,snapshot));
                    auto audit = QJsonDocument::fromJson(snapshot.toString().toUtf8()).object();
                    QJsonObject outputs;
                    for (const auto &name : {"musicAudioOutput", "videoAudioOutput"}) {
                        const auto objectName = QString::fromLatin1(name) == QStringLiteral("videoAudioOutput")
                            ? QStringLiteral("vlcVideoOutput") : QString::fromLatin1(name);
                        if (auto *output = root->findChild<QObject *>(objectName))
                            outputs.insert(QString::fromLatin1(name), output->property("volume").toDouble());
                    }
                    audit.insert("audioOutputs", outputs);
                    qInfo().noquote()<<"REPAIR_AUDIT"<<QString::fromUtf8(QJsonDocument(audit).toJson(QJsonDocument::Compact));
                    qInfo("REPAIR_PHASE image-begin");
                    const auto output=qEnvironmentVariable("PI_REPAIR_IMAGE");
                    window->grabWindow().save(output);
                    QMetaObject::invokeMethod(root,"repairCaptureKeyboard",Q_ARG(QVariant,QVariant(output+".keyboard.png")));
                    if(spec.value("cacheDefault").toBool())
                        QMetaObject::invokeMethod(root,"repairCaptureCacheProbe",Q_ARG(QVariant,QVariant(output+".cache.png")));
                    qInfo("REPAIR_PHASE image-end");
                });
            });
        });
    }
#endif
    if(options.isSet("preview-page")) {
        const int page=options.value("preview-page").toInt();
        QTimer::singleShot(700,root,[root,page] {
            QMetaObject::invokeMethod(root,"auditPage",Q_ARG(QVariant,QVariant(page)));
            QTimer::singleShot(1200,root,[root] {
                QVariant geometry;
                QMetaObject::invokeMethod(root,"targetPageAudit",Q_RETURN_ARG(QVariant,geometry));
                qInfo().noquote()<<"UI_PAGE_GEOMETRY"<<geometry.toString();
            });
        });
    }
    if (options.isSet("quick-panel-test"))
        QTimer::singleShot(1800, root, [root] {
            QMetaObject::invokeMethod(root, "showDevicePanel", Q_ARG(QVariant, QVariant(false)));
            QTimer::singleShot(1800, root, [] { QCursor::setPos(1010, 590); });
        });

    if (options.isSet("notification-layout-test"))
        QTimer::singleShot(1200, root, [root] {
            QMetaObject::invokeMethod(root, "notify", Q_ARG(QVariant, QVariant("示例模块已连接")),
                Q_ARG(QVariant, QVariant("传感器模块已连接，四项测量数据正在更新。")),
                Q_ARG(QVariant, QVariant(false)), Q_ARG(QVariant, QVariant("示例模块")),
                Q_ARG(QVariant, QVariant("extension")));
            QMetaObject::invokeMethod(root, "notify", Q_ARG(QVariant, QVariant("控制桥连接中断")),
                Q_ARG(QVariant, QVariant("PBR1 USB 控制桥超过5秒无响应。")),
                Q_ARG(QVariant, QVariant(true)), Q_ARG(QVariant, QVariant("控制桥")),
                Q_ARG(QVariant, QVariant("lan")));
            QMetaObject::invokeMethod(root, "showDevicePanel", Q_ARG(QVariant, QVariant(true)));
        });

    if (options.isSet("notification-banner-test"))
        QTimer::singleShot(1200, root, [root] {
            QMetaObject::invokeMethod(root, "notify", Q_ARG(QVariant, QVariant("控制桥连接中断")),
                Q_ARG(QVariant, QVariant("PBR1 USB 控制桥超过5秒无响应。")),
                Q_ARG(QVariant, QVariant(true)), Q_ARG(QVariant, QVariant("控制桥")),
                Q_ARG(QVariant, QVariant("lan")));
        });

    if (options.isSet("app-drawer-test"))
        QTimer::singleShot(1200, window, [root, window, &options] {
            QMetaObject::invokeMethod(root, "openAppDrawer");
            QTimer::singleShot(900, root, [root, window, &options] {
                QVariant snapshot;
                QMetaObject::invokeMethod(root, "appDrawerAuditSnapshot", Q_RETURN_ARG(QVariant, snapshot));
                qInfo().noquote() << "APP_DRAWER_AUDIT" << snapshot.toString();
                if (options.isSet("screenshot"))
                    window->grabWindow().save(options.value("screenshot"));
            });
        });

    if (options.isSet("keyboard-test"))
        QTimer::singleShot(1400, root, [root] {
            QVariant result;
            QMetaObject::invokeMethod(root, "auditKeyboardDictionary", Q_RETURN_ARG(QVariant, result));
            qInfo().noquote() << "KEYBOARD_DICTIONARY_AUDIT" << result.toString();
        });

    if (options.isSet("window-animation-test"))
        QTimer::singleShot(1200, root, [root] {
            if (!QMetaObject::invokeMethod(root, "auditWindowAnimation"))
                qWarning("Could not start window-animation audit");
        });

    if (window && options.isSet("notification-test"))
        QTimer::singleShot(2500, window, [window, &systemTools] {
            QMetaObject::invokeMethod(window, "notify", Q_ARG(QVariant, QVariant("通知测试")),
                Q_ARG(QVariant, QVariant("中文播报与通知横幅测试，可在通知中心查看记录。")),
                Q_ARG(QVariant, QVariant(true)));
            systemTools.speak("中文播报与通知横幅测试");
        });

    if (window && options.isSet("render-benchmark"))
        QTimer::singleShot(3000, window, [window] { window->setProperty("renderBenchmark", true); });

    if (options.isSet("paint-test"))
        QTimer::singleShot(1200, window, [root, window, &options] {
            QMetaObject::invokeMethod(root, "auditPage", Q_ARG(QVariant, QVariant(7)));
            QTimer::singleShot(900, root, [root, window, &options] {
                QVariant exercisePassed;
                QMetaObject::invokeMethod(root, "paintAuditExercise", Q_RETURN_ARG(QVariant, exercisePassed));
                qInfo().noquote() << "PAINT_AUDIT"
                    << "exercisePassed=" << exercisePassed.toBool()
                    << "canvas=" << root->property("paintCanvasWidth").toDouble()
                    << "x" << root->property("paintCanvasHeight").toDouble()
                    << "toolbarHeight=" << root->property("paintToolbarHeight").toDouble()
                    << "eraser=" << root->property("paintEraser").toBool()
                    << "rectangle=" << root->property("paintRectangle").toBool()
                    << "brushSize=" << root->property("paintSize").toDouble()
                    << "section=" << root->property("section").toInt();
                if (window && options.isSet("screenshot"))
                    window->grabWindow().save(options.value("screenshot"));
            });
        });
}

} // namespace DiagnosticRunner
