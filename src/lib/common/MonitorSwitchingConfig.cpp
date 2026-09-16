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
QJsonArray integerArray(const QList<int> &values)
{
  QJsonArray result;
  for (const auto value : values)
    result.append(value);
  return result;
}

QJsonObject routeToJson(const MonitorInputRoute &route)
{
  return {
      {QStringLiteral("computerName"), route.computerName},
      {QStringLiteral("inputId"), route.inputId},
      {QStringLiteral("inputLabel"), route.inputLabel},
      {QStringLiteral("inputValue"), route.inputValue},
      {QStringLiteral("expectedReadValues"), integerArray(route.expectedReadValues)},
      {QStringLiteral("lastTestStatus"), route.lastTestStatus},
      {QStringLiteral("lastTestMessage"), route.lastTestMessage},
      {QStringLiteral("lastTestedAt"), route.lastTestedAt},
  };
}

MonitorInputRoute routeFromJson(const QJsonObject &object)
{
  MonitorInputRoute route;
  route.computerName = object.value(QStringLiteral("computerName")).toString();
  route.inputId = object.value(QStringLiteral("inputId")).toString();
  route.inputLabel = object.value(QStringLiteral("inputLabel")).toString();
  route.inputValue = object.value(QStringLiteral("inputValue")).toInt(-1);
  for (const auto &value : object.value(QStringLiteral("expectedReadValues")).toArray())
    route.expectedReadValues.append(value.toInt(-1));
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
      !root.value(QStringLiteral("monitor")).isObject() || !root.value(QStringLiteral("routes")).isArray()) {
    if (error)
      *error = QStringLiteral("The monitor switching configuration has an invalid structure.");
    return config;
  }
  const auto storedSchemaVersion = root.value(QStringLiteral("schemaVersion")).toInt(0);
  if (storedSchemaVersion != 1 && storedSchemaVersion != SchemaVersion) {
    if (error)
      *error = QStringLiteral("Unsupported monitor switching configuration version: %1").arg(storedSchemaVersion);
    config.enabled = false;
    return config;
  }
  if (storedSchemaVersion == SchemaVersion &&
      (!root.value(QStringLiteral("activeConfigHash")).isString() ||
       !root.value(QStringLiteral("profileId")).isString() ||
       !root.value(QStringLiteral("profileRevision")).isDouble() ||
       std::floor(root.value(QStringLiteral("profileRevision")).toDouble()) !=
           root.value(QStringLiteral("profileRevision")).toDouble())) {
    if (error)
      *error = QStringLiteral("The monitor switching configuration has invalid profile metadata.");
    return config;
  }

  config.schemaVersion = SchemaVersion;
  config.enabled = storedSchemaVersion == SchemaVersion && root.value(QStringLiteral("enabled")).toBool(false);
  config.activeConfigHash = root.value(QStringLiteral("activeConfigHash")).toString();
  const auto monitor = root.value(QStringLiteral("monitor")).toObject();
  if (!monitor.value(QStringLiteral("id")).isString() || !monitor.value(QStringLiteral("name")).isString()) {
    if (error)
      *error = QStringLiteral("The configured monitor is invalid.");
    return MonitorSwitchingConfig{};
  }
  if (storedSchemaVersion == SchemaVersion &&
      (!monitor.value(QStringLiteral("manufacturerId")).isString() ||
       !monitor.value(QStringLiteral("productId")).isDouble() ||
       std::floor(monitor.value(QStringLiteral("productId")).toDouble()) !=
           monitor.value(QStringLiteral("productId")).toDouble() ||
       !monitor.value(QStringLiteral("modelName")).isString())) {
    if (error)
      *error = QStringLiteral("The configured monitor identity is invalid.");
    return MonitorSwitchingConfig{};
  }
  config.monitorId = monitor.value(QStringLiteral("id")).toString();
  config.monitorName = monitor.value(QStringLiteral("name")).toString();
  config.monitorManufacturerId = monitor.value(QStringLiteral("manufacturerId")).toString();
  config.monitorProductId = monitor.value(QStringLiteral("productId")).toInt(-1);
  config.monitorModelName = monitor.value(QStringLiteral("modelName")).toString();
  config.profileId = root.value(QStringLiteral("profileId")).toString();
  config.profileRevision = root.value(QStringLiteral("profileRevision")).toInt(0);

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
    if (storedSchemaVersion == SchemaVersion &&
        (!routeObject.value(QStringLiteral("inputId")).isString() ||
         !routeObject.value(QStringLiteral("expectedReadValues")).isArray())) {
      if (error)
        *error = QStringLiteral("A monitor input route has invalid profile metadata.");
      return MonitorSwitchingConfig{};
    }
    for (const auto &readValue : routeObject.value(QStringLiteral("expectedReadValues")).toArray()) {
      if (!readValue.isDouble() || std::floor(readValue.toDouble()) != readValue.toDouble() ||
          readValue.toDouble() < 0 || readValue.toDouble() > 65535) {
        if (error)
          *error = QStringLiteral("A monitor input route has an invalid expected read value.");
        return MonitorSwitchingConfig{};
      }
    }
    config.routes.append(routeFromJson(routeObject));
  }

  if (storedSchemaVersion == 1) {
    config.enabled = false;
    config.activeConfigHash.clear();
  }
  if (!config.isActiveConfigurationValid())
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
      {QStringLiteral("activeConfigHash"), activeConfigHash},
      {QStringLiteral("profileId"), profileId},
      {QStringLiteral("profileRevision"), profileRevision},
      {QStringLiteral("monitor"),
       QJsonObject{
           {QStringLiteral("id"), monitorId},
           {QStringLiteral("name"), monitorName},
           {QStringLiteral("manufacturerId"), monitorManufacturerId},
           {QStringLiteral("productId"), monitorProductId},
           {QStringLiteral("modelName"), monitorModelName},
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
            {QStringLiteral("inputId"), route.inputId},
            {QStringLiteral("inputLabel"), route.inputLabel},
            {QStringLiteral("inputValue"), route.inputValue},
            {QStringLiteral("expectedReadValues"), integerArray(route.expectedReadValues)},
        }
    );
  }

  const QJsonObject verifiedData{
      {QStringLiteral("schemaVersion"), schemaVersion},
      {QStringLiteral("monitorId"), monitorId},
      {QStringLiteral("monitorName"), monitorName},
      {QStringLiteral("monitorManufacturerId"), monitorManufacturerId},
      {QStringLiteral("monitorProductId"), monitorProductId},
      {QStringLiteral("monitorModelName"), monitorModelName},
      {QStringLiteral("profileId"), profileId},
      {QStringLiteral("profileRevision"), profileRevision},
      {QStringLiteral("routes"), routeArray},
  };
  return QString::fromLatin1(
      QCryptographicHash::hash(QJsonDocument(verifiedData).toJson(QJsonDocument::Compact), QCryptographicHash::Sha256)
          .toHex()
  );
}

bool MonitorSwitchingConfig::isActiveConfigurationValid() const
{
  return !activeConfigHash.isEmpty() && activeConfigHash == configurationHash();
}

void MonitorSwitchingConfig::invalidate()
{
  enabled = false;
  activeConfigHash.clear();
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
  QList<int> inputValues;
  for (const auto &route : routes) {
    if (route.inputValue < 0 || route.inputValue > 65535)
      return QStringLiteral("Select a monitor input for every participating computer.");
    if (inputValues.contains(route.inputValue))
      return QStringLiteral("Assign a different monitor input to each participating computer.");
    inputValues.append(route.inputValue);
  }
  return {};
}
