/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/Gamepad.h"
#include "touch/TouchController.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QWheelEvent>

namespace {
QQuickItem *findItem(QQuickItem *root, const QString &name)
{
  if(root->objectName() == name) return root;
  for(auto child : root->childItems())
    if(auto item = findItem(child, name)) return item;
  return nullptr;
}
}

int main(int argc, char **argv)
{
  qputenv("LANGUAGE", "C");
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QGuiApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("darktabletouch"));
  QCoreApplication::setOrganizationName(QStringLiteral("darktabletouch"));
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  QStringList core{app.applicationFilePath()};
  QString screenshot, selfTest, coverage;
  bool testCommandLine = false;
  const QStringList arguments = app.arguments();
  for(int i = 1; i < arguments.size(); i++)
  {
    if(arguments[i] == QStringLiteral("--screenshot") && i + 1 < arguments.size()) screenshot = arguments[++i];
    else if(arguments[i] == QStringLiteral("--coverage-report") && i + 1 < arguments.size()) coverage = arguments[++i];
    else if(arguments[i] == QStringLiteral("--self-test") && i + 1 < arguments.size()) selfTest = arguments[++i];
    else if(arguments[i] == QStringLiteral("--self-test-command-line")) testCommandLine = true;
    else core.append(arguments[i]);
  }
  if(core.contains(QStringLiteral("--version")) || core.contains(QStringLiteral("-v")))
  {
    QTextStream(stdout) << "DarktableTouch Touch UI\nengine " << dt_touch_version() << '\n';
    return 0;
  }
  if(core.contains(QStringLiteral("--help")) || core.contains(QStringLiteral("-h")))
  {
    QTextStream(stdout) << "usage: darktabletouch --ui=touch|classic [engine options] [photo paths]\n"
                       << "DarktableTouch uses U.S. English and preserves Unicode paths\n";
    return 0;
  }
  QTemporaryDir testDirectory;
  QString testPhoto, testNeighbor;
  if(!selfTest.isEmpty() || !screenshot.isEmpty()) qputenv("QT_FORCE_STDERR_LOGGING", "1");
  if(!selfTest.isEmpty())
  {
    const QDir temporary(testDirectory.path());
    const QString folder = temporary.filePath(QString::fromUtf8("ผู้ใช้/ทดสอบ ภาพถ่าย #100%/กำลัง_กําลัง_🌅"));
    QDir().mkpath(folder);
    testPhoto = QDir(folder).filePath(QString::fromUtf8("รูปที่ ๑🚀.") + QFileInfo(selfTest).suffix());
    if(!QFile::copy(selfTest, testPhoto))
    {
      QTextStream(stderr) << "could not copy the self-test RAW fixture\n";
      return 1;
    }
    const QString child = QDir(folder).filePath(QStringLiteral("child folder"));
    QDir().mkpath(child);
    testNeighbor = QDir(child).filePath(QStringLiteral("neighbor.") + QFileInfo(selfTest).suffix());
    if(!QFile::copy(selfTest, testNeighbor)) return 1;
    core << QStringLiteral("--configdir") << temporary.filePath(QString::fromUtf8("ผู้ใช้/ตั้งค่า"))
         << QStringLiteral("--cachedir") << temporary.filePath(QString::fromUtf8("ผู้ใช้/แคช"))
         << QStringLiteral("--library") << temporary.filePath(QString::fromUtf8("ผู้ใช้/ตั้งค่า/library.db"))
         << QStringLiteral("--disable-opencl") << QStringLiteral("--conf") << QStringLiteral("host_memory_limit=2048")
         << QStringLiteral("--conf") << QStringLiteral("worker_threads=2") << QStringLiteral("-t") << QStringLiteral("2");
    if(testCommandLine) core << testPhoto;
  }
  auto store = new PreviewStore;
  TouchController controller(store);
  Gamepad gamepad;
  bool reportFailed = false;
  QQmlApplicationEngine engine;
  if(!selfTest.isEmpty())
    QObject::connect(&engine, &QQmlEngine::warnings, &app, [&](const QList<QQmlError> &) { reportFailed = true; });
  engine.addImageProvider(QStringLiteral("photos"), store);
  engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("gamepad"), &gamepad);
  engine.rootContext()->setContextProperty(QStringLiteral("managedSession"), qEnvironmentVariableIsSet("DARKTABLETOUCH_MANAGED"));
  QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed,
                    &app, [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
  engine.loadFromModule(QStringLiteral("DarktableTouch"), QStringLiteral("Main"));
  if(engine.rootObjects().isEmpty()) return 1;
  auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  if(selfTest.isEmpty() && screenshot.isEmpty()) window->showMaximized();
  if(!selfTest.isEmpty() && testCommandLine) window->resize(960, 540);
  bool startedImport = false, startedImage = false, finished = false;
  int stage = 0;
  int libraryStage = -1;
  int rejectedTestId = 0;
  QImage cropBaseline;
  QVariantMap batchSettings;
  QByteArray batchOriginal;
  QTimer libraryTest;
  libraryTest.setInterval(180);
  const auto libraryScreenshot = [&](const QString &suffix) {
    if(screenshot.isEmpty()) return true;
    const QFileInfo file(screenshot);
    return window->grabWindow().save(file.dir().filePath(file.completeBaseName() + suffix + QStringLiteral(".png")));
  };
  QObject::connect(&libraryTest, &QTimer::timeout, &app, [&] {
    if(controller.busy()) return;
    const auto fail = [&](const char *message) {
      QTextStream(stderr) << message << ": " << controller.message() << '\n'; libraryTest.stop(); controller.shutdown();
    };
    auto collectionDialog = window->findChild<QObject *>(QStringLiteral("collectionDialog"));
    auto review = window->findChild<QObject *>(QStringLiteral("importReview"));
    if(!collectionDialog || !review) { fail("library dialogs are unavailable"); return; }
    const int selected = controller.library().value(QStringLiteral("selected")).toList().size();
    switch(libraryStage)
    {
      case -1:
        controller.importPath(testNeighbor);
        break;
      case 0:
        if(auto menu = window->findChild<QObject *>(QStringLiteral("quickMenu"))) QMetaObject::invokeMethod(menu, "close");
        controller.setEditorVisible(false);
        controller.libraryCommand(QStringLiteral("all"));
        break;
      case 1:
        if(selected != 2) { fail("select all did not select the collection"); return; }
        if(!libraryScreenshot(QStringLiteral("-library"))) { fail("library screenshot failed"); return; }
        controller.libraryCommand(QStringLiteral("invert"));
        break;
      case 2:
        if(selected != 0) { fail("invert did not clear the selected collection"); return; }
        controller.libraryCommand(QStringLiteral("single"), {}, controller.imageId());
        break;
      case 3:
        if(selected != 1) { fail("single selection did not select the image"); return; }
        QMetaObject::invokeMethod(collectionDialog, "open");
        break;
      case 4:
      {
        const QString child = QFileInfo(testNeighbor).absolutePath();
        const QString parent = QFileInfo(testPhoto).absolutePath();
        collectionDialog->setProperty("expanded", QVariantMap{{parent, false}});
        QMetaObject::invokeMethod(collectionDialog, "toggleFolder", Q_ARG(QVariant, child), Q_ARG(QVariant, false));
        QVariant checkState;
        QMetaObject::invokeMethod(collectionDialog, "folderState", Q_RETURN_ARG(QVariant, checkState), Q_ARG(QVariant, parent));
        if(checkState.toInt() != Qt::PartiallyChecked) { fail("folder tree selection did not preserve a collapsed subfolder"); return; }
        QMetaObject::invokeMethod(collectionDialog, "toggleFolder", Q_ARG(QVariant, child), Q_ARG(QVariant, true));
        QMetaObject::invokeMethod(collectionDialog, "folderState", Q_RETURN_ARG(QVariant, checkState), Q_ARG(QVariant, parent));
        if(checkState.toInt() != Qt::Checked) { fail("folder tree could not reselect a subfolder"); return; }
        collectionDialog->setProperty("expanded", QVariantMap{});
        auto folderDraft = collectionDialog->property("draft").toMap();
        folderDraft.insert(QStringLiteral("filtering"), QVariantList{});
        collectionDialog->setProperty("draft", folderDraft);
        if(!libraryScreenshot(QStringLiteral("-folders"))) { fail("folder screenshot failed"); return; }
        collectionDialog->setProperty("page", 1);
        break;
      }
      case 5:
      {
        if(!libraryScreenshot(QStringLiteral("-collection"))) { fail("collection screenshot failed"); return; }
        auto draft = controller.library();
        int ratingProperty = -1;
        for(const auto &property : draft.value(QStringLiteral("properties")).toList())
          if(property.toMap().value(QStringLiteral("label")).toString() == QStringLiteral("rating"))
            ratingProperty = property.toMap().value(QStringLiteral("item")).toInt();
        if(ratingProperty < 0) { fail("rating filter is unavailable"); return; }
        draft.insert(QStringLiteral("filtering"), QVariantList{QVariantMap{{QStringLiteral("item"), ratingProperty},
          {QStringLiteral("mode"), 0}, {QStringLiteral("off"), 0}, {QStringLiteral("top"), 0}, {QStringLiteral("string"), QStringLiteral(">=0")}}});
        collectionDialog->setProperty("draft", draft);
        collectionDialog->setProperty("showFilters", true);
        collectionDialog->setProperty("page", 2);
        break;
      }
      case 6:
        if(!libraryScreenshot(QStringLiteral("-filters"))) { fail("filter screenshot failed"); return; }
        collectionDialog->setProperty("page", 3);
        break;
      case 7:
      {
        if(!libraryScreenshot(QStringLiteral("-sort"))) { fail("sort screenshot failed"); return; }
        QMetaObject::invokeMethod(collectionDialog, "close");
        auto settings = controller.library();
        settings.insert(QStringLiteral("collection"), QVariantList{QVariantMap{{QStringLiteral("item"), 2},
          {QStringLiteral("mode"), 0}, {QStringLiteral("off"), 0}, {QStringLiteral("top"), 0},
          {QStringLiteral("string"), QStringLiteral("no-such-photo")}}});
        controller.libraryCommand(QStringLiteral("collection"), settings);
        break;
      }
      case 8:
      {
        if(!controller.images().isEmpty() || selected != 0) { fail("collection filtering did not update images and selection"); return; }
        auto settings = controller.library();
        settings.insert(QStringLiteral("collection"), QVariantList{QVariantMap{{QStringLiteral("item"), 0},
          {QStringLiteral("mode"), 0}, {QStringLiteral("off"), 0}, {QStringLiteral("top"), 0}, {QStringLiteral("string"), QStringLiteral("%")}}});
        controller.libraryCommand(QStringLiteral("collection"), settings);
        break;
      }
      case 9:
        if(controller.images().size() != 2) { fail("reset collection did not restore the imported photos"); return; }
        controller.reviewImport({QUrl::fromLocalFile(QFileInfo(testPhoto).absolutePath())}, false);
        break;
      case 10:
        if(controller.importCandidates().size() != 1 || !controller.importCandidates().first().toMap().value(QStringLiteral("existing")).toBool()
           || controller.importCandidates().first().toMap().value(QStringLiteral("path")).toString() != testPhoto)
        { fail("Unicode import review did not identify the existing photo"); return; }
        break;
      case 11:
      {
        QMetaObject::invokeMethod(review, "close");
        cropBaseline = store->requestImage(QStringLiteral("current"), nullptr, QSize()).copy();
        controller.rememberParameter(QStringLiteral("exposure"), QStringLiteral("exposure"), 0.01);
        const double originalValue = controller.values().value(QStringLiteral("exposure")).toDouble();
        QMetaObject::invokeMethod(&gamepad, "action", Q_ARG(QString, QStringLiteral("increase")), Q_ARG(bool, true));
        if(qAbs(controller.values().value(QStringLiteral("exposure")).toDouble() - originalValue - 0.01) > 0.00001) { fail("controller increase did not adjust the last focused value"); return; }
        QMetaObject::invokeMethod(&gamepad, "action", Q_ARG(QString, QStringLiteral("decrease")), Q_ARG(bool, true));
        controller.setEditorVisible(true); controller.selectTool(QStringLiteral("crop"));
        break;
      }
      case 12:
      {
        auto overlay = window->findChild<QObject *>(QStringLiteral("cropOverlay"));
        if(!controller.cropEditing() || !overlay) { fail("interactive crop did not start"); return; }
        QMetaObject::invokeMethod(overlay, "setBounds", Q_ARG(QVariant, (QVariantList{0.1,0.1,0.9,0.9})));
        if(!libraryScreenshot(QStringLiteral("-crop"))) { fail("crop screenshot failed"); return; }
        controller.cropCommand(QStringLiteral("apply"), {{QStringLiteral("bounds"), QVariantList{0.1,0.1,0.9,0.9}}, {QStringLiteral("ratio_n"),0}, {QStringLiteral("ratio_d"),0}});
        break;
      }
      case 13:
        if(controller.cropEditing() || cropBaseline == store->requestImage(QStringLiteral("current"), nullptr, QSize())) { fail("applying crop did not change the preview"); return; }
        controller.undo(); break;
      case 14:
        if(cropBaseline != store->requestImage(QStringLiteral("current"), nullptr, QSize())) { fail("crop undo did not restore the original preview"); return; }
        controller.cropCommand(QStringLiteral("begin")); break;
      case 15:
        controller.cropCommand(QStringLiteral("auto"), {{QStringLiteral("aspect"),1.5}}); break;
      case 16:
        if(controller.cropBounds().size() != 4 || controller.cropBounds()[2].toDouble() <= controller.cropBounds()[0].toDouble()) { fail("automatic crop did not return a frame"); return; }
        controller.cropCommand(QStringLiteral("cancel")); break;
      case 17:
      {
        if(cropBaseline != store->requestImage(QStringLiteral("current"), nullptr, QSize())) { fail("canceling crop changed the saved preview"); return; }
        controller.setEditorVisible(false);
        auto dialog = window->findChild<QObject *>(QStringLiteral("batchExportDialog"));
        if(!dialog) { fail("batch export dialog is unavailable"); return; }
        QMetaObject::invokeMethod(dialog, "open"); break;
      }
      case 18:
        if(!libraryScreenshot(QStringLiteral("-batch-export"))) { fail("batch export screenshot failed"); return; }
        if(auto dialog = window->findChild<QObject *>(QStringLiteral("batchExportDialog"))) QMetaObject::invokeMethod(dialog, "close");
        batchSettings = {{QStringLiteral("scope"),QStringLiteral("collection")}, {QStringLiteral("storage"),QStringLiteral("folder")},
          {QStringLiteral("folder"), QUrl::fromLocalFile(QDir(testDirectory.path()).filePath(QString::fromUtf8("ส่งออกชุด #100%")))},
          {QStringLiteral("pattern"),QString::fromUtf8("ภาพ_{sequence}")}, {QStringLiteral("format"),QStringLiteral("png")},
          {QStringLiteral("bits"),16}, {QStringLiteral("width"),128}, {QStringLiteral("height"),128}};
        controller.exportBatch(batchSettings); break;
      case 19:
      {
        const QDir output(batchSettings.value(QStringLiteral("folder")).toUrl().toLocalFile());
        if(output.entryList({QStringLiteral("*.png")}, QDir::Files).size() != 2) { fail("batch export did not write both photos"); return; }
        QFile first(output.filePath(QString::fromUtf8("ภาพ_0001.png"))); if(!first.open(QIODevice::ReadOnly)) { fail("exported file could not be read"); return; } batchOriginal = first.readAll();
        if(batchOriginal.size() < 25 || static_cast<unsigned char>(batchOriginal[24]) != 16) { fail("batch bit depth was not applied"); return; }
        batchSettings.insert(QStringLiteral("collision"),QStringLiteral("skip")); controller.exportBatch(batchSettings); break;
      }
      case 20:
      {
        QFile first(QDir(batchSettings.value(QStringLiteral("folder")).toUrl().toLocalFile()).filePath(QString::fromUtf8("ภาพ_0001.png"))); if(!first.open(QIODevice::ReadOnly)) { fail("exported file could not be read"); return; }
        if(first.readAll() != batchOriginal) { fail("skip-existing export replaced a file"); return; }
        rejectedTestId = controller.images().first().toMap().value(QStringLiteral("imgid")).toInt();
        controller.setAdvanceAfterReject(true); controller.selectImage(rejectedTestId);
        break;
      }
      case 21:
        if(!libraryScreenshot(QStringLiteral("-import-review"))) { fail("import review screenshot failed"); return; }
        QMetaObject::invokeMethod(review, "close");
        QMetaObject::invokeMethod(window, "showImageActions", Q_ARG(QVariant, controller.images().first()));
        break;
      case 22:
        if(!libraryScreenshot(QStringLiteral("-image-actions"))) { fail("image actions screenshot failed"); return; }
        QMetaObject::invokeMethod(window, "chooseImageAction", Q_ARG(QVariant, QStringLiteral("remove")));
        break;
      case 23:
        if(!libraryScreenshot(QStringLiteral("-remove-confirmation"))) { fail("confirmation screenshot failed"); return; }
        if(auto confirmation = window->findChild<QObject *>(QStringLiteral("confirmImageAction"))) QMetaObject::invokeMethod(confirmation, "close");
        if(controller.images().size() != 2) { fail("canceling image removal changed the collection"); return; }
        controller.setShowRejected(true);
        controller.libraryCommand(QStringLiteral("reject"), {}, controller.imageId());
        break;
      case 24:
      {
        bool rejected = false;
        for(const auto &image : controller.images())
          if(image.toMap().value(QStringLiteral("imgid")).toInt() == rejectedTestId) rejected = image.toMap().value(QStringLiteral("rejected")).toBool();
        if(!rejected || controller.imageId() == rejectedTestId) { fail("reject did not advance to the next image"); return; }
        controller.setShowRejected(false);
        if(controller.images().size() != 1) { fail("quick rejected filter did not hide the image"); return; }
        controller.setShowRejected(true); controller.setAdvanceAfterReject(false);
        controller.libraryCommand(QStringLiteral("unrate"), {}, rejectedTestId);
        break;
      }
      case 25:
        for(const auto &image : controller.images())
          if(image.toMap().value(QStringLiteral("imgid")).toInt() == rejectedTestId && image.toMap().value(QStringLiteral("rejected")).toBool())
          { fail("unreject did not update the library"); return; }
        controller.libraryCommand(QStringLiteral("remove"), {}, controller.imageId());
        break;
      case 26:
        if(controller.imageId() != 0 || controller.editorVisible() || controller.images().size() != 1 || !QFile::exists(testPhoto))
        { fail("catalog removal did not release the editing session or preserve the file"); return; }
        controller.libraryCommand(QStringLiteral("remove-folder"), {{QStringLiteral("folder"), QFileInfo(testNeighbor).absolutePath()}});
        break;
      case 27:
        if(!controller.images().isEmpty() || !QFile::exists(testNeighbor)) { fail("folder removal did not preserve the source and update the catalog"); return; }
        libraryTest.stop(); finished = true;
        QTextStream(stdout) << "passed: Touch RAW editing/export, layout, collections, filters, selection, sorting, and Unicode import review\n";
        controller.shutdown();
        break;
      default: return;
    }
    ++libraryStage;
  });
  QImage baseline, adjusted, automaticBaseline, automaticAdjusted;
  int manualMode = -1;
  QObject::connect(&controller, &TouchController::importReviewChanged, &app, [&] {
    if(selfTest.isEmpty() || testCommandLine || startedImage) return;
    QTimer::singleShot(200, &app, [&] {
      auto review = window->findChild<QObject *>(QStringLiteral("importReview"));
      auto confirm = window->findChild<QObject *>(QStringLiteral("confirmImport"));
      if(controller.importCandidates().size() != 1
         || controller.importCandidates().first().toMap().value(QStringLiteral("existing")).toBool()
         || !review || review->property("selectedCount").toInt() != 1 || !confirm || !confirm->property("enabled").toBool()
         || !libraryScreenshot(QStringLiteral("-import-new")))
      { QTextStream(stderr) << "new-photo import review is invalid\n"; controller.shutdown(); return; }
      QMetaObject::invokeMethod(confirm, "clicked");
    });
  });
  QObject::connect(&controller, &TouchController::stateChanged, &app, [&] {
    if(controller.ready() && !coverage.isEmpty())
    {
      const QString reportPath = coverage;
      coverage.clear();
      QSaveFile file(reportPath);
      if(!file.open(QIODevice::WriteOnly)
         || file.write(QJsonDocument::fromVariant(controller.moduleCoverage()).toJson()) < 0 || !file.commit())
      { reportFailed = true; QTextStream(stderr) << "could not write the module coverage report\n"; controller.shutdown(); return; }
    }
    if(controller.ready() && !startedImport && !testPhoto.isEmpty())
    {
      startedImport = true;
      if(testCommandLine)
      {
        startedImage = true;
        if(controller.images().isEmpty())
        { QTextStream(stderr) << "command-line RAW import failed\n"; controller.shutdown(); }
      }
      else controller.reviewImport({QUrl::fromLocalFile(testPhoto)}, false);
    }
    if(stage == 7 && controller.message().startsWith(QStringLiteral("exported to")))
    {
      stage = 8;
      QMetaObject::invokeMethod(window, "showImageInformation");
      QTimer::singleShot(600, &app, [&] {
        auto info = window->findChild<QQuickItem *>(QStringLiteral("imageInformation"));
        auto strip = window->findChild<QQuickItem *>(QStringLiteral("filmstripBar"));
        auto panel = window->findChild<QQuickItem *>(QStringLiteral("toolPanel"));
        auto menu = window->findChild<QQuickItem *>(QStringLiteral("editorMenuButton"));
        auto filmstrip = window->findChild<QQuickItem *>(QStringLiteral("filmstrip"));
        auto imageStage = window->findChild<QQuickItem *>(QStringLiteral("imageStage"));
        auto thumb = findItem(window->contentItem(), QStringLiteral("filmThumbnail-") + QString::number(controller.imageId()));
        if(!info || !strip || !panel || !menu || !filmstrip || !imageStage || !thumb || info->opacity() < 0.99
           || thumb->property("status").toInt() != 1 || thumb->width() <= 0 || thumb->height() <= 0
           || controller.imageInformation().value(QStringLiteral("filename")).toString() != QFileInfo(testPhoto).fileName()
           || menu->height() < 64 || menu->width() < 64
           || menu->mapToScene(QPointF()).y() < strip->mapToScene(QPointF()).y()
           || menu->mapToScene(QPointF(0, menu->height())).y() > strip->mapToScene(QPointF(0, strip->height())).y()
           || menu->mapToScene(QPointF()).y() < imageStage->mapToScene(QPointF(0, imageStage->height())).y()
           || menu->mapToScene(QPointF(menu->width(), 0)).x() > filmstrip->mapToScene(QPointF()).x()
           || strip->mapToScene(QPointF(strip->width(), 0)).x() > panel->mapToScene(QPointF()).x() + 1)
        { QTextStream(stderr) << "editor layout or image information is invalid\n"; controller.shutdown(); return; }
        auto imageCanvas = window->findChild<QQuickItem *>(QStringLiteral("imageCanvas"));
        if(!imageCanvas) { QTextStream(stderr) << "image canvas is unavailable\n"; controller.shutdown(); return; }
        const double anchorX = imageCanvas->width() * 0.7, anchorY = imageCanvas->height() * 0.6;
        const QPointF sceneAnchor = imageCanvas->mapToScene(QPointF(anchorX, anchorY));
        QWheelEvent wheel(sceneAnchor, window->mapToGlobal(sceneAnchor.toPoint()), QPoint(), QPoint(0, 120),
          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QGuiApplication::sendEvent(window, &wheel);
        const double wheelZoom = window->property("viewZoom").toDouble();
        if(wheelZoom <= 1 || qAbs((imageCanvas->property("contentX").toDouble() + anchorX) / (imageCanvas->width() * wheelZoom) - 0.7) > 0.00001)
        { QTextStream(stderr) << "mouse wheel zoom did not preserve the cursor point\n"; controller.shutdown(); return; }
        QMetaObject::invokeMethod(window, "zoomAt", Q_ARG(QVariant, 1.0), Q_ARG(QVariant, anchorX), Q_ARG(QVariant, anchorY));
        QMetaObject::invokeMethod(window, "zoomAt", Q_ARG(QVariant, 2.0), Q_ARG(QVariant, anchorX), Q_ARG(QVariant, anchorY));
        const double normalizedX = (imageCanvas->property("contentX").toDouble() + anchorX) / (imageCanvas->width() * 2);
        const double normalizedY = (imageCanvas->property("contentY").toDouble() + anchorY) / (imageCanvas->height() * 2);
        if(qAbs(normalizedX - 0.7) > 0.00001 || qAbs(normalizedY - 0.6) > 0.00001)
        { QTextStream(stderr) << "zoom did not preserve its pointer anchor\n"; controller.shutdown(); return; }
        QMetaObject::invokeMethod(window, "placeZoom", Q_ARG(QVariant, 3.0), Q_ARG(QVariant, anchorX - 20),
          Q_ARG(QVariant, anchorY - 10), Q_ARG(QVariant, normalizedX), Q_ARG(QVariant, normalizedY));
        if(qAbs((imageCanvas->property("contentX").toDouble() + anchorX - 20) / (imageCanvas->width() * 3) - normalizedX) > 0.00001)
        { QTextStream(stderr) << "moving pinch anchor was not preserved\n"; controller.shutdown(); return; }
        QMetaObject::invokeMethod(window, "zoomAt", Q_ARG(QVariant, 1.0), Q_ARG(QVariant, anchorX), Q_ARG(QVariant, anchorY));
        if(!screenshot.isEmpty())
        {
          const QFileInfo file(screenshot);
          const QString infoShot = file.dir().filePath(file.completeBaseName() + QStringLiteral("-information.png"));
          if(!window->grabWindow().save(infoShot))
          { QTextStream(stderr) << "could not write the information screenshot\n"; controller.shutdown(); return; }
        }
        auto filmHandle = window->findChild<QQuickItem *>(QStringLiteral("filmstripResizeHandle"));
        auto moduleHandle = window->findChild<QQuickItem *>(QStringLiteral("moduleResizeHandle"));
        if(!filmHandle || !moduleHandle || filmHandle->width() < 112 || filmHandle->height() != 24
           || moduleHandle->width() != 24 || moduleHandle->height() < 112
           || filmstrip->y() > 14)
        { QTextStream(stderr) << "panel resize handles are unavailable\n"; controller.shutdown(); return; }
        const qreal originalStripHeight = strip->height(), originalPanelWidth = panel->width();
        QMetaObject::invokeMethod(filmHandle, "adjusted", Q_ARG(double, 40));
        QMetaObject::invokeMethod(moduleHandle, "adjusted", Q_ARG(double, 64));
        QTimer::singleShot(150, &app, [&, strip, panel, filmHandle, moduleHandle, imageStage, menu, originalStripHeight, originalPanelWidth] {
          if(strip->height() <= originalStripHeight || panel->width() <= originalPanelWidth)
          { QTextStream(stderr) << "panel handles did not resize the panels\n"; controller.shutdown(); return; }
          QMetaObject::invokeMethod(filmHandle, "adjusted", Q_ARG(double, 10000));
          QMetaObject::invokeMethod(moduleHandle, "adjusted", Q_ARG(double, 10000));
          QTimer::singleShot(150, &app, [&, strip, panel, filmHandle, moduleHandle, imageStage, menu] {
            if(imageStage->width() < 399 || strip->height() > window->height() * 0.45 + 1
               || menu->mapToScene(QPointF()).y() < imageStage->mapToScene(QPointF(0, imageStage->height())).y())
            { QTextStream(stderr) << "resizing covered the image or moved the menu into the viewport\n"; controller.shutdown(); return; }
            if(!screenshot.isEmpty())
            {
              const QFileInfo file(screenshot);
              if(!window->grabWindow().save(file.dir().filePath(file.completeBaseName() + QStringLiteral("-resized.png"))))
              { QTextStream(stderr) << "could not write the resized-panel screenshot\n"; controller.shutdown(); return; }
            }
            QMetaObject::invokeMethod(filmHandle, "adjusted", Q_ARG(double, -10000));
            QMetaObject::invokeMethod(moduleHandle, "adjusted", Q_ARG(double, -10000));
            QTimer::singleShot(150, &app, [&, strip, panel, filmHandle, moduleHandle] {
              if(qAbs(strip->height() - 96) > 1 || qAbs(panel->width() - 340) > 1)
              { QTextStream(stderr) << "panel minimum sizes were not retained\n"; controller.shutdown(); return; }
              QMetaObject::invokeMethod(filmHandle, "resetRequested");
              QMetaObject::invokeMethod(moduleHandle, "resetRequested");
            });
          });
        });
        if(auto tool = findItem(window->contentItem(), QStringLiteral("tool-agx")))
          QMetaObject::invokeMethod(tool, "clicked");
        QTimer::singleShot(600, &app, [&] {
          auto exposure = findItem(window->contentItem(), QStringLiteral("card-exposure"));
          auto agx = findItem(window->contentItem(), QStringLiteral("card-agx"));
          if(!exposure || !agx || exposure->height() > 65 || agx->height() <= 64)
          { QTextStream(stderr) << "tool accordion did not collapse the previous controls\n"; controller.shutdown(); return; }
          if(auto tool = findItem(window->contentItem(), QStringLiteral("tool-exposure")))
            QMetaObject::invokeMethod(tool, "clicked");
        });
      });
      QTimer::singleShot(5000, &app, [&] {
        auto info = window->findChild<QQuickItem *>(QStringLiteral("imageInformation"));
        if(!info || info->opacity() > 0.01)
        { QTextStream(stderr) << "image information did not fade out\n"; controller.shutdown(); return; }
        QFile exported(QDir(testDirectory.path()).filePath(QString::fromUtf8("ผู้ใช้/ส่งออก.png")));
        if(!exported.open(QIODevice::ReadOnly) || exported.read(8) != QByteArray::fromHex("89504e470d0a1a0a"))
        { QTextStream(stderr) << "export did not create a valid PNG\n"; controller.shutdown(); return; }
        exported.close();
        if(!screenshot.isEmpty() && !window->grabWindow().save(screenshot))
        { QTextStream(stderr) << "could not write the UI screenshot\n"; controller.shutdown(); return; }
        if(!screenshot.isEmpty()) QMetaObject::invokeMethod(window, "showHistory");
        QTimer::singleShot(150, &app, [&] {
          if(!screenshot.isEmpty())
          {
            const QFileInfo file(screenshot);
            const QString historyShot = file.dir().filePath(file.completeBaseName() + QStringLiteral("-history.png"));
            if(!window->grabWindow().save(historyShot))
            { QTextStream(stderr) << "could not write the history screenshot\n"; controller.shutdown(); return; }
          }
          if(!screenshot.isEmpty()) QMetaObject::invokeMethod(window, "showQuickMenu");
          QTimer::singleShot(150, &app, [&] {
            if(!screenshot.isEmpty())
            {
              const QFileInfo file(screenshot);
              const QString menuShot = file.dir().filePath(file.completeBaseName() + QStringLiteral("-menu.png"));
              if(!window->grabWindow().save(menuShot))
              { QTextStream(stderr) << "could not write the menu screenshot\n"; controller.shutdown(); return; }
            }
            libraryStage = -1;
            libraryTest.start();
          });
        });
      });
    }
  });
  QObject::connect(&controller, &TouchController::imagesChanged, &app, [&] {
    if(startedImport && !startedImage && !controller.images().isEmpty())
    {
      startedImage = true;
      controller.selectImage(controller.images().first().toMap().value(QStringLiteral("imgid")).toInt());
    }
  });
  QObject::connect(&controller, &TouchController::previewChanged, &app, [&] {
    if(testPhoto.isEmpty() || controller.imageId() == 0 || stage >= 7) return;
    QSize size;
    const QImage frame = store->requestImage(QStringLiteral("current"), &size, QSize());
    if(stage == 0)
    {
      baseline = frame.copy(); stage = 1;
      controller.setParameter(QStringLiteral("exposure"), 0.5, true);
      if(controller.values().value(QStringLiteral("exposure")).toDouble() != 0.5) { QTextStream(stderr) << "pending exposure value did not remain visible\n"; controller.shutdown(); return; }
    }
    else if(stage == 1)
    {
      adjusted = frame.copy();
      if(adjusted == baseline) { QTextStream(stderr) << "Exposure did not change the preview\n"; controller.shutdown(); return; }
      stage = 2; controller.undo();
    }
    else if(stage == 2)
    {
      if(frame != baseline) { QTextStream(stderr) << "undo did not restore the original preview\n"; controller.shutdown(); return; }
      stage = 3; controller.redo();
    }
    else if(stage == 3)
    {
      if(frame != adjusted) { QTextStream(stderr) << "redo did not restore the edited preview\n"; controller.shutdown(); return; }
      stage = 4; controller.jumpHistory(0);
    }
    else if(stage == 4)
    {
      if(controller.historyEnd() != 0) { QTextStream(stderr) << "history selection did not reach original\n"; controller.shutdown(); return; }
      stage = 6; controller.undo();
    }
    else if(stage == 6)
    {
      if(frame != adjusted) { QTextStream(stderr) << "undo did not restore the selected history state\n"; controller.shutdown(); return; }
      int automaticMode = -1;
      manualMode = controller.values().value(QStringLiteral("mode")).toInt();
      for(int i = 0; i < controller.fields()->rowCount(); i++)
      {
        const auto field = controller.fields()->data(controller.fields()->index(i, 0), Qt::UserRole + 1).toMap();
        if(field.value(QStringLiteral("name")).toString() != QStringLiteral("mode")) continue;
        for(const auto &choice : field.value(QStringLiteral("values")).toList())
          if(choice.toMap().value(QStringLiteral("name")).toString() == QStringLiteral("automatic"))
            automaticMode = choice.toMap().value(QStringLiteral("value")).toInt();
      }
      if(automaticMode < 0) { QTextStream(stderr) << "automatic exposure mode is unavailable\n"; controller.shutdown(); return; }
      stage = -1; controller.setParameter(QStringLiteral("mode"), automaticMode, true);
    }
    else if(stage == -1)
    {
      automaticBaseline = frame.copy();
      stage = -2; controller.setParameter(QStringLiteral("deflicker_target_level"), -2.0, true);
    }
    else if(stage == -2)
    {
      automaticAdjusted = frame.copy();
      if(frame == automaticBaseline) { QTextStream(stderr) << "automatic target brightness did not change the preview\n"; controller.shutdown(); return; }
      stage = -3; controller.undo();
    }
    else if(stage == -3)
    {
      if(frame != automaticBaseline) { QTextStream(stderr) << "automatic exposure undo did not restore the preview\n"; controller.shutdown(); return; }
      stage = -4; controller.redo();
    }
    else if(stage == -4)
    {
      if(frame != automaticAdjusted) { QTextStream(stderr) << "automatic exposure redo did not restore the preview\n"; controller.shutdown(); return; }
      stage = -5;
      QTimer::singleShot(200, &app, [&] {
        auto target = findItem(window->contentItem(), QStringLiteral("parameter-deflicker_target_level"));
        auto percentile = findItem(window->contentItem(), QStringLiteral("parameter-deflicker_percentile"));
        auto manual = findItem(window->contentItem(), QStringLiteral("parameter-exposure"));
        if(!target || !target->isVisible() || !percentile || !percentile->isVisible()
           || (manual && manual->isVisible()) || !libraryScreenshot(QStringLiteral("-automatic")))
        { QTextStream(stderr) << "automatic exposure controls are invalid\n"; controller.shutdown(); return; }
        auto numeric = findItem(window->contentItem(), QStringLiteral("numeric-deflicker_target_level"));
        if(!numeric) { QTextStream(stderr) << "automatic numeric entry is unavailable\n"; controller.shutdown(); return; }
        int focusEdits = 0;
        const auto focusConnection = QObject::connect(&controller, &TouchController::parameterRequested,
          &app, [&] { ++focusEdits; }, Qt::DirectConnection);
        numeric->forceActiveFocus();
        window->contentItem()->forceActiveFocus();
        QObject::disconnect(focusConnection);
        if(focusEdits) { QTextStream(stderr) << "focusing an unchanged number created an edit\n"; controller.shutdown(); return; }
        controller.setParameter(QStringLiteral("mode"), manualMode, true);
      });
    }
    else if(stage == -5)
    {
      if(frame != adjusted) { QTextStream(stderr) << "returning to manual exposure changed its saved adjustment\n"; controller.shutdown(); return; }
      stage = 7;
      const QString output = QDir(testDirectory.path()).filePath(QString::fromUtf8("ผู้ใช้/ส่งออก.png"));
      controller.exportTo(QUrl::fromLocalFile(output), QStringLiteral("png"));
    }
  });
  if(!selfTest.isEmpty())
    QTimer::singleShot(150000, &app, [&] {
      if(!finished) { QTextStream(stderr) << "Touch workflow self-test timed out: " << controller.message() << '\n'; controller.shutdown(); }
    });
  else if(!screenshot.isEmpty())
    QObject::connect(&controller, &TouchController::stateChanged, &app, [&] {
      if(controller.ready() && !finished)
      {
        finished = true;
        QTimer::singleShot(500, &app, [&] { window->grabWindow().save(screenshot); controller.shutdown(); });
      }
    });
  controller.initialize(core);
  const int result = app.exec();
  return reportFailed || (!selfTest.isEmpty() && !finished) ? 1 : result;
}
