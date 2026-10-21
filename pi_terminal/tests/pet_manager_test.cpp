#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QJSValue>
#include <QTemporaryDir>
#include <QDir>
#include <QQmlComponent>
#include "ui/TerminalUi.h"
#include "ui/SystemTools.h"
#include "ui/PetLibrary.h"
#include <limits>

class ManagerLanguage : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString uiLanguage MEMBER language CONSTANT)
public:
    QString language;
};

static QVariantList rows() {
    return {
        QVariantMap{{"id","002_amiya"},{"character","阿米娅"},{"appellation","Amiya"},{"name","阿米娅 · 默认服装"},{"modelName","默认服装"},{"category","default"},{"search","阿米娅 Amiya 002_amiya"},{"installed",true},{"current",true}},
        QVariantMap{{"id","002_amiya_winter#1"},{"character","阿米娅"},{"appellation","Amiya"},{"name","阿米娅 · 忒斯特收藏/I"},{"modelName","忒斯特收藏/I"},{"category","skin"},{"search","阿米娅 Amiya winter"},{"installed",false},{"current",false}},
        QVariantMap{{"id","002_amiya_epoque#4"},{"character","阿米娅"},{"appellation","Amiya"},{"name","阿米娅 · 忒斯特收藏/II"},{"modelName","忒斯特收藏/II"},{"category","skin"},{"search","阿米娅 Amiya epoque"},{"installed",false},{"current",false}},
        QVariantMap{{"id","dyn_amiya"},{"character","阿米娅"},{"name","阿米娅 · 动态"},{"modelName","动态"},{"category","illustration"},{"search","阿米娅 illustration"},{"installed",false},{"current",false}},
        QVariantMap{{"id","103_angel"},{"character","能天使"},{"appellation","Exusiai"},{"name","能天使 · 默认服装"},{"modelName","默认服装"},{"category","default"},{"search","能天使 Exusiai"},{"installed",false},{"current",false}},
        QVariantMap{{"id","103_angel_skin"},{"character","能天使"},{"appellation","Exusiai"},{"name","能天使 · 服装"},{"modelName","服装"},{"category","skin"},{"search","能天使 Exusiai"},{"installed",false},{"current",false}},
        QVariantMap{{"id","148_nearl"},{"character","临光"},{"appellation","Nearl"},{"name","临光 · 默认服装"},{"modelName","默认服装"},{"category","default"},{"search","临光 Nearl"},{"installed",false},{"current",false}}
    };
}
static QVariantList array(const QVariant &value) {
    return value.metaType().id()==qMetaTypeId<QJSValue>() ? value.value<QJSValue>().toVariant().toList() : value.toList();
}
class PetManagerTests : public QObject {
    Q_OBJECT
    QTemporaryDir isolated_;
private slots:
    void initTestCase() {
        QVERIFY(isolated_.isValid());
        qputenv("XDG_CONFIG_HOME", (isolated_.path()+"/config").toUtf8());
        qputenv("XDG_DATA_HOME", (isolated_.path()+"/data").toUtf8());
        QQuickStyle::setStyle("Material"); initializeTerminalUi();
    }
    void responsiveListDetail_data() {
        QTest::addColumn<QSize>("size"); QTest::addColumn<QString>("language");
        for (const auto &lang : {QString("zh"),QString("en")})
            for (const QSize size : {QSize(1024,600),QSize(800,480),QSize(480,600),QSize(320,320)})
                QTest::newRow(qPrintable(lang+QString::number(size.width())+"x"+QString::number(size.height()))) << size << lang;
    }
    void coldCatalogHasExplicitLoadAction() {
        QQmlApplicationEngine engine;
        engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->resize(320,320);
        auto *manager=window->findChild<QObject *>("petManagerDialog"); QVERIFY(manager);
        QMetaObject::invokeMethod(manager,"open");
        QTRY_VERIFY(manager->property("opened").toBool());
        auto *refresh=manager->findChild<QQuickItem *>("petManagerRefresh"); QVERIFY(refresh);
        QCOMPARE(refresh->property("text").toString(),QString("加载目录"));
        QVERIFY(refresh->width()>=120);
        auto *search=manager->findChild<QQuickItem *>("petManagerSearch"); QVERIFY(search);
        QVERIFY(search->width()>=80);
        QCOMPARE(array(manager->property("characters")).size(),1);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        if(qEnvironmentVariableIsSet("ARKPETS_MANAGER_SCREENSHOTS")) {
            const QString dir=qEnvironmentVariable("ARKPETS_MANAGER_SCREENSHOTS");QDir().mkpath(dir);
            QVERIFY(window->grabWindow().save(dir+"/cold-catalog.png"));
        }
    }
    void hiddenEntryRequiresFiveTitleTaps() {
        QQmlEngine engine;
        QQmlComponent component(&engine,QUrl("qrc:/qt/qml/OrangePiTerminal/SettingsGroup.qml"));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object,qPrintable(component.errorString()));
        auto *group=qobject_cast<QQuickItem *>(object.data());QVERIFY(group);
        QQuickWindow window;window.resize(640,400);
        group->setParentItem(window.contentItem());group->setWidth(600);
        group->setProperty("title",QString("显示与个性化"));
        group->setHeight(group->implicitHeight());
        group->setProperty("titleTapEnabled",true);
        window.show();QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *title=group->findChild<QQuickItem *>("settingsGroupTitle");QVERIFY(title);
        QSignalSpy triggered(group,SIGNAL(titleTapped()));
        const auto tap=[&]{QTest::mouseClick(&window,Qt::LeftButton,Qt::NoModifier,title->mapToScene(QPointF(title->width()/2,title->height()/2)).toPoint());};
        QTest::qWait(50);
        QVERIFY(title->height()>=48);
        for(int i=0;i<4;++i)tap();
        QCOMPARE(triggered.count(),0);
        tap();QCOMPARE(triggered.count(),1);
        QCOMPARE(group->property("titleTapCount").toInt(),0);
        QTest::mouseDClick(&window,Qt::LeftButton,Qt::NoModifier,title->mapToScene(QPointF(title->width()/2,title->height()/2)).toPoint());
        QCOMPARE(group->property("titleTapCount").toInt(),2); // Two physical clicks, including native double-click delivery.
        group->setProperty("titleTapCount",0);
        tap();tap();
        // Simulate an expired interval without slowing down the test.
        group->setProperty("titleTapStarted",0.0);
        for(int i=0;i<4;++i)tap();
        QCOMPARE(triggered.count(),1);
        tap();QCOMPARE(triggered.count(),2);
        tap();group->setVisible(false);group->setVisible(true);
        QCOMPARE(group->property("titleTapCount").toInt(),0);
        group->setProperty("titleTapEnabled",false);
        for(int i=0;i<5;++i)tap();
        QCOMPARE(triggered.count(),2);
    }
    void sizePreferenceAndRealSlider() {
        PetLibrary library;
        library.setSizeMultiplier(1.5);
        {PetLibrary reopened;QCOMPARE(reopened.sizeMultiplier(),1.5);}
        library.setSizeMultiplier(20);QCOMPARE(library.sizeMultiplier(),2.0);
        library.setSizeMultiplier(-1);QCOMPARE(library.sizeMultiplier(),0.5);
        library.setSizeMultiplier(std::numeric_limits<double>::quiet_NaN());QCOMPARE(library.sizeMultiplier(),0.5);
        library.setSizeMultiplier(1);
        library.setQuality("high");
        {PetLibrary reopened;QCOMPARE(reopened.quality(),QString("high"));}
        library.setQuality("invalid");QCOMPARE(library.quality(),QString("high"));
        library.setQuality("standard");
        QQmlApplicationEngine engine;engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *manager=window->findChild<QObject *>("petManagerDialog");QVERIFY(manager);
        QMetaObject::invokeMethod(manager,"open");QTRY_VERIFY(manager->property("opened").toBool());
        auto *button=manager->findChild<QQuickItem *>("petManagerSize");QVERIFY(button);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,button->mapToScene(QPointF(24,24)).toPoint());
        auto *dialog=manager->findChild<QObject *>("petSizeDialog");QVERIFY(dialog);
        QTRY_VERIFY(dialog->property("opened").toBool());
        auto *slider=dialog->findChild<QQuickItem *>("petSizeSlider");QVERIFY(slider && slider->height()>=48);
        auto *quality=dialog->findChild<QObject *>("petQualitySelector");QVERIFY(quality);
        quality->setProperty("currentIndex",2);
        QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,2));
        {PetLibrary reopened;QCOMPARE(reopened.quality(),QString("high"));}
        quality->setProperty("currentIndex",1);QMetaObject::invokeMethod(quality,"activated",Q_ARG(int,1));
        auto *character=window->findChild<QQuickItem *>("desktopPetCharacter");QVERIFY(character);
        const qreal initialWidth=character->width();
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,slider->mapToScene(QPointF(slider->width()*0.85,slider->height()/2)).toPoint());
        QTRY_VERIFY(character->width()>initialWidth*1.5);
        if(qEnvironmentVariableIsSet("ARKPETS_MANAGER_SCREENSHOTS")) {
            const QString dir=qEnvironmentVariable("ARKPETS_MANAGER_SCREENSHOTS");QDir().mkpath(dir);
            QVERIFY(window->grabWindow().save(dir+"/size-control.png"));
        }
        auto *reset=dialog->findChild<QQuickItem *>("petSizeReset");QVERIFY(reset);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,reset->mapToScene(QPointF(reset->width()/2,reset->height()/2)).toPoint());
        QTRY_COMPARE(character->width(),initialWidth);
        {PetLibrary reopened;QCOMPARE(reopened.sizeMultiplier(),1.0);}
    }
    void responsiveListDetail() {
        QFETCH(QSize,size); QFETCH(QString,language);
        ManagerLanguage tools; tools.language=language;
        QQmlApplicationEngine engine;
        engine.rootContext()->setContextProperty("systemTools", &tools);
        engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window); window->resize(size);
        auto *manager=window->findChild<QObject *>("petManagerDialog"); QVERIFY(manager);
        manager->setProperty("sourceRows", rows());
        QVERIFY(QMetaObject::invokeMethod(manager,"open"));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(manager->property("opened").toBool());
        QTRY_COMPARE(array(manager->property("characters")).size(),3);
         QVERIFY(QMetaObject::invokeMethod(manager,"selectCharacter",Q_ARG(QVariant,QString("阿米娅"))));
        QTRY_COMPARE(array(manager->property("models")).size(),4);
         auto *downloaded=manager->findChild<QQuickItem *>("petDownloadedRoleFilter");QVERIFY(downloaded);
         auto *notDownloaded=manager->findChild<QQuickItem *>("petNotDownloadedRoleFilter");QVERIFY(notDownloaded);
         if(manager->property("shortScreen").toBool()) {
            auto *filterButton=manager->findChild<QQuickItem *>("petManagerCompactFilters");QVERIFY(filterButton&&filterButton->isVisible()&&filterButton->height()>=48);
            auto *filterMenu=manager->findChild<QObject *>("petDownloadStatusMenu");QVERIFY(filterMenu);
             auto *downloadedMenu=manager->findChild<QObject *>("petDownloadedRoleFilterMenu");QVERIFY(downloadedMenu);
             auto *notDownloadedMenu=manager->findChild<QObject *>("petNotDownloadedRoleFilterMenu");QVERIFY(notDownloadedMenu);
            QVERIFY(downloadedMenu->property("checkable").toBool()&&notDownloadedMenu->property("checkable").toBool());
             const auto toggleFilter=[&](const char *itemName,bool showDownloaded,bool showNotDownloaded,int characterCount) {
                QVERIFY(QMetaObject::invokeMethod(filterButton,"clicked"));
                QTRY_VERIFY(filterMenu->property("visible").toBool());
                auto *item=manager->findChild<QQuickItem *>(itemName);QVERIFY(item&&item->isVisible());
                QTest::qWait(120);
                QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,item->mapToScene(QPointF(item->width()/2,item->height()/2)).toPoint());
                 QTRY_COMPARE(manager->property("showDownloadedCharacters").toBool(),showDownloaded);
                 QTRY_COMPARE(manager->property("showNotDownloadedCharacters").toBool(),showNotDownloaded);
                 QTRY_COMPARE(array(manager->property("characters")).size(),characterCount);
             };
             toggleFilter("petDownloadedRoleFilterMenu",false,true,2);
             toggleFilter("petDownloadedRoleFilterMenu",true,true,3);
             toggleFilter("petNotDownloadedRoleFilterMenu",true,false,1);
             toggleFilter("petNotDownloadedRoleFilterMenu",true,true,3);
         } else if(!manager->property("compact").toBool()) {
            QVERIFY(downloaded->height()>=48&&notDownloaded->height()>=48);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,downloaded->mapToScene(QPointF(downloaded->width()/2,downloaded->height()/2)).toPoint());
             QTRY_COMPARE(array(manager->property("characters")).size(),2);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,downloaded->mapToScene(QPointF(downloaded->width()/2,downloaded->height()/2)).toPoint());
             QTRY_COMPARE(array(manager->property("characters")).size(),3);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,notDownloaded->mapToScene(QPointF(notDownloaded->width()/2,notDownloaded->height()/2)).toPoint());
             QTRY_COMPARE(array(manager->property("characters")).size(),1);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,notDownloaded->mapToScene(QPointF(notDownloaded->width()/2,notDownloaded->height()/2)).toPoint());
             QTRY_COMPARE(array(manager->property("characters")).size(),3);
         } else {
             QVERIFY(!downloaded->isVisible());
             QVERIFY(!notDownloaded->isVisible());
         }
        auto *category=manager->findChild<QObject *>("petModelCategory"); QVERIFY(category);
        category->setProperty("currentIndex",1); QTRY_COMPARE(array(manager->property("models")).size(),1);
        category->setProperty("currentIndex",2); QTRY_COMPARE(array(manager->property("models")).size(),2);
        category->setProperty("currentIndex",3); QTRY_COMPARE(array(manager->property("models")).size(),1);
        category->setProperty("currentIndex",2);
        auto *search=manager->findChild<QObject *>("petManagerSearch"); QVERIFY(search);
        search->setProperty("text","winter");
        QTRY_COMPARE(array(manager->property("characters")).size(),1);
        QTRY_COMPARE(array(manager->property("models")).size(),1);
        QCOMPARE(manager->property("selectedModelId").toString(),QString("002_amiya_winter#1"));
        // Selecting a remote row only opens an explicit license confirmation.
        QVERIFY(QMetaObject::invokeMethod(manager,"applySelection"));
        auto *consent=manager->findChild<QObject *>("petDownloadConsent"); QVERIFY(consent);
        QTRY_VERIFY(consent->property("visible").toBool());
        QTRY_VERIFY(consent->property("opened").toBool());
        QVERIFY(consent->property("width").toDouble()<=size.width());
        QVERIFY(consent->property("height").toDouble()<=size.height());
        if (qEnvironmentVariableIsSet("ARKPETS_MANAGER_SCREENSHOTS")) {
            const QString dir=qEnvironmentVariable("ARKPETS_MANAGER_SCREENSHOTS"); QDir().mkpath(dir);
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(dir+'/'+language+QString::number(size.width())+"x"+QString::number(size.height())+"-consent.png"));
        }
        QVERIFY(QMetaObject::invokeMethod(consent,"close"));
        QTRY_VERIFY(!consent->property("visible").toBool());
        search->setProperty("text",""); category->setProperty("currentIndex",0);
        QTest::qWait(60);
        const bool compact=manager->property("compact").toBool();
        QCOMPARE(compact,size.width()<700);
        if (compact) {
            QVERIFY(manager->property("showModels").toBool());
            auto *back=manager->findChild<QQuickItem *>("petManagerBack"); QVERIFY(back && back->isVisible());
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,back->mapToScene(QPointF(back->width()/2,back->height()/2)).toPoint());
            QTRY_VERIFY(!manager->property("showModels").toBool());
            QMetaObject::invokeMethod(manager,"selectCharacter",Q_ARG(QVariant,QString("阿米娅")));
        }
        manager->setProperty("selectedModelId",QString("002_amiya_winter#1"));
        QCOMPARE(manager->property("surface").value<QColor>(),QColor("#08121f"));
        QCOMPARE(manager->property("primary").value<QColor>(),QColor("#85deff"));
        if(!compact) {
            auto *maximize=manager->findChild<QQuickItem *>("petManagerMaximize");QVERIFY(maximize);
            QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,maximize->mapToScene(QPointF(24,24)).toPoint());
            QTRY_VERIFY(manager->property("maximized").toBool());
            QTRY_COMPARE(manager->property("width").toInt(),size.width());
            QVERIFY(manager->property("y").toDouble()+manager->property("height").toDouble()<=size.height()-64+1);
            manager->setProperty("maximized",false);
        }
        for (const auto &name : {"petManagerClose","petManagerRefresh","petManagerSearch","petManagerApply"}) {
            auto *item=manager->findChild<QQuickItem *>(name); QVERIFY(item);
            QVERIFY2(item->height()>=48,qPrintable(QString(name)+" touch target below 48"));
            const QPointF p=item->mapToScene(QPointF());
            QVERIFY2(p.x()>=0 && p.y()>=0 && p.x()+item->width()<=size.width()+1 && p.y()+item->height()<=size.height()+1,name);
        }
        auto *list=manager->findChild<QQuickItem *>("petModelList"); QVERIFY(list);
        QVERIFY2(list->height()>=48,"No usable model list viewport on short screen");
        QTest::qWait(100);
        auto *apply=manager->findChild<QQuickItem *>("petManagerApply"); QVERIFY(apply);
        QVERIFY(apply->isEnabled());
        auto *actionLabel=apply->property("contentItem").value<QQuickItem *>(); QVERIFY(actionLabel);
        QCOMPARE(actionLabel->property("color").value<QColor>(),QColor("#081b29"));
        const QImage capture=window->grabWindow(); QVERIFY(!capture.isNull());
        // Native GL text uses RGB subpixel antialiasing, so English thin glyphs
        // rarely contain exact solid-color pixels. Inspect the label interior,
        // excluding rounded edges/shadows, as well as its declared ink above.
        const QRect buttonRect=apply->mapRectToScene(apply->boundingRect()).toAlignedRect().adjusted(20,12,-20,-12).intersected(capture.rect());
        int darkInkPixels=0;
        for (int y=buttonRect.top();y<=buttonRect.bottom();++y)
            for (int x=buttonRect.left();x<=buttonRect.right();++x) {
                const QColor c=capture.pixelColor(x,y);
                if (c.red()<120 && c.green()<160 && c.blue()<180) ++darkInkPixels;
            }
        QVERIFY2(darkInkPixels>40,"Filled primary action needs a visibly contrasting dark label on the shared system accent");
        if (qEnvironmentVariableIsSet("ARKPETS_MANAGER_SCREENSHOTS")) {
            const QString dir=qEnvironmentVariable("ARKPETS_MANAGER_SCREENSHOTS"); QDir().mkpath(dir);
            QTest::qWait(120);
            QVERIFY(window->grabWindow().save(dir+'/'+language+QString::number(size.width())+"x"+QString::number(size.height())+".png"));
        }
    }
    void taskbarAndActualVirtualKeyboard() {
        SystemTools tools;
        QQmlEngine engine;engine.addImportPath("qrc:/qt/qml");
        engine.rootContext()->setContextProperty("systemTools",&tools);
        QQmlComponent component(&engine);
        component.setData(R"(
import QtQuick
import QtQuick.Controls
import OrangePiTerminal
ApplicationWindow {
    id:win;width:1024;height:600;visible:true
    footer:Rectangle {height:64
        Button {objectName:"taskbarKeyboard";text:"Keyboard";focusPolicy:Qt.NoFocus;onClicked:kb.show()}
    }
    PetManager {id:manager;parent:Overlay.overlay;availableBottom:win.contentItem.height;virtualKeyboard:kb;onKeyboardRequested:kb.show()}
    FloatingKeyboard {id:kb;hostWindow:win;z:2000}
    Component.onCompleted:manager.open()
})",QUrl());
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());QVERIFY2(object,qPrintable(component.errorString()));
        auto *window=qobject_cast<QQuickWindow *>(object.data());QVERIFY(window);
        QVERIFY(QTest::qWaitForWindowExposed(window));window->requestActivate();
        auto *manager=window->findChild<QObject *>("petManagerDialog");QVERIFY(manager);
        QTRY_VERIFY(manager->property("opened").toBool());
        auto *search=manager->findChild<QQuickItem *>("petManagerSearch");QVERIFY(search);
        auto *keyboardButton=manager->findChild<QQuickItem *>("petManagerKeyboard");QVERIFY(keyboardButton);
        auto *keyboard=window->findChild<QObject *>("floatingKeyboard");QVERIFY(keyboard);
        QVERIFY(manager->property("y").toDouble()+manager->property("height").toDouble()<=536);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,keyboardButton->mapToScene(QPointF(24,24)).toPoint());
        QTRY_VERIFY(keyboard->property("visible").toBool());
        QTRY_VERIFY(manager->property("y").toDouble()+manager->property("height").toDouble()<=keyboard->property("y").toDouble());
        // Exercise the real input-method path, not setProperty(search.text).
        keyboard->setProperty("chineseMode",false);
        QVERIFY(QMetaObject::invokeMethod(keyboard,"typeLetter",Q_ARG(QVariant,QString("a"))));
        QTRY_COMPARE(search->property("text").toString(),QString("a"));
        QVERIFY(QMetaObject::invokeMethod(keyboard,"backspace"));
        QTRY_COMPARE(search->property("text").toString(),QString());
        keyboard->setProperty("chineseMode",true);
        keyboard->setProperty("pinyin",QString("shuiyue"));
        QVERIFY(QMetaObject::invokeMethod(keyboard,"ensureDictionaryLoaded"));
        // Commit through the same real IME path used by a candidate button.
        QVERIFY(QMetaObject::invokeMethod(keyboard,"commitCandidate",Q_ARG(QVariant,QString("水月"))));
        QTRY_COMPARE(search->property("text").toString(),QString("水月"));
        QVERIFY(QMetaObject::invokeMethod(keyboard,"hide"));
        QTRY_VERIFY(!keyboard->property("visible").toBool());
        QTRY_VERIFY(manager->property("height").toDouble()>400);
        // The manager must not swallow input to the system taskbar underneath.
        auto *taskbar=window->findChild<QQuickItem *>("taskbarKeyboard");QVERIFY(taskbar);
        QTest::mouseClick(window,Qt::LeftButton,Qt::NoModifier,taskbar->mapToScene(QPointF(taskbar->width()/2,taskbar->height()/2)).toPoint());
        QTRY_VERIFY(keyboard->property("visible").toBool());
        if(qEnvironmentVariableIsSet("ARKPETS_MANAGER_SCREENSHOTS")) {
            QTest::qWait(100);
            QVERIFY(window->grabWindow().save(qEnvironmentVariable("ARKPETS_MANAGER_SCREENSHOTS")+"/virtual-keyboard.png"));
        }
    }
};
QTEST_MAIN(PetManagerTests)
#include "pet_manager_test.moc"
