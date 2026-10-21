#include <QtTest>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickWindow>
#include <QQuickStyle>
#include <QImage>
#include <QFile>
#include <QDir>
#include <QStandardPaths>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <functional>
#include "ui/TerminalUi.h"
#include "ui/PetSpriteItem.h"
#include "ui/PetLibrary.h"

class DesktopPetTests : public QObject {
    Q_OBJECT
    QTemporaryDir isolated_;
private slots:
    void initTestCase() {
        QVERIFY(isolated_.isValid());
        qputenv("XDG_CONFIG_HOME", (isolated_.path()+"/config").toUtf8());
        qputenv("XDG_DATA_HOME", (isolated_.path()+"/data").toUtf8());
        QQuickStyle::setStyle("Material");
        initializeTerminalUi();
    }
    void assetsAndBehaviour() {
        for (const auto &name : {"idle", "walk", "interact", "sit", "sleep"}) {
            QImage sheet(QString(":/arkpets/amiya/%1.png").arg(name));
            QVERIFY(!sheet.isNull());
            QCOMPARE(sheet.width(), 256 * 8);
            QVERIFY(sheet.hasAlphaChannel());
            int opaque = 0;
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                    if (qAlpha(sheet.pixel(x, y)) > 100) ++opaque;
            QVERIFY(opaque > 2000);
        }
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/OrangePiTerminal/DesktopPet.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> pet(component.create());
        QVERIFY2(pet, qPrintable(component.errorString()));
        QQuickWindow host;
        qobject_cast<QQuickItem *>(pet.data())->setParentItem(host.contentItem());
        pet->setProperty("active",true); // This isolated behavior fixture has no Main.qml host objects.
        host.show();
        pet->setProperty("width", 1024); pet->setProperty("height", 600);
        pet->setProperty("floorY", 536); pet->setProperty("petX", 100);
        pet->setProperty("action", "walk");
        QVERIFY(QMetaObject::invokeMethod(pet.data(), "advance", Q_ARG(QVariant, 0.1)));
        QVERIFY(pet->property("petX").toDouble() > 100);
        pet->setProperty("petX", 1024); // Clamp and turn on hitting right boundary.
        QMetaObject::invokeMethod(pet.data(), "advance", Q_ARG(QVariant, 0.1));
        QCOMPARE(pet->property("direction").toInt(), -1);
        QVERIFY(pet->property("petX").toDouble() <= pet->property("maximumX").toDouble());
        pet->setProperty("width", 320);
        QVERIFY(pet->property("petX").toDouble() <= pet->property("maximumX").toDouble());
        QSignalSpy interacted(pet.data(), SIGNAL(interacted()));
        QVERIFY(QMetaObject::invokeMethod(pet.data(), "interact"));
        QCOMPARE(interacted.count(), 1);
        QCOMPARE(pet->property("action").toString(), QString("interact"));
        QMetaObject::invokeMethod(pet.data(), "interact");
        QCOMPARE(interacted.count(), 1); // No stacked/restarting interaction spam.
        QMetaObject::invokeMethod(pet.data(), "finishInteraction");
        QCOMPARE(pet->property("action").toString(), QString("idle"));
        pet->setProperty("active", false);
        const qreal x = pet->property("petX").toDouble();
        QMetaObject::invokeMethod(pet.data(), "advance", Q_ARG(QVariant, 0.1));
        QCOMPARE(pet->property("petX").toDouble(), x);
    }
    void stationaryPoseAnchorDoesNotJitter() {
        PetSpriteItem sprite;
        const QUrl folder = qEnvironmentVariableIsSet("ARKPETS_TEST_MODEL")
            ? QUrl::fromLocalFile(qEnvironmentVariable("ARKPETS_TEST_MODEL"))
            : QUrl("qrc:/arkpets/amiya");
        sprite.setFolder(folder);
        QVERIFY2(sprite.ready(), qPrintable(sprite.error()));
        double commonAnchor = -1;
        const QStringList actionNames = {"idle", "walk", "interact", "sit", "sleep"};
        for (const QString &action : actionNames) {
            sprite.setAction(action);
            const double anchor = sprite.frameFootY();
            if (commonAnchor < 0) commonAnchor = anchor;
            QCOMPARE(anchor, commonAnchor); // Switching pose never moves the root.
            const int count = sprite.info().value("actions").toMap().value(action).toMap().value("frames").toInt();
            QVERIFY(count > 1);
            for (int frame = 0; frame < count; ++frame) {
                sprite.setCurrentFrame(frame);
                QCOMPARE(sprite.frameFootY(), anchor);
            }
            const auto feet = sprite.info().value("actions").toMap().value(action).toMap().value("frameFeet").toList();
            QVERIFY(!feet.isEmpty());
            QList<int> sortedFeet;
            for (const auto &foot : feet) sortedFeet.append(foot.toInt());
            std::sort(sortedFeet.begin(), sortedFeet.end());
            const int actionIndex = actionNames.indexOf(action);
            const auto offsets = sprite.info().value("actionOffsetsY").toList();
            const auto poseOffsets = sprite.info().value("poseOffsetsY").toList();
            QCOMPARE(poseOffsets.size(),actionNames.size());
            QCOMPARE(poseOffsets.at(actionIndex).toInt(),action=="sit"?50:action=="sleep"?25:5);
            const auto contacts = sprite.info().value("actionContactY").toList();
            QCOMPARE(contacts.size(),actionNames.size());
            const auto frameContacts=sprite.info().value("actions").toMap().value(action).toMap().value("frameContacts").toList();
            QList<int> sortedSupport;for(const auto &contact:frameContacts)sortedSupport.append(contact.toInt());
            std::sort(sortedSupport.begin(),sortedSupport.end());QVERIFY(!sortedSupport.isEmpty());
            const int referenceContact=sortedSupport[sortedSupport.size()/2];
            if(action=="sit") {
                const double reference=sprite.info().value("referenceHeight").toDouble();
                const double target=sprite.info().value("targetHeight").toDouble();
                QCOMPARE(sortedFeet[sortedFeet.size()/2]-referenceContact,qRound(50*reference/target));
            }
            QCOMPARE(contacts.at(actionIndex).toInt(),referenceContact);
            QCOMPARE(referenceContact + offsets.at(actionIndex).toInt(),qRound(commonAnchor));
        }
    }
    void directionMirroringFollowsTravel() {
        QQmlEngine engine;
        QQmlComponent component(&engine, QUrl("qrc:/qt/qml/OrangePiTerminal/DesktopPet.qml"));
        QVERIFY2(component.isReady(), qPrintable(component.errorString()));
        QScopedPointer<QObject> object(component.create());
        QVERIFY2(object, qPrintable(component.errorString()));
        auto *pet = qobject_cast<QQuickItem *>(object.data()); QVERIFY(pet);
        PetSpriteItem *sprite = nullptr;
        std::function<void(QQuickItem *)> findSprite = [&](QQuickItem *item) {
            if (auto *candidate = qobject_cast<PetSpriteItem *>(item)) { sprite = candidate; return; }
            for (auto *child : item->childItems()) if (!sprite) findSprite(child);
        };
        findSprite(pet); QVERIFY(sprite);
        pet->setProperty("direction", 1);
        QCOMPARE(sprite->mirrored(), false); // rightward travel keeps source orientation
        pet->setProperty("direction", -1);
        QCOMPARE(sprite->mirrored(), true);  // leftward travel mirrors it
    }
    void realWindowClickLayerAndResize() {
        QQmlApplicationEngine engine;
        engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        QVERIFY(window);
        QVERIFY(!window->property("desktopPetEnabled").toBool());
        window->setProperty("desktopPetEnabled",true);
        auto *pet = window->findChild<QQuickItem *>("desktopPetOverlay");
        auto *character = window->findChild<QQuickItem *>("desktopPetCharacter");
        auto *button = window->findChild<QQuickItem *>("backgroundButton");
        auto *sprite = window->findChild<PetSpriteItem *>("desktopPetSprite");
        QVERIFY(pet && character && button && sprite);
        pet->setProperty("roaming", false);
        QTRY_VERIFY(pet->property("assetsReady").toBool());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QCOMPARE(sprite->renderTarget(), window->rendererInterface()->graphicsApi() == QSGRendererInterface::OpenGL
                     ? QQuickPaintedItem::FramebufferObject : QQuickPaintedItem::Image);
        QVERIFY(qAbs(character->y()+pet->property("poseFootY").toDouble()*pet->property("renderScale").toDouble()-536)<0.01);
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
        QCOMPARE(window->property("backgroundClicks").toInt(), 1); // Empty overlay passes through.
        QSignalSpy interacted(pet, SIGNAL(interacted()));
        QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, character->mapToScene(QPointF(character->width()/2, character->height()*0.55)).toPoint());
        QTRY_COMPARE(interacted.count(), 1);
        QTRY_COMPARE_WITH_TIMEOUT(pet->property("action").toString(), QString("idle"), 3000);
        const int frame = pet->property("frameIndex").toInt();
        QTRY_VERIFY(pet->property("frameIndex").toInt() != frame);
        QVERIFY(character->contains(QPointF(character->width()/2, character->height()/2)));
        QVERIFY(!character->contains(QPointF(1, 1))); // Empty corners are not hit targets.
        // Drag via actual mouse motion. A drag must not also fire a click.
        const QPoint press = character->mapToScene(QPointF(character->width()/2, character->height()*0.55)).toPoint();
        const qreal startX = pet->property("petX").toDouble();
        const int clicks = interacted.count();
        QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, press);
        QTest::mouseMove(window, press + QPoint(100, -80), 40);
        QTRY_VERIFY(pet->property("dragging").toBool());
        QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, press + QPoint(100, -80));
        QVERIFY(!pet->property("dragging").toBool());
        QVERIFY(pet->property("petX").toDouble() > startX + 50);
        QCOMPARE(interacted.count(), clicks);
        QVERIFY(pet->property("falling").toBool());
        QTRY_VERIFY_WITH_TIMEOUT(!pet->property("falling").toBool(),3000);
        QVERIFY(qAbs(character->y()+pet->property("poseFootY").toDouble()*pet->property("renderScale").toDouble()-536)<0.01);
        const auto oldFolder = sprite->folder();
        sprite->setFolder(QUrl::fromLocalFile("/nonexistent-invalid-character"));
        QVERIFY(!sprite->error().isEmpty());
        QCOMPARE(sprite->folder(), oldFolder); // Failed load keeps old decoded frames.
        window->resize(480, 400);
        QTRY_VERIFY(pet->property("petX").toDouble() <= pet->property("maximumX").toDouble());
        QTRY_VERIFY(qAbs(character->y()+pet->property("poseFootY").toDouble()*pet->property("renderScale").toDouble()-336)<0.01);
        window->resize(1024, 600);
        QTest::qWait(300);
        const QImage capture = window->grabWindow();
        QVERIFY(!capture.isNull());
        // A pet-shaped set of non-background pixels must actually appear ON
        // TOP of the ordinary application rectangle, not merely exist in QML.
        const QPoint origin = character->mapToScene(QPointF()).toPoint();
        int drawn = 0;
        for (int y = 10; y < 95; ++y)
            for (int x = 10; x < 98; ++x) {
                const QColor color = capture.pixelColor(origin + QPoint(x, y));
                if (color != QColor("#243950")) ++drawn;
            }
        QVERIFY2(drawn > 1000, "Pet is not visibly composited above the application surface");
        if (qEnvironmentVariableIsSet("ARKPETS_SCREENSHOT"))
            QVERIFY(capture.save(qEnvironmentVariable("ARKPETS_SCREENSHOT")));
    }
    void gravityAndBehaviorStates() {
        QQmlApplicationEngine engine;
        engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->setProperty("desktopPetEnabled",true);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *pet=window->findChild<QQuickItem *>("desktopPetOverlay"); QVERIFY(pet);
        pet->setProperty("roaming",false);pet->setProperty("reducedMotion",true);
        auto *sprite=window->findChild<PetSpriteItem *>("desktopPetSprite");QVERIFY(sprite);
        const double ground=pet->property("groundY").toDouble();
        QMetaObject::invokeMethod(pet,"beginDrag");
        QMetaObject::invokeMethod(pet,"moveDrag",Q_ARG(QVariant,200),Q_ARG(QVariant,ground-150));
        QMetaObject::invokeMethod(pet,"endDrag");
        QVERIFY(pet->property("falling").toBool());
        const double y=pet->property("petY").toDouble();
        QMetaObject::invokeMethod(pet,"advancePhysics",Q_ARG(QVariant,0.1));
        const double d1=pet->property("petY").toDouble()-y;
        QMetaObject::invokeMethod(pet,"advancePhysics",Q_ARG(QVariant,0.1));
        const double d2=pet->property("petY").toDouble()-y-d1;
        QVERIFY(d1>0);QVERIFY(d2>d1); // Acceleration, not a linear/snap animation.
        for(int i=0;i<20;++i)QMetaObject::invokeMethod(pet,"advancePhysics",Q_ARG(QVariant,0.1));
        QVERIFY(!pet->property("falling").toBool());
        QCOMPARE(pet->property("petY").toDouble(),ground);
        QCOMPARE(pet->property("velocityY").toDouble(),0.0);
        for(const QString &state:{QString("walk"),QString("sit"),QString("sleep")}) {
            QVariant result;
            QVERIFY(QMetaObject::invokeMethod(pet,"setBehavior",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,state)));
            QVERIFY(result.toBool());QCOMPARE(pet->property("action").toString(),state);
            if(state=="sit") {
                const auto poseOffsets=sprite->info().value("poseOffsetsY").toList();
                QCOMPARE(poseOffsets.at(3).toInt(),50);
                const auto contacts=sprite->info().value("actionContactY").toList();
                const auto sitFeet=sprite->info().value("actions").toMap().value("sit").toMap().value("frameFeet").toList();
                QVERIFY(!sitFeet.isEmpty());
                QVERIFY(sitFeet.at(sitFeet.size()/2).toInt()-contacts.at(3).toInt()>20);
            }
            if(qEnvironmentVariableIsSet("ARKPETS_BEHAVIOR_SCREENSHOTS")) {
                auto *sprite=window->findChild<PetSpriteItem *>("desktopPetSprite");QVERIFY(sprite);
                const int count=sprite->info().value("actions").toMap().value(state).toMap().value("frames").toInt();
                pet->setProperty("frameIndex",count/2);QTest::qWait(30);
                const QString dir=qEnvironmentVariable("ARKPETS_BEHAVIOR_SCREENSHOTS");QDir().mkpath(dir);
                QVERIFY(window->grabWindow().save(dir+'/'+state+".png"));
            }
        }
        QMetaObject::invokeMethod(pet,"chooseBehavior",Q_ARG(QVariant,0.8));
        QCOMPARE(pet->property("action").toString(),QString("sit"));
        QMetaObject::invokeMethod(pet,"chooseBehavior",Q_ARG(QVariant,0.95));
        QCOMPARE(pet->property("action").toString(),QString("sleep"));
        QMetaObject::invokeMethod(pet,"beginDrag");
        QMetaObject::invokeMethod(pet,"moveDrag",Q_ARG(QVariant,200),Q_ARG(QVariant,0));
        QMetaObject::invokeMethod(pet,"endDrag");
        window->resize(480,320);
        for(int i=0;i<30;++i)QMetaObject::invokeMethod(pet,"advancePhysics",Q_ARG(QVariant,0.1));
        QVERIFY(!pet->property("falling").toBool());
        QVERIFY(pet->property("petX").toDouble()<=pet->property("maximumX").toDouble());
    }
    void scalingKeepsGroundAndBounds() {
        QQmlApplicationEngine engine;engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());QVERIFY(window);
        window->setProperty("desktopPetEnabled",true);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *pet=window->findChild<QQuickItem *>("desktopPetOverlay");
        auto *character=window->findChild<QQuickItem *>("desktopPetCharacter");QVERIFY(pet&&character);
        pet->setProperty("roaming",false);pet->setProperty("petX",900);
        for(double scale:{0.375,0.75,1.5}) {
            pet->setProperty("petScale",scale);QTest::qWait(20);
            QVERIFY(pet->property("petX").toDouble()<=pet->property("maximumX").toDouble());
            const double foot=pet->property("poseFootY").toDouble()*pet->property("renderScale").toDouble();
            QVERIFY(qAbs(character->y()+foot-pet->property("floorY").toDouble())<0.01);
        }
        window->resize(320,320);pet->setProperty("petScale",2.0);QTest::qWait(30);
        QVERIFY(character->width()<=pet->width()+0.01);
        QVERIFY(character->height()<=pet->property("floorY").toDouble()+0.01);
        QMetaObject::invokeMethod(pet,"beginDrag");
        QMetaObject::invokeMethod(pet,"moveDrag",Q_ARG(QVariant,100),Q_ARG(QVariant,0));
        pet->setProperty("petScale",0.5);
        QMetaObject::invokeMethod(pet,"endDrag");
        for(int i=0;i<50;++i)QMetaObject::invokeMethod(pet,"advancePhysics",Q_ARG(QVariant,0.1));
        QVERIFY(!pet->property("falling").toBool());
        QVERIFY(qAbs(character->y()-pet->property("groundY").toDouble())<0.01);
    }
    void noBlankFramesAcrossEveryCellAndTransition() {
        QQmlApplicationEngine engine;
        engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        window->setProperty("desktopPetEnabled",true);
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *pet = window->findChild<QQuickItem *>("desktopPetOverlay");
        auto *character = window->findChild<QQuickItem *>("desktopPetCharacter");
        QVERIFY(pet && character);
        pet->setProperty("roaming", false); pet->setProperty("reducedMotion", true);
        pet->setProperty("petX", 400);
        // Stop the controller and directly enumerate ALL atlas frames, then
        // switch action/direction repeatedly: regression for blank end cells.
        auto *sprite = window->findChild<PetSpriteItem *>("desktopPetSprite");
        QVERIFY(sprite && sprite->ready());
        auto *clock=window->findChild<QObject *>("desktopPetClock");QVERIFY(clock);clock->setProperty("running",false);
        const qreal fixedCanvasY = character->y();
        pet->setOpacity(0);QTest::qWait(30);const QImage background=window->grabWindow();pet->setOpacity(1);
        for (int cycle = 0; cycle < 2; ++cycle)
            for (const QString &action : {QString("idle"), QString("walk"), QString("interact"),QString("sit"),QString("sleep")}) {
                pet->setProperty("action",action);sprite->setMirrored(cycle == 1);
                QCOMPARE(pet->property("clip").toBool(),action!="sit");
                const int actionIndex=QStringList{"idle","walk","interact","sit","sleep"}.indexOf(action);
                const auto actionOffsets=pet->property("animationInfo").toMap().value("actionOffsetsY").toList();
                const auto actionInfo=pet->property("animationInfo").toMap().value("actions").toMap().value(action).toMap();
                const auto frameFeet=actionInfo.value("frameFeet").toList();
                const auto frameContacts=actionInfo.value("frameContacts").toList();
                const double actionOffset=actionOffsets.at(actionIndex).toDouble();
                QVERIFY2(qAbs(character->y()-fixedCanvasY)<0.01,
                          qPrintable(action+" moved the fixed transparent canvas"));
                QVERIFY2(qAbs(character->y()+sprite->frameFootY()*pet->property("renderScale").toDouble()
                              -pet->property("floorY").toDouble())<0.01,
                          qPrintable(action+" changed the fixed canvas ground anchor"));
                const int count = sprite->info().value("actions").toMap().value(action).toMap().value("frames").toInt();
                double stationaryTop = -1;
                for (int frame = 0; frame < count; ++frame) {
                    sprite->setCurrentFrame(frame);
                    QTest::qWait(2);
                    if (action == "sit" || action == "sleep") {
                        if (stationaryTop < 0) stationaryTop = character->y();
                        else QVERIFY2(qAbs(character->y()-stationaryTop)<0.01,
                                      qPrintable(action+" root moved between animation frames"));
                    }
                    const double scale=pet->property("renderScale").toDouble();
                    const double floor=pet->property("floorY").toDouble();
                    const double contactY=frameContacts.at(frame).toDouble();
                    const double contact=character->y()+(contactY+actionOffset)*scale;
                    QVERIFY2(qAbs(contact-floor)<=3.1*scale,qPrintable(action+" contact anchor drifted in frame "+QString::number(frame)));
                    const double foot=character->y()+(frameFeet.at(frame).toDouble()+actionOffset)*scale;
                    if(action=="sit")QVERIFY(foot>floor+20*scale);
                    if(action=="sleep")QVERIFY(contact<=floor+3.1*scale&&contact>=floor-3.1*scale);
                    const QImage capture = window->grabWindow();
                    QVERIFY(!capture.isNull());
                    const int dockY=qBound(0,qRound(pet->property("floorY").toDouble()),capture.height());
                    if(action=="sit") {
                        const QPoint origin=character->mapToScene(QPointF()).toPoint();
                        int visibleLegPixels=0;
                        const int bottom=qMin(capture.height(),dockY+qRound(60*pet->property("renderScale").toDouble()));
                        for(int y=dockY+1;y<bottom;y+=2)for(int x=origin.x();x<origin.x()+character->width();x+=2) {
                            const QColor actual=capture.pixelColor(x,y),expected=background.pixelColor(x,y);
                            const int delta=qMax(qMax(qAbs(actual.red()-expected.red()),qAbs(actual.green()-expected.green())),
                                                 qAbs(actual.blue()-expected.blue()));
                            if(delta>12)++visibleLegPixels;
                        }
                        QVERIFY2(visibleLegPixels>20,"Sit pose legs are still clipped at the taskbar contact line");
                    } else {
                        for(int y=dockY;y<capture.height();y+=3)
                            for(int x=0;x<capture.width();x+=8) {
                                const QColor actual=capture.pixelColor(x,y),expected=background.pixelColor(x,y);
                                const int delta=qMax(qMax(qAbs(actual.red()-expected.red()),qAbs(actual.green()-expected.green())),
                                                     qMax(qAbs(actual.blue()-expected.blue()),qAbs(actual.alpha()-expected.alpha())));
                                QVERIFY2(delta<=3,qPrintable(action+" pet pixels painted over the taskbar at "+QString::number(x)+","+QString::number(y)));
                            }
                    }
                    const QPoint origin = character->mapToScene(QPointF()).toPoint();
                    int drawn = 0;
                    for (int y = 0; y < character->height(); ++y)
                        for (int x = 0; x < character->width(); ++x)
                            if (capture.rect().contains(origin+QPoint(x,y)) && capture.pixelColor(origin+QPoint(x,y)) != background.pixelColor(origin+QPoint(x,y))) ++drawn;
                    QVERIFY2(drawn > 500, qPrintable(action+" blank frame "+QString::number(frame)));
                    if (frame == 0) {
                        // On each action boundary, pixels transparent in the new
                        // sprite must show the scene underneath, not previous-frame fragments.
                        const QImage sheet(QString(":/arkpets/amiya/%1.png").arg(action));
                        QVERIFY(!sheet.isNull());
                        const int fw = sprite->info().value("frameWidth").toInt();
                        const int fh = sprite->info().value("frameHeight").toInt();
                        const int columns = sheet.width()/fw;
                        const QRect frameRect((frame%columns)*fw,(frame/columns)*fh,fw,fh);
                        const int offset = sprite->info().value("actionOffsetsY").toList().at(
                            QStringList{"idle","walk","interact","sit","sleep"}.indexOf(action)).toInt();
                        const double scale = character->width()/fw;
                        int transparentSamples = 0;
                        for (int y = 3; y < character->height()-3; y += 4)
                            for (int x = 3; x < character->width()-3; x += 4) {
                                const int sourceX = qBound(0, int((x+0.5)/scale), fw-1);
                                const int sourceY = int((y+0.5)/scale)-offset;
                                if (sourceY < 2 || sourceY >= fh-2) continue;
                                const int atlasX = cycle == 1 ? fw-1-sourceX : sourceX;
                                bool clear = true;
                                for (int dy = -1; dy <= 1 && clear; ++dy)
                                    for (int dx = -1; dx <= 1; ++dx)
                                        if (qAlpha(sheet.pixel(frameRect.x()+atlasX+dx,frameRect.y()+sourceY+dy)) != 0) {
                                            clear = false; break;
                                        }
                                if (!clear) continue;
                                ++transparentSamples;
                                const QColor actual = capture.pixelColor(origin+QPoint(x,y));
                                const QColor expected = background.pixelColor(origin+QPoint(x,y));
                                const int delta = qMax(qMax(qAbs(actual.red()-expected.red()),qAbs(actual.green()-expected.green())),
                                                       qMax(qAbs(actual.blue()-expected.blue()),qAbs(actual.alpha()-expected.alpha())));
                                QVERIFY2(delta <= 3, qPrintable(action+" left pixels in a transparent canvas area; delta="+QString::number(delta)));
                            }
                        QVERIFY(transparentSamples > 100);
                    }
                }
            }
    }
    void downloadedModel() {
        if (!qEnvironmentVariableIsSet("ARKPETS_TEST_MODEL")) QSKIP("Optional real downloaded model supplied by local end-to-end run");
        QQmlApplicationEngine engine;
        engine.load(QUrl::fromLocalFile(ARKPETS_PREVIEW_QML));
        QVERIFY(!engine.rootObjects().isEmpty());
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
        auto *pet = window->findChild<QQuickItem *>("desktopPetOverlay");
        auto *sprite = window->findChild<PetSpriteItem *>("desktopPetSprite");
        QVERIFY(pet && sprite);
        pet->setProperty("modelFolder", QUrl::fromLocalFile(qEnvironmentVariable("ARKPETS_TEST_MODEL")));
        QVERIFY2(sprite->error().isEmpty(), qPrintable(sprite->error()));
        QVERIFY(sprite->ready());
        QVERIFY(sprite->info().value("frameWidth").toInt()>=96);
        QVERIFY(sprite->info().value("frameWidth").toInt()<=384);
        QVERIFY(!sprite->info().value("model").toString().isEmpty());
        if(sprite->info().value("modelType").toString()=="DynIllust") {
            pet->setProperty("roaming",true);pet->setProperty("idleRemaining",0);
            const double x=pet->property("petX").toDouble();
            QMetaObject::invokeMethod(pet,"advance",Q_ARG(QVariant,0.1));
            QCOMPARE(pet->property("action").toString(),QString("idle"));
            QCOMPARE(pet->property("petX").toDouble(),x);
        }
        QCOMPARE(sprite->folder(), QUrl::fromLocalFile(qEnvironmentVariable("ARKPETS_TEST_MODEL")));
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QVERIFY(QMetaObject::invokeMethod(pet, "interact"));
        QTRY_COMPARE_WITH_TIMEOUT(pet->property("action").toString(), QString("idle"), 12000);
        QVERIFY(!window->grabWindow().isNull());
    }
    void legacyThreeActionModelDoesNotPretendToSupportPostures() {
        QTemporaryDir model;QVERIFY(model.isValid());
        QFile manifest(":/arkpets/amiya/animations.json");QVERIFY(manifest.open(QIODevice::ReadOnly));
        auto data=QJsonDocument::fromJson(manifest.readAll()).object();
        auto actions=data.value("actions").toObject();actions.remove("sit");actions.remove("sleep");
        data.insert("actions",actions);data.remove("conversionVersion");
        QFile target(model.path()+"/animations.json");QVERIFY(target.open(QIODevice::WriteOnly));
        target.write(QJsonDocument(data).toJson());target.close();
        for(const auto &name:{"idle","walk","interact"})QVERIFY(QFile::copy(QString(":/arkpets/amiya/%1.png").arg(name),model.path()+'/'+name+".png"));
        QQmlEngine engine;QQmlComponent component(&engine,QUrl("qrc:/qt/qml/OrangePiTerminal/DesktopPet.qml"));
        QVERIFY2(component.isReady(),qPrintable(component.errorString()));
        QScopedPointer<QObject> pet(component.create());QVERIFY(pet);
        pet->setProperty("modelFolder",QUrl::fromLocalFile(model.path()));
        QVERIFY(pet->property("assetsReady").toBool());
        QVariant result;
        QMetaObject::invokeMethod(pet.data(),"setBehavior",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QString("sit")));
        QVERIFY(!result.toBool());QCOMPARE(pet->property("action").toString(),QString("idle"));
        QMetaObject::invokeMethod(pet.data(),"setBehavior",Q_RETURN_ARG(QVariant,result),Q_ARG(QVariant,QString("sleep")));
        QVERIFY(!result.toBool());
    }
    void libraryWorkerCachedHandoffAndFailurePreservesSelection() {
        if (!qEnvironmentVariableIsSet("ARKPETS_TEST_MODEL")) QSKIP("Optional real download cache required");
        // Seed only this test's isolated AppLocalData. Exercise the real QProcess
        // worker handoff without another network transfer or Java conversion.
        const QString modelDir = qEnvironmentVariable("ARKPETS_TEST_MODEL");
        const QString sourceRoot = QFileInfo(modelDir).dir().absolutePath()+"/..";
        const QString root = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/arkpets";
        QDir().mkpath(root+"/characters");
        QFile sourceCatalog(sourceRoot+"/catalog.json"); QVERIFY(sourceCatalog.open(QIODevice::ReadOnly));
        QFile targetCatalog(root+"/catalog.json"); QVERIFY(targetCatalog.open(QIODevice::WriteOnly));
        targetCatalog.write(sourceCatalog.readAll()); targetCatalog.close();
        const QString key = QFileInfo(modelDir).fileName();
        QDir().mkpath(root+"/characters/"+key);
        for (const auto &name : {"idle.png", "walk.png", "interact.png", "sit.png", "sleep.png", "animations.json"}) {
            if(!QFile::exists(modelDir+'/'+name))continue;
            const QString to = root+"/characters/"+key+'/'+name;
            if (!QFile::exists(to)) QVERIFY(QFile::copy(modelDir+'/'+name, to));
        }
        QFile manifest(modelDir+"/animations.json"); QVERIFY(manifest.open(QIODevice::ReadOnly));
        const auto manifestData=QJsonDocument::fromJson(manifest.readAll()).object();
        const QString id = manifestData.value("model").toString();
        PetLibrary library;
        library.setQuality(manifestData.value("quality").toString("standard"));
        QVERIFY(library.hasCatalog());
        library.download(id, true);
        QTRY_VERIFY_WITH_TIMEOUT(!library.busy(), 15000);
        QVERIFY2(library.currentFolder().isLocalFile(), qPrintable(library.status()));
        QCOMPARE(library.currentFolder(), QUrl::fromLocalFile(root+"/characters/"+key));
        const QUrl selected = library.currentFolder();
        bool found = false;
        for (const auto &row : library.rows()) {
            const auto map = row.toMap();
            if (map.value("id").toString() != id) continue;
            found = true;
            QVERIFY(!map.value("character").toString().isEmpty());
            QVERIFY(!map.value("modelName").toString().isEmpty());
            QVERIFY(map.value("installed").toBool());
            QVERIFY(map.value("current").toBool());
        }
        QVERIFY(found);
        library.resetToAmiya();
        QCOMPARE(library.currentModelId(), QString("002_amiya"));
        library.useInstalled(id);
        QCOMPARE(library.currentFolder(), selected);
        QCOMPARE(library.currentModelId(), id);
        library.download("../not-a-model", true);
        QTRY_VERIFY_WITH_TIMEOUT(!library.busy(), 15000);
        QCOMPARE(library.currentFolder(), selected);
        QVERIFY(!library.status().isEmpty());
        library.resetToAmiya();
        library.setQuality("standard");
    }
    void normalizedVisibleHeightAcrossModels() {
        if(!qEnvironmentVariableIsSet("ARKPETS_TEST_MODEL"))QSKIP("Optional converted model required");
        PetSpriteItem sprite;
        for(const QUrl url:{QUrl("qrc:/arkpets/amiya"),QUrl::fromLocalFile(qEnvironmentVariable("ARKPETS_TEST_MODEL"))}) {
            sprite.setFolder(url);QVERIFY2(sprite.error().isEmpty(),qPrintable(sprite.error()));
            const auto info=sprite.info();
            const double scale=0.75*info.value("targetHeight").toDouble()/info.value("referenceHeight").toDouble();
            QCOMPARE(info.value("targetHeight").toInt(),176);
            QVERIFY(qAbs(info.value("referenceHeight").toDouble()*scale-132)<0.01);
        }
    }
};
QTEST_MAIN(DesktopPetTests)
#include "desktop_pet_test.moc"
