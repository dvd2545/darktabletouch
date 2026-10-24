/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include "touch/Gamepad.h"
#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickItem>
#include <cmath>

#ifdef _WIN32
#include <windows.h>
#include <xinput.h>
#endif

Gamepad::Gamepad(QObject *parent) : QObject(parent)
{
#ifdef _WIN32
  wchar_t directory[32768];
  const UINT length = GetSystemDirectoryW(directory, 32768);
  if(length && length < 32768)
  {
    const QDir system(QString::fromWCharArray(directory, int(length)));
    for(const QString &name : {QStringLiteral("xinput1_4.dll"), QStringLiteral("xinput9_1_0.dll")})
    {
      m_library.setFileName(system.filePath(name));
      if(m_library.load()) break;
    }
  }
#endif
  connect(&m_timer, &QTimer::timeout, this, &Gamepad::poll);
  m_repeat.start();
  m_timer.start(16);
}

void Gamepad::sendKey(int key, bool pressed)
{
  QObject *focus = QGuiApplication::focusObject();
  if(!focus) return;
  QKeyEvent event(pressed ? QEvent::KeyPress : QEvent::KeyRelease, key, Qt::NoModifier);
  event.setAccepted(false);
  QCoreApplication::sendEvent(focus, &event);
  if(pressed && !event.isAccepted())
    if(auto item = qobject_cast<QQuickItem *>(focus))
    {
      const bool forward = key != Qt::Key_Left && key != Qt::Key_Up && key != Qt::Key_Backtab;
      QQuickItem *next = item->nextItemInFocusChain(forward);
      for(int i = 0; next && next != item && i < 512; i++)
      {
        if(next->isVisible() && next->isEnabled() && next->activeFocusOnTab())
        { next->forceActiveFocus(Qt::TabFocusReason); break; }
        next = next->nextItemInFocusChain(forward);
      }
    }
}

void Gamepad::poll()
{
#ifdef _WIN32
  using GetState = DWORD (WINAPI *)(DWORD, XINPUT_STATE *);
  const auto getState = reinterpret_cast<GetState>(m_library.resolve("XInputGetState"));
  if(!getState) { m_timer.stop(); return; }
  XINPUT_STATE state{};
  bool connected = false;
  for(DWORD index = 0; index < XUSER_MAX_COUNT; index++)
    if(getState(index, &state) == ERROR_SUCCESS) { connected = true; break; }
  if(connected != m_connected) { m_connected = connected; emit connectedChanged(); }
  const bool active = QGuiApplication::applicationState() == Qt::ApplicationActive;
  m_timer.setInterval(connected && active ? 16 : 500);
  const WORD buttons = connected && active ? state.Gamepad.wButtons : 0;
  struct Binding { WORD mask; int key; const char *action; };
  const Binding bindings[] = {
    {XINPUT_GAMEPAD_DPAD_UP, Qt::Key_Backtab, nullptr},
    {XINPUT_GAMEPAD_DPAD_DOWN, Qt::Key_Tab, nullptr},
    {XINPUT_GAMEPAD_DPAD_LEFT, Qt::Key_Left, nullptr},
    {XINPUT_GAMEPAD_DPAD_RIGHT, Qt::Key_Right, nullptr},
    {XINPUT_GAMEPAD_A, Qt::Key_Space, nullptr},
    {XINPUT_GAMEPAD_B, Qt::Key_Escape, nullptr},
    {XINPUT_GAMEPAD_X, 0, "compare"},
    {XINPUT_GAMEPAD_Y, 0, "search"},
    {XINPUT_GAMEPAD_LEFT_SHOULDER, 0, "previous"},
    {XINPUT_GAMEPAD_RIGHT_SHOULDER, 0, "next"},
    {XINPUT_GAMEPAD_BACK, 0, "library"},
    {XINPUT_GAMEPAD_START, 0, "menu"}
  };
  for(const auto &binding : bindings)
    if((buttons ^ m_buttons) & binding.mask)
    {
      const bool pressed = (buttons & binding.mask) != 0;
      if(binding.key) sendKey(binding.key, pressed);
      else emit action(QString::fromLatin1(binding.action), pressed);
    }
  const WORD directions = buttons & 15;
  if(directions != m_repeatButtons)
  {
    m_repeatButtons = directions;
    m_repeat.restart();
  }
  else if(directions && m_repeat.elapsed() > 350)
  {
    for(const auto &binding : bindings)
      if(binding.mask & directions) { sendKey(binding.key, true); sendKey(binding.key, false); }
    m_repeat.restart();
  }
  m_buttons = buttons;
  if(!connected || !active) return;
  const auto stick = [](SHORT x, SHORT y) {
    const double nx = x / 32768.0, ny = -y / 32768.0;
    const double magnitude = std::hypot(nx, ny);
    if(magnitude <= 0.20) return QPointF();
    const double scale = (qMin(magnitude, 1.0) - 0.20) / (0.80 * magnitude);
    return QPointF(nx * scale, ny * scale);
  };
  const QPointF left = stick(state.Gamepad.sThumbLX, state.Gamepad.sThumbLY);
  const QPointF right = stick(state.Gamepad.sThumbRX, state.Gamepad.sThumbRY);
  if(!left.isNull()) emit pan(left);
  if(!right.isNull()) emit cursor(right);
  const double trigger = (state.Gamepad.bRightTrigger - state.Gamepad.bLeftTrigger) / 255.0;
  if(std::abs(trigger) > 0.12) emit zoom(trigger);
#else
  m_timer.stop();
#endif
}
