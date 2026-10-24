/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QUuid>

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  const auto arguments = app.arguments();
  const int configIndex = arguments.indexOf(QStringLiteral("--configdir"));
  if(configIndex < 0 || configIndex + 1 == arguments.size()) return 2;
  if(!qEnvironmentVariableIsSet("DARKTABLETOUCH_MANAGED")) return 3;
  const QDir config(arguments[configIndex + 1]);
  const bool touch = app.applicationFilePath().contains(QStringLiteral("darktable-touch"));
  QFile log(config.filePath(QStringLiteral("children.jsonl")));
  if(!log.open(QIODevice::Append)) return 4;
  log.write(QJsonDocument(QJsonObject{{QStringLiteral("ui"), touch ? "touch" : "classic"},
             {QStringLiteral("restoring"), qEnvironmentVariableIsSet("DARKTABLETOUCH_RESTORE_SESSION")},
             {QStringLiteral("arguments"), QJsonArray::fromStringList(arguments.mid(1))}}).toJson(QJsonDocument::Compact) + '\n');
  log.close();
  if(!arguments.contains(QStringLiteral("--probe-stale")))
  {
    QSaveFile state(config.filePath(QStringLiteral("touch-session.json")));
    if(!state.open(QIODevice::WriteOnly)) return 5;
    state.write(QJsonDocument(QJsonObject{{QStringLiteral("version"), 1},
      {QStringLiteral("request_id"), QUuid::createUuid().toString()},
      {QStringLiteral("next_ui"), touch ? "classic" : ""}}).toJson());
    if(!state.commit()) return 6;
  }
  return arguments.contains(QStringLiteral("--probe-fail")) ? 7 : 0;
}
