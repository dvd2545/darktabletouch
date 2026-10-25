/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/TouchController.h"
#include <QFileInfo>
#include <QFile>
#include <QUuid>
#include <QRegularExpression>
#include <QDirIterator>
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

void FieldModel::setValue(const QString &name, const QVariant &value)
{
  for(int i = 0; i < m_rows.size(); i++)
  {
    auto field = m_rows[i].toMap();
    if(field.value(QStringLiteral("name")).toString() != name) continue;
    field.insert(QStringLiteral("value"), value); m_rows[i] = field;
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
void PreviewStore::removeThumbnail(int image)
{
  QWriteLocker lock(&m_lock); m_thumbnails.remove(image);
}


EngineWorker::EngineWorker(std::shared_ptr<std::atomic<quint64>> revision, std::shared_ptr<std::atomic<bool>> cancel) : m_exportCancel(std::move(cancel)), m_revision(std::move(revision)) {}

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
  emit libraryReady(takeJson(dt_touch_library()).object());
}

void EngineWorker::importPaths(QStringList paths)
{
  if(!m_initialized) return;
  int added = 0, existing = 0, failed = 0;
  bool reported = false;
  for(const QString &path : paths)
  {
    char *error = nullptr;
    char *result = dt_touch_import(path.toUtf8().constData(), &error);
    const auto object = takeJson(result).object();
    if(error) { report(error); reported = true; }
    added += object.value(QStringLiteral("imported")).toInt();
    existing += object.value(QStringLiteral("already")).toInt();
    failed += object.value(QStringLiteral("failed")).toInt();
  }
  emit imagesChanged(images());
  emit libraryReady(takeJson(dt_touch_library()).object());
  const QString summary = tr("%1 added, %2 already in library, %3 failed; the current collection and filters are unchanged")
    .arg(added).arg(existing).arg(failed);
  if(failed) emit this->failed(summary);
  else if(!reported) emit completed(summary);
}

void EngineWorker::libraryCommand(QString action, QJsonObject settings, int image)
{
  if(!m_initialized) return;
  if(action == QStringLiteral("touch-settings")) {
    dt_touch_settings_set(settings.value(QStringLiteral("show_rejected")).toBool(), settings.value(QStringLiteral("advance_after_reject")).toBool()); return;
  }
  if(action == QStringLiteral("remove-folder") || action == QStringLiteral("remove-collection"))
  {
    abortCrop();
    const auto ids = action == QStringLiteral("remove-folder")
      ? takeJson(dt_touch_folder_images(settings.value(QStringLiteral("folder")).toString().toUtf8().constData())).array() : QJsonArray();
    QJsonArray targets = ids;
    if(action == QStringLiteral("remove-collection")) for(const auto &entry : images()) targets.append(entry.toObject().value(QStringLiteral("imgid")));
    char *error = nullptr;
    if(!dt_touch_editor_save(m_editor, &error)) { report(error); return; }
    const int active = m_imageId;
    clearHistory(); dt_touch_editor_free(m_editor); m_editor = nullptr;
    int removed = 0;
    for(const auto &id : targets) { if(!dt_touch_image_action("remove", id.toInt(), &error)) break; removed++; }
    if(dt_touch_image_exists(active)) openImage(active, m_revision->load());
    else { m_imageId = 0; m_editorVisible = false; emit imageClosed(); }
    emit imagesChanged(images()); emit libraryReady(takeJson(dt_touch_library()).object());
    if(error) { const QString failure = QString::fromUtf8(error); dt_touch_free(error); emit failed(tr("removed %1 photos before stopping: %2").arg(removed).arg(failure)); }
    else emit completed(tr("removed %1 photos from the catalog; files remain on disk").arg(removed));
    return;
  }
  if(action == QStringLiteral("values"))
  {
    const int property = settings.value(QStringLiteral("property")).toInt();
    emit collectionValuesReady(property, takeJson(dt_touch_collection_values(property)).array());
    emit completed(tr("collection values loaded"));
    return;
  }
  char *error = nullptr;
  const bool collection = action == QStringLiteral("collection");
  const bool imageAction = action == QStringLiteral("remove") || action == QStringLiteral("trash")
    || action == QStringLiteral("reject") || action == QStringLiteral("unrate");
  const bool closing = image == m_imageId && (action == QStringLiteral("remove") || action == QStringLiteral("trash"));
  if(closing)
  {
    abortCrop();
    if(!dt_touch_editor_save(m_editor, &error)) { report(error); return; }
    clearHistory(); dt_touch_editor_free(m_editor); m_editor = nullptr;
  }
  const bool success = collection
    ? dt_touch_collection_set(QJsonDocument(settings).toJson(QJsonDocument::Compact).constData(), &error)
    : imageAction ? dt_touch_image_action(action.toUtf8().constData(), image, &error)
    : dt_touch_selection(action.toUtf8().constData(), image, &error);
  if(closing)
  {
    if(dt_touch_image_exists(image))
      openImage(image, m_revision->load());
    else { m_imageId = 0; m_editorVisible = false; emit imageClosed(); }
  }
  if(collection || imageAction) emit imagesChanged(images());
  emit libraryReady(takeJson(dt_touch_library()).object());
  if(!success) { report(error); return; }
  if(imageAction) emit imageActionFinished(action);
  emit completed(collection ? tr("collection updated") : imageAction ? tr("image action completed") : tr("selection updated"));
}

void EngineWorker::reviewImport(QStringList paths, bool recursive)
{
  if(!m_initialized) return;
  QJsonArray candidates;
  QSet<QString> seen;
  bool scanFailed = false;
  const auto add = [&](const QString &path) {
    const QString absolute = QFileInfo(path).absoluteFilePath();
    if(!validText(absolute))
    { emit failed(tr("an import filename cannot be represented as valid UTF-8")); scanFailed = true; return; }
    if(seen.contains(absolute)) return;
    seen.insert(absolute);
    const int existing = dt_touch_import_candidate(absolute.toUtf8().constData());
    if(existing < 0) return;
    candidates.append(QJsonObject{{QStringLiteral("path"), absolute},
      {QStringLiteral("filename"), QFileInfo(absolute).fileName()},
      {QStringLiteral("existing"), existing > 0}});
  };
  for(const QString &path : paths)
  {
    const QFileInfo source(path);
    if(!source.exists() || !source.isReadable())
    { emit failed(tr("the import source is missing or cannot be read: %1").arg(path)); scanFailed = true; continue; }
    if(source.isDir())
    {
      QDirIterator files(path, QDir::Files | QDir::NoSymLinks,
        recursive ? QDirIterator::Subdirectories : QDirIterator::NoIteratorFlags);
      while(files.hasNext()) add(files.next());
    }
    else add(path);
  }
  emit importReviewed(candidates);
  if(!scanFailed) emit completed(candidates.size() == 1 ? tr("1 supported photo found")
    : tr("%1 supported photos found").arg(candidates.size()));
}

void EngineWorker::clearHistory()
{
  dt_touch_state_free(m_before);
  m_before = nullptr;
  for(const auto &edit : m_edits)
  {
    dt_touch_state_free(edit.before);
    dt_touch_state_free(edit.after); dt_touch_catalog_free(edit.catalog);
  }
  m_edits.clear();
  m_position = 0;
  emit historyChanged(false, false);
}

void EngineWorker::openImage(int id, quint64 revision)
{
  if(!m_initialized) return;
  abortCrop();
  char *error = nullptr;
  if(!dt_touch_editor_save(m_editor, &error)) { report(error); return; }
  auto next = dt_touch_editor_new(id, &error);
  if(!next) { report(error); return; }
  clearHistory();
  dt_touch_editor_free(m_editor);
  m_editor = next;
  m_imageId = id;
  emit imageOpened(id, revision);
  const auto information = takeJson(dt_touch_editor_information(m_editor, &error)).object();
  if(error) report(error);
  else emit informationReady(information, revision);
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
  auto module = takeJson(text).object();
  QJsonObject states;
  for(const char *op : {"exposure", "agx", "lens", "denoiseprofile", "flip", "crop"})
    states.insert(QString::fromLatin1(op), bool(dt_touch_editor_enabled(m_editor, op)));
  module.insert(QStringLiteral("enabled_modules"), states);
  emit moduleReady(module, revision);
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
    dt_touch_state_free(m_edits.back().after); dt_touch_catalog_free(m_edits.back().catalog);
    m_edits.pop_back();
  }
  m_edits.push_back({m_before, dt_touch_editor_capture(m_editor)});
  m_before = nullptr;
  if(m_edits.size() > 100)
  {
    dt_touch_state_free(m_edits.front().before);
    dt_touch_state_free(m_edits.front().after); dt_touch_catalog_free(m_edits.front().catalog);
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
  loadModule(m_operation, revision);
  render(revision, commit ? 1280 : 640);
}

void EngineWorker::changeEnabled(QString operation, bool enabled, quint64 revision)
{
  abortCrop();
  if(!m_editor) return;
  beginEdit();
  char *error = nullptr;
  if(!dt_touch_editor_enable(m_editor, operation.toUtf8().constData(), enabled, &error))
  { report(error); return; }
  commitEdit();
  loadModule(m_operation, revision);
  render(revision);
}

void EngineWorker::resetModule(QString operation, quint64 revision)
{
  abortCrop();
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
  abortCrop();
  if((direction < 0 && !m_position) || (direction > 0 && m_position == m_edits.size())) return;
  const auto &edit = m_edits[direction < 0 ? m_position - 1 : m_position];
  char *error = nullptr;
  char *warning = nullptr;
  if(edit.catalog) {
    if(!dt_touch_catalog_restore(edit.catalog, direction > 0, &error)) { report(error); return; }
    warning = error; error = nullptr;
  } else {
    if(!m_editor || !dt_touch_editor_restore(m_editor, direction < 0 ? edit.before : edit.after, &error)) { report(error); return; }
  }
  if(direction < 0) --m_position; else ++m_position;
  emit historyChanged(m_position > 0, m_position < m_edits.size());
  if(edit.catalog) {
    if(!dt_touch_editor_reload(m_editor, &error)) { dt_touch_free(warning); report(error); return; }
    emit imagesChanged(images()); emit libraryReady(takeJson(dt_touch_library()).object());
  } else if(!dt_touch_editor_save(m_editor, &error)) {
    warning = error; error = nullptr;
  }
  if(m_editor) {
    loadModule(m_operation, revision);
    if(!render(revision)) { dt_touch_free(warning); return; }
  }
  if(edit.catalog) emit historyActionDone(edit.targets);
  if(warning) report(warning);
  else emit completed(tr("edits saved"));
}

bool EngineWorker::render(quint64 revision, int extent)
{
  if(!m_editor || revision != m_revision->load()) return false;
  uint8_t *pixels = nullptr;
  int width = 0, height = 0;
  char *error = nullptr;
  if(!dt_touch_editor_preview(m_editor, extent, extent, &pixels, &width, &height, &error))
  { report(error); return false; }
  emit frameReady(takePixels(pixels, width, height), revision);
  return true;
}

void EngineWorker::jumpHistory(int end, quint64 revision)
{
  abortCrop();
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
  emit thumbnailReady(id, QImage(pixels, width, height, width * 4, QImage::Format_RGBA8888,
                                  dt_touch_free, pixels));
}

void EngineWorker::exportImage(QString path, QString format)
{
  abortCrop();
  if(!m_editor) return;
  char *error = nullptr;
  if(!dt_touch_editor_export(m_editor, path.toUtf8().constData(), format.toUtf8().constData(), &error))
  { report(error); return; }
  emit completed(tr("exported to %1").arg(path));
}

void EngineWorker::shutdown()
{
  abortCrop();
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
  m_exportCancel = std::make_shared<std::atomic<bool>>(false);
  m_worker = new EngineWorker(m_revision, m_exportCancel);
  m_worker->moveToThread(&m_thread);
  connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
  connect(this, &TouchController::boot, m_worker, &EngineWorker::initialize);
  connect(this, &TouchController::importRequested, m_worker, &EngineWorker::importPaths);
  connect(this, &TouchController::libraryRequested, m_worker, &EngineWorker::libraryCommand);
  connect(this, &TouchController::importReviewRequested, m_worker, &EngineWorker::reviewImport);
  connect(m_worker, &EngineWorker::libraryReady, this, [this](QJsonObject library) {
    m_library = library.toVariantMap();
    if(!m_touchSettingsLoaded) {
      const auto settings = library.value(QStringLiteral("touch_settings")).toObject();
      m_showRejected = settings.value(QStringLiteral("show_rejected")).toBool();
      m_advanceAfterReject = settings.value(QStringLiteral("advance_after_reject")).toBool();
      m_touchSettingsLoaded = true; updateImages(QJsonArray::fromVariantList(m_allImages));
    }
    emit libraryChanged();
  });
  connect(m_worker, &EngineWorker::collectionValuesReady, this, [this](int property, QJsonArray values) {
    m_collectionValues = {{QStringLiteral("property"), property}, {QStringLiteral("values"), values.toVariantList()}};
    emit collectionValuesChanged();
  });
  connect(m_worker, &EngineWorker::imageActionFinished, this, [this](const QString &action) {
    if(action == QStringLiteral("reject") && m_rejectNext) {
      const int next = m_rejectNext; m_rejectNext = 0;
      QTimer::singleShot(0, this, [this, next] { selectImage(next); });
    }
  });
  connect(m_worker, &EngineWorker::imageClosed, this, [this] {
    m_imageId = 0; m_editorVisible = false; m_imageInformation.clear(); m_fields.setFields({});
    m_history.clear(); m_historyEnd = 0; emit moduleChanged(); emit historyChanged(); emit stateChanged();
  });
  connect(m_worker, &EngineWorker::importReviewed, this, [this](QJsonArray candidates) {
    m_importCandidates = candidates.toVariantList(); emit importReviewChanged();
  });
  connect(this, &TouchController::historyRequestedAction, m_worker, &EngineWorker::historyCommand);
  connect(m_worker, &EngineWorker::clipboardReady, this, [this](QJsonObject clipboard) { m_copiedHistory = clipboard.toVariantMap(); emit clipboardChanged(); });
  connect(m_worker, &EngineWorker::historyActionDone, this, [this](QJsonArray changed) {
    for(const auto &id : changed) { m_store->removeThumbnail(id.toInt()); requestThumbnail(id.toInt()); }
    m_waitingFrame = false; m_busy = false; emit stateChanged();
  });
  connect(this, &TouchController::cropRequested, m_worker, &EngineWorker::cropCommand);
  connect(m_worker, &EngineWorker::cropReady, this, [this](QJsonArray bounds, QSize originalSize) { if(originalSize.isValid()) m_cropOriginalSize = originalSize; m_cropBounds = bounds.toVariantList(); m_cropEditing = !bounds.isEmpty(); emit cropChanged(); });
  connect(this, &TouchController::openRequested, m_worker, &EngineWorker::openImage);
  connect(this, &TouchController::moduleRequested, m_worker, &EngineWorker::loadModule);
  connect(this, &TouchController::parameterRequested, m_worker, &EngineWorker::setParameter);
  connect(this, &TouchController::enabledRequested, m_worker, &EngineWorker::changeEnabled);
  connect(this, &TouchController::resetRequested, m_worker, &EngineWorker::resetModule);
  connect(this, &TouchController::historyRequested, m_worker, &EngineWorker::historyStep);
  connect(this, &TouchController::historyJumpRequested, m_worker, &EngineWorker::jumpHistory);
  connect(this, &TouchController::thumbnailRequested, m_worker, &EngineWorker::thumbnail);
  connect(this, &TouchController::batchExportRequested, m_worker, &EngineWorker::exportBatch);
  connect(m_worker, &EngineWorker::exportProgress, this, [this](int done, int total) { m_message = tr("exported %1 of %2 photos").arg(done).arg(total); emit stateChanged(); });
  connect(m_worker, &EngineWorker::exportFinished, this, [this] { m_exportRunning = false; m_busy = false; emit stateChanged(); });
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
      QStringLiteral("straightening, guides, presets, and full workflow validation"),
      QStringLiteral("presets, specialized guides, and full workflow validation")};
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
    m_busy = m_exportRunning;
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
  connect(m_worker, &EngineWorker::informationReady, this, [this](QJsonObject information, quint64 revision) {
    if(revision != m_revision->load()) return;
    m_imageInformation = information.toVariantMap();
    emit imageInformationChanged();
  });
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
    if(revision > m_frameRevision && !m_failed) { m_deferredModule = module; return; }
    applyModule(module);
  });
  connect(m_worker, &EngineWorker::frameReady, this, [this](QImage image, quint64 revision) {
    if(revision != m_revision->load()) return;
    m_store->setFrame(image);
    if(m_needBefore) { m_store->setFrame(image, true); m_needBefore = false; }
    m_waitingFrame = false;
    m_frameRevision = revision;
    if(!m_deferredModule.isEmpty()) { const auto module = m_deferredModule; m_deferredModule = {}; applyModule(module); }
    m_busy = m_exportRunning;
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
    ++m_thumbnailVersions[id];
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
    if(!m_deferredModule.isEmpty()) { const auto module = m_deferredModule; m_deferredModule = {}; applyModule(module); }
    m_waitingFrame = false; m_rejectNext = 0; m_failed = true; m_message = message; m_busy = false; emit stateChanged();
  });
  connect(m_worker, &EngineWorker::completed, this, [this](const QString &message) {
    m_failed = false; m_message = message; m_busy = m_waitingFrame || m_exportRunning; emit stateChanged();

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
  m_allImages = images.toVariantList();
  m_images.clear();
  for(const auto &image : m_allImages)
    if(m_showRejected || !image.toMap().value(QStringLiteral("rejected")).toBool()) m_images.append(image);
  for(auto &item : m_images)
  {
    auto map = item.toMap();
    map.insert(QStringLiteral("filename"), QFileInfo(map.value(QStringLiteral("path")).toString()).fileName());
    item = map;
  }
  m_busy = m_waitingFrame || m_exportRunning;
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

void TouchController::importSelectedPaths(const QStringList &paths)
{
  QVariantList urls;
  for(const auto &path : paths) urls.append(QUrl::fromLocalFile(path));
  importUrls(urls);
}

void TouchController::reviewImport(const QVariantList &urls, bool recursive)
{
  if(!m_ready || m_busy) return;
  QStringList paths;
  for(const auto &item : urls)
  {
    const QUrl url = item.toUrl();
    if(!url.isValid() || !url.isLocalFile() || !validText(url.toLocalFile()))
    { m_failed = true; m_message = tr("import requires a valid Unicode local file path"); emit stateChanged(); return; }
    paths.append(url.toLocalFile());
  }
  if(paths.isEmpty()) return;
  m_busy = true; m_failed = false; m_message = tr("scanning import sources"); emit stateChanged();
  emit importReviewRequested(paths, recursive);
}

void TouchController::libraryCommand(const QString &action, const QVariantMap &settings, int image)
{
  if(!m_ready || m_busy) return;
  if(action == QStringLiteral("collection"))
    for(const QString &group : {QStringLiteral("collection"), QStringLiteral("filtering")})
      for(const auto &rule : settings.value(group).toList())
        if(!validText(rule.toMap().value(QStringLiteral("string")).toString()))
        { m_failed = true; m_message = tr("a collection value cannot be represented as valid UTF-8"); emit stateChanged(); return; }
  if(action == QStringLiteral("reject") && m_advanceAfterReject)
    for(int i = 0; i + 1 < m_images.size(); ++i)
      if(m_images[i].toMap().value(QStringLiteral("imgid")).toInt() == image)
        m_rejectNext = m_images[i + 1].toMap().value(QStringLiteral("imgid")).toInt();
  flush(true);
  m_busy = true; m_failed = false; emit stateChanged();
  emit libraryRequested(action, QJsonObject::fromVariantMap(settings), image);
}

void TouchController::selectImage(int id)
{
  if(!m_ready || id <= 0) return;
  flush(true);
  m_deferredModule = {};
  m_waitingFrame = true;
  m_editorVisible = true; m_busy = true; m_before = false;
  m_message = tr("opening photo");
  emit stateChanged();
  emit openRequested(id, advance());
}

void TouchController::selectTool(const QString &operation)
{
  if(!m_ready || !m_imageId) return;
  if(m_cropEditing) cropCommand(QStringLiteral("cancel"));
  flush(true);
  m_deferredModule = {};
  m_operation = operation;
  m_fields.setFields({});
  emit moduleChanged();
  if(operation == QStringLiteral("crop")) cropCommand(QStringLiteral("begin"));
  else emit moduleRequested(operation, m_revision->load());
}

void TouchController::setParameter(const QString &name, const QVariant &value, bool commit, const QString &operation)
{
  if(!m_imageId) return;
  const QString target = operation.isEmpty() ? m_operation : operation;
  if(!m_pendingName.isEmpty() && (m_pendingName != name || m_pendingOperation != target)) flush(true);
  m_pendingName = name;
  m_pendingOperation = target;
  m_pendingValue = value;
  for(int i = 0; i < m_fields.rowCount(); ++i) {
    const auto field = m_fields.data(m_fields.index(i), Qt::UserRole + 1).toMap();
    if(field.value(QStringLiteral("name")).toString() == name && field.value(QStringLiteral("operation"), m_operation).toString() == target) {
      m_fields.setValue(name, value); emit moduleChanged(); break;
    }
  }
  if(name == m_lastName && target == m_lastOperation) m_lastField.insert(QStringLiteral("value"), value);
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
  if(!m_imageId) return;
  flush(true); m_waitingFrame = true; m_busy = true; emit stateChanged(); emit enabledRequested(m_operation, enabled, advance());
}
void TouchController::reset() { if(!m_imageId) return; m_waitingFrame = true; m_busy = true; emit stateChanged(); flush(true); emit resetRequested(m_operation, advance()); }
void TouchController::undo() { if(!m_canUndo) return; m_waitingFrame = true; m_busy = true; emit stateChanged(); flush(true); emit historyRequested(-1, advance()); }
void TouchController::redo() { if(!m_canRedo) return; m_waitingFrame = true; m_busy = true; emit stateChanged(); flush(true); emit historyRequested(1, advance()); }
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
  if(!visible && m_cropEditing) cropCommand(QStringLiteral("cancel"));
  flush(true); m_editorVisible = visible; emit stateChanged();
}
void TouchController::setBefore(bool before) { m_before = before; emit previewChanged(); }
void TouchController::shutdown() { m_exportCancel->store(true); flush(true); emit shutdownRequested(); }

void TouchController::saveView(double zoom, double centerX, double centerY)
{
  emit viewRequested(m_imageId, m_editorVisible, zoom, centerX, centerY);
}

void TouchController::switchInterface(const QString &mode)
{
  flush(true);
  emit switchRequested(mode);
}

void TouchController::setShowRejected(bool value)
{
  m_showRejected = value;
  emit libraryRequested(QStringLiteral("touch-settings"), QJsonObject{{QStringLiteral("show_rejected"), m_showRejected}, {QStringLiteral("advance_after_reject"), m_advanceAfterReject}}, 0);
  updateImages(QJsonArray::fromVariantList(m_allImages));
}
void TouchController::setAdvanceAfterReject(bool value)
{
  m_advanceAfterReject = value;
  emit libraryRequested(QStringLiteral("touch-settings"), QJsonObject{{QStringLiteral("show_rejected"), m_showRejected}, {QStringLiteral("advance_after_reject"), m_advanceAfterReject}}, 0);
  emit stateChanged();
}
void TouchController::rememberParameter(const QString &name, const QString &operation, double step)
{
  m_lastName = name; m_lastOperation = operation.isEmpty() ? m_operation : operation; m_lastStep = step;
  for(int i = 0; i < m_fields.rowCount(); ++i)
  {
    const auto field = m_fields.data(m_fields.index(i), Qt::UserRole + 1).toMap();
    if(field.value(QStringLiteral("name")).toString() == name) { m_lastField = field; break; }
  }
}
void TouchController::adjustLastParameter(int direction)
{
  if(m_lastName.isEmpty() || !m_imageId || m_cropEditing || m_waitingFrame) return;
  const double value = qBound(m_lastField.value(QStringLiteral("min"), -1e9).toDouble(),
    m_lastField.value(QStringLiteral("value")).toDouble() + direction * m_lastStep,
    m_lastField.value(QStringLiteral("max"), 1e9).toDouble());
  setParameter(m_lastName, value, true, m_lastOperation);
}

void TouchController::applyModule(const QJsonObject &module)
{
  auto fields = module.value(QStringLiteral("fields")).toArray().toVariantList();
  if(module.value(QStringLiteral("operation")).toString() == QStringLiteral("exposure"))
  {
    const QStringList order{QStringLiteral("mode"), QStringLiteral("exposure"), QStringLiteral("deflicker_target_level"),
      QStringLiteral("deflicker_percentile"), QStringLiteral("black")};
    const auto rank = [&order](const QVariant &value) {
      const int position = order.indexOf(value.toMap().value(QStringLiteral("name")).toString());
      return position < 0 ? 100 : position;
    };
    std::stable_sort(fields.begin(), fields.end(), [&rank](const QVariant &a, const QVariant &b) { return rank(a) < rank(b); });
  }
  m_moduleStates = module.value(QStringLiteral("enabled_modules")).toObject().toVariantMap();
  m_fields.setFields(fields);
  m_toolLabel = module.value(QStringLiteral("label")).toString();
  m_moduleEnabled = module.value(QStringLiteral("enabled")).toBool();
  emit moduleChanged();
}

void EngineWorker::abortCrop()
{
  if(!m_cropBefore) return;
  char *error = nullptr;
  if(!dt_touch_editor_restore(m_editor, m_cropBefore, &error)) report(error);
  dt_touch_state_free(m_cropBefore); m_cropBefore = nullptr;
  emit cropReady({});
}
void EngineWorker::cropCommand(QString action, QJsonObject settings, quint64 revision)
{
  if(!m_editor) return;
  char *error = nullptr;
  if(action == QStringLiteral("begin"))
  {
    if(m_cropBefore) return;
    const bool enabled = dt_touch_editor_enabled(m_editor, "crop");
    m_cropBefore = dt_touch_editor_capture(m_editor);
    if(!dt_touch_editor_enable(m_editor, "crop", false, &error)) { abortCrop(); report(error); return; }
    loadModule(QStringLiteral("crop"), revision);
    if(!render(revision)) { abortCrop(); return; }
    float bounds[4] = {0, 0, 1, 1};
    if(enabled && !dt_touch_editor_crop_bounds(m_editor, bounds, &error)) { abortCrop(); report(error); return; }
    int originalSize[2]; dt_touch_editor_crop_original(m_editor, originalSize);
    emit cropReady(QJsonArray{bounds[0], bounds[1], bounds[2], bounds[3]}, QSize(originalSize[0], originalSize[1]));
    return;
  }
  if(!m_cropBefore) return;
  if(action == QStringLiteral("auto"))
  {
    float bounds[4];
    if(!dt_touch_editor_auto_crop(m_editor, settings.value(QStringLiteral("aspect")).toDouble(), bounds, &error)) { report(error); return; }
    emit cropReady(QJsonArray{bounds[0], bounds[1], bounds[2], bounds[3]}); emit completed(tr("automatic crop is ready to apply")); return;
  }
  if(action == QStringLiteral("apply"))
  {
    const auto rect = settings.value(QStringLiteral("bounds")).toArray();
    if(rect.size() != 4) { emit failed(tr("crop frame needs four coordinates")); return; }
    if(!dt_touch_editor_restore(m_editor, m_cropBefore, &error)) { report(error); return; }
    beginEdit();
    const float bounds[4] = {float(rect[0].toDouble()), float(rect[1].toDouble()), float(rect[2].toDouble()), float(rect[3].toDouble())};
    if(!dt_touch_editor_crop_apply(m_editor, bounds, settings.value(QStringLiteral("ratio_n")).toInt(), settings.value(QStringLiteral("ratio_d")).toInt(), &error))
    { report(error); return; }
    dt_touch_state_free(m_cropBefore); m_cropBefore = nullptr;
    commitEdit();
  }
  else abortCrop();
  loadModule(m_operation, revision); render(revision); emit cropReady({});
}
void TouchController::cropCommand(const QString &action, const QVariantMap &settings)
{
  if(!m_imageId) return;
  flush(true); m_busy = true; m_waitingFrame = action != QStringLiteral("auto"); emit stateChanged();
  emit cropRequested(action, QJsonObject::fromVariantMap(settings), advance());
}
void TouchController::setEnabledFor(const QString &operation, bool enabled)
{
  if(!m_imageId) return;
  m_moduleStates.insert(operation, enabled);
  if(operation == m_operation) m_moduleEnabled = enabled;
  emit moduleChanged();
  m_waitingFrame = true; m_busy = true; emit stateChanged();
  flush(true); emit enabledRequested(operation, enabled, advance());
}

void TouchController::exportBatch(const QVariantMap &settings)
{
  if(!m_ready || m_busy || m_exportRunning) return;
  auto request = settings;
  QVariantList entries;
  const auto selected = m_library.value(QStringLiteral("selected")).toList();
  const QString scope = settings.value(QStringLiteral("scope")).toString();
  for(const auto &image : m_images) {
    const int id = image.toMap().value(QStringLiteral("imgid")).toInt();
    if(scope == QStringLiteral("collection") || (scope == QStringLiteral("current") && id == m_imageId)
       || (scope == QStringLiteral("selected") && selected.contains(id))) entries.append(image);
  }
  if(scope == QStringLiteral("current") && entries.isEmpty() && m_imageId > 0) entries.append(QVariantMap{{QStringLiteral("imgid"), m_imageId}});
  if(entries.isEmpty()) { m_failed = true; m_message = tr("no photos selected for export"); emit stateChanged(); return; }
  request.insert(QStringLiteral("images"), entries);
  const QUrl folder(settings.value(QStringLiteral("folder")).toUrl());
  if(settings.value(QStringLiteral("storage")).toString() == QStringLiteral("folder") && (!folder.isLocalFile() || !validText(folder.toLocalFile())))
  { m_failed = true; m_message = tr("choose an export folder"); emit stateChanged(); return; }
  request.insert(QStringLiteral("folder"), folder.toLocalFile());
  flush(true); m_exportCancel->store(false); m_exportRunning = true; m_busy = true; m_failed = false; emit stateChanged();
  emit batchExportRequested(QJsonObject::fromVariantMap(request));
}
void EngineWorker::exportBatch(QJsonObject settings)
{
  abortCrop();
  char *error = nullptr;
  if(!dt_touch_editor_save(m_editor, &error)) { report(error); emit exportFinished(); return; }
  const auto entries = settings.value(QStringLiteral("images")).toArray();
  const QString format = settings.value(QStringLiteral("format")).toString(QStringLiteral("jpeg"));
  const QString extension = format == QStringLiteral("jpeg") ? QStringLiteral("jpg") : format == QStringLiteral("tiff") ? QStringLiteral("tif") : QStringLiteral("png");
  const QString pattern = settings.value(QStringLiteral("pattern")).toString(QStringLiteral("{name}_edited"));
  const QString policy = settings.value(QStringLiteral("collision")).toString(QStringLiteral("unique"));
  int done = 0, skipped = 0;
  for(int i = 0; i < entries.size(); i++)
  {
    if(m_exportCancel->load()) break;
    const auto image = entries[i].toObject(); const int id = image.value(QStringLiteral("imgid")).toInt();
    char *nativePath = dt_touch_image_path(id);
    if(!nativePath) { emit failed(tr("the export image is no longer in the catalog")); emit exportFinished(); return; }
    const QFileInfo source(QString::fromUtf8(nativePath)); dt_touch_free(nativePath);
    const QString directory = settings.value(QStringLiteral("storage")).toString() == QStringLiteral("source")
      ? QDir(source.absolutePath()).filePath(QStringLiteral("exported")) : settings.value(QStringLiteral("folder")).toString();
    QString name = pattern; name.replace(QStringLiteral("{name}"), source.completeBaseName());
    name.replace(QStringLiteral("{sequence}"), QStringLiteral("%1").arg(i + 1, 4, 10, QChar('0'))); name.replace(QStringLiteral("{id}"), QString::number(id));
    if(name.isEmpty() || !validText(name) || name.contains(QRegularExpression(QStringLiteral("[<>:\"/\\\\|?*{}]"))) || name.endsWith('.') || name.endsWith(' '))
    { emit failed(tr("invalid export name: %1").arg(name)); emit exportFinished(); return; }
    if(!QDir().mkpath(directory)) { emit failed(tr("could not create export folder: %1").arg(directory)); emit exportFinished(); return; }
    QString target = QDir(directory).filePath(name + '.' + extension);
    if(QFile::exists(target)) {
      if(policy == QStringLiteral("skip")) { skipped++; continue; }
      if(policy == QStringLiteral("error")) { emit failed(tr("export file already exists: %1").arg(target)); emit exportFinished(); return; }
      int counter = 1; do { target = QDir(directory).filePath(name + QStringLiteral("_%1.").arg(counter++) + extension); } while(QFile::exists(target));
    }
    const QString temporary = QDir(directory).filePath(QStringLiteral(".darktabletouch-%1.%2").arg(QUuid::createUuid().toString(QUuid::Id128), extension));
    const bool success = dt_touch_export_image(id, temporary.toUtf8().constData(), format.toUtf8().constData(),
      settings.value(QStringLiteral("width")).toInt(), settings.value(QStringLiteral("height")).toInt(),
      settings.value(QStringLiteral("quality")).toInt(95), settings.value(QStringLiteral("bits")).toInt(8),
      settings.value(QStringLiteral("upscale")).toBool(), settings.value(QStringLiteral("high_quality")).toBool(true),
      settings.value(QStringLiteral("srgb")).toBool(true), &error);
    if(!success || !QFile::rename(temporary, target)) {
      QFile::remove(temporary);
      if(error) report(error); else emit failed(tr("could not finish export without replacing an existing file: %1").arg(target));
      emit exportFinished(); return;
    }
    emit exportProgress(++done, entries.size());
  }
  emit completed(tr("export finished: %1 written, %2 skipped%3").arg(done).arg(skipped).arg(m_exportCancel->load() ? tr("; canceled") : QString()));
  emit exportFinished();
}

void TouchController::historyCommand(const QString &action, const QVariantMap &settings, int image)
{
  if(!m_ready || m_busy) return;
  QVariantMap request = settings;
  if(action == QStringLiteral("copy")) request.insert(QStringLiteral("source"), image > 0 ? image : m_imageId);
  else {
    QVariantList targets;
    const QString scope = settings.value(QStringLiteral("scope"), QStringLiteral("current")).toString();
    if(scope == QStringLiteral("current")) { const int id = image > 0 ? image : m_imageId; if(id > 0) targets.append(id); }
    else for(const auto &entry : m_images) {
      const int id = entry.toMap().value(QStringLiteral("imgid")).toInt();
      if(scope == QStringLiteral("collection") || m_library.value(QStringLiteral("selected")).toList().contains(id)) targets.append(id);
    }
    if(targets.isEmpty()) { m_failed = true; m_message = tr("select at least one destination image"); emit stateChanged(); return; }
    request.insert(QStringLiteral("targets"), targets); request.insert(QStringLiteral("reset"), action == QStringLiteral("reset"));
    if(action == QStringLiteral("paste")) { request.insert(QStringLiteral("source"), m_copiedHistory.value(QStringLiteral("source"))); request.insert(QStringLiteral("token"), m_copiedHistory.value(QStringLiteral("token"))); }
  }
  flush(true); m_busy = true; m_waitingFrame = action != QStringLiteral("copy"); emit stateChanged();
  emit historyRequestedAction(action, QJsonObject::fromVariantMap(request), action == QStringLiteral("copy") ? m_revision->load() : advance());
}

void EngineWorker::historyCommand(QString action, QJsonObject settings, quint64 revision)
{
  abortCrop(); char *error = nullptr;
  if(!dt_touch_editor_save(m_editor, &error)) { report(error); return; }
  if(action == QStringLiteral("copy")) {
    char *text = dt_touch_history_clipboard(settings.value(QStringLiteral("source")).toInt(), &error);
    if(!text) { report(error); return; }
    emit clipboardReady(takeJson(text).object()); emit completed(tr("history copied; choose the steps to paste")); return;
  }
  dt_touch_catalog_edit_t *catalog = dt_touch_history_apply(QJsonDocument(settings).toJson(QJsonDocument::Compact).constData(), &error);
  if(!catalog) { report(error); return; }
  while(m_edits.size() > m_position) { dt_touch_state_free(m_edits.back().before); dt_touch_state_free(m_edits.back().after); dt_touch_catalog_free(m_edits.back().catalog); m_edits.pop_back(); }
  m_edits.push_back({nullptr, nullptr, catalog, settings.value(QStringLiteral("targets")).toArray()});
  if(m_edits.size() > 100) {
    dt_touch_state_free(m_edits.front().before);
    dt_touch_state_free(m_edits.front().after);
    dt_touch_catalog_free(m_edits.front().catalog);
    m_edits.erase(m_edits.begin());
  }
  m_position = m_edits.size();
  if(!dt_touch_editor_reload(m_editor, &error)) { report(error); return; }
  emit historyChanged(true, false);
  emit imagesChanged(images()); emit libraryReady(takeJson(dt_touch_library()).object());
  if(m_editor) { loadModule(m_operation, revision); render(revision); }
  emit historyActionDone(settings.value(QStringLiteral("targets")).toArray());
  emit completed(action == QStringLiteral("reset") ? tr("adjustments reset; undo restores the previous history") : tr("selected history pasted"));
}
