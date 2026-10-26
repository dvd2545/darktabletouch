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
#include <QQmlProperty>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QQuickItem>
#include <QJsonDocument>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QTextStream>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QKeyEvent>

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
  if(!selfTest.isEmpty()) { window->setProperty("toolsOpen", true); window->setProperty("autoHideFilmstrip", false); }
  bool startedImport = false, startedImage = false, finished = false;
  int stage = 0;
  int libraryStage = -1;
  int rejectedTestId = 0;
  QImage cropBaseline;
  QVariantMap batchSettings;
  QByteArray batchOriginal;
  int historyTarget = 0, historyVersion = 0, historyWait = 0;
  QImage historyBaseline;
  QString blockedSidecar, sidecarBackup;
  double fieldBaseline = 0, fieldDefault = 0;
  qreal drawerCanvasWidth = 0, compactStripHeight = 0;
  bool controlsFailed = false;
  QElapsedTimer pointerClock;
  pointerClock.start();
  QPointF touchPosition;
  const auto pointer = [&](QEvent::Type type, QPointF position) {
    QMouseEvent event(type, position, window->mapToGlobal(position),
      type == QEvent::MouseMove ? Qt::NoButton : Qt::LeftButton,
      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    event.setTimestamp(pointerClock.elapsed() + 1);
    QCoreApplication::sendEvent(window, &event);
  };
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
        if(review->property("visible").toBool()) { QMetaObject::invokeMethod(review, "close"); return; }
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
        for(const auto &name : {"editorUndoButton", "editorCompareButton", "editorExportButton"})
        {
          auto action = window->findChild<QObject *>(QString::fromLatin1(name));
          if(!action || action->property("enabled").toBool()) { fail("editor actions must respect the active crop transaction"); return; }
        }
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
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("batchExportDialog"));
        dialog->setProperty("scope", QStringLiteral("collection"));
        auto start = dialog->findChild<QObject *>(QStringLiteral("exportStartButton"));
        bool workflowFailed = false;
        if(dialog->property("targetCount").toInt() != 2 || dialog->property("validationMessage").toString().isEmpty()
           || !start || start->property("enabled").toBool() || !start->property("showLabel").toBool())
        { QTextStream(stderr) << "export must show its target count and explain the missing destination\n"; workflowFailed = true; }
        if(!dialog->findChild<QObject *>(QStringLiteral("dialogCloseButton")))
        { QTextStream(stderr) << "export dialog needs an explicit close action\n"; workflowFailed = true; }
        auto paste = window->findChild<QObject *>(QStringLiteral("pasteHistoryButton"));
        if(!paste || paste->property("implicitWidth").toDouble() < 120)
        { QTextStream(stderr) << "labeled icon actions must reserve room for their text\n"; workflowFailed = true; }
        if(!window->findChild<QObject *>(QStringLiteral("editorActionBar")))
        { QTextStream(stderr) << "editor needs persistent navigation and editing actions\n"; workflowFailed = true; }
        if(workflowFailed) { fail("workflow discoverability regression"); return; }
        if(!libraryScreenshot(QStringLiteral("-batch-export"))) { fail("batch export screenshot failed"); return; }
        batchSettings = {{QStringLiteral("scope"),QStringLiteral("collection")}, {QStringLiteral("storage"),QStringLiteral("folder")},
          {QStringLiteral("folder"), QUrl::fromLocalFile(QDir(testDirectory.path()).filePath(QString::fromUtf8("ส่งออกชุด #100%")))},
          {QStringLiteral("pattern"),QString::fromUtf8("ภาพ_{sequence}")}, {QStringLiteral("format"),QStringLiteral("png")},
          {QStringLiteral("bits"),16}, {QStringLiteral("width"),128}, {QStringLiteral("height"),128}};
        dialog->setProperty("folder", batchSettings.value(QStringLiteral("folder")));
        dialog->findChild<QObject *>(QStringLiteral("exportPattern"))->setProperty("text", batchSettings.value(QStringLiteral("pattern")));
        dialog->findChild<QObject *>(QStringLiteral("exportFormat"))->setProperty("currentIndex", 1);
        dialog->findChild<QObject *>(QStringLiteral("exportBitDepth"))->setProperty("currentIndex", 1);
        dialog->findChild<QObject *>(QStringLiteral("exportMaxWidth"))->setProperty("text", QStringLiteral("128"));
        dialog->findChild<QObject *>(QStringLiteral("exportMaxHeight"))->setProperty("text", QStringLiteral("128"));
        if(!start->property("enabled").toBool()) { fail("valid export settings did not enable the start action"); return; }
        QMetaObject::invokeMethod(start, "clicked");
        break;
      }
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
        historyWait = 0;
        controller.libraryCommand(QStringLiteral("reject"), {}, controller.imageId());
        break;
      case 24:
      {
        bool rejected = false;
        for(const auto &image : controller.images())
          if(image.toMap().value(QStringLiteral("imgid")).toInt() == rejectedTestId) rejected = image.toMap().value(QStringLiteral("rejected")).toBool();
        if(!rejected || controller.imageId() == rejectedTestId) {
          if(!controller.hasError() && ++historyWait < 10) return;
          fail("reject did not advance to the next image"); return;
        }
        controller.setShowRejected(false);
        if(controller.images().size() != 1) { fail("quick rejected filter did not hide the image"); return; }
        controller.setShowRejected(true); controller.setAdvanceAfterReject(false);
        controller.libraryCommand(QStringLiteral("unrate"), {}, rejectedTestId);
        libraryStage = 39;
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
        QTextStream(stdout) << "passed: Touch RAW editing/export, layout, library, selective history/reset, thumbnail undo/redo, pointer tap/hold/drag, contextual drawers, focus/double-tap gestures, photo strip states, dropdown contrast, and Unicode import review\n";
        controller.shutdown();
        break;
      case 40:
        controller.selectTool(QStringLiteral("exposure"));
        break;
      case 41:
        controller.setParameter(QStringLiteral("exposure"), 1.35, true);
        break;
      case 42:
        controller.historyCommand(QStringLiteral("copy"));
        break;
      case 43:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("historyPasteDialog"));
        if(controller.copiedHistory().isEmpty()) { if(++historyWait < 20) return; fail("history copy did not complete"); return; }
        historyWait = 0;
        if(!dialog) { fail("selective history paste dialog is unavailable"); return; }
        QMetaObject::invokeMethod(dialog, "open");
        break;
      }
      case 44:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("historyPasteDialog"));
        if(!dialog->property("opened").toBool() || dialog->property("selectedCount").toInt() < 1
           || !libraryScreenshot(QStringLiteral("-history-paste"))) { fail("history selection defaults are invalid"); return; }
        const auto list = dialog->findChild<QQuickItem *>(QStringLiteral("pasteHistorySteps"));
        if(!list || list->height() < 128) { fail("history selection list has no usable height"); return; }
        auto choices = dialog->property("choices").toList();
        for(auto &entry : choices) {
          auto step = entry.toMap();
          step.insert(QStringLiteral("selected"), step.value(QStringLiteral("operation")).toString() == QStringLiteral("exposure") && step.value(QStringLiteral("latest")).toBool());
          entry = step;
        }
        dialog->setProperty("choices", choices);
        for(const auto &entry : controller.images()) {
          const int id = entry.toMap().value(QStringLiteral("imgid")).toInt();
          if(id != controller.imageId()) historyTarget = id;
        }
        dialog->setProperty("imageId", historyTarget);
        historyVersion = controller.thumbnailVersion(historyTarget);
        QMetaObject::invokeMethod(dialog->findChild<QObject *>(QStringLiteral("pasteHistoryButton")), "clicked");
        break;
      }
      case 45:
        if(controller.hasError()) { fail("selective paste failed"); return; }
        if(controller.thumbnailVersion(historyTarget) <= historyVersion) { if(++historyWait < 20) return; fail("paste did not refresh its destination thumbnail"); return; }
        historyWait = 0; historyVersion = controller.thumbnailVersion(historyTarget);
        controller.undo();
        break;
      case 46:
        if(controller.thumbnailVersion(historyTarget) <= historyVersion) { if(++historyWait < 20) return; fail("catalog undo did not refresh its destination thumbnail"); return; }
        historyWait = 0; historyVersion = controller.thumbnailVersion(historyTarget);
        controller.redo();
        break;
      case 47:
        if(controller.thumbnailVersion(historyTarget) <= historyVersion) { if(++historyWait < 20) return; fail("catalog redo did not refresh its destination thumbnail"); return; }
        historyBaseline = store->requestImage(QStringLiteral("current"), nullptr, QSize()).copy();
        QMetaObject::invokeMethod(window, "showResetHistory", Q_ARG(QVariant, controller.imageId()));
        break;
      case 48:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("resetHistoryDialog"));
        if(!dialog || !dialog->property("opened").toBool() || !libraryScreenshot(QStringLiteral("-history-reset"))) { fail("reset confirmation is unavailable"); return; }
        QMetaObject::invokeMethod(dialog->findChild<QObject *>(QStringLiteral("resetHistoryButton")), "clicked");
        break;
      }
      case 49:
      {
        if(controller.hasError() || historyBaseline == store->requestImage(QStringLiteral("current"), nullptr, QSize())) { fail("reset did not update the current preview"); return; }
        const QString name = controller.imageInformation().value(QStringLiteral("filename")).toString();
        if(name != QFileInfo(testPhoto).fileName() && name != QFileInfo(testNeighbor).fileName()) { fail("active test sidecar could not be identified"); return; }
        blockedSidecar = (name == QFileInfo(testPhoto).fileName() ? testPhoto : testNeighbor) + QStringLiteral(".xmp");
        sidecarBackup = blockedSidecar + QStringLiteral(".backup");
        if(!QFile::rename(blockedSidecar, sidecarBackup) || !QDir().mkdir(blockedSidecar)) { fail("could not create the sidecar failure fixture"); return; }
        controller.undo();
        break;
      }
      case 50:
        if(historyBaseline != store->requestImage(QStringLiteral("current"), nullptr, QSize()) || !controller.hasError() || !controller.canRedo()) { fail("failed sidecar undo left the editor or undo position stale"); return; }
        if(!QDir().rmdir(blockedSidecar) || !QFile::rename(sidecarBackup, blockedSidecar)) { fail("could not restore the test sidecar"); return; }
        controller.redo(); libraryStage = 69;
        break;
      case 70:
        if(historyBaseline == store->requestImage(QStringLiteral("current"), nullptr, QSize()) || controller.hasError()) { fail("redo after a sidecar error failed"); return; }
        controller.undo();
        break;
      case 71:
        if(historyBaseline != store->requestImage(QStringLiteral("current"), nullptr, QSize()) || controller.hasError()) { fail("undo after sidecar recovery failed"); return; }
        controller.setEditorVisible(false);
        window->setProperty("selectionMode", true);
        controller.libraryCommand(QStringLiteral("none"));
        libraryStage = 50;
        break;
      case 51:
      {
        auto item = findItem(window->contentItem(), QStringLiteral("thumbnail-%1").arg(controller.imageId()));
        if(!item) { fail("gallery thumbnail is unavailable for pointer injection"); return; }
        touchPosition = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        pointer(QEvent::MouseButtonPress, touchPosition);
        break;
      }
      case 52:
        pointer(QEvent::MouseButtonRelease, touchPosition);
        break;
      case 53:
        if(selected != 1) { fail("pointer tap did not activate exactly once"); return; }
        if(auto item = findItem(window->contentItem(), QStringLiteral("thumbnail-%1").arg(controller.imageId())))
          touchPosition = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        pointer(QEvent::MouseButtonPress, touchPosition);
        break;
      case 54: case 55: case 56:
        break;
      case 57:
      {
        auto menu = window->findChild<QObject *>(QStringLiteral("imageActions"));
        if(!menu || !menu->property("visible").toBool() || selected != 1) { fail("pointer hold did not open image options before release without tapping"); return; }
        pointer(QEvent::MouseButtonRelease, touchPosition);
        break;
      }
      case 58:
      {
        if(selected != 1) { fail("pointer hold release triggered a tap"); return; }
        const QPointF outside(2, 2);
        QMouseEvent press(QEvent::MouseButtonPress, outside, window->mapToGlobal(outside), Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QMouseEvent release(QEvent::MouseButtonRelease, outside, window->mapToGlobal(outside), Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press); QCoreApplication::sendEvent(window, &release);
        break;
      }
      case 59:
        if(window->findChild<QObject *>(QStringLiteral("imageActions"))->property("opened").toBool()) { fail("outside press did not dismiss image options"); return; }
        pointer(QEvent::MouseButtonPress, touchPosition);
        break;
      case 60:
        touchPosition += QPointF(0, 100);
        pointer(QEvent::MouseMove, touchPosition);
        break;
      case 61: case 62: case 63: case 64:
        break;
      case 65:
        pointer(QEvent::MouseButtonRelease, touchPosition);
        break;
      case 66:
        if(window->findChild<QObject *>(QStringLiteral("imageActions"))->property("opened").toBool() || selected != 1) { fail("pointer drag triggered a hold or a tap"); return; }
        window->setProperty("selectionMode", false);
        if(auto dialog = window->findChild<QObject *>(QStringLiteral("historyPasteDialog"))) QMetaObject::invokeMethod(dialog, "open");
        break;
      case 67:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("historyPasteDialog"));
        auto combo = dialog->findChild<QObject *>(QStringLiteral("historyScopeControl"));
        auto popup = combo ? combo->property("popup").value<QObject *>() : nullptr;
        if(!popup) { fail("history destination dropdown is unavailable"); return; }
        QMetaObject::invokeMethod(popup, "open");
        break;
      }
      case 68:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("historyPasteDialog"));
        auto combo = dialog->findChild<QObject *>(QStringLiteral("historyScopeControl"));
        auto popup = combo->property("popup").value<QObject *>();
        auto list = popup->property("contentItem").value<QQuickItem *>();
        auto choice = list ? list->property("currentItem").value<QQuickItem *>() : nullptr;
        auto text = choice ? choice->property("contentItem").value<QQuickItem *>() : nullptr;
        auto background = choice ? choice->property("background").value<QQuickItem *>() : nullptr;
        auto comboBackground = combo->property("background").value<QQuickItem *>();
        if(!text || !background || text->property("color").value<QColor>() != QColor(QStringLiteral("#07191b"))
           || background->property("color").value<QColor>() != QColor(QStringLiteral("#00dbe5"))
           || !comboBackground || comboBackground->property("color").value<QColor>() != QColor(QStringLiteral("#202329"))
           || !libraryScreenshot(QStringLiteral("-dropdown"))) { fail("dropdown selection contrast is invalid"); return; }
        QMetaObject::invokeMethod(popup, "close"); QMetaObject::invokeMethod(dialog, "close");
        controller.setEditorVisible(true);
        libraryStage = 79;
        break;
      }
      case 80:
      {
        auto bar = window->findChild<QQuickItem *>(QStringLiteral("editorActionBar"));
        auto compare = window->findChild<QObject *>(QStringLiteral("editorCompareButton"));
        auto back = window->findChild<QObject *>(QStringLiteral("editorLibraryButton"));
        if(!bar || !bar->isVisible() || !compare || !back || bar->height() < 48 || bar->width() > window->width())
        { fail("persistent editor actions are not usable at the supported window size"); return; }
        qreal right = 0;
        for(const auto &name : {"editorLibraryButton", "editorUndoButton", "editorRedoButton", "editorCompareButton", "editorExportButton"})
        {
          auto action = window->findChild<QQuickItem *>(QString::fromLatin1(name));
          if(!action || !action->isVisible()) { fail("persistent editor action is missing"); return; }
          const auto position = action->mapToItem(bar, QPointF());
          if(position.x() < right || position.y() < 0 || position.x() + action->width() > bar->width()
             || position.y() + action->height() > bar->height())
          { fail("persistent editor actions overlap or exceed their toolbar"); return; }
          right = position.x() + action->width();
        }
        auto target = qobject_cast<QQuickItem *>(compare);
        if(!target || !target->isEnabled() || !back->property("enabled").toBool())
        { fail("persistent navigation actions are unexpectedly disabled"); return; }
        const auto position = target->mapToScene(QPointF(target->width() / 2, target->height() / 2));
        touchPosition = position;
        pointer(QEvent::MouseButtonPress, position);
        break;
      }
      case 81:
        if(!controller.before()) { fail("persistent compare did not show the original"); return; }
        pointer(QEvent::MouseButtonRelease, touchPosition);
        if(controller.before()) { fail("releasing comparison did not restore the edit"); return; }
        QMetaObject::invokeMethod(window->findChild<QObject *>(QStringLiteral("editorLibraryButton")), "clicked");
        break;
      case 82:
      {
        if(controller.editorVisible() || controller.before() || !controller.imageId())
        { fail("returning to the library lost the editing context"); return; }
        window->setProperty("selectionMode", true);
        auto paste = window->findChild<QObject *>(QStringLiteral("pasteSelectedButton"));
        auto reset = window->findChild<QObject *>(QStringLiteral("resetSelectedButton"));
        if(!paste || !reset) { fail("selection actions are unavailable"); return; }
        QMetaObject::invokeMethod(paste, "clicked");
        auto pasteDialog = window->findChild<QObject *>(QStringLiteral("historyPasteDialog"));
        if(pasteDialog->property("scope").toString() != QStringLiteral("selected"))
        { fail("selection paste did not retain its target scope"); return; }
        QMetaObject::invokeMethod(pasteDialog, "close");
        QMetaObject::invokeMethod(reset, "clicked");
        auto resetDialog = window->findChild<QObject *>(QStringLiteral("resetHistoryDialog"));
        if(resetDialog->property("scope").toString() != QStringLiteral("selected"))
        { fail("selection reset did not retain its target scope"); return; }
        QMetaObject::invokeMethod(resetDialog, "close");
        window->setProperty("selectionMode", false);
        controller.libraryCommand(QStringLiteral("none"));
        break;
      }
      case 83:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("batchExportDialog"));
        controller.setEditorVisible(true);
        dialog->setProperty("scope", QStringLiteral("selected"));
        QMetaObject::invokeMethod(dialog, "open");
        break;
      }
      case 84:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("batchExportDialog"));
        QMetaObject::invokeMethod(&gamepad, "action", Q_ARG(QString, QStringLiteral("compare")), Q_ARG(bool, true));
        if(controller.before())
        { QTextStream(stderr) << "controller compare must not change the image behind a modal\n"; controlsFailed = true; }
        const double zoom = window->property("viewZoom").toDouble();
        QMetaObject::invokeMethod(&gamepad, "zoom", Q_ARG(double, 1.0));
        if(window->property("viewZoom").toDouble() != zoom)
        { QTextStream(stderr) << "controller zoom must not change the image behind a modal\n"; controlsFailed = true; }
        controller.setBefore(false);
        auto start = dialog->findChild<QObject *>(QStringLiteral("exportStartButton"));
        if(dialog->property("targetCount").toInt() != 0 || dialog->property("validationMessage").toString().isEmpty()
           || !start || start->property("enabled").toBool())
        { fail("empty selection export must explain why it cannot start"); return; }
        dialog->setProperty("scope", QStringLiteral("current"));
        dialog->setProperty("folder", QUrl::fromLocalFile(testDirectory.path()));
        if(dialog->property("targetCount").toInt() != 1 || !start->property("enabled").toBool())
        { fail("current-photo export did not become available with a destination"); return; }
        auto pattern = dialog->findChild<QObject *>(QStringLiteral("exportPattern"));
        auto width = dialog->findChild<QObject *>(QStringLiteral("exportMaxWidth"));
        if(!pattern || !width) { fail("advanced export controls are unavailable"); return; }
        pattern->setProperty("text", QStringLiteral("../escape"));
        if(start->property("enabled").toBool()) { fail("export accepted a filename separator"); return; }
        pattern->setProperty("text", QStringLiteral("{name}_edited"));
        width->setProperty("text", QStringLiteral("-1"));
        if(start->property("enabled").toBool()) { fail("export accepted an invalid dimension"); return; }
        width->setProperty("text", QStringLiteral("0"));
        if(!start->property("enabled").toBool()) { fail("corrected export settings did not recover"); return; }
        dialog->findChild<QObject *>(QStringLiteral("exportAdvanced"))->setProperty("checked", true);
        break;
      }
      case 85:
      {
        auto field = window->findChild<QQuickItem *>(QStringLiteral("exportMaxHeight"));
        if(!field) { fail("export height control is unavailable"); return; }
        field->forceActiveFocus();
        break;
      }
      case 86:
      {
        auto dialog = window->findChild<QObject *>(QStringLiteral("batchExportDialog"));
        auto field = dialog->findChild<QQuickItem *>(QStringLiteral("exportMaxHeight"));
        auto viewport = dialog->property("focusViewport").value<QQuickItem *>();
        if(!field || !viewport) { fail("export focus viewport is unavailable"); return; }
        for(const auto &name : {"exportWidthLabel", "exportHeightLabel"})
        {
          auto label = dialog->findChild<QQuickItem *>(QString::fromLatin1(name));
          if(!label || !label->isVisible() || label->property("text").toString().isEmpty())
          { fail("export dimensions need persistent visible labels"); return; }
        }
        const auto position = field->mapToItem(viewport, QPointF());
        if(!field->hasActiveFocus() || position.y() < -1 || position.y() + field->height() > viewport->height() + 1)
        { fail("keyboard focus did not reveal advanced export controls"); return; }
        if(!libraryScreenshot(QStringLiteral("-export-advanced"))) { fail("advanced export screenshot failed"); return; }
        historyWait = 0;
        QMetaObject::invokeMethod(dialog->findChild<QObject *>(QStringLiteral("dialogCloseButton")), "clicked");
        break;
      }
      case 87:
        if(window->findChild<QObject *>(QStringLiteral("batchExportDialog"))->property("visible").toBool()) {
          if(++historyWait < 10) return;
          fail("explicit dialog close did not dismiss export"); return;
        }
        controller.setEditorVisible(true);
        controller.selectTool(QStringLiteral("exposure"));
        break;
      case 88:
      {
        auto numeric = findItem(window->contentItem(), QStringLiteral("numeric-exposure"));
        if(!numeric) { fail("exposure value input is unavailable"); return; }
        fieldBaseline = controller.values().value(QStringLiteral("exposure")).toDouble();
        for(int i = 0; i < controller.fields()->rowCount(); ++i)
        {
          const auto field = controller.fields()->data(controller.fields()->index(i, 0), Qt::UserRole + 1).toMap();
          if(field.value(QStringLiteral("name")).toString() == QStringLiteral("exposure"))
            fieldDefault = field.value(QStringLiteral("default")).toDouble();
        }
        numeric->forceActiveFocus();
        numeric->setProperty("text", QStringLiteral("9999999"));
        numeric->setProperty("edited", true);
        auto next = window->findChild<QQuickItem *>(QStringLiteral("editorLibraryButton"));
        if(!next) { fail("numeric blur target is unavailable"); return; }
        next->forceActiveFocus();
        break;
      }
      case 89:
      {
        auto error = findItem(window->contentItem(), QStringLiteral("validation-exposure"));
        auto discard = findItem(window->contentItem(), QStringLiteral("discard-exposure"));
        if(!error || !error->isVisible() || !discard || controller.values().value(QStringLiteral("exposure")).toDouble() != fieldBaseline)
        { fail("invalid numeric edits need inline recovery without changing the image"); return; }
        if(!libraryScreenshot(QStringLiteral("-invalid-value"))) { fail("numeric validation screenshot failed"); return; }
        QMetaObject::invokeMethod(discard, "clicked");
        break;
      }
      case 90:
      {
        auto numeric = findItem(window->contentItem(), QStringLiteral("numeric-exposure"));
        auto slider = findItem(window->contentItem(), QStringLiteral("parameter-exposure"));
        if(!numeric || !slider) { fail("numeric controls are unavailable"); return; }
        if(numeric->property("edited").toBool()) { fail("discard did not clear the invalid draft"); return; }
        if(QQmlProperty(numeric, QStringLiteral("Accessible.name"), QQmlEngine::contextForObject(numeric)).read().toString().isEmpty()
           || QQmlProperty(slider, QStringLiteral("Accessible.name"), QQmlEngine::contextForObject(slider)).read().toString().isEmpty())
        { fail("accessible field labels are missing"); return; }
        numeric->forceActiveFocus();
        if(!window->property("imageShortcutsEnabled").isValid() || window->property("imageShortcutsEnabled").toBool())
        { fail("image shortcuts must not intercept text editing"); return; }
        numeric->setProperty("text", QStringLiteral("9999999")); numeric->setProperty("edited", true);
        QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
        QCoreApplication::sendEvent(numeric, &escape);
        if(numeric->property("edited").toBool() || !controller.editorVisible()
           || controller.values().value(QStringLiteral("exposure")).toDouble() != fieldBaseline)
        { fail("Escape must discard a numeric draft without leaving the editor"); return; }
        numeric->setProperty("text", QStringLiteral("2.25")); numeric->setProperty("edited", true);
        QKeyEvent enter(QEvent::KeyPress, Qt::Key_Return, Qt::NoModifier);
        QCoreApplication::sendEvent(numeric, &enter);
        break;
      }
      case 91:
      {
        if(qAbs(controller.values().value(QStringLiteral("exposure")).toDouble() - 2.25) > 0.00001)
        { fail("a valid numeric edit did not commit"); return; }
        auto options = findItem(window->contentItem(), QStringLiteral("field-options-exposure"));
        if(!options) { fail("field options are unavailable"); return; }
        QMetaObject::invokeMethod(options, "clicked");
        auto reset = findItem(window->contentItem(), QStringLiteral("reset-exposure"));
        if(!reset || !reset->property("enabled").toBool()) { fail("field reset is unavailable"); return; }
        QMetaObject::invokeMethod(reset, "clicked");
        break;
      }
      case 92:
        if(qAbs(controller.values().value(QStringLiteral("exposure")).toDouble() - fieldDefault) > 0.00001)
        { fail("field reset did not restore the native default"); return; }
        controller.undo();
        break;
      case 93:
        if(qAbs(controller.values().value(QStringLiteral("exposure")).toDouble() - 2.25) > 0.00001)
        { fail("field reset was not undoable"); return; }
        controller.undo();
        break;
      case 94:
        if(controlsFailed || qAbs(controller.values().value(QStringLiteral("exposure")).toDouble() - fieldBaseline) > 0.00001)
        { fail("precision editing did not preserve its previous state"); return; }
        window->contentItem()->forceActiveFocus();
        break;
      case 95:
        drawerCanvasWidth = window->findChild<QQuickItem *>(QStringLiteral("imageStage"))->width();
        window->setProperty("toolsOpen", false);
        window->setProperty("filmstripState", QStringLiteral("hidden"));
        break;
      case 96:
        if(window->findChild<QQuickItem *>(QStringLiteral("imageStage"))->width() <= drawerCanvasWidth
           || window->findChild<QQuickItem *>(QStringLiteral("filmstripBar"))->isVisible())
        { fail("closing tools and photos did not recover canvas space"); return; }
        QMetaObject::invokeMethod(window, "toggleChrome");
        break;
      case 97:
      {
        auto canvas = window->findChild<QQuickItem *>(QStringLiteral("imageStage"));
        if(!window->property("distractionFree").toBool() || canvas->width() < window->width() - 1
           || canvas->height() < window->height() - 1)
        { fail("focus mode did not give the image the full viewport"); return; }
        if(!libraryScreenshot(QStringLiteral("-focus"))) { fail("focus screenshot failed"); return; }
        QMetaObject::invokeMethod(window, "toggleChrome");
        QMetaObject::invokeMethod(findItem(window->contentItem(), QStringLiteral("category-color")), "clicked");
        break;
      }
      case 98:
        if(!window->property("toolsOpen").toBool() || window->property("toolCategory").toString() != QStringLiteral("color")
           || !findItem(window->contentItem(), QStringLiteral("card-agx"))->isVisible()
           || findItem(window->contentItem(), QStringLiteral("card-exposure"))->isVisible())
        { fail("the color category did not open its contextual controls"); return; }
        QMetaObject::invokeMethod(window, "showQuickMenu");
        break;
      case 99:
      {
        auto sheet = window->findChild<QObject *>(QStringLiteral("quickMenu"));
        if(!sheet->property("visible").toBool() || sheet->property("width").toDouble() > 420
           || sheet->property("height").toDouble() > 440 || !libraryScreenshot(QStringLiteral("-actions")))
        { fail("quick actions did not open as a compact sheet"); return; }
        historyWait = 0;
        QMetaObject::invokeMethod(sheet, "close");
        QMetaObject::invokeMethod(findItem(window->contentItem(), QStringLiteral("category-color")), "clicked");
        break;
      }
      case 100:
        if(window->findChild<QObject *>(QStringLiteral("quickMenu"))->property("visible").toBool()) {
          if(++historyWait < 10) return;
          fail("quick actions did not finish closing before the swipe"); return;
        }
        if(window->property("toolsOpen").toBool()) { fail("tapping the active category did not close the drawer"); return; }
        {
          auto testImageStage = window->findChild<QQuickItem *>(QStringLiteral("imageStage"));
          touchPosition = testImageStage->mapToScene(QPointF(testImageStage->width() / 2, testImageStage->height() - 4));
          pointer(QEvent::MouseButtonPress, touchPosition);
        }
        break;
      case 101: case 102:
        touchPosition.ry() -= 40;
        pointer(QEvent::MouseMove, touchPosition);
        break;
      case 103:
        pointer(QEvent::MouseButtonRelease, touchPosition);
        break;
      case 104:
        compactStripHeight = window->findChild<QQuickItem *>(QStringLiteral("filmstripBar"))->height();
        if(!window->findChild<QQuickItem *>(QStringLiteral("filmstripBar"))->isVisible() || compactStripHeight < 76)
        {
          QTextStream(stderr) << "strip state=" << window->property("filmstripState").toString()
            << " zoom=" << window->property("viewZoom").toDouble() << " height=" << compactStripHeight << '\n';
          fail("hidden photos could not be restored"); return;
        }
        QMetaObject::invokeMethod(window, "cycleFilmstrip");
        break;
      case 105:
        if(window->findChild<QQuickItem *>(QStringLiteral("filmstripBar"))->height() <= compactStripHeight)
        { fail("expanded photos did not grow"); return; }
        QMetaObject::invokeMethod(window, "toggleChrome");
        break;
      case 106:
        touchPosition = QPointF(window->width() / 2, window->height() / 2);
        pointer(QEvent::MouseButtonPress, touchPosition);
        break;
      case 107:
        pointer(QEvent::MouseButtonRelease, touchPosition);
        break;
      case 108: case 109:
        break;
      case 110:
        if(window->property("distractionFree").toBool()) { fail("a canvas tap did not restore editor controls"); return; }
        window->setProperty("toolsOpen", true);
        window->setProperty("filmstripState", QStringLiteral("compact"));
        break;
      case 111:
        window->setProperty("viewZoom", 1);
        {
          auto testImageStage = window->findChild<QQuickItem *>(QStringLiteral("imageStage"));
          touchPosition = testImageStage->mapToScene(QPointF(testImageStage->width() / 2, testImageStage->height() / 2));
          pointer(QEvent::MouseButtonPress, touchPosition);
        }
        break;
      case 112:
        pointer(QEvent::MouseButtonRelease, touchPosition);
        break;
      case 113:
        pointer(QEvent::MouseButtonPress, touchPosition); pointer(QEvent::MouseButtonRelease, touchPosition);
        break;
      case 114: case 115:
        break;
      case 116:
        if(window->property("viewZoom").toDouble() != 2 || window->property("distractionFree").toBool())
        { fail("double-tap zoom also toggled editor controls"); return; }
        window->setProperty("viewZoom", 1);
        controller.setEditorVisible(false);
        libraryStage = 24;
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
      else {
        auto import = findItem(window->contentItem(), QStringLiteral("emptyImportButton"));
        if(!import || !import->isVisible() || !import->isEnabled())
        { QTextStream(stderr) << "the empty library needs a primary import action\n"; controller.shutdown(); return; }
        controller.reviewImport({QUrl::fromLocalFile(testPhoto)}, false);
      }
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
           || menu->height() < 56 || menu->width() < 56
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
        if(!filmHandle || !moduleHandle || filmHandle->width() < 112 || filmHandle->height() < 56
           || moduleHandle->width() < 56 || moduleHandle->height() < 112
           || filmstrip->height() < 56
           || filmstrip->mapToScene(QPointF()).y() < filmHandle->mapToScene(QPointF(0, filmHandle->height())).y())
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
            if(imageStage->width() < 319 || strip->height() > window->height() * 0.45 + 1
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
              if(strip->height() < 76 || panel->width() < 320)
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
          auto toggle = findItem(window->contentItem(), QStringLiteral("enable-exposure"));
          if(!exposure || !agx || !toggle || toggle->isVisible() || exposure->isVisible() || !agx->isVisible() || agx->height() <= 64)
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
