/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/MonitorSwitchingConfig.h"
#include "platform/IDisplayInputController.h"

#include <QDialog>

#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QPushButton;
class QTableWidget;
class ServerConfig;
class MonitorSwitchingDialogTests;

class MonitorSwitchingDialog : public QDialog
{
  Q_OBJECT

public:
  using WaitFunction = std::function<bool(const QString &, int)>;
  using ConfirmServerFunction = std::function<bool()>;
  using IdentifyInputFunction = std::function<std::optional<QString>(const QStringList &)>;

  MonitorSwitchingDialog(
      QWidget *parent, const ServerConfig &serverConfig, const QStringList &connectedClients,
      std::unique_ptr<IDisplayInputController> controller = {}, WaitFunction waitFunction = {},
      ConfirmServerFunction confirmServer = {}, IdentifyInputFunction identifyInput = {}
  );
  ~MonitorSwitchingDialog() override;
  void setConnectedClients(const QStringList &connectedClients);

Q_SIGNALS:
  void configurationEnabled();

private:
  friend class MonitorSwitchingDialogTests;

  void refreshMonitors();
  void populateComputers();
  void configurationEdited();
  void runTestsAndEnable();
  void disableMonitorSwitching();
  void updateStatus();
  void updateRouteStatus(const MonitorInputRoute &route);
  void saveDisabledConfiguration();
  bool waitWithProgress(const QString &message, int milliseconds);
  bool
  restoreServerInput(const QString &monitorId, const MonitorInputRoute &serverRoute, MonitorInputRoute &testedRoute);
  bool discoverInputRoutes(MonitorSwitchingConfig &config, const QList<uint16_t> &candidateValues);
  std::optional<QString> identifyInputComputer(const QStringList &computerNames);
  static QList<uint16_t> standardInputValues();
  MonitorSwitchingConfig configFromUi() const;
  QStringList configuredComputerNames() const;

  const ServerConfig &m_serverConfig;
  QStringList m_connectedClients;
  QString m_discoveryError;
  QString m_inputDetectionError;
  std::unique_ptr<IDisplayInputController> m_controller;
  MonitorSwitchingConfig m_config;
  QComboBox *m_monitorCombo = nullptr;
  QTableWidget *m_routeTable = nullptr;
  QLabel *m_statusLabel = nullptr;
  QPushButton *m_testButton = nullptr;
  QPushButton *m_disableButton = nullptr;
  bool m_loading = false;
  WaitFunction m_waitFunction;
  ConfirmServerFunction m_confirmServer;
  IdentifyInputFunction m_identifyInput;
};
