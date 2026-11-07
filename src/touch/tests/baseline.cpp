/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/Gamepad.h"
#include "touch/TouchController.h"
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QLocale>
#include <QMouseEvent>
#include <QMutex>
#include <QMutexLocker>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QSaveFile>
#include <QScreen>
#include <QTextStream>
#include <functional>

static QQuickItem *_find_item(QQuickItem *root, const QString &name)
{
  if(root->objectName() == name) return root;
  for(auto child : root->childItems())
    if(auto found = _find_item(child, name)) return found;
  return nullptr;
}

static bool _wait_for(const std::function<bool()> &done, int timeout = 120000)
{
  QElapsedTimer timer;
  timer.start();
  while(!done() && timer.elapsed() < timeout)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  return done();
}

static QQuickItem *_find_photo(QQuickItem *root)
{
  if(root->property("source").toUrl().toString().startsWith("image://photos/current")) return root;
  for(auto child : root->childItems())
    if(auto found = _find_photo(child)) return found;
  return nullptr;
}

int main(int argc, char **argv)
{
  QElapsedTimer clock;
  clock.start();
  qputenv("LANGUAGE", "C");
  QQuickStyle::setStyle(QStringLiteral("Basic"));
  QGuiApplication app(argc, argv);
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  const auto args = app.arguments();
  const auto option = [&](const QString &name) {
    const int index = args.indexOf(name);
    return index >= 0 && index + 1 < args.size() ? args[index + 1] : QString();
  };
  const QString work = option("--work");
  const QString build = option("--build");
  const QString output = option("--output");
  if(work.isEmpty() || build.isEmpty() || output.isEmpty()) return 2;
  auto store = new PreviewStore;
  TouchController controller(store);
  Gamepad gamepad;
  QQmlApplicationEngine engine;
  engine.addImageProvider(QStringLiteral("photos"), store);
  engine.rootContext()->setContextProperty(QStringLiteral("controller"), &controller);
  engine.rootContext()->setContextProperty(QStringLiteral("gamepad"), &gamepad);
  engine.rootContext()->setContextProperty(QStringLiteral("managedSession"), false);
  engine.loadFromModule(QStringLiteral("DarktableTouch"), QStringLiteral("Main"));
  if(engine.rootObjects().isEmpty()) return 3;
  auto window = qobject_cast<QQuickWindow *>(engine.rootObjects().first());
  if(!window) return 3;
  window->resize(1280, 720);
  window->showFullScreen();
  window->requestActivate();
  std::atomic<quint64> frames{0};
  QVector<double> scrollFrames;
  QMutex scrollMutex;
  std::atomic<bool> scrolling{false};
  std::atomic<int> readyForSync{0}, presentedSerial{0};
  int readyForSwap = 0;
  std::atomic<qint64> operationStart{0};
  std::atomic<double> first{-1}, final{-1};
  double previousFrame = 0;
  QObject::connect(window, &QQuickWindow::beforeSynchronizing, &app, [&] {
    readyForSwap = readyForSync.exchange(0);
  }, Qt::DirectConnection);
  QObject::connect(window, &QQuickWindow::frameSwapped, &app, [&] {
    ++frames;
    const double now = clock.nsecsElapsed() / 1e6;
    if(scrolling)
    {
      QMutexLocker lock(&scrollMutex);
      if(previousFrame > 0) scrollFrames.append(now - previousFrame);
      previousFrame = now;
    }
    const qint64 start = operationStart.load();
    if(start && readyForSwap)
    {
      const double elapsed = (clock.nsecsElapsed() - start) / 1e6;
      final = elapsed;
      if(first < 0) first = elapsed;
      presentedSerial = readyForSwap;
    }
  }, Qt::DirectConnection);
  const auto presented = [&] {
    const auto before = frames.load();
    window->update();
    return _wait_for([&] { return frames > before; }, 10000);
  };
  const auto idle = [&](int ms) {
    const qint64 until = clock.elapsed() + ms;
    return _wait_for([&] { return clock.elapsed() >= until; }, ms + 1000);
  };
  const auto finish = [&](int status) {
    controller.shutdown();
    const bool closed = _wait_for([&] { return controller.canClose(); });
    delete window;
    return closed ? status : 4;
  };
  const auto fail = [&](const char *message) {
    QTextStream(stderr) << message << ": " << controller.message() << '\n';
    return finish(4);
  };
  QStringList core{args[0], "--configdir", work + "/config", "--cachedir", work + "/cache",
    "--datadir", build + "/share/darktable", "--moduledir", build + "/lib/darktable",
    "--disable-opencl", "--conf", "write_sidecar_files=never"};
  controller.initialize(core);
  if(!_wait_for([&] { return controller.ready() || controller.hasError(); }) || controller.hasError())
    return fail("engine startup failed");
  if(args.contains("--seed"))
  {
    controller.importPath(work + "/photos");
    if(!_wait_for([&] { return controller.images().size() >= 24 && !controller.busy(); }))
      return fail("fixture import failed");
    controller.setEditorVisible(false);
    return finish(0);
  }
  if(controller.images().size() < 24 || controller.editorVisible()) return fail("invalid baseline catalog");
  auto grid = _find_item(window->contentItem(), "libraryGrid");
  if(!grid || !grid->isVisible() || !presented()) return fail("library not presented");
  QJsonObject result{{"library_from_main_ms", clock.nsecsElapsed() / 1e6}};
  QTextStream(stdout) << "baseline-library-visible\n" << Qt::flush;
  // advance once per presented frame so stalls are retained in the frame intervals
  const double distance = grid->property("contentHeight").toDouble() - grid->height();
  if(distance <= 0) return fail("fixture grid does not scroll");
  scrolling = true;
  for(int step = 0; step < 120; ++step)
  {
    grid->setProperty("contentY", distance * step / 119.0);
    if(!presented()) return fail("scroll did not present");
  }
  scrolling = false;
  QJsonArray intervals;
  {
    QMutexLocker lock(&scrollMutex);
    for(double value : scrollFrames) intervals.append(value);
  }
  if(intervals.size() < 100) return fail("too few scroll frames");
  result.insert("thumbnail_scroll_frame_ms", intervals);
  grid->setProperty("contentY", 0);
  if(!presented()) return fail("grid reset did not present");
  const int firstId = controller.images()[0].toMap().value("imgid").toInt();
  auto thumbnail = _find_item(window->contentItem(), "thumbnail-" + QString::number(firstId));
  if(!thumbnail) return fail("first thumbnail missing");
  window->setProperty("toolsOpen", false);
  window->setProperty("distractionFree", true);
  bool picture = false;
  int imageSerial = 0;
  auto photo = _find_photo(window->contentItem());
  if(!photo) return fail("viewer image missing");
  const auto connection = QObject::connect(&controller, &TouchController::previewChanged, &app, [&] {
    if(operationStart && !store->requestImage("current", nullptr, QSize()).isNull())
    {
      ++imageSerial;
      picture = true;
    }
  });
  const auto frameConnection = QObject::connect(window, &QQuickWindow::afterAnimating, &app, [&] {
    if(operationStart && picture && photo->property("status").toInt() == 1
       && photo->property("source").toUrl().toString() == controller.preview())
    {
      readyForSync = imageSerial;
      picture = false;
    }
  });
  const auto pointer = [&](QEvent::Type type, QPointF position) {
    QMouseEvent event(type, position, window->mapToGlobal(position), Qt::LeftButton,
      type == QEvent::MouseButtonRelease ? Qt::NoButton : Qt::LeftButton, Qt::NoModifier);
    event.setTimestamp(clock.elapsed() + 1);
    QCoreApplication::sendEvent(window, &event);
  };
  const QPointF point = thumbnail->mapToScene(QPointF(thumbnail->width() / 2, thumbnail->height() / 2));
  operationStart = clock.nsecsElapsed();
  pointer(QEvent::MouseButtonPress, point);
  pointer(QEvent::MouseButtonRelease, point);
  if(!_wait_for([&] { return first >= 0 && !controller.busy() && controller.imageId() == firstId; }))
    return fail("tap did not render");
  // include the viewport refinement scheduled by Main.qml's 250 ms timer
  idle(350);
  if(!_wait_for([&] { return !controller.busy() && !picture && presentedSerial == imageSerial; }))
    return fail("render did not finish");
  result.insert("tap_first_picture_ms", first.load());
  result.insert("fullscreen_render_ms", final.load());
  operationStart = 0;
  // initialize the neighbor once, then give idle prefetch time to prepare it
  controller.navigateImage(1);
  if(!_wait_for([&] { return !controller.busy(); })) return fail("neighbor did not render");
  idle(350);
  if(!_wait_for([&] { return !controller.busy(); })) return fail("neighbor refinement failed");
  const int neighborId = controller.imageId();
  controller.selectImage(firstId);
  if(!_wait_for([&] { return !controller.busy(); })) return fail("return did not finish");
  if(!_wait_for([&] { return !store->preparedFrames()->get(neighborId).isNull(); }, 60000))
    return fail("neighbor was not prefetched");
  first = -1;
  picture = false;
  operationStart = clock.nsecsElapsed();
  // the same navigation entry point used when Main.qml recognizes a swipe
  controller.navigateImage(1);
  if(!_wait_for([&] { return first >= 0 && !controller.busy(); })) return fail("swipe did not present");
  result.insert("prefetched_swipe_ms", first.load());
  operationStart = 0;
  QObject::disconnect(connection);
  QObject::disconnect(frameConnection);
  result.insert("render_width", store->requestImage("current", nullptr, QSize()).width());
  result.insert("render_height", store->requestImage("current", nullptr, QSize()).height());
  result.insert("graphics_api", int(window->rendererInterface()->graphicsApi()));
  result.insert("qt_version", qVersion());
  result.insert("engine_version", dt_touch_version());
  result.insert("screen_refresh_hz", window->screen()->refreshRate());
  result.insert("device_pixel_ratio", window->devicePixelRatio());
  result.insert("platform_plugin", QGuiApplication::platformName());
  result.insert("window_width", window->width());
  result.insert("window_height", window->height());
  const QString capture = option("--capture");
  if(!capture.isEmpty() && !window->grabWindow().save(capture)) return fail("could not capture viewer");
  QSaveFile file(output);
  const auto bytes = QJsonDocument(result).toJson();
  if(!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
    return fail("could not save baseline round");
  controller.setEditorVisible(false);
  return finish(0);
}
