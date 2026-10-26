/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once

#include "touch/engine_bridge.h"
#include <QAbstractListModel>
#include <QCache>
#include <QImage>
#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QQuickImageProvider>
#include <QReadWriteLock>
#include <QSet>
#include <QThread>
#include <QTimer>
#include <QVariantList>
#include <atomic>
#include <memory>
#include <vector>

class FieldModel final : public QAbstractListModel
{
public:
  explicit FieldModel(QObject *parent = nullptr) : QAbstractListModel(parent) {}
  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void setFields(const QVariantList &fields);
  QVariantMap values() const;
  void setValue(const QString &name, const QVariant &value);
private:
  QVariantList m_rows;
};

class PreviewStore final : public QQuickImageProvider
{
public:
  PreviewStore();
  QImage requestImage(const QString &id, QSize *size, const QSize &requested) override;
  void setFrame(const QImage &image, bool before = false);
  void setThumbnail(int image, const QImage &thumbnail);
  bool hasThumbnail(int image) const;
  void removeThumbnail(int image);
private:
  mutable QReadWriteLock m_lock;
  QImage m_frame, m_before;
  QCache<int, QImage> m_thumbnails;
};

class EngineWorker final : public QObject
{
  Q_OBJECT
public:
  explicit EngineWorker(std::shared_ptr<std::atomic<quint64>> revision, std::shared_ptr<std::atomic<bool>> cancel);
public slots:
  void initialize(QStringList arguments);
  void importPaths(QStringList paths);
  void libraryCommand(QString action, QJsonObject settings, int image);
  void reviewImport(QStringList paths, bool recursive);
  void openImage(int id, quint64 revision);
  void loadModule(QString operation, quint64 revision);
  void setParameter(QString operation, QString name, QVariant value, bool commit, quint64 revision);
  void changeEnabled(QString operation, bool enabled, quint64 revision);
  void resetModule(QString operation, quint64 revision);
  void historyStep(int direction, quint64 revision);
  void jumpHistory(int end, quint64 revision);
  void thumbnail(int id);
  void cropCommand(QString action, QJsonObject settings, quint64 revision);
  void historyCommand(QString action, QJsonObject settings, quint64 revision);
  void exportImage(QString path, QString format);
  void exportBatch(QJsonObject settings);
  void saveView(int image, bool editor, double zoom, double centerX, double centerY);
  void switchInterface(QString mode);
  void shutdown();
  void retrySave();
signals:
  void initialized(QJsonArray images, QJsonArray modules, QJsonObject session);
  void imagesChanged(QJsonArray images);
  void libraryReady(QJsonObject library);
  void collectionValuesReady(int property, QJsonArray values);
  void imageClosed();
  void imageActionFinished(QString action);
  void cropReady(QJsonArray bounds, QSize originalSize = QSize());
  void exportProgress(int done, int total);
  void exportFinished();
  void clipboardReady(QJsonObject clipboard);
  void historyActionDone(QJsonArray changed);
  void importReviewed(QJsonArray candidates);
  void imageOpened(int id, quint64 revision);
  void informationReady(QJsonObject information, quint64 revision);
  void moduleReady(QJsonObject module, quint64 revision);
  void frameReady(QImage image, quint64 revision);
  void thumbnailReady(int id, QImage image);
  void historyChanged(bool undo, bool redo);
  void historyReady(QJsonObject history, quint64 revision);
  void completed(QString message);
  void failed(QString message);
  void saveStatus(QString recoveryPath, QString issue);
  void stopped();
private:
  struct Edit { dt_touch_state_t *before; dt_touch_state_t *after; dt_touch_catalog_edit_t *catalog = nullptr; QJsonArray targets; };
  bool render(quint64 revision, int extent = 1280);
  void abortCrop();
  void beginEdit();
  void commitEdit();
  void clearHistory();
  void report(char *error);
  bool saveEditor();
  QJsonArray images();
  std::shared_ptr<std::atomic<bool>> m_exportCancel;
  std::shared_ptr<std::atomic<quint64>> m_revision;
  dt_touch_editor_t *m_editor = nullptr;
  dt_touch_state_t *m_before = nullptr;
  dt_touch_state_t *m_cropBefore = nullptr;
  std::vector<Edit> m_edits;
  size_t m_position = 0;
  bool m_initialized = false;
  QString m_operation = QStringLiteral("exposure");
  QString m_nextInterface;
  int m_imageId = 0;
  bool m_editorVisible = false;
  double m_zoom = 1, m_centerX = 0.5, m_centerY = 0.5;
};

class TouchController final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(QVariantMap copiedHistory READ copiedHistory NOTIFY clipboardChanged)
  Q_PROPERTY(bool exportRunning READ exportRunning NOTIFY stateChanged)
  Q_PROPERTY(bool cropEditing READ cropEditing NOTIFY cropChanged)
  Q_PROPERTY(QSize cropOriginalSize READ cropOriginalSize NOTIFY cropChanged)
  Q_PROPERTY(QVariantList cropBounds READ cropBounds NOTIFY cropChanged)
  Q_PROPERTY(QVariantMap moduleStates READ moduleStates NOTIFY moduleChanged)
  Q_PROPERTY(bool showRejected READ showRejected WRITE setShowRejected NOTIFY stateChanged)
  Q_PROPERTY(bool advanceAfterReject READ advanceAfterReject WRITE setAdvanceAfterReject NOTIFY stateChanged)
  Q_PROPERTY(bool quickReject READ quickReject WRITE setQuickReject NOTIFY stateChanged)
  Q_PROPERTY(bool quickDelete READ quickDelete WRITE setQuickDelete NOTIFY stateChanged)
  Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
  Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
  Q_PROPERTY(int historyEnd READ historyEnd NOTIFY historyChanged)
  Q_PROPERTY(bool canClose READ canClose NOTIFY stateChanged)
  Q_PROPERTY(int imageId READ imageId NOTIFY stateChanged)
  Q_PROPERTY(QVariantMap imageInformation READ imageInformation NOTIFY imageInformationChanged)
  Q_PROPERTY(bool hasError READ hasError NOTIFY stateChanged)
  Q_PROPERTY(QString saveIssue READ saveIssue NOTIFY stateChanged)
  Q_PROPERTY(QString recoveryPath READ recoveryPath NOTIFY stateChanged)
  Q_PROPERTY(QVariantList images READ images NOTIFY imagesChanged)
  Q_PROPERTY(QVariantMap library READ library NOTIFY libraryChanged)
  Q_PROPERTY(QVariantMap collectionValues READ collectionValues NOTIFY collectionValuesChanged)
  Q_PROPERTY(QVariantList importCandidates READ importCandidates NOTIFY importReviewChanged)
  Q_PROPERTY(QVariantList tools READ tools NOTIFY stateChanged)
  Q_PROPERTY(QVariantList moduleCoverage READ moduleCoverage NOTIFY stateChanged)
  Q_PROPERTY(QAbstractItemModel *fields READ fields NOTIFY moduleChanged)
  Q_PROPERTY(QVariantMap values READ values NOTIFY moduleChanged)
  Q_PROPERTY(QString operation READ operation NOTIFY moduleChanged)
  Q_PROPERTY(QString toolLabel READ toolLabel NOTIFY moduleChanged)
  Q_PROPERTY(bool moduleEnabled READ moduleEnabled NOTIFY moduleChanged)
  Q_PROPERTY(QString preview READ preview NOTIFY previewChanged)
  Q_PROPERTY(QString message READ message NOTIFY stateChanged)
  Q_PROPERTY(bool editorVisible READ editorVisible WRITE setEditorVisible NOTIFY stateChanged)
  Q_PROPERTY(bool before READ before WRITE setBefore NOTIFY previewChanged)
public:
  explicit TouchController(PreviewStore *store, QObject *parent = nullptr);
  ~TouchController() override;
  void initialize(const QStringList &arguments);
  QVariantMap copiedHistory() const { return m_copiedHistory; }
  Q_INVOKABLE void historyCommand(const QString &action, const QVariantMap &settings = {}, int image = 0);
  bool exportRunning() const { return m_exportRunning; }
  Q_INVOKABLE void exportBatch(const QVariantMap &settings);
  Q_INVOKABLE void cancelExport() { m_exportCancel->store(true); }
  bool cropEditing() const { return m_cropEditing; }
  QSize cropOriginalSize() const { return m_cropOriginalSize; }
  QVariantList cropBounds() const { return m_cropBounds; }
  QVariantMap moduleStates() const { return m_moduleStates; }
  Q_INVOKABLE void cropCommand(const QString &action, const QVariantMap &settings = {});
  bool showRejected() const { return m_showRejected; }
  bool advanceAfterReject() const { return m_advanceAfterReject; }
  bool quickReject() const { return m_quickReject; }
  bool quickDelete() const { return m_quickDelete; }
  void setQuickReject(bool value);
  void setQuickDelete(bool value);
  void setShowRejected(bool value);
  void setAdvanceAfterReject(bool value);
  Q_INVOKABLE void rememberParameter(const QString &name, const QString &operation, double step);
  Q_INVOKABLE void adjustLastParameter(int direction);
  bool ready() const { return m_ready; }
  bool busy() const { return m_busy; }
  bool canUndo() const { return m_canUndo; }
  bool canRedo() const { return m_canRedo; }
  QVariantList history() const { return m_history; }
  int historyEnd() const { return m_historyEnd; }
  bool canClose() const { return m_canClose; }
  int imageId() const { return m_imageId; }
  QVariantMap imageInformation() const { return m_imageInformation; }
  bool hasError() const { return m_failed; }
  QString saveIssue() const { return m_saveIssue; }
  QString recoveryPath() const { return m_recoveryPath; }
  QVariantList images() const { return m_images; }
  QVariantMap library() const { return m_library; }
  QVariantMap collectionValues() const { return m_collectionValues; }
  QVariantList importCandidates() const { return m_importCandidates; }
  QVariantList tools() const { return m_tools; }
  QVariantList moduleCoverage() const { return m_moduleCoverage; }
  QAbstractItemModel *fields() { return &m_fields; }
  QVariantMap values() const { return m_fields.values(); }
  QString operation() const { return m_operation; }
  QString toolLabel() const { return m_toolLabel; }
  bool moduleEnabled() const { return m_moduleEnabled; }
  QString preview() const;
  QString message() const { return m_message; }
  bool editorVisible() const { return m_editorVisible; }
  void setEditorVisible(bool visible);
  bool before() const { return m_before; }
  void setBefore(bool before);
  Q_INVOKABLE void importUrls(const QVariantList &urls);
  Q_INVOKABLE void importPath(const QString &path);
  Q_INVOKABLE void importSelectedPaths(const QStringList &paths);
  Q_INVOKABLE void reviewImport(const QVariantList &urls, bool recursive);
  Q_INVOKABLE void libraryCommand(const QString &action, const QVariantMap &settings = {}, int image = 0);
  Q_INVOKABLE void selectImage(int id);
  Q_INVOKABLE void selectTool(const QString &operation);
  Q_INVOKABLE void setParameter(const QString &name, const QVariant &value, bool commit, const QString &operation = QString());
  Q_INVOKABLE void setEnabled(bool enabled);
  Q_INVOKABLE void setEnabledFor(const QString &operation, bool enabled);
  Q_INVOKABLE void reset();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void jumpHistory(int end);
  Q_INVOKABLE void requestThumbnail(int image);
  Q_INVOKABLE int thumbnailVersion(int image) const { return m_thumbnailVersions.value(image); }
  Q_INVOKABLE void exportTo(const QUrl &url, const QString &format);
  Q_INVOKABLE void navigateImage(int direction);
  Q_INVOKABLE void shutdown();
  Q_INVOKABLE void retrySave();
  Q_INVOKABLE void openRecoveryFolder();
  Q_INVOKABLE void saveView(double zoom, double centerX, double centerY);
  Q_INVOKABLE void switchInterface(const QString &mode);
signals:
  void clipboardChanged();
  void historyRequestedAction(QString action, QJsonObject settings, quint64 revision);
  void cropChanged();
  void cropRequested(QString action, QJsonObject settings, quint64 revision);
  void stateChanged();
  void imagesChanged();
  void libraryChanged();
  void collectionValuesChanged();
  void importReviewChanged();
  void imageInformationChanged();
  void moduleChanged();
  void historyChanged();
  void previewChanged();
  void thumbnailChanged(int id);
  void closingReady();
  void viewRestored(double zoom, double centerX, double centerY);
  void boot(QStringList arguments);
  void importRequested(QStringList paths);
  void libraryRequested(QString action, QJsonObject settings, int image);
  void importReviewRequested(QStringList paths, bool recursive);
  void openRequested(int id, quint64 revision);
  void moduleRequested(QString operation, quint64 revision);
  void parameterRequested(QString operation, QString name, QVariant value, bool commit, quint64 revision);
  void enabledRequested(QString operation, bool enabled, quint64 revision);
  void resetRequested(QString operation, quint64 revision);
  void historyRequested(int direction, quint64 revision);
  void historyJumpRequested(int end, quint64 revision);
  void thumbnailRequested(int id);
  void exportRequested(QString path, QString format);
  void batchExportRequested(QJsonObject settings);
  void shutdownRequested();
  void viewRequested(int image, bool editor, double zoom, double centerX, double centerY);
  void switchRequested(QString mode);
private:
  quint64 advance();
  void updateImages(const QJsonArray &images);
  void flush(bool commit);
  void applyModule(const QJsonObject &module);
  PreviewStore *m_store;
  QThread m_thread;
  EngineWorker *m_worker;
  std::shared_ptr<std::atomic<bool>> m_exportCancel;
  std::shared_ptr<std::atomic<quint64>> m_revision;
  QTimer m_editTimer;
  QVariantList m_allImages;
  bool m_touchSettingsLoaded = false;
  bool m_showRejected = false, m_advanceAfterReject = false;
  bool m_quickReject = false, m_quickDelete = false;
  int m_rejectNext = 0;
  QString m_lastName, m_lastOperation;
  double m_lastStep = 0.01;
  QVariantMap m_lastField;
  QVariantList m_images, m_tools, m_moduleCoverage;
  QVariantList m_history;
  QVariantMap m_imageInformation;
  bool m_exportRunning = false;
  bool m_cropEditing = false;
  QSize m_cropOriginalSize;
  QVariantList m_cropBounds;
  QVariantMap m_moduleStates;
  QVariantMap m_copiedHistory;
  QVariantMap m_library;
  QVariantMap m_collectionValues;
  QVariantList m_importCandidates;
  FieldModel m_fields;
  QSet<int> m_requestedThumbnails;
  QHash<int, int> m_thumbnailVersions;
  QString m_operation = QStringLiteral("exposure");
  QString m_toolLabel = QStringLiteral("exposure");
  QString m_pendingName, m_pendingOperation;
  QVariant m_pendingValue;
  QString m_message = tr("starting the photo engine");
  QString m_saveIssue, m_recoveryPath;
  int m_imageId = 0;
  int m_historyEnd = 0;
  quint64 m_frameRevision = 0;
  bool m_ready = false, m_busy = true, m_canUndo = false, m_canRedo = false;
  bool m_canClose = false, m_editorVisible = false, m_before = false, m_moduleEnabled = false;
  bool m_needBefore = true;
  bool m_waitingFrame = false;
  bool m_failed = false;
  QJsonObject m_restoreView, m_deferredModule;
};
