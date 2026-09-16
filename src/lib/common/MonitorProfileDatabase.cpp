/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/MonitorProfileDatabase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace {
bool integerValue(const QJsonValue &value, int minimum, int maximum, int &result)
{
  if (!value.isDouble() || std::floor(value.toDouble()) != value.toDouble())
    return false;
  result = value.toInt(minimum - 1);
  return result >= minimum && result <= maximum;
}

bool parseInput(const QJsonObject &object, MonitorProfileInput &input, QString *error)
{
  if (!object.value(QStringLiteral("id")).isString() || !object.value(QStringLiteral("label")).isString() ||
      !integerValue(object.value(QStringLiteral("writeValue")), 0, 65535, input.writeValue)) {
    if (error)
      *error = QStringLiteral("A monitor profile input is invalid.");
    return false;
  }
  input.id = object.value(QStringLiteral("id")).toString().trimmed();
  input.label = object.value(QStringLiteral("label")).toString().trimmed();
  if (input.id.isEmpty() || input.label.isEmpty()) {
    if (error)
      *error = QStringLiteral("A monitor profile input has an empty ID or label.");
    return false;
  }

  const auto readValues = object.value(QStringLiteral("readValues"));
  if (!readValues.isUndefined() && !readValues.isArray()) {
    if (error)
      *error = QStringLiteral("A monitor profile input has invalid read values.");
    return false;
  }
  for (const auto &value : readValues.toArray()) {
    int parsed = -1;
    if (!integerValue(value, 0, 65535, parsed)) {
      if (error)
        *error = QStringLiteral("A monitor profile input has an invalid read value.");
      return false;
    }
    if (!input.readValues.contains(parsed))
      input.readValues.append(parsed);
  }
  return true;
}

bool parseProfile(const QJsonObject &object, MonitorProfile &profile, QString *error)
{
  if (!object.value(QStringLiteral("id")).isString() || !object.value(QStringLiteral("manufacturerId")).isString() ||
      !object.value(QStringLiteral("inputs")).isArray()) {
    if (error)
      *error = QStringLiteral("A monitor profile is missing required fields.");
    return false;
  }
  profile.id = object.value(QStringLiteral("id")).toString().trimmed();
  profile.manufacturerId = object.value(QStringLiteral("manufacturerId")).toString().trimmed().toUpper();
  profile.source = object.value(QStringLiteral("source")).toString().trimmed();
  if (profile.id.isEmpty() || profile.manufacturerId.size() != 3 || profile.source.isEmpty()) {
    if (error)
      *error = QStringLiteral("A monitor profile has an invalid identity.");
    return false;
  }

  const auto productValue = object.value(QStringLiteral("productId"));
  if (!productValue.isUndefined() && !integerValue(productValue, 0, 65535, profile.productId)) {
    if (error)
      *error = QStringLiteral("A monitor profile has an invalid product ID.");
    return false;
  }
  const auto revisionValue = object.value(QStringLiteral("revision"));
  if (!revisionValue.isUndefined() && !integerValue(revisionValue, 1, 2147483647, profile.revision)) {
    if (error)
      *error = QStringLiteral("A monitor profile has an invalid revision.");
    return false;
  }
  const auto namesValue = object.value(QStringLiteral("modelNames"));
  if (!namesValue.isUndefined() && !namesValue.isArray()) {
    if (error)
      *error = QStringLiteral("A monitor profile has invalid model names.");
    return false;
  }
  for (const auto &name : namesValue.toArray()) {
    if (!name.isString() || name.toString().trimmed().isEmpty()) {
      if (error)
        *error = QStringLiteral("A monitor profile has an invalid model name.");
      return false;
    }
    profile.modelNames.append(name.toString().trimmed());
  }
  if (profile.productId < 0 && profile.modelNames.isEmpty()) {
    if (error)
      *error = QStringLiteral("A monitor profile needs a product ID or model name.");
    return false;
  }

  QSet<QString> inputIds;
  QSet<int> writeValues;
  for (const auto &value : object.value(QStringLiteral("inputs")).toArray()) {
    if (!value.isObject()) {
      if (error)
        *error = QStringLiteral("A monitor profile input is invalid.");
      return false;
    }
    MonitorProfileInput input;
    if (!parseInput(value.toObject(), input, error))
      return false;
    if (inputIds.contains(input.id) || writeValues.contains(input.writeValue)) {
      if (error)
        *error = QStringLiteral("A monitor profile contains duplicate inputs.");
      return false;
    }
    inputIds.insert(input.id);
    writeValues.insert(input.writeValue);
    profile.inputs.append(input);
  }
  if (profile.inputs.isEmpty()) {
    if (error)
      *error = QStringLiteral("A monitor profile does not contain any inputs.");
    return false;
  }
  return true;
}
} // namespace

MonitorProfileDatabase MonitorProfileDatabase::loadBundled(QString *error)
{
  return loadFile(QStringLiteral(":/monitor-profiles.json"), error);
}

MonitorProfileDatabase MonitorProfileDatabase::loadActive(QString *error)
{
  QString bundledError;
  auto database = loadBundled(&bundledError);
  if (!database.isValid()) {
    if (error)
      *error = bundledError;
    return database;
  }

  const auto path = userFilePath();
  if (!QFile::exists(path)) {
    if (error)
      error->clear();
    return database;
  }
  QString userError;
  const auto userDatabase = loadFile(path, &userError);
  if (!userDatabase.isValid()) {
    if (error)
      *error = QStringLiteral("The uploaded monitor database is invalid: %1").arg(userError);
    return database;
  }
  database.merge(userDatabase);
  if (error)
    error->clear();
  return database;
}

MonitorProfileDatabase MonitorProfileDatabase::loadFile(const QString &path, QString *error)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    if (error)
      *error = file.errorString();
    return {};
  }
  return fromJson(file.readAll(), error);
}

QString MonitorProfileDatabase::userFilePath()
{
  const auto overridePath = qEnvironmentVariable("TELEPORT_MONITOR_PROFILE_DATABASE_PATH");
  if (!overridePath.isEmpty())
    return overridePath;
  return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation))
      .filePath(QStringLiteral("Teleport/monitor-profiles.json"));
}

bool MonitorProfileDatabase::installUserDatabase(const QString &sourcePath, QString *error)
{
  QFile source(sourcePath);
  if (!source.open(QIODevice::ReadOnly)) {
    if (error)
      *error = source.errorString();
    return false;
  }
  const auto data = source.readAll();
  QString validationError;
  if (!fromJson(data, &validationError).isValid()) {
    if (error)
      *error = validationError;
    return false;
  }

  const QFileInfo destinationInfo(userFilePath());
  if (!QDir().mkpath(destinationInfo.absolutePath())) {
    if (error)
      *error = QStringLiteral("Could not create %1").arg(destinationInfo.absolutePath());
    return false;
  }
  QSaveFile destination(destinationInfo.filePath());
  if (!destination.open(QIODevice::WriteOnly) || destination.write(data) != data.size() || !destination.commit()) {
    if (error)
      *error = destination.errorString();
    return false;
  }
  if (error)
    error->clear();
  return true;
}

MonitorProfileDatabase MonitorProfileDatabase::fromJson(const QByteArray &data, QString *error)
{
  if (error)
    error->clear();
  MonitorProfileDatabase database;
  QJsonParseError parseError;
  const auto document = QJsonDocument::fromJson(data, &parseError);
  if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
    if (error)
      *error = parseError.errorString();
    return database;
  }
  const auto root = document.object();
  int schemaVersion = 0;
  if (!integerValue(root.value(QStringLiteral("schemaVersion")), 1, 2147483647, schemaVersion) ||
      schemaVersion != SchemaVersion || !root.value(QStringLiteral("databaseVersion")).isString() ||
      !root.value(QStringLiteral("profiles")).isArray()) {
    if (error)
      *error = QStringLiteral("Unsupported or invalid monitor profile database.");
    return database;
  }
  database.m_databaseVersion = root.value(QStringLiteral("databaseVersion")).toString().trimmed();
  if (database.m_databaseVersion.isEmpty()) {
    if (error)
      *error = QStringLiteral("The monitor profile database version is empty.");
    return database;
  }

  QSet<QString> profileIds;
  QSet<QString> productKeys;
  for (const auto &value : root.value(QStringLiteral("profiles")).toArray()) {
    if (!value.isObject()) {
      if (error)
        *error = QStringLiteral("A monitor profile is invalid.");
      return {};
    }
    MonitorProfile profile;
    if (!parseProfile(value.toObject(), profile, error))
      return {};
    if (profileIds.contains(profile.id)) {
      if (error)
        *error = QStringLiteral("The monitor profile database contains duplicate profile IDs.");
      return {};
    }
    if (profile.productId >= 0) {
      const auto productKey = QStringLiteral("%1:%2").arg(profile.manufacturerId).arg(profile.productId);
      if (productKeys.contains(productKey)) {
        if (error)
          *error = QStringLiteral("The monitor profile database contains duplicate monitor identities.");
        return {};
      }
      productKeys.insert(productKey);
    }
    profileIds.insert(profile.id);
    database.m_profiles.append(profile);
  }
  database.m_valid = true;
  return database;
}

QString MonitorProfileDatabase::databaseVersion() const
{
  return m_databaseVersion;
}

QList<MonitorProfile> MonitorProfileDatabase::profiles() const
{
  return m_profiles;
}

QList<MonitorProfile> MonitorProfileDatabase::findAllByModelName(const QString &modelName) const
{
  const auto normalizedModelName = normalizedName(modelName);
  if (normalizedModelName.isEmpty())
    return {};

  QList<MonitorProfile> matches;
  for (const auto &profile : m_profiles) {
    if (std::ranges::any_of(profile.modelNames, [&](const auto &name) {
          return normalizedName(name) == normalizedModelName;
        })) {
      matches.append(profile);
    }
  }
  std::ranges::sort(matches, {}, &MonitorProfile::id);
  return matches;
}

bool MonitorProfileDatabase::isValid() const
{
  return m_valid;
}

QString MonitorProfileDatabase::normalizedName(const QString &name)
{
  auto result = name.toUpper();
  result.removeIf([](const QChar character) { return !character.isLetterOrNumber(); });
  return result;
}

void MonitorProfileDatabase::merge(const MonitorProfileDatabase &overrides)
{
  for (const auto &profile : overrides.m_profiles) {
    const auto existing = std::ranges::find_if(m_profiles, [&](const auto &candidate) {
      if (candidate.id == profile.id)
        return true;
      if (candidate.manufacturerId != profile.manufacturerId)
        return false;
      return profile.productId >= 0 && candidate.productId >= 0 && candidate.productId == profile.productId;
    });
    if (existing == m_profiles.end())
      m_profiles.append(profile);
    else
      *existing = profile;
  }
  m_databaseVersion = QStringLiteral("%1+%2").arg(m_databaseVersion, overrides.m_databaseVersion);
}
