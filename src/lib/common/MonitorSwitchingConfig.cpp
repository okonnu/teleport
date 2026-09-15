/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/MonitorSwitchingConfig.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace {
QJsonObject routeToJson(const MonitorInputRoute &route)
{
  return {
      {QStringLiteral("computerName"), route.computerName},
      {QStringLiteral("inputLabel"), route.inputLabel},
      {QStringLiteral("inputValue"), route.inputValue},
      {QStringLiteral("lastTestStatus"), route.lastTestStatus},
      {QStringLiteral("lastTestMessage"), route.lastTestMessage},
      {QStringLiteral("lastTestedAt"), route.lastTestedAt},
  };
}

MonitorInputRoute routeFromJson(const QJsonObject &object)
{
  MonitorInputRoute route;
  route.computerName = object.value(QStringLiteral("computerName")).toString();
  route.inputLabel = object.value(QStringLiteral("inputLabel")).toString();
  route.inputValue = object.value(QStringLiteral("inputValue")).toInt(-1);
  route.lastTestStatus = object.value(QStringLiteral("lastTestStatus")).toString();
  route.lastTestMessage = object.value(QStringLiteral("lastTestMessage")).toString();
  route.lastTestedAt = object.value(QStringLiteral("lastTestedAt")).toString();
  return route;
}
} // namespace

QString MonitorSwitchingConfig::filePath()
{
  const auto overridePath = qEnvironmentVariable("TELEPORT_MONITOR_CONFIG_PATH");
  if (!overridePath.isEmpty())
    return overridePath;
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
      .filePath(QStringLiteral("Teleport/monitor-switching.json"));
}

MonitorSwitchingConfig MonitorSwitchingConfig::load(QString *error)
{
  if (error)
    error->clear();
  MonitorSwitchingConfig config;
  QFile file(filePath());
  if (!file.exists())
    return config;

  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = file.errorString();
    return config;
  }

  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(file.readAll(), &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error)
      *error = parseError.errorString();
    return config;
  }

  const auto root = document.object();
  if (!root.value(QStringLiteral("schemaVersion")).isDouble() || !root.value(QStringLiteral("enabled")).isBool() ||
      !root.value(QStringLiteral("verifiedConfigHash")).isString() ||
      !root.value(QStringLiteral("monitor")).isObject() || !root.value(QStringLiteral("routes")).isArray()) {
    if (error)
      *error = QStringLiteral("The monitor switching configuration has an invalid structure.");
    return config;
  }
  config.schemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(0);
  if (config.schemaVersion != SchemaVersion) {
    if (error)
      *error = QStringLiteral("Unsupported monitor switching configuration version: %1").arg(config.schemaVersion);
    config.enabled = false;
    return config;
  }

  config.enabled = root.value(QStringLiteral("enabled")).toBool(false);
  config.verifiedConfigHash = root.value(QStringLiteral("verifiedConfigHash")).toString();
  const auto monitor = root.value(QStringLiteral("monitor")).toObject();
  if (!monitor.value(QStringLiteral("id")).isString() || !monitor.value(QStringLiteral("name")).isString()) {
    if (error)
      *error = QStringLiteral("The configured monitor is invalid.");
    return MonitorSwitchingConfig{};
  }
  config.monitorId = monitor.value(QStringLiteral("id")).toString();
  config.monitorName = monitor.value(QStringLiteral("name")).toString();

  for (const auto &value : root.value(QStringLiteral("routes")).toArray()) {
    if (!value.isObject()) {
      if (error)
        *error = QStringLiteral("A monitor input route is invalid.");
      return MonitorSwitchingConfig{};
    }
    const auto routeObject = value.toObject();
    const auto inputValue = routeObject.value(QStringLiteral("inputValue"));
    if (!routeObject.value(QStringLiteral("computerName")).isString() ||
        !routeObject.value(QStringLiteral("inputLabel")).isString() || !inputValue.isDouble() ||
        std::floor(inputValue.toDouble()) != inputValue.toDouble()) {
      if (error)
        *error = QStringLiteral("A monitor input route is invalid.");
      return MonitorSwitchingConfig{};
    }
    config.routes.append(routeFromJson(routeObject));
  }

  if (!config.isVerified())
    config.enabled = false;
  return config;
}

bool MonitorSwitchingConfig::save(QString *error) const
{
  const QFileInfo info(filePath());
  if (!QDir().mkpath(info.absolutePath())) {
    if (error)
      *error = QStringLiteral("Could not create %1").arg(info.absolutePath());
    return false;
  }

  QJsonArray routeArray;
  for (const auto &route : routes)
    routeArray.append(routeToJson(route));

  const QJsonObject root{
      {QStringLiteral("schemaVersion"), schemaVersion},
      {QStringLiteral("enabled"), enabled},
      {QStringLiteral("verifiedConfigHash"), verifiedConfigHash},
      {QStringLiteral("monitor"),
       QJsonObject{
           {QStringLiteral("id"), monitorId},
           {QStringLiteral("name"), monitorName},
       }},
      {QStringLiteral("routes"), routeArray},
  };

  QSaveFile file(filePath());
  if (!file.open(QIODevice::WriteOnly)) {
    if (error)
      *error = file.errorString();
    return false;
  }
  const auto data = QJsonDocument(root).toJson(QJsonDocument::Indented);
  if (file.write(data) != data.size() || !file.commit()) {
    if (error)
      *error = file.errorString();
    return false;
  }
  return true;
}

QString MonitorSwitchingConfig::configurationHash() const
{
  auto sortedRoutes = routes;
  std::ranges::sort(sortedRoutes, {}, &MonitorInputRoute::computerName);

  QJsonArray routeArray;
  for (const auto &route : sortedRoutes) {
    routeArray.append(
        QJsonObject{
            {QStringLiteral("computerName"), route.computerName},
            {QStringLiteral("inputLabel"), route.inputLabel},
            {QStringLiteral("inputValue"), route.inputValue},
        }
    );
  }

  const QJsonObject verifiedData{
      {QStringLiteral("schemaVersion"), schemaVersion},
      {QStringLiteral("monitorId"), monitorId},
      {QStringLiteral("monitorName"), monitorName},
      {QStringLiteral("routes"), routeArray},
  };
  return QString::fromLatin1(
      QCryptographicHash::hash(QJsonDocument(verifiedData).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
          .toHex()
  );
}

bool MonitorSwitchingConfig::isVerified() const
{
  return !verifiedConfigHash.isEmpty() && verifiedConfigHash == configurationHash();
}

void MonitorSwitchingConfig::invalidate()
{
  enabled = false;
  verifiedConfigHash.clear();
}

std::optional<MonitorInputRoute> MonitorSwitchingConfig::routeForComputer(const QString &computerName) const
{
  const auto route = std::ranges::find(routes, computerName, &MonitorInputRoute::computerName);
  if (route == routes.end())
    return std::nullopt;
  return *route;
}

QString
MonitorSwitchingConfig::validateSetupSelection(const QString &serverName, const QStringList &configuredComputers) const
{
  if (schemaVersion != SchemaVersion)
    return QStringLiteral("The configuration version is unsupported.");
  if (monitorId.trimmed().isEmpty() || monitorName.trimmed().isEmpty())
    return QStringLiteral("Select a monitor.");
  if (routes.size() < 2)
    return QStringLiteral("Select the server and at least one other computer.");

  bool hasServer = false;
  QStringList names;
  for (const auto &route : routes) {
    if (route.computerName.trimmed().isEmpty() || route.inputLabel.trimmed().isEmpty())
      return QStringLiteral("Every selected computer needs an input label.");
    if (names.contains(route.computerName))
      return QStringLiteral("A computer has more than one monitor route.");
    if (!configuredComputers.isEmpty() && !configuredComputers.contains(route.computerName))
      return QStringLiteral("A monitor route refers to an unknown computer: %1").arg(route.computerName);
    names.append(route.computerName);
    hasServer = hasServer || route.computerName == serverName;
  }
  if (!hasServer)
    return QStringLiteral("The server computer must have a monitor route.");
  return {};
}

QString MonitorSwitchingConfig::validate(const QString &serverName, const QStringList &configuredComputers) const
{
  if (const auto selectionError = validateSetupSelection(serverName, configuredComputers); !selectionError.isEmpty())
    return selectionError;
  for (const auto &route : routes) {
    if (route.inputValue < 0 || route.inputValue > 65535)
      return QStringLiteral("Run input detection so every selected computer has a verified monitor input.");
  }
  return {};
}
