/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <optional>

struct MonitorProfileInput
{
  QString id;
  QString label;
  int writeValue = -1;
  QList<int> readValues;

  bool operator==(const MonitorProfileInput &) const = default;
};

struct MonitorProfile
{
  QString id;
  QString manufacturerId;
  int productId = -1;
  QStringList modelNames;
  int revision = 1;
  QString source;
  QList<MonitorProfileInput> inputs;

  bool operator==(const MonitorProfile &) const = default;
};

struct MonitorProfileIdentity
{
  QString manufacturerId;
  int productId = -1;
  QString modelName;
};

class MonitorProfileDatabase
{
public:
  static constexpr int SchemaVersion = 1;

  static MonitorProfileDatabase loadBundled(QString *error = nullptr);
  static MonitorProfileDatabase loadActive(QString *error = nullptr);
  static MonitorProfileDatabase loadFile(const QString &path, QString *error = nullptr);
  static QString userFilePath();
  static bool installUserDatabase(const QString &sourcePath, QString *error = nullptr);

  static MonitorProfileDatabase fromJson(const QByteArray &data, QString *error = nullptr);

  QString databaseVersion() const;
  QList<MonitorProfile> profiles() const;
  std::optional<MonitorProfile> find(const MonitorProfileIdentity &identity) const;
  bool isValid() const;

private:
  static QString normalizedName(const QString &name);
  void merge(const MonitorProfileDatabase &overrides);

  QString m_databaseVersion;
  QList<MonitorProfile> m_profiles;
  bool m_valid = false;
};
