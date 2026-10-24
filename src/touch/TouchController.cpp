/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/TouchController.h"
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonValue>
#include <QUrl>
#include <algorithm>

namespace {
QJsonDocument takeJson(char *text)
{
  const auto document = QJsonDocument::fromJson(text ? QByteArray(text) : QByteArray());
  dt_touch_free(text);
  return document;
}

bool validText(const QString &text)
{
  return !text.contains(QChar::Null) && QString::fromUtf8(text.toUtf8()) == text;
}
}

int FieldModel::rowCount(const QModelIndex &parent) const
{
  return parent.isValid() ? 0 : int(m_rows.size());
}

QVariant FieldModel::data(const QModelIndex &index, int role) const
{
  return index.isValid() && index.row() < m_rows.size() && role == Qt::UserRole + 1
    ? m_rows[index.row()] : QVariant();
}

QHash<int, QByteArray> FieldModel::roleNames() const
{
  return {{Qt::UserRole + 1, QByteArrayLiteral("field")}};
}

void FieldModel::setFields(const QVariantList &fields)
{
  bool same = fields.size() == m_rows.size();
  for(int i = 0; same && i < fields.size(); i++)
    same = fields[i].toMap().value(QStringLiteral("name")) == m_rows[i].toMap().value(QStringLiteral("name"));
  if(!same)
  {
    beginResetModel(); m_rows = fields; endResetModel(); return;
  }
  for(int i = 0; i < fields.size(); i++)
    if(m_rows[i] != fields[i])
    {
      m_rows[i] = fields[i];
      emit dataChanged(index(i), index(i), {Qt::UserRole + 1});
    }
}

QVariantMap FieldModel::values() const
{
  QVariantMap result;
  for(const auto &row : m_rows)
  {
    const auto field = row.toMap();
    result.insert(field.value(QStringLiteral("name")).toString(), field.value(QStringLiteral("value")));
  }
  return result;
}

namespace {
QImage takePixels(uint8_t *pixels, int width, int height)
{
  return QImage(pixels, width, height, width * 4, QImage::Format_RGB32,
                 dt_touch_free, pixels);
}
}

PreviewStore::PreviewStore() : QQuickImageProvider(QQuickImageProvider::Image), m_thumbnails(64 * 1024) {}

QImage PreviewStore::requestImage(const QString &id, QSize *size, const QSize &requested)
{
  QWriteLocker lock(&m_lock);
  QImage image;
  if(id.startsWith(QStringLiteral("thumb/")))
  {
    const int key = id.section('/', 1, 1).section('?', 0, 0).toInt();
    if(const auto cached = m_thumbnails.object(key)) image = *cached;
  }
  else image = id.startsWith(QStringLiteral("before")) ? m_before : m_frame;
  if(image.isNull())
  {
    image = QImage(16, 16, QImage::Format_RGB32);
    image.fill(QColor(QStringLiteral("#202226")));
  }
  if(size) *size = image.size();
  return requested.isValid() ? image.scaled(requested, Qt::KeepAspectRatio, Qt::SmoothTransformation) : image;
}

void PreviewStore::setFrame(const QImage &image, bool before)
{
  QWriteLocker lock(&m_lock);
  if(before) m_before = image;
  else m_frame = image;
}

void PreviewStore::setThumbnail(int image, const QImage &thumbnail)
{
  QWriteLocker lock(&m_lock);
  m_thumbnails.insert(image, new QImage(thumbnail), int(thumbnail.sizeInBytes() / 1024 + 1));
}

bool PreviewStore::hasThumbnail(int image) const
{
  QReadLocker lock(&m_lock);
  return m_thumbnails.contains(image);
}

EngineWorker::EngineWorker(std::shared_ptr<std::atomic<quint64>> revision) : m_revision(std::move(revision)) {}

void EngineWorker::report(char *error)
{
  emit failed(error ? QString::fromUtf8(error) : tr("the photo engine could not complete this operation"));
  dt_touch_free(error);
}

QJsonArray EngineWorker::images()
{
  char *error = nullptr;
  char *text = dt_touch_images(&error);
  if(error) report(error);
  return takeJson(text).array();
}

void EngineWorker::initialize(QStringList arguments)
{
  std::vector<QByteArray> owned;
  for(const QString &argument : arguments)
  {
    if(!validText(argument))
    { emit failed(tr("a command-line argument cannot be represented as valid UTF-8")); return; }
    owned.push_back(argument.toUtf8());
  }
  std::vector<char *> argv;
  for(auto &argument : owned) argv.push_back(argument.data());
  argv.push_back(nullptr);
  m_initialized = dt_touch_initialize(int(owned.size()), argv.data());
  if(!m_initialized)
  {
    emit failed(tr("could not initialize the photo engine; check the catalog lock and data directories"));
    return;
  }
  char *error = nullptr;
  char *session = dt_touch_session_json(&error);
  if(error) { report(error); error = nullptr; }
  auto view = takeJson(session).object();
  if(!qEnvironmentVariableIsSet("DARKTABLETOUCH_RESTORE_SESSION"))
    for(size_t i = 1; i < argv.size() && argv[i]; i++)
    {
      char *imported = dt_touch_import(argv[i], &error);
      const auto result = takeJson(imported).object().value(QStringLiteral("images")).toArray();
      if(error) { report(error); error = nullptr; }
      if(!result.isEmpty() && QFileInfo(QString::fromUtf8(argv[i])).isFile())
      {
        view = QJsonObject{{QStringLiteral("view"), QStringLiteral("editor")},
          {QStringLiteral("image_id"), result.first().toObject().value(QStringLiteral("imgid"))},
          {QStringLiteral("zoom_factor"), 1}, {QStringLiteral("center_x"), 0.5}, {QStringLiteral("center_y"), 0.5}};
      }
    }
  emit initialized(images(), takeJson(dt_touch_modules()).array(), view);
}

void EngineWorker::importPaths(QStringList paths)
{
  if(!m_initialized) return;
  for(const QString &path : paths)
  {
    char *error = nullptr;
    char *result = dt_touch_import(path.toUtf8().constData(), &error);
    const auto object = takeJson(result).object();
    if(error) report(error);
    else if(object.value(QStringLiteral("failed")).toInt() > 0)
      emit failed(tr("some files could not be imported; check the source format and access permissions"));
  }
  emit imagesChanged(images());
}

void EngineWorker::clearHistory()
{
  dt_touch_state_free(m_before);
  m_before = nullptr;
  for(const auto &edit : m_edits)
  {
    dt_touch_state_free(edit.before);
    dt_touch_state_free(edit.after);
  }
  m_edits.clear();
  m_position = 0;
  emit historyChanged(false, false);
}

void EngineWorker::openImage(int id, quint64 revision)
{
  if(!m_initialized) return;
  char *error = nullptr;
  if(!dt_touch_editor_save(m_editor, &error)) { report(error); return; }
  auto next = dt_touch_editor_new(id, &error);
  if(!next) { report(error); return; }
  clearHistory();
  dt_touch_editor_free(m_editor);
  m_editor = next;
  m_imageId = id;
  emit imageOpened(id, revision);
  loadModule(m_operation, revision);
  render(revision);
}

void EngineWorker::loadModule(QString operation, quint64 revision)
{
  if(!m_editor) return;
  m_operation = operation;
  char *error = nullptr;
  char *text = dt_touch_editor_module(m_editor, operation.toUtf8().constData(), &error);
  if(!text) { report(error); return; }
  emit moduleReady(takeJson(text).object(), revision);
  emit historyReady(takeJson(dt_touch_editor_history(m_editor)).object(), revision);
}

void EngineWorker::beginEdit()
{
  if(!m_before) m_before = dt_touch_editor_capture(m_editor);
}

void EngineWorker::commitEdit()
{
  if(!m_before) return;
  while(m_edits.size() > m_position)
  {
    dt_touch_state_free(m_edits.back().before);
    dt_touch_state_free(m_edits.back().after);
    m_edits.pop_back();
  }
  m_edits.push_back({m_before, dt_touch_editor_capture(m_editor)});
  m_before = nullptr;
  if(m_edits.size() > 100)
  {
    dt_touch_state_free(m_edits.front().before);
    dt_touch_state_free(m_edits.front().after);
    m_edits.erase(m_edits.begin());
  }
  m_position = m_edits.size();
  emit historyChanged(m_position > 0, false);
  char *error = nullptr;
  if(!dt_touch_editor_save(m_editor, &error)) report(error);
  else emit completed(tr("edits saved"));
}

void EngineWorker::setParameter(QString operation, QString name, QVariant value,
                                 bool commit, quint64 revision)
{
  if(!m_editor) return;
  beginEdit();
  const auto document = QJsonDocument(QJsonArray{QJsonValue::fromVariant(value)}).toJson(QJsonDocument::Compact);
  const auto scalar = document.mid(1, document.size() - 2);
  char *error = nullptr;
  if(!dt_touch_editor_set(m_editor, operation.toUtf8().constData(), name.toUtf8().constData(),
                           scalar.constData(), &error)) { report(error); return; }
  if(commit) commitEdit();
  loadModule(operation, revision);
  render(revision, commit ? 1280 : 640);
}

void EngineWorker::changeEnabled(QString operation, bool enabled, quint64 revision)
{
  if(!m_editor) return;
  beginEdit();
  char *error = nullptr;
  if(!dt_touch_editor_enable(m_editor, operation.toUtf8().constData(), enabled, &error))
  { report(error); return; }
  commitEdit();
  loadModule(operation, revision);
  render(revision);
}

void EngineWorker::resetModule(QString operation, quint64 revision)
{
  if(!m_editor) return;
  beginEdit();
  char *error = nullptr;
  if(!dt_touch_editor_reset(m_editor, operation.toUtf8().constData(), &error))
  { report(error); return; }
  commitEdit();
  loadModule(operation, revision);
  render(revision);
}

void EngineWorker::historyStep(int direction, quint64 revision)
{
  if(!m_editor || (direction < 0 && !m_position)
     || (direction > 0 && m_position == m_edits.size())) return;
  const auto state = direction < 0 ? m_edits[m_position - 1].before : m_edits[m_position].after;
  char *error = nullptr;
  if(!dt_touch_editor_restore(m_editor, state, &error)) { report(error); return; }
  if(direction < 0) --m_position;
  else ++m_position;
  if(!dt_touch_editor_save(m_editor, &error)) report(error);
  else emit completed(tr("edits saved"));
  emit historyChanged(m_position > 0, m_position < m_edits.size());
  loadModule(m_operation, revision);
  render(revision);
}

void EngineWorker::render(quint64 revision, int extent)
{
  if(!m_editor || revision != m_revision->load()) return;
  uint8_t *pixels = nullptr;
  int width = 0, height = 0;
  char *error = nullptr;
  if(!dt_touch_editor_preview(m_editor, extent, extent, &pixels, &width, &height, &error))
  { report(error); return; }
  emit frameReady(takePixels(pixels, width, height), revision);
}

void EngineWorker::jumpHistory(int end, quint64 revision)
{
  if(!m_editor) return;
  beginEdit();
  char *error = nullptr;
  if(!dt_touch_editor_history_select(m_editor, end, &error)) { report(error); return; }
  commitEdit();
  loadModule(m_operation, revision);
  render(revision);
}

void EngineWorker::thumbnail(int id)
{
  if(!m_initialized) return;
  uint8_t *pixels = nullptr;
  int width = 0, height = 0;
  char *error = nullptr;
  if(!dt_touch_thumbnail(id, &pixels, &width, &height, &error))
  {
    dt_touch_free(error);
    emit thumbnailReady(id, QImage());
    return;
  }
  emit thumbnailReady(id, QImage(pixels, width, height, width * 4, QImage::Format_RGBX8888,
                                  dt_touch_free, pixels));
}

void EngineWorker::exportImage(QString path, QString format)
{
  if(!m_editor) return;
  char *error = nullptr;
  if(!dt_touch_editor_export(m_editor, path.toUtf8().constData(), format.toUtf8().constData(), &error))
  { report(error); return; }
  emit completed(tr("exported to %1").arg(path));
}

void EngineWorker::shutdown()
{
  if(!m_initialized) { emit stopped(); return; }
  char *error = nullptr;
  if(!dt_touch_editor_save(m_editor, &error)) { m_nextInterface.clear(); report(error); return; }
  if(!dt_touch_save_session(m_nextInterface.toUtf8().constData(), m_imageId, m_editorVisible,
                             m_zoom, m_centerX, m_centerY, &error)) { m_nextInterface.clear(); report(error); return; }
  clearHistory();
  dt_touch_editor_free(m_editor);
  m_editor = nullptr;
  if(m_initialized) dt_touch_shutdown();
  m_initialized = false;
  emit stopped();
}

void EngineWorker::saveView(int image, bool editor, double zoom, double centerX, double centerY)
{
  m_imageId = image;
  m_editorVisible = editor;
  m_zoom = zoom; m_centerX = centerX; m_centerY = centerY;
}

void EngineWorker::switchInterface(QString mode)
{
  if(mode != QStringLiteral("touch") && mode != QStringLiteral("classic")) return;
  m_nextInterface = mode;
  shutdown();
}

TouchController::TouchController(PreviewStore *store, QObject *parent)
  : QObject(parent), m_store(store), m_revision(std::make_shared<std::atomic<quint64>>(0))
{
  m_fields.setParent(this);
  m_worker = new EngineWorker(m_revision);
  m_worker->moveToThread(&m_thread);
  connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(this, &TouchController::boot, m_worker, &EngineWorker::initialize);
  connect(this, &TouchController::importRequested, m_worker, &EngineWorker::importPaths);
  connect(this, &TouchController::openRequested, m_worker, &EngineWorker::openImage);
  connect(this, &TouchController::moduleRequested, m_worker, &EngineWorker::loadModule);
  connect(this, &TouchController::parameterRequested, m_worker, &EngineWorker::setParameter);
  connect(this, &TouchController::enabledRequested, m_worker, &EngineWorker::changeEnabled);
  connect(this, &TouchController::resetRequested, m_worker, &EngineWorker::resetModule);
  connect(this, &TouchController::historyRequested, m_worker, &EngineWorker::historyStep);
  connect(this, &TouchController::historyJumpRequested, m_worker, &EngineWorker::jumpHistory);
  connect(this, &TouchController::thumbnailRequested, m_worker, &EngineWorker::thumbnail);
  connect(this, &TouchController::exportRequested, m_worker, &EngineWorker::exportImage);
  connect(this, &TouchController::shutdownRequested, m_worker, &EngineWorker::shutdown);
  connect(this, &TouchController::viewRequested, m_worker, &EngineWorker::saveView);
  connect(this, &TouchController::switchRequested, m_worker, &EngineWorker::switchInterface);
  connect(m_worker, &EngineWorker::initialized, this, [this](QJsonArray images, QJsonArray modules, QJsonObject session) {
    updateImages(images);
    const QStringList operations{QStringLiteral("exposure"), QStringLiteral("agx"), QStringLiteral("lens"),
      QStringLiteral("denoiseprofile"), QStringLiteral("flip"), QStringLiteral("crop")};
    const QStringList labels{QStringLiteral("exposure"), QStringLiteral("AgX"), QStringLiteral("lens correction"),
      QStringLiteral("denoise (profiled)"), QStringLiteral("rotation"), QStringLiteral("crop")};
    const QStringList missing{
      QStringLiteral("picker, presets, and full workflow validation"),
      QStringLiteral("range/tone/look organization, graphs, presets, and full workflow validation"),
      QStringLiteral("camera/lens selection, methods, presets, and full workflow validation"),
      QStringLiteral("profile selection, wavelet interactions, presets, and full workflow validation"),
      QStringLiteral("rotate-and-perspective adapter, straightening, guides, presets, and full workflow validation"),
      QStringLiteral("frame manipulation, aspect ratios, apply/cancel, presets, and full workflow validation")};
    for(const auto &module : modules)
    {
      auto entry = module.toObject().toVariantMap();
      const int priority = operations.indexOf(entry.value(QStringLiteral("operation")).toString());
      entry.insert(QStringLiteral("priority"), priority + 1);
      entry.insert(QStringLiteral("touch_status"), priority < 0 ? QStringLiteral("not_started") : QStringLiteral("partial"));
      entry.insert(QStringLiteral("missing_features"), priority < 0
        ? QStringLiteral("Touch adapter and user-facing controls") : missing[priority]);
      m_moduleCoverage.append(entry);
    }
    for(int i = 0; i < operations.size(); ++i)
    {
      bool available = false;
      for(const auto &module : modules)
        if(module.toObject().value(QStringLiteral("operation")).toString() == operations[i]) available = true;
      m_tools.append(QVariantMap{{QStringLiteral("operation"), operations[i]},
                                 {QStringLiteral("label"), labels[i]}, {QStringLiteral("available"), available}});
    }
    m_ready = true;
    m_busy = false;
    if(!m_failed) m_message = tr("ready");
    emit stateChanged();
    if(session.value(QStringLiteral("image_id")).toInt() > 0)
    {
      m_restoreView = session;
      selectImage(session.value(QStringLiteral("image_id")).toInt());
      setEditorVisible(session.value(QStringLiteral("view")).toString() == QStringLiteral("editor"));
    }
  });
  connect(m_worker, &EngineWorker::imagesChanged, this, &TouchController::updateImages);
  connect(m_worker, &EngineWorker::imageOpened, this, [this](int id, quint64 revision) {
    if(revision != m_revision->load()) return;
    m_imageId = id;
    m_failed = false;
    m_needBefore = true;
    emit stateChanged();
  });
  connect(m_worker, &EngineWorker::moduleReady, this, [this](QJsonObject module, quint64 revision) {
    if(revision != m_revision->load()) return;
    if(module.value(QStringLiteral("operation")).toString() != m_operation) return;
    auto fields = module.value(QStringLiteral("fields")).toArray().toVariantList();
    if(module.value(QStringLiteral("operation")).toString() == QStringLiteral("exposure"))
    {
      const QStringList order{QStringLiteral("exposure"), QStringLiteral("black"), QStringLiteral("mode")};
      const auto rank = [&order](const QVariant &value) {
        const int position = order.indexOf(value.toMap().value(QStringLiteral("name")).toString());
        return position < 0 ? 100 : position;
      };
      std::stable_sort(fields.begin(), fields.end(), [&rank](const QVariant &a, const QVariant &b) { return rank(a) < rank(b); });
    }
    m_fields.setFields(fields);
    m_toolLabel = module.value(QStringLiteral("label")).toString();
    m_moduleEnabled = module.value(QStringLiteral("enabled")).toBool();
    emit moduleChanged();
  });
  connect(m_worker, &EngineWorker::frameReady, this, [this](QImage image, quint64 revision) {
    if(revision != m_revision->load()) return;
    m_store->setFrame(image);
    if(m_needBefore) { m_store->setFrame(image, true); m_needBefore = false; }
    m_frameRevision = revision;
    m_busy = false;
    if(!m_failed) m_message = tr("edits are saved when an adjustment finishes");
    emit previewChanged();
    emit stateChanged();
    if(!m_restoreView.isEmpty())
    {
      const auto view = m_restoreView; m_restoreView = QJsonObject();
      emit viewRestored(view.value(QStringLiteral("zoom_factor")).toDouble(1),
                          view.value(QStringLiteral("center_x")).toDouble(0.5),
                          view.value(QStringLiteral("center_y")).toDouble(0.5));
    }
  });
  connect(m_worker, &EngineWorker::thumbnailReady, this, [this](int id, QImage image) {
    m_requestedThumbnails.remove(id);
    if(!image.isNull()) m_store->setThumbnail(id, image);
    emit thumbnailChanged(id);
  });
  connect(m_worker, &EngineWorker::historyChanged, this, [this](bool undo, bool redo) {
    m_canUndo = undo; m_canRedo = redo; emit stateChanged();
  });
  connect(m_worker, &EngineWorker::historyReady, this, [this](QJsonObject history, quint64 revision) {
    if(revision != m_revision->load()) return;
    m_history = history.value(QStringLiteral("steps")).toArray().toVariantList();
    m_historyEnd = history.value(QStringLiteral("end")).toInt();
    emit historyChanged();
  });
  connect(m_worker, &EngineWorker::failed, this, [this](const QString &message) {
    m_failed = true; m_message = message; m_busy = false; emit stateChanged();
  });
  connect(m_worker, &EngineWorker::completed, this, [this](const QString &message) {
    m_failed = false; m_message = message; m_busy = false; emit stateChanged();
  });
  connect(m_worker, &EngineWorker::stopped, this, [this] {
    m_canClose = true; emit stateChanged(); emit closingReady(); m_thread.quit();
  });
  m_editTimer.setSingleShot(true);
  m_editTimer.setInterval(50);
  connect(&m_editTimer, &QTimer::timeout, this, [this] { flush(false); });
  m_thread.setObjectName(QStringLiteral("darktabletouch engine"));
  m_thread.start();
}

TouchController::~TouchController()
{
  if(m_thread.isRunning())
  {
    QMetaObject::invokeMethod(m_worker, &EngineWorker::shutdown, Qt::BlockingQueuedConnection);
    m_thread.quit();
    m_thread.wait();
  }
}

void TouchController::initialize(const QStringList &arguments) { emit boot(arguments); }
quint64 TouchController::advance() { return ++(*m_revision); }

QString TouchController::preview() const
{
  return QStringLiteral("image://photos/%1?revision=%2").arg(m_before ? QStringLiteral("before") : QStringLiteral("current"))
    .arg(m_frameRevision);
}

void TouchController::updateImages(const QJsonArray &images)
{
  m_images = images.toVariantList();
  for(auto &item : m_images)
  {
    auto map = item.toMap();
    map.insert(QStringLiteral("filename"), QFileInfo(map.value(QStringLiteral("path")).toString()).fileName());
    item = map;
  }
  m_busy = false;
  emit imagesChanged();
  emit stateChanged();
}

void TouchController::importUrls(const QVariantList &urls)
{
  QStringList paths;
  for(const auto &item : urls)
  {
    const QUrl url = item.toUrl();
    if(!url.isValid() || !url.isLocalFile() || !validText(url.toLocalFile()))
    { m_message = tr("import requires a valid Unicode local file path"); emit stateChanged(); return; }
    paths.append(url.toLocalFile());
  }
  if(paths.isEmpty()) return;
  m_busy = true; m_message = tr("importing photos"); emit stateChanged();
  emit importRequested(paths);
}

void TouchController::importPath(const QString &path)
{
  importUrls({QUrl::fromLocalFile(path)});
}

void TouchController::selectImage(int id)
{
  if(!m_ready || id <= 0) return;
  flush(true);
  m_editorVisible = true; m_busy = true; m_before = false;
  m_message = tr("opening photo");
  emit stateChanged();
  emit openRequested(id, advance());
}

void TouchController::selectTool(const QString &operation)
{
  if(!m_ready || !m_imageId) return;
  flush(true);
  m_operation = operation;
  emit moduleChanged();
  emit moduleRequested(operation, m_revision->load());
}

void TouchController::setParameter(const QString &name, const QVariant &value, bool commit)
{
  if(!m_imageId) return;
  if(!m_pendingName.isEmpty() && (m_pendingName != name || m_pendingOperation != m_operation)) flush(true);
  m_pendingName = name;
  m_pendingOperation = m_operation;
  m_pendingValue = value;
  if(commit) flush(true);
  else if(!m_editTimer.isActive()) m_editTimer.start();
}

void TouchController::flush(bool commit)
{
  if(m_pendingName.isEmpty()) return;
  m_editTimer.stop();
  emit parameterRequested(m_pendingOperation, m_pendingName, m_pendingValue, commit, advance());
  if(commit) { m_pendingName.clear(); m_pendingOperation.clear(); }
}

void TouchController::setEnabled(bool enabled)
{
  flush(true); emit enabledRequested(m_operation, enabled, advance());
}
void TouchController::reset() { flush(true); emit resetRequested(m_operation, advance()); }
void TouchController::undo() { flush(true); emit historyRequested(-1, advance()); }
void TouchController::redo() { flush(true); emit historyRequested(1, advance()); }
void TouchController::jumpHistory(int end)
{
  flush(true);
  if(end != m_historyEnd) emit historyJumpRequested(end, advance());
}

void TouchController::requestThumbnail(int image)
{
  if(!m_ready || m_store->hasThumbnail(image) || m_requestedThumbnails.contains(image)) return;
  m_requestedThumbnails.insert(image); emit thumbnailRequested(image);
}

void TouchController::exportTo(const QUrl &url, const QString &format)
{
  if(!url.isValid() || !url.isLocalFile() || !validText(url.toLocalFile()))
  { m_message = tr("export requires a valid Unicode local file path"); emit stateChanged(); return; }
  flush(true); m_busy = true; m_message = tr("exporting photo"); emit stateChanged();
  emit exportRequested(url.toLocalFile(), format);
}

void TouchController::navigateImage(int direction)
{
  for(int i = 0; i < m_images.size(); i++)
    if(m_images[i].toMap().value(QStringLiteral("imgid")).toInt() == m_imageId)
    {
      const int next = qBound(0, i + direction, int(m_images.size()) - 1);
      selectImage(m_images[next].toMap().value(QStringLiteral("imgid")).toInt());
      return;
    }
}

void TouchController::setEditorVisible(bool visible)
{
  flush(true); m_editorVisible = visible; emit stateChanged();
}
void TouchController::setBefore(bool before) { m_before = before; emit previewChanged(); }
void TouchController::shutdown() { flush(true); emit shutdownRequested(); }

void TouchController::saveView(double zoom, double centerX, double centerY)
{
  emit viewRequested(m_imageId, m_editorVisible, zoom, centerX, centerY);
}

void TouchController::switchInterface(const QString &mode)
{
  flush(true);
  emit switchRequested(mode);
}
