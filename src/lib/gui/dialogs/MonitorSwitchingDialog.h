/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "common/MonitorProfileDatabase.h"
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
  using ConfirmInputFunction = std::function<bool(const MonitorInputRoute &)>;

  MonitorSwitchingDialog(
      QWidget *parent, const ServerConfig &serverConfig, const QStringList &connectedClients,
      std::unique_ptr<IDisplayInputController> controller = {}, WaitFunction waitFunction = {},
      ConfirmServerFunction confirmServer = {}, ConfirmInputFunction confirmInput = {}
  );
  ~MonitorSwitchingDialog() override;
  void setConnectedClients(const QStringList &connectedClients);

Q_SIGNALS:
  void configurationEnabled();

private:
  friend class MonitorSwitchingDialogTests;

  void refreshMonitors();
  void refreshInputSources();
  void importProfileDatabase();
  void monitorSelectionChanged();
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
  bool testRoute(MonitorSwitchingConfig &config, MonitorInputRoute &route, const MonitorInputRoute &serverRoute);
  bool testAssignments(MonitorSwitchingConfig &config);
  MonitorSwitchingConfig configFromUi() const;
  QStringList configuredComputerNames() const;

  const ServerConfig &m_serverConfig;
  QStringList m_connectedClients;
  QString m_discoveryError;
  QString m_inputSourceMessage;
  QString m_testError;
  QString m_profileDatabaseError;
  std::unique_ptr<IDisplayInputController> m_controller;
  QList<DisplayInputSource> m_inputSources;
  MonitorProfileDatabase m_profileDatabase;
  std::optional<MonitorProfile> m_matchedProfile;
  MonitorSwitchingConfig m_config;
  QComboBox *m_monitorCombo = nullptr;
  QTableWidget *m_routeTable = nullptr;
  QLabel *m_statusLabel = nullptr;
  QPushButton *m_testButton = nullptr;
  QPushButton *m_disableButton = nullptr;
  bool m_loading = false;
  WaitFunction m_waitFunction;
  ConfirmServerFunction m_confirmServer;
  ConfirmInputFunction m_confirmInput;
};
