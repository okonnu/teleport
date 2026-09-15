/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/DisplayInputCoordinator.h"

#include "base/Log.h"
#include "common/MonitorSwitchingConfig.h"
#include "platform/DisplayInputControllerFactory.h"
#include "platform/IDisplayInputController.h"

DisplayInputCoordinator::DisplayInputCoordinator() : DisplayInputCoordinator(createDisplayInputController())
{
}

DisplayInputCoordinator::DisplayInputCoordinator(std::unique_ptr<IDisplayInputController> controller)
    : m_controller(std::move(controller))
{
}

DisplayInputCoordinator::~DisplayInputCoordinator() = default;

void DisplayInputCoordinator::handleScreenSwitched(const QString &computerName)
{
  QString error;
  const auto config = MonitorSwitchingConfig::load(&error);
  if (!error.isEmpty()) {
    LOG_ERR("monitor switching configuration could not be loaded: %s", error.toUtf8().constData());
    return;
  }
  if (!config.enabled || !config.isVerified())
    return;
  if (config.monitorId.trimmed().isEmpty()) {
    LOG_ERR("monitor switching configuration has no monitor identifier");
    return;
  }

  const auto route = config.routeForComputer(computerName);
  if (!route) {
    LOG_INFO("monitor switching has no route for computer: %s", computerName.toUtf8().constData());
    return;
  }
  if (route->inputLabel.trimmed().isEmpty() || route->inputValue < 0 || route->inputValue > 65535) {
    LOG_ERR("monitor switching route is invalid for computer: %s", computerName.toUtf8().constData());
    return;
  }

  const auto result = m_controller->writeInput(config.monitorId, static_cast<uint16_t>(route->inputValue));
  if (result.succeeded()) {
    LOG_INFO(
        "monitor input switched for computer %s to %s (%d)", computerName.toUtf8().constData(),
        route->inputLabel.toUtf8().constData(), route->inputValue
    );
  } else {
    LOG_ERR(
        "monitor input switch failed for computer %s: %s", computerName.toUtf8().constData(),
        result.message.toUtf8().constData()
    );
  }
}
