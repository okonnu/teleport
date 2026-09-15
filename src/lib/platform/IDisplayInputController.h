/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QList>
#include <QString>

#include <cstdint>
#include <optional>

enum class DisplayInputStatus
{
  Success,
  UnsupportedPlatform,
  MonitorNotFound,
  ReadUnsupported,
  ReadFailure,
  WriteFailure,
  VerificationMismatch
};

struct DisplayMonitor
{
  QString id;
  QString name;

  bool operator==(const DisplayMonitor &) const = default;
};

struct DisplayDiscoveryResult
{
  DisplayInputStatus status = DisplayInputStatus::Success;
  QString message;
  QList<DisplayMonitor> monitors;

  bool succeeded() const
  {
    return status == DisplayInputStatus::Success;
  }
};

struct DisplayInputResult
{
  DisplayInputStatus status = DisplayInputStatus::Success;
  QString message;
  std::optional<uint16_t> value;

  bool succeeded() const
  {
    return status == DisplayInputStatus::Success;
  }
};

class IDisplayInputController
{
public:
  virtual ~IDisplayInputController() = default;

  virtual DisplayDiscoveryResult discoverMonitors() = 0;
  virtual DisplayInputResult readInput(const QString &monitorId) = 0;
  virtual DisplayInputResult writeInput(const QString &monitorId, uint16_t inputValue) = 0;
};
