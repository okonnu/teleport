/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/Settings.h"
#include "gui/config/ServerConfig.h"
#include "gui/dialogs/MonitorSwitchingDialog.h"
#include "platform/IDisplayInputController.h"

#include <QApplication>
#include <QComboBox>
#include <QMessageBox>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class FakeDisplayInputController : public IDisplayInputController
{
public:
  DisplayDiscoveryResult discoverMonitors() override
  {
    return {DisplayInputStatus::Success, {}, monitors};
  }

  DisplayInputSourcesResult discoverInputSources(const QString &) override
  {
    ++discoverInputSourcesCalls;
    return inputSourcesResult;
  }

  DisplayInputResult readInput(const QString &) override
  {
    ++readCalls;
    if (readResults.isEmpty())
      return {DisplayInputStatus::ReadFailure, QStringLiteral("No fake read result."), {}};
    return readResults.takeFirst();
  }

  DisplayInputResult writeInput(const QString &, uint16_t inputValue) override
  {
    writtenValues.append(inputValue);
    if (writeResults.isEmpty())
      return {DisplayInputStatus::WriteFailure, QStringLiteral("No fake write result."), {}};
    return writeResults.takeFirst();
  }

  QList<DisplayInputResult> readResults;
  QList<DisplayInputResult> writeResults;
  QList<uint16_t> writtenValues;
  QList<DisplayMonitor> monitors{{QStringLiteral("monitor"), QStringLiteral("Test monitor")}};
  int discoverInputSourcesCalls = 0;
  int readCalls = 0;
  DisplayInputSourcesResult inputSourcesResult{
      DisplayInputStatus::Success,
      {},
      {{1, QStringLiteral("HDMI 1")}, {16, QStringLiteral("DisplayPort 2")}},
  };
};

class MonitorSwitchingDialogTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void inputSourcesPopulateDropdowns();
  void matchedProfileEnablesWithoutDdcTests();
  void verifiedAssignments();
  void currentServerInputMustBeReadable();
  void readUnavailableUsesVisualConfirmation();
  void readbackMismatchFails();
  void writeFailureStillRestores();
  void restoreFailureStopsTesting();
  void cancellationStillRestores();
  void clientDisconnectionFails();

private:
  static DisplayInputResult success(uint16_t value);
  static DisplayInputResult failure(DisplayInputStatus status, const QString &message);
  static MonitorInputRoute serverRoute();
  static MonitorInputRoute clientRoute();
  static MonitorSwitchingConfig monitorConfig();
  static void acceptMessageBoxes(int count);
  ServerConfig serverConfig() const;

  QTemporaryDir m_directory;
};

void MonitorSwitchingDialogTests::initTestCase()
{
  QVERIFY(m_directory.isValid());
  qputenv("XDG_CONFIG_HOME", m_directory.path().toUtf8());
  qputenv("TELEPORT_MONITOR_CONFIG_PATH", m_directory.filePath(QStringLiteral("monitor.json")).toUtf8());
  Settings::setValue(Settings::Core::ComputerName, QStringLiteral("server"));
}

DisplayInputResult MonitorSwitchingDialogTests::success(uint16_t value)
{
  return {DisplayInputStatus::Success, {}, value};
}

DisplayInputResult MonitorSwitchingDialogTests::failure(DisplayInputStatus status, const QString &message)
{
  return {status, message, {}};
}

MonitorInputRoute MonitorSwitchingDialogTests::serverRoute()
{
  return {QStringLiteral("server"), QStringLiteral("Server input"), 1, {}, {}, {}};
}

MonitorInputRoute MonitorSwitchingDialogTests::clientRoute()
{
  return {QStringLiteral("client"), QStringLiteral("Client input"), 16, {}, {}, {}};
}

MonitorSwitchingConfig MonitorSwitchingDialogTests::monitorConfig()
{
  MonitorSwitchingConfig config;
  config.monitorId = QStringLiteral("monitor");
  config.monitorName = QStringLiteral("Test monitor");
  return config;
}

void MonitorSwitchingDialogTests::acceptMessageBoxes(int count)
{
  auto *timer = new QTimer(qApp);
  timer->setInterval(1);
  QObject::connect(timer, &QTimer::timeout, timer, [timer, count, accepted = 0]() mutable {
    const auto widgets = QApplication::topLevelWidgets();
    for (auto *widget : widgets) {
      auto *messageBox = qobject_cast<QMessageBox *>(widget);
      if (!messageBox || !messageBox->isVisible())
        continue;
      const auto result = messageBox->standardButtons().testFlag(QMessageBox::Yes) ? QMessageBox::Yes : QMessageBox::Ok;
      timer->stop();
      messageBox->done(result);
      if (++accepted == count) {
        timer->deleteLater();
      } else {
        QTimer::singleShot(10, timer, [timer] { timer->start(); });
      }
      return;
    }
  });
  timer->start();
}

ServerConfig MonitorSwitchingDialogTests::serverConfig() const
{
  ServerConfig config;
  if (!config.screenExists(QStringLiteral("client")))
    config.addClient(QStringLiteral("client"));
  return config;
}

void MonitorSwitchingDialogTests::inputSourcesPopulateDropdowns()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(nullptr, config, {QStringLiteral("client")}, std::move(controller));

  dialog.m_monitorCombo->setCurrentIndex(1);
  bool foundDisplayPort = false;
  for (auto *combo : dialog.findChildren<QComboBox *>()) {
    const int index = combo->findData(16);
    if (index >= 0 && combo->itemText(index) == QStringLiteral("DisplayPort 2 [DDC 16]"))
      foundDisplayPort = true;
  }
  QVERIFY(foundDisplayPort);
}

void MonitorSwitchingDialogTests::matchedProfileEnablesWithoutDdcTests()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  controller->monitors = {{QStringLiteral("monitor"), QStringLiteral("LC49G95T"), QStringLiteral("SAM"), 0x7052,
                           QStringLiteral("LC49G95T")}};
  auto *controllerPointer = controller.get();
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(nullptr, config, {QStringLiteral("client")}, std::move(controller));

  QString error;
  dialog.m_profileDatabase = MonitorProfileDatabase::fromJson(
      R"({"schemaVersion":1,"databaseVersion":"test","profiles":[{"id":"SAM-7052","manufacturerId":"SAM","productId":28754,"modelNames":["LC49G95T"],"revision":1,"source":"test","inputs":[{"id":"hdmi","label":"HDMI","writeValue":17,"readValues":[1]},{"id":"displayport-2","label":"DisplayPort 2","writeValue":16,"readValues":[4]}]}]})",
      &error
  );
  QVERIFY2(dialog.m_profileDatabase.isValid(), qPrintable(error));
  dialog.m_monitorCombo->setCurrentIndex(1);
  const auto discoveryCallsBeforeLookup = controllerPointer->discoverInputSourcesCalls;
  dialog.refreshInputSources();
  QVERIFY(dialog.m_matchedProfile);
  QCOMPARE(controllerPointer->discoverInputSourcesCalls, discoveryCallsBeforeLookup);

  for (int row = 0; row < dialog.m_routeTable->rowCount(); ++row) {
    dialog.m_routeTable->item(row, 0)->setCheckState(Qt::Checked);
    auto *combo = qobject_cast<QComboBox *>(dialog.m_routeTable->cellWidget(row, 3));
    const auto computer = dialog.m_routeTable->item(row, 1)->text();
    combo->setCurrentIndex(combo->findData(computer == QStringLiteral("server") ? 17 : 16));
  }
  const auto discoveryCallsBeforeEnable = controllerPointer->discoverInputSourcesCalls;
  acceptMessageBoxes(1);
  dialog.runTestsAndEnable();

  QVERIFY(dialog.m_config.enabled);
  QCOMPARE(dialog.m_config.profileId, QStringLiteral("SAM-7052"));
  QCOMPARE(controllerPointer->discoverInputSourcesCalls, discoveryCallsBeforeEnable);
  QCOMPARE(controllerPointer->readCalls, 0);
  QVERIFY(controllerPointer->writtenValues.isEmpty());
}

void MonitorSwitchingDialogTests::verifiedAssignments()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  controller->writeResults = {success(16), success(1)};
  controller->readResults = {success(1), success(16), success(1)};
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(
      nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) { return true; },
      [] { return true; }, [](const MonitorInputRoute &) { return true; }
  );
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(dialog.testAssignments(monitor));
  QCOMPARE(monitor.routeForComputer(QStringLiteral("client"))->lastTestStatus, QStringLiteral("verified"));
  QCOMPARE(controllerPointer->writtenValues, QList<uint16_t>({16, 1}));
}

void MonitorSwitchingDialogTests::currentServerInputMustBeReadable()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  controller->readResults = {failure(DisplayInputStatus::ReadFailure, QStringLiteral("unavailable"))};
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(nullptr, config, {QStringLiteral("client")}, std::move(controller));
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(!dialog.testAssignments(monitor));
  QVERIFY(controllerPointer->writtenValues.isEmpty());
  QVERIFY(dialog.m_testError.contains(QStringLiteral("safely")));
}

void MonitorSwitchingDialogTests::readUnavailableUsesVisualConfirmation()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  controller->writeResults = {success(16), success(1)};
  controller->readResults = {
      success(1), failure(DisplayInputStatus::ReadFailure, QStringLiteral("unavailable")), success(1)
  };
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(
      nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) { return true; },
      [] { return true; }, [](const MonitorInputRoute &) { return true; }
  );
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(dialog.testAssignments(monitor));
  QCOMPARE(monitor.routeForComputer(QStringLiteral("client"))->lastTestStatus, QStringLiteral("verified visually"));
}

void MonitorSwitchingDialogTests::readbackMismatchFails()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  controller->writeResults = {success(16), success(1)};
  controller->readResults = {success(1), success(17), success(1)};
  auto config = serverConfig();
  bool confirmationRequested = false;
  MonitorSwitchingDialog dialog(
      nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) { return true; }, {},
      [&confirmationRequested](const MonitorInputRoute &) {
        confirmationRequested = true;
        return true;
      }
  );
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(!dialog.testAssignments(monitor));
  QVERIFY(!confirmationRequested);
  QCOMPARE(monitor.routeForComputer(QStringLiteral("client"))->lastTestStatus, QStringLiteral("failed"));
}

void MonitorSwitchingDialogTests::writeFailureStillRestores()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  controller->writeResults = {
      failure(DisplayInputStatus::WriteFailure, QStringLiteral("write failed")),
      success(1),
  };
  controller->readResults = {success(1), success(1)};
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) {
    return true;
  });
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(!dialog.testAssignments(monitor));
  QVERIFY(controllerPointer->writeResults.isEmpty());
  QCOMPARE(controllerPointer->writtenValues, QList<uint16_t>({16, 1}));
}

void MonitorSwitchingDialogTests::restoreFailureStopsTesting()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  controller->writeResults = {
      success(16),
      failure(DisplayInputStatus::WriteFailure, QStringLiteral("restore failed")),
  };
  controller->readResults = {success(1), success(16)};
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) {
    return true;
  });
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};
  acceptMessageBoxes(1);

  QVERIFY(!dialog.testAssignments(monitor));
  QVERIFY(monitor.routeForComputer(QStringLiteral("client"))
              ->lastTestMessage.contains(QStringLiteral("restore"), Qt::CaseInsensitive));
}

void MonitorSwitchingDialogTests::cancellationStillRestores()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  controller->writeResults = {success(16), success(1)};
  controller->readResults = {success(1), success(1)};
  auto config = serverConfig();
  int waits = 0;
  MonitorSwitchingDialog dialog(
      nullptr, config, {QStringLiteral("client")}, std::move(controller), [&waits](auto, auto) { return ++waits != 1; }
  );
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(!dialog.testAssignments(monitor));
  QVERIFY(controllerPointer->writeResults.isEmpty());
  QCOMPARE(
      monitor.routeForComputer(QStringLiteral("client"))->lastTestMessage,
      QStringLiteral("The assignment test was canceled.")
  );
}

void MonitorSwitchingDialogTests::clientDisconnectionFails()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  controller->readResults = {success(1)};
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(nullptr, config, {}, std::move(controller), [](auto, auto) { return true; });
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(!dialog.testAssignments(monitor));
  QVERIFY(monitor.routeForComputer(QStringLiteral("client"))->lastTestMessage.contains(QStringLiteral("disconnected")));
  QVERIFY(controllerPointer->writtenValues.isEmpty());
}

QTEST_MAIN(MonitorSwitchingDialogTests)
#include "MonitorSwitchingDialogTests.moc"
