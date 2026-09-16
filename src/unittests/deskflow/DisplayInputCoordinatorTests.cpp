/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/Log.h"
#include "common/MonitorSwitchingConfig.h"
#include "deskflow/DisplayInputCoordinator.h"
#include "platform/IDisplayInputController.h"

#include <QTemporaryDir>
#include <QtTest>

class FakeDisplayInputController : public IDisplayInputController
{
public:
  DisplayInputSourcesResult discoverInputSources(const QString &) override
  {
    return {DisplayInputStatus::Success, {}, {}};
  }

  DisplayDiscoveryResult discoverMonitors() override
  {
    return {DisplayInputStatus::Success, {}, {}};
  }

  DisplayInputResult readInput(const QString &) override
  {
    return {DisplayInputStatus::ReadUnsupported, {}, {}};
  }

  DisplayInputResult writeInput(const QString &monitorId, uint16_t inputValue) override
  {
    ++writeCount;
    lastMonitorId = monitorId;
    lastInputValue = inputValue;
    return {DisplayInputStatus::Success, {}, inputValue};
  }

  int writeCount = 0;
  QString lastMonitorId;
  uint16_t lastInputValue = 0;
};

class DisplayInputCoordinatorTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void init();
  void cleanup();
  void verifiedRouteWritesOnce();
  void disabledConfigurationDoesNotWrite();
  void changedConfigurationDoesNotWrite();
  void missingRouteDoesNotWrite();

private:
  MonitorSwitchingConfig validConfig() const;

  QTemporaryDir m_directory;
  Log m_log;
};

void DisplayInputCoordinatorTests::init()
{
  QVERIFY(m_directory.isValid());
  qputenv("TELEPORT_MONITOR_CONFIG_PATH", m_directory.filePath(QStringLiteral("monitor-switching.json")).toUtf8());
}

void DisplayInputCoordinatorTests::cleanup()
{
  qunsetenv("TELEPORT_MONITOR_CONFIG_PATH");
}

MonitorSwitchingConfig DisplayInputCoordinatorTests::validConfig() const
{
  MonitorSwitchingConfig config;
  config.monitorId = QStringLiteral("monitor-id");
  config.monitorName = QStringLiteral("Test monitor");
  config.routes = {
      {QStringLiteral("server"), QStringLiteral("Server input"), 1, {}, {}, {}},
      {QStringLiteral("client"), QStringLiteral("Client input"), 16, {}, {}, {}},
  };
  config.activeConfigHash = config.configurationHash();
  config.enabled = true;
  return config;
}

void DisplayInputCoordinatorTests::verifiedRouteWritesOnce()
{
  auto config = validConfig();
  QVERIFY(config.save());
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  DisplayInputCoordinator coordinator(std::move(controller));

  coordinator.handleScreenSwitched(QStringLiteral("client"));

  QCOMPARE(controllerPointer->writeCount, 1);
  QCOMPARE(controllerPointer->lastMonitorId, QStringLiteral("monitor-id"));
  QCOMPARE(controllerPointer->lastInputValue, uint16_t(16));
}

void DisplayInputCoordinatorTests::disabledConfigurationDoesNotWrite()
{
  auto config = validConfig();
  config.enabled = false;
  QVERIFY(config.save());
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  DisplayInputCoordinator coordinator(std::move(controller));

  coordinator.handleScreenSwitched(QStringLiteral("client"));

  QCOMPARE(controllerPointer->writeCount, 0);
}

void DisplayInputCoordinatorTests::changedConfigurationDoesNotWrite()
{
  auto config = validConfig();
  config.routes[1].inputValue = 17;
  QVERIFY(config.save());
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  DisplayInputCoordinator coordinator(std::move(controller));

  coordinator.handleScreenSwitched(QStringLiteral("client"));

  QCOMPARE(controllerPointer->writeCount, 0);
}

void DisplayInputCoordinatorTests::missingRouteDoesNotWrite()
{
  auto config = validConfig();
  QVERIFY(config.save());
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  DisplayInputCoordinator coordinator(std::move(controller));

  coordinator.handleScreenSwitched(QStringLiteral("other"));

  QCOMPARE(controllerPointer->writeCount, 0);
}

QTEST_MAIN(DisplayInputCoordinatorTests)
#include "DisplayInputCoordinatorTests.moc"
