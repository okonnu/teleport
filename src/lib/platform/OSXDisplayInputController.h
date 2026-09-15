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

class OSXDisplayInputController : public IDisplayInputController
{
public:
  DisplayDiscoveryResult discoverMonitors() override;
  DisplayInputResult readInput(const QString &monitorId) override;
  DisplayInputResult writeInput(const QString &monitorId, uint16_t inputValue) override;

  static QByteArray makeReadPacket();
  static QByteArray makeWritePacket(uint16_t inputValue);
  static std::optional<uint16_t> parseReadReply(const QByteArray &reply);
  static bool runWithRetries(const std::function<bool()> &operation);
};
