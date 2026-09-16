/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2021 Istvan T.
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "platform/IDisplayInputController.h"

#include <QByteArray>

#include <functional>

struct DdcCapabilitiesFragment
{
  int offset = 0;
  QByteArray data;

  bool operator==(const DdcCapabilitiesFragment &) const = default;
};

class OSXDisplayInputController : public IDisplayInputController
{
public:
  DisplayDiscoveryResult discoverMonitors() override;
  DisplayInputSourcesResult discoverInputSources(const QString &monitorId) override;
  DisplayInputResult readInput(const QString &monitorId) override;
  DisplayInputResult writeInput(const QString &monitorId, uint16_t inputValue) override;

  static QByteArray makeReadPacket();
  static QByteArray makeWritePacket(uint16_t inputValue);
  static QByteArray makeCapabilitiesPacket(uint16_t offset);
  static std::optional<uint16_t> parseReadReply(const QByteArray &reply);
  static std::optional<DdcCapabilitiesFragment> parseCapabilitiesReply(const QByteArray &reply);
  static QList<uint16_t> parseInputValues(const QByteArray &capabilities);
  static QString inputSourceName(uint16_t inputValue);
  static bool runWithRetries(const std::function<bool()> &operation);
};
