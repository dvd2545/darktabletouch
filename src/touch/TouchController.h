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
private:
  mutable QReadWriteLock m_lock;
  QImage m_frame, m_before;
  QCache<int, QImage> m_thumbnails;
};

class EngineWorker final : public QObject
{
  Q_OBJECT
public:
  explicit EngineWorker(std::shared_ptr<std::atomic<quint64>> revision);
public slots:
  void initialize(QStringList arguments);
  void importPaths(QStringList paths);
  void openImage(int id, quint64 revision);
  void loadModule(QString operation, quint64 revision);
  void setParameter(QString operation, QString name, QVariant value, bool commit, quint64 revision);
  void changeEnabled(QString operation, bool enabled, quint64 revision);
  void resetModule(QString operation, quint64 revision);
  void historyStep(int direction, quint64 revision);
  void thumbnail(int id);
  void exportImage(QString path, QString format);
  void saveView(int image, bool editor, double zoom, double centerX, double centerY);
  void switchInterface(QString mode);
  void shutdown();
signals:
  void initialized(QJsonArray images, QJsonArray modules, QJsonObject session);
  void imagesChanged(QJsonArray images);
  void imageOpened(int id, quint64 revision);
  void moduleReady(QJsonObject module, quint64 revision);
  void frameReady(QImage image, quint64 revision);
  void thumbnailReady(int id, QImage image);
  void historyChanged(bool undo, bool redo);
  void completed(QString message);
  void failed(QString message);
  void stopped();
private:
  struct Edit { dt_touch_state_t *before; dt_touch_state_t *after; };
  void render(quint64 revision, int extent = 1280);
  void beginEdit();
  void commitEdit();
  void clearHistory();
  void report(char *error);
  QJsonArray images();
  std::shared_ptr<std::atomic<quint64>> m_revision;
  dt_touch_editor_t *m_editor = nullptr;
  dt_touch_state_t *m_before = nullptr;
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
  Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
  Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
  Q_PROPERTY(bool canUndo READ canUndo NOTIFY stateChanged)
  Q_PROPERTY(bool canRedo READ canRedo NOTIFY stateChanged)
  Q_PROPERTY(bool canClose READ canClose NOTIFY stateChanged)
  Q_PROPERTY(int imageId READ imageId NOTIFY stateChanged)
  Q_PROPERTY(QVariantList images READ images NOTIFY imagesChanged)
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
  bool ready() const { return m_ready; }
  bool busy() const { return m_busy; }
  bool canUndo() const { return m_canUndo; }
  bool canRedo() const { return m_canRedo; }
  bool canClose() const { return m_canClose; }
  int imageId() const { return m_imageId; }
  QVariantList images() const { return m_images; }
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
  Q_INVOKABLE void selectImage(int id);
  Q_INVOKABLE void selectTool(const QString &operation);
  Q_INVOKABLE void setParameter(const QString &name, const QVariant &value, bool commit);
  Q_INVOKABLE void setEnabled(bool enabled);
  Q_INVOKABLE void reset();
  Q_INVOKABLE void undo();
  Q_INVOKABLE void redo();
  Q_INVOKABLE void requestThumbnail(int image);
  Q_INVOKABLE void exportTo(const QUrl &url, const QString &format);
  Q_INVOKABLE void navigateImage(int direction);
  Q_INVOKABLE void shutdown();
  Q_INVOKABLE void saveView(double zoom, double centerX, double centerY);
  Q_INVOKABLE void switchInterface(const QString &mode);
signals:
  void stateChanged();
  void imagesChanged();
  void moduleChanged();
  void previewChanged();
  void thumbnailChanged(int id);
  void closingReady();
  void viewRestored(double zoom, double centerX, double centerY);
  void boot(QStringList arguments);
  void importRequested(QStringList paths);
  void openRequested(int id, quint64 revision);
  void moduleRequested(QString operation, quint64 revision);
  void parameterRequested(QString operation, QString name, QVariant value, bool commit, quint64 revision);
  void enabledRequested(QString operation, bool enabled, quint64 revision);
  void resetRequested(QString operation, quint64 revision);
  void historyRequested(int direction, quint64 revision);
  void thumbnailRequested(int id);
  void exportRequested(QString path, QString format);
  void shutdownRequested();
  void viewRequested(int image, bool editor, double zoom, double centerX, double centerY);
  void switchRequested(QString mode);
private:
  quint64 advance();
  void updateImages(const QJsonArray &images);
  void flush(bool commit);
  PreviewStore *m_store;
  QThread m_thread;
  EngineWorker *m_worker;
  std::shared_ptr<std::atomic<quint64>> m_revision;
  QTimer m_editTimer;
  QVariantList m_images, m_tools, m_moduleCoverage;
  FieldModel m_fields;
  QSet<int> m_requestedThumbnails;
  QString m_operation = QStringLiteral("exposure");
  QString m_toolLabel = QStringLiteral("exposure");
  QString m_pendingName, m_pendingOperation;
  QVariant m_pendingValue;
  QString m_message = tr("starting the photo engine");
  int m_imageId = 0;
  quint64 m_frameRevision = 0;
  bool m_ready = false, m_busy = true, m_canUndo = false, m_canRedo = false;
  bool m_canClose = false, m_editorVisible = false, m_before = false, m_moduleEnabled = false;
  bool m_needBefore = true;
  bool m_failed = false;
  QJsonObject m_restoreView;
};
