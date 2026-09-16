/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>

#include <optional>

struct MonitorInputRoute
{
  QString computerName;
  QString inputLabel;
  int inputValue = 0;
  QString lastTestStatus;
  QString lastTestMessage;
  QString lastTestedAt;
  QString inputId;
  QList<int> expectedReadValues;

  bool operator==(const MonitorInputRoute &) const = default;
};

struct MonitorSwitchingConfig
{
  static constexpr int SchemaVersion = 2;

  int schemaVersion = SchemaVersion;
  bool enabled = false;
  QString activeConfigHash;
  QString monitorId;
  QString monitorName;
  QString monitorManufacturerId;
  int monitorProductId = -1;
  QString monitorModelName;
  QString profileId;
  int profileRevision = 0;
  QList<MonitorInputRoute> routes;

  static QString filePath();
  static MonitorSwitchingConfig load(QString *error = nullptr);

  bool save(QString *error = nullptr) const;
  QString configurationHash() const;
  bool isActiveConfigurationValid() const;
  void invalidate();
  std::optional<MonitorInputRoute> routeForComputer(const QString &computerName) const;
  QString validateSetupSelection(const QString &serverName, const QStringList &configuredComputers = {}) const;
  QString validate(const QString &serverName, const QStringList &configuredComputers = {}) const;
};
