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
#include <QMessageBox>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

class FakeDisplayInputController : public IDisplayInputController
{
public:
  DisplayDiscoveryResult discoverMonitors() override
  {
    return {DisplayInputStatus::Success, {}, {{QStringLiteral("monitor"), QStringLiteral("Test monitor")}}};
  }

  DisplayInputResult readInput(const QString &) override
  {
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
};

class MonitorSwitchingDialogTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void automaticInputDiscovery();
  void currentServerInputMustBeReadable();
  void readUnavailableUsesVisualIdentification();
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

void MonitorSwitchingDialogTests::automaticInputDiscovery()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  auto *controllerPointer = controller.get();
  controller->writeResults = {success(16), success(1)};
  controller->readResults = {success(1), success(16), success(1)};
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(
      nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) { return true; },
      [] { return true; }, [](const QStringList &) { return std::optional<QString>{QStringLiteral("client")}; }
  );
  auto monitor = monitorConfig();
  auto client = clientRoute();
  client.inputValue = -1;
  monitor.routes = {serverRoute(), client};

  QVERIFY(dialog.discoverInputRoutes(monitor, {16}));
  QCOMPARE(monitor.routeForComputer(QStringLiteral("server"))->inputValue, 1);
  QCOMPARE(monitor.routeForComputer(QStringLiteral("client"))->inputValue, 16);
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

  QVERIFY(!dialog.discoverInputRoutes(monitor, {16}));
  QVERIFY(controllerPointer->writtenValues.isEmpty());
  QVERIFY(dialog.m_inputDetectionError.contains(QStringLiteral("safely restore")));
}

void MonitorSwitchingDialogTests::readUnavailableUsesVisualIdentification()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  controller->writeResults = {success(16), success(1)};
  controller->readResults = {
      success(1), failure(DisplayInputStatus::ReadFailure, QStringLiteral("unavailable")), success(1)
  };
  auto config = serverConfig();
  MonitorSwitchingDialog dialog(
      nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) { return true; },
      [] { return true; }, [](const QStringList &) { return std::optional<QString>{QStringLiteral("client")}; }
  );
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(dialog.discoverInputRoutes(monitor, {16}));
  QCOMPARE(monitor.routeForComputer(QStringLiteral("client"))->inputValue, 16);
  QCOMPARE(monitor.routeForComputer(QStringLiteral("client"))->lastTestStatus, QStringLiteral("verified visually"));
}

void MonitorSwitchingDialogTests::readbackMismatchFails()
{
  auto controller = std::make_unique<FakeDisplayInputController>();
  controller->writeResults = {success(16), success(1)};
  controller->readResults = {success(1), success(17), success(1)};
  auto config = serverConfig();
  bool identificationRequested = false;
  MonitorSwitchingDialog dialog(
      nullptr, config, {QStringLiteral("client")}, std::move(controller), [](auto, auto) { return true; }, {},
      [&identificationRequested](const QStringList &) {
        identificationRequested = true;
        return std::optional<QString>{QStringLiteral("client")};
      }
  );
  auto monitor = monitorConfig();
  monitor.routes = {serverRoute(), clientRoute()};

  QVERIFY(!dialog.discoverInputRoutes(monitor, {16}));
  QVERIFY(!identificationRequested);
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

  QVERIFY(!dialog.discoverInputRoutes(monitor, {16}));
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

  QVERIFY(!dialog.discoverInputRoutes(monitor, {16}));
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

  QVERIFY(!dialog.discoverInputRoutes(monitor, {16}));
  QVERIFY(controllerPointer->writeResults.isEmpty());
  QCOMPARE(
      monitor.routeForComputer(QStringLiteral("client"))->lastTestMessage,
      QStringLiteral("Input detection was canceled.")
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

  QVERIFY(!dialog.discoverInputRoutes(monitor, {16}));
  QVERIFY(monitor.routeForComputer(QStringLiteral("client"))->lastTestMessage.contains(QStringLiteral("disconnected")));
  QVERIFY(controllerPointer->writtenValues.isEmpty());
}

QTEST_MAIN(MonitorSwitchingDialogTests)
#include "MonitorSwitchingDialogTests.moc"
