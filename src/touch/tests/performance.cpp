/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/TouchController.h"
#include <QGuiApplication>
#include <QDir>
#include <QDirIterator>
#include <QMap>
#include <QFile>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTextStream>
#include <algorithm>
#include <functional>

static bool waitFor(const std::function<bool()> &done, int timeout = 120000)
{
  QElapsedTimer timer;
  timer.start();
  while(!done() && timer.elapsed() < timeout)
  {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(2);
  }
  return done();
}

static int _max_channel_difference(const QImage &a, const QImage &b)
{
  if(a.size() != b.size()) return 255;
  int maximum = 0;
  for(int y = 0; y < a.height(); y++)
    for(int x = 0; x < a.width() * 3; x++)
      maximum = std::max(maximum, std::abs(int(a.constScanLine(y)[x]) - b.constScanLine(y)[x]));
  return maximum;
}

int main(int argc, char **argv)
{
  QGuiApplication app(argc, argv);
  const auto arguments = app.arguments();
  const auto option = [&](const QString &name, const QString &fallback) {
    const int pos = arguments.indexOf(name);
    return pos >= 0 && pos + 1 < arguments.size() ? arguments[pos + 1] : fallback;
  };
  const QString root = option("--root", QString());
  const int rounds = option("--rounds", "20").toInt();
  const int idle = option("--idle-ms", "750").toInt();
  const bool rounding = arguments.contains("--opencl") && arguments.contains("--diagnose-gpu-rounding");
  const bool resumeModules = arguments.contains("--resume-module-edits");
  if(root.isEmpty()) return 2;
  QTemporaryDir temporary;
  if(!temporary.isValid()) return 2;
  QStringList sources;
  QMap<QString, QStringList> folders;
  QDirIterator files(root, {"*.CR3", "*.cr3"}, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
  while(files.hasNext())
  {
    const QFileInfo file(files.next());
    folders[file.absolutePath()].append(file.absoluteFilePath());
  }
  for(auto names : folders)
  {
    if(names.size() < 5 || sources.size() >= 10) continue;
    names.sort();
    for(int i = 0; i < 5; ++i)
    {
      const QString original = names[i];
      const QString copy = QDir(temporary.path()).filePath(QString::number(sources.size()) + ".CR3");
      if(!QFile::copy(original, copy)) return 2;
      QFile xmp(original + ".xmp");
      if(xmp.open(QIODevice::ReadOnly))
      {
        auto bytes = xmp.readAll();
        bytes.replace("xmp:Rating=\"-1\"", "xmp:Rating=\"0\"");
        QFile output(copy + ".xmp");
        if(!output.open(QIODevice::WriteOnly) || output.write(bytes) != bytes.size()) return 2;
      }
      sources.append(copy);
    }
  }
  if(sources.size() != 10) return 2;
#ifdef DT_TOUCH_FRAME_CACHE_TEST
  PreparedFrames cache;
  QImage sample(16, 16, QImage::Format_RGB32);
  sample.fill(Qt::red);
  const auto stale = cache.generation();
  cache.put(1, sample, sources[0], stale);
  if(cache.get(1).isNull()) return 11;
  cache.remove(1);
  cache.put(1, sample, sources[0], stale);
  if(!cache.get(1).isNull()) return 11;
  cache.put(1, sample, sources[0], cache.generation());
  if(!QFile::rename(sources[0], sources[0] + ".offline")) return 11;
  if(!cache.get(1).isNull()) return 11;
  if(!QFile::rename(sources[0] + ".offline", sources[0])) return 11;
  for(int id = 2; id <= 7; id++) cache.put(id, sample, sources[0], cache.generation());
  if(!cache.get(1).isNull() || cache.bytes() != 5 * quint64(sample.sizeInBytes())) return 11;
  QTextStream(stderr) << "passed: prepared frame invalidation, missing source, and eviction\n";
#endif
  PreviewStore store;
  TouchController controller(&store);
  QObject::connect(&controller, &TouchController::stateChanged, &app, [&] {
    if(controller.hasError()) QTextStream(stderr) << "engine error: " << controller.message() << '\n';
  });
  QStringList core{arguments[0], "--configdir", QDir(temporary.path()).filePath("config"),
    "--cachedir", option("--cache-dir", QDir(temporary.path()).filePath("cache")), "--conf", "write_sidecar_files=never",
    "--datadir", option("--datadir", QDir::current().filePath("build/share/darktable")),
    "--moduledir", option("--moduledir", QDir::current().filePath("build/lib/darktable"))};
  if(arguments.contains("--opencl"))
    core << "--conf" << "clplatform_amdacceleratedparallelprocessing=true" << "--conf" << "opencl=true";
  else core << "--disable-opencl";
  core << "-d" << "memory" << "-d" << "perf";
  if(arguments.contains("--opencl")) core << "-d" << "opencl";
  controller.initialize(core);
  if(!waitFor([&] { return controller.ready() || controller.hasError(); }) || controller.hasError()) return 3;
  controller.importSelectedPaths(sources);
  if(!waitFor([&] { return controller.images().size() == 10 && !controller.busy(); }))
  {
    QTextStream(stderr) << "import did not finish: " << controller.images().size() << ' ' << controller.message() << '\n';
    return 4;
  }
  QVector<int> ids;
  for(const auto &entry : controller.images()) ids.append(entry.toMap().value("imgid").toInt());
  QJsonArray samples;
  QHash<int, QByteArray> hashes;
  QHash<int, QImage> referencePixels;
  QElapsedTimer operation;
  double first = -1, last = -1;
  QImage firstPixels;
  const auto connection = QObject::connect(&controller, &TouchController::previewChanged, &app, [&] {
    if(!operation.isValid()) return;
    last = operation.nsecsElapsed() / 1e6;
    if(first < 0)
    {
      first = last;
      firstPixels = store.requestImage("current", nullptr, QSize());
    }
  });
  const auto run = [&](int id, const QString &kind, int round, const std::function<void()> &start) {
    first = last = -1;
    firstPixels = {};
    operation.start();
    start();
    if(!waitFor([&] { return last >= 0 && !controller.busy() && controller.imageId() == id; }) || controller.hasError())
    {
      QTextStream(stderr) << "failed " << kind << " image=" << id << " active=" << controller.imageId()
                          << " first=" << first << " last=" << last << ": " << controller.message() << '\n';
      return false;
    }
    operation.invalidate();
    const QImage pixels = store.requestImage("current", nullptr, QSize());
    const QImage rgb = pixels.convertToFormat(QImage::Format_RGB888);
    QCryptographicHash checksum(QCryptographicHash::Sha256);
    for(int y = 0; y < rgb.height(); y++)
      checksum.addData(QByteArrayView(reinterpret_cast<const char *>(rgb.constScanLine(y)), rgb.width() * 3));
    const auto hash = checksum.result();
    if((kind == "switch" || kind == "warm-switch") && firstPixels.size() == pixels.size()
       && _max_channel_difference(firstPixels.convertToFormat(QImage::Format_RGB888), rgb) > (rounding ? 1 : 0))
    {
      QTextStream(stderr) << "first frame belongs to a different edit or image: " << id << '\n';
      return false;
    }
    int repeatDelta = 0;
    if(kind == "cold") { hashes[id] = hash; referencePixels[id] = rgb; }
    else if((kind == "switch" || kind == "warm-switch") && hashes.value(id) != hash)
    {
      repeatDelta = _max_channel_difference(referencePixels.value(id), rgb);
      if(!rounding || repeatDelta > 1)
      {
        QTextStream(stderr) << "preview changed without an edit for image " << id << ": " << repeatDelta << '\n';
        return false;
      }
    }
    const QString frames = option("--frames-dir", QString());
    if(kind == "cold" && !frames.isEmpty())
    {
      QDir().mkpath(frames);
      if(!pixels.save(QDir(frames).filePath(QString::number(id) + ".png"))) return false;
    }
    samples.append(QJsonObject{{"kind", kind}, {"image", id}, {"round", round},
                               {"first_ms", first}, {"final_ms", last}, {"repeat_max_channel_delta", repeatDelta},
                               {"rgb_sha256", QString::fromLatin1(hash.toHex())}});
    QFile checkpoint(option("--output", "build/touch-performance.json"));
    if(checkpoint.open(QIODevice::WriteOnly))
    {
      checkpoint.write(QJsonDocument(QJsonObject{{"opencl_requested", arguments.contains("--opencl")},
        {"heavy_modules", arguments.contains("--heavy")}, {"gpu_rounding_diagnostic", rounding}, {"resume_module_edits", resumeModules},
        {"rounds", rounds}, {"idle_ms", idle}, {"samples", samples}}).toJson());
      checkpoint.flush();
    }
    QTextStream(stderr) << kind << ' ' << round << ' ' << id << " first=" << first << " final=" << last << '\n';
    QElapsedTimer pause;
    pause.start();
    waitFor([&] { return pause.elapsed() >= idle; }, idle + 1000);
    return true;
  };
  const auto lensField = [&] {
    const auto values = controller.values();
    const int method = values.value("method").toInt();
    if(method == 0) return values.value("md_version").toInt() == 0
      ? QStringLiteral("scale_md_v1") : QStringLiteral("scale_md");
    return method == 1 ? QStringLiteral("scale") : QStringLiteral("v_strength");
  };
  if(arguments.contains("--probe-lens"))
  {
    const int id = ids[1];
    if(!run(id, "probe-open", 0, [&] { controller.selectImage(id); })
       || !run(id, "probe-enable-lens", 0, [&] { controller.setEnabledFor("lens", true); })) return 9;
    controller.selectTool("lens");
    if(!waitFor([&] { return controller.values().contains("method"); })) return 9;
    const auto values = controller.values();
    QJsonObject diagnostic;
    for(const char *key : {"method", "md_version", "scale", "scale_md", "scale_md_v1", "v_strength", "camera", "lens"})
      diagnostic.insert(QString::fromLatin1(key), QJsonValue::fromVariant(values.value(QString::fromLatin1(key))));
    QTextStream(stderr) << "lens parameters: " << QJsonDocument(diagnostic).toJson(QJsonDocument::Compact) << '\n';
    const QString field = lensField();
    const bool vignette = field == "v_strength";
    for(int i = 0; i < 2; i++)
      if(!run(id, "lens-probe", i, [&] { controller.setParameter(field, vignette ? (i ? 0.5 : 0.25) : (i ? 1.0 : 1.05), true, "lens"); })) return 9;
    if(samples.last().toObject().value("rgb_sha256") == samples[samples.size() - 2].toObject().value("rgb_sha256")) return 9;
    controller.shutdown();
    return waitFor([&] { return controller.canClose(); }) ? 0 : 9;
  }
  if(arguments.contains("--heavy"))
    for(int id : resumeModules ? QVector<int>{ids[1]} : ids)
    {
      if(!run(id, "setup", 0, [&] { controller.selectImage(id); })) return 9;
      for(const QString &module : {QStringLiteral("agx"), QStringLiteral("lens"), QStringLiteral("denoiseprofile")})
      {
        if(!run(id, "enable-" + module, 0, [&] { controller.setEnabledFor(module, true); })) return 9;
        if(!controller.moduleStates().value(module).toBool()) return 9;
      }
    }
  for(int round = 0; !resumeModules && round <= rounds; ++round)
  {
    auto sequence = ids;
    if(round % 2) std::reverse(sequence.begin(), sequence.end());
    for(int id : sequence)
      if(!run(id, round ? "switch" : "cold", round, [&] { controller.selectImage(id); })) return 5;
  }
  if(!resumeModules && arguments.contains("--warm-pair"))
  {
    if(!run(ids[0], "prepare-warm", 0, [&] { controller.selectImage(ids[0]); })) return 5;
    QElapsedTimer warm;
    warm.start();
    waitFor([&] { return warm.elapsed() >= 5000; }, 6000);
    for(int i = 0; i < 40; ++i)
    {
      const int id = ids[i % 2];
      if(!run(id, "warm-switch", i, [&] { controller.selectImage(id); })) return 5;
    }
  }
  if(resumeModules)
  {
    if(!run(ids[1], "seed-exposure", 0, [&] { controller.setParameter("exposure", 0.25, true, "exposure"); })
       || !run(ids[1], "seed-agx", 0, [&] { controller.setParameter("look_saturation", 1.0, true, "agx"); })) return 6;
  }
  for(int i = 0; !resumeModules && i < 20; ++i)
    if(!run(controller.imageId(), "exposure", i, [&] { controller.setParameter("exposure", i % 2 ? 0.25 : 0.5, true, "exposure"); })) return 6;
  if(arguments.contains("--heavy"))
    for(const auto &edit : {QStringList{"agx", "look_saturation"}, QStringList{"lens", "scale"},
                            QStringList{"denoiseprofile", "strength"}})
    {
      if(resumeModules && edit[0] == "agx") continue;
      controller.selectTool(edit[0]);
      if(!waitFor([&] { return controller.values().contains(edit[1]); })) return 6;
      const QString field = edit[0] == "lens" ? lensField() : edit[1];
      QTextStream(stderr) << "edit field: " << edit[0] << '.' << field << '\n';
      QByteArray previous;
      for(int i = 0; i < 20; ++i)
      {
        if(!run(controller.imageId(), edit[0] + "-edit", i, [&] {
          const double value = field == "v_strength" ? (i % 2 ? 0.5 : 0.25)
            : edit[0] == "lens" ? (i % 2 ? 1.0 : 1.05) : (i % 2 ? 1.0 : 1.2);
          controller.setParameter(field, value, true, edit[0]);
        })) return 6;
        const auto hash = samples.last().toObject().value("rgb_sha256").toString().toLatin1();
        if(hash == previous)
        {
          QTextStream(stderr) << "module did not change visible pixels: " << edit[0] << '\n';
          return 6;
        }
        previous = hash;
      }
    }
  controller.selectTool("exposure");
  if(!waitFor([&] { return controller.values().contains("exposure"); })) return 6;
  if(arguments.contains("--stress"))
  {
    if(!run(ids[5], "interrupt-render", 0, [&] {
      controller.selectImage(ids[8]);
      QTimer::singleShot(100, &controller, [&] { controller.selectImage(ids[5]); });
    })) return 10;
    if(!run(ids[3], "rapid-navigation", 0, [&] {
      controller.selectImage(ids[0]); controller.selectImage(ids[1]);
      controller.selectImage(ids[2]); controller.selectImage(ids[3]);
    })) return 10;
    controller.setParameter("exposure", 0.75, true, "exposure");
    if(!run(ids[4], "commit-and-switch", 0, [&] { controller.selectImage(ids[4]); })) return 10;
    if(!run(ids[3], "restore-committed-image", 0, [&] { controller.selectImage(ids[3]); })) return 10;
    if(controller.values().value("exposure").toDouble() != 0.75) return 10;
    const QImage protectedFrame = store.requestImage("current", nullptr, QSize());
    if(!QFile::rename(sources[6], sources[6] + ".offline")) return 10;
    controller.selectImage(ids[6]);
    if(!waitFor([&] { return controller.hasError() && !controller.busy(); }) || controller.imageId() != ids[3]) return 10;
    if(store.requestImage("current", nullptr, QSize()) != protectedFrame) return 10;
    if(!QFile::rename(sources[6] + ".offline", sources[6])) return 10;
    if(!run(ids[3], "recover-missing-source", 0, [&] { controller.selectImage(ids[3]); })) return 10;
    qputenv("DARKTABLETOUCH_TEST_AVAILABLE_MEMORY_MIB", "1024");
    if(!run(ids[4], "low-memory-switch", 0, [&] { controller.selectImage(ids[4]); })) return 10;
    qunsetenv("DARKTABLETOUCH_TEST_AVAILABLE_MEMORY_MIB");
    if(!run(ids[3], "recover-memory", 0, [&] { controller.selectImage(ids[3]); })) return 10;
    if(controller.values().value("exposure").toDouble() != 0.75) return 10;
  }
  QObject::disconnect(connection);
  QFile output(option("--output", "build/touch-performance.json"));
  if(!output.open(QIODevice::WriteOnly)) return 7;
  output.write(QJsonDocument(QJsonObject{{"opencl_requested", arguments.contains("--opencl")},
    {"heavy_modules", arguments.contains("--heavy")}, {"gpu_rounding_diagnostic", rounding}, {"resume_module_edits", resumeModules},
    {"rounds", rounds}, {"idle_ms", idle}, {"samples", samples}}).toJson());
  controller.shutdown();
  if(!waitFor([&] { return controller.canClose(); })) return 8;
  return 0;
}
