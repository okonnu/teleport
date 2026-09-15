/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <memory>

class IDisplayInputController;
class QString;

class DisplayInputCoordinator
{
public:
  DisplayInputCoordinator();
  explicit DisplayInputCoordinator(std::unique_ptr<IDisplayInputController> controller);
  ~DisplayInputCoordinator();

  void handleScreenSwitched(const QString &computerName);

private:
  std::unique_ptr<IDisplayInputController> m_controller;
};
