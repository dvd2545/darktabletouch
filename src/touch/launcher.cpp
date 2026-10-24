/*
    This file is part of darktable,
    Copyright (C) 2026 darktable developers.
    SPDX-License-Identifier: GPL-3.0-or-later
*/

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSettings>
#include <QTextStream>
#include <glib.h>

static QJsonObject _session(const QString &config)
{
  QFile file(QDir(config).filePath(QStringLiteral("touch-session.json")));
  if(!file.open(QIODevice::ReadOnly) || file.size() > 1024 * 1024) return {};
  const auto state = QJsonDocument::fromJson(file.readAll()).object();
  return state.value(QStringLiteral("version")).toInt() == 1 ? state : QJsonObject();
}

int main(int argc, char **argv)
{
  QCoreApplication app(argc, argv);
  QCoreApplication::setApplicationName(QStringLiteral("darktabletouch"));
  QLocale::setDefault(QLocale(QLocale::English, QLocale::UnitedStates));
  QStringList arguments = app.arguments().mid(1);
  for(const QString &argument : arguments)
    if(QString::fromUtf8(argument.toUtf8()) != argument)
    {
      QTextStream(stderr) << "a command-line argument cannot be represented as valid UTF-8\n";
      return 2;
    }
  QString config = QDir(QString::fromUtf8(g_get_user_config_dir())).filePath(QStringLiteral("darktabletouch"));
  for(int i = 0; i + 1 < arguments.size(); i++)
    if(arguments[i] == QStringLiteral("--configdir")) config = arguments[i + 1];
  if(!QDir().mkpath(config))
  {
    QTextStream(stderr) << "could not create the DarktableTouch configuration directory\n";
    return 1;
  }
  QSettings settings(QDir(config).filePath(QStringLiteral("launcher.ini")), QSettings::IniFormat);
  QString mode = settings.value(QStringLiteral("ui"), QStringLiteral("touch")).toString();
  for(int i = 0; i < arguments.size();)
  {
    if(arguments[i].startsWith(QStringLiteral("--ui=")))
    { mode = arguments.takeAt(i).mid(5); }
    else if(arguments[i] == QStringLiteral("--ui") && i + 1 < arguments.size())
    { arguments.removeAt(i); mode = arguments.takeAt(i); }
    else i++;
  }
  if(mode != QStringLiteral("touch") && mode != QStringLiteral("classic"))
  {
    QTextStream(stderr) << "--ui must be touch or classic\n";
    return 2;
  }
  bool restoreSession = false;
  for(;;)
  {
    QString binary = mode == QStringLiteral("touch") ? QStringLiteral("darktable-touch") : QStringLiteral("darktable");
#ifdef Q_OS_WIN
    binary += QStringLiteral(".exe");
#endif
    const QString program = QDir(app.applicationDirPath()).filePath(binary);
    if(!QFileInfo::exists(program))
    {
      QTextStream(stderr) << "the selected interface is not installed: " << program << '\n';
      return 1;
    }
    settings.setValue(QStringLiteral("ui"), mode);
    settings.sync();
    if(settings.status() != QSettings::NoError)
    {
      QTextStream(stderr) << "could not remember the selected interface\n";
      return 1;
    }
    QProcess child;
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("DARKTABLETOUCH_MANAGED"), QStringLiteral("1"));
    if(restoreSession) environment.insert(QStringLiteral("DARKTABLETOUCH_RESTORE_SESSION"), QStringLiteral("1"));
    else environment.remove(QStringLiteral("DARKTABLETOUCH_RESTORE_SESSION"));
    child.setProcessEnvironment(environment);
    child.setProgram(program);
    child.setArguments(arguments);
    child.setProcessChannelMode(QProcess::ForwardedChannels);
    const QString previousRequest = _session(config).value(QStringLiteral("request_id")).toString();
    child.start();
    if(!child.waitForStarted())
    {
      QTextStream(stderr) << "could not start the selected interface: " << child.errorString() << '\n';
      return 1;
    }
    if(!child.waitForFinished(-1) || child.exitStatus() != QProcess::NormalExit) return 1;
    if(child.exitCode()) return child.exitCode();
    const auto state = _session(config);
    const QString request = state.value(QStringLiteral("request_id")).toString();
    const QString next = state.value(QStringLiteral("next_ui")).toString();
    if(request.isEmpty() || request == previousRequest || next == mode
       || (next != QStringLiteral("touch") && next != QStringLiteral("classic"))) return 0;
    mode = next;
    restoreSession = true;
  }
}
