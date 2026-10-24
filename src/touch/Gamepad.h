/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#pragma once
#include <QElapsedTimer>
#include <QLibrary>
#include <QObject>
#include <QPointF>
#include <QTimer>

class Gamepad final : public QObject
{
  Q_OBJECT
  Q_PROPERTY(int choicesOpen MEMBER m_choicesOpen)
  Q_PROPERTY(bool editing MEMBER m_editing)
  Q_PROPERTY(bool connected READ connected NOTIFY connectedChanged)
public:
  explicit Gamepad(QObject *parent = nullptr);
  bool connected() const { return m_connected; }
signals:
  void connectedChanged();
  void action(QString name, bool pressed);
  void pan(QPointF delta);
  void cursor(QPointF delta);
  void zoom(double delta);
private:
  void poll();
  void sendKey(int key, bool pressed);
  QTimer m_timer;
  QLibrary m_library;
  QElapsedTimer m_repeat;
  quint16 m_buttons = 0;
  quint16 m_repeatButtons = 0;
  bool m_connected = false;
  bool m_editing = false;
  int m_choicesOpen = 0;
};
