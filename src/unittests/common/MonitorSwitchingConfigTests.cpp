/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/MonitorSwitchingConfig.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class MonitorSwitchingConfigTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void saveLoadAndVerify();
  void editingInvalidatesVerification();
  void validateRoutes();
  void reconcileComputerChanges();
  void migratesVersionOneDisabled();
  void rejectsInvalidJsonStructure();
};

namespace {
MonitorSwitchingConfig validConfig()
{
  MonitorSwitchingConfig config;
  config.monitorId = QStringLiteral("display-id");
  config.monitorName = QStringLiteral("Test monitor");
  config.routes = {
      {QStringLiteral("server"), QStringLiteral("Server input"), 7, {}, {}, {}},
      {QStringLiteral("client"), QStringLiteral("Client input"), 42, {}, {}, {}},
  };
  return config;
}
} // namespace

void MonitorSwitchingConfigTests::saveLoadAndVerify()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  qputenv("TELEPORT_MONITOR_CONFIG_PATH", directory.filePath(QStringLiteral("monitor-switching.json")).toUtf8());

  auto config = validConfig();
  config.activeConfigHash = config.configurationHash();
  config.enabled = true;
  QString error;
  QVERIFY2(config.save(&error), qPrintable(error));

  const auto loaded = MonitorSwitchingConfig::load(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QVERIFY(loaded.enabled);
  QVERIFY(loaded.isActiveConfigurationValid());
  QCOMPARE(loaded.monitorId, config.monitorId);
  QCOMPARE(loaded.routes, config.routes);
  qunsetenv("TELEPORT_MONITOR_CONFIG_PATH");
}

void MonitorSwitchingConfigTests::editingInvalidatesVerification()
{
  auto config = validConfig();
  config.activeConfigHash = config.configurationHash();
  config.enabled = true;
  QVERIFY(config.isActiveConfigurationValid());

  config.routes[1].inputValue = 43;
  QVERIFY(!config.isActiveConfigurationValid());
  config.invalidate();
  QVERIFY(!config.enabled);
  QVERIFY(config.activeConfigHash.isEmpty());
}

void MonitorSwitchingConfigTests::validateRoutes()
{
  auto config = validConfig();
  QVERIFY(config.validate(QStringLiteral("server"), {QStringLiteral("server"), QStringLiteral("client")}).isEmpty());

  config.routes.removeFirst();
  QVERIFY(!config.validate(QStringLiteral("server")).isEmpty());
  config = validConfig();
  config.routes[1].computerName = QStringLiteral("unknown");
  QVERIFY(!config.validate(QStringLiteral("server"), {QStringLiteral("server"), QStringLiteral("client")}).isEmpty());

  config = validConfig();
  config.routes[1].inputValue = -1;
  QVERIFY(config.validateSetupSelection(QStringLiteral("server"), {QStringLiteral("server"), QStringLiteral("client")})
              .isEmpty());
  QVERIFY(!config.validate(QStringLiteral("server"), {QStringLiteral("server"), QStringLiteral("client")}).isEmpty());

  config = validConfig();
  config.routes[1].inputValue = config.routes[0].inputValue;
  QVERIFY(!config.validate(QStringLiteral("server"), {QStringLiteral("server"), QStringLiteral("client")}).isEmpty());
}

void MonitorSwitchingConfigTests::reconcileComputerChanges()
{
  const auto config = validConfig();
  QVERIFY(config
              .validate(
                  QStringLiteral("server"),
                  {QStringLiteral("server"), QStringLiteral("client"), QStringLiteral("new-client")}
              )
              .isEmpty());
  QVERIFY(!config.validate(QStringLiteral("server"), {QStringLiteral("server")}).isEmpty());
  QVERIFY(!config
               .validate(QStringLiteral("renamed-server"), {QStringLiteral("renamed-server"), QStringLiteral("client")})
               .isEmpty());
}

void MonitorSwitchingConfigTests::migratesVersionOneDisabled()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto path = directory.filePath(QStringLiteral("monitor-switching.json"));
  qputenv("TELEPORT_MONITOR_CONFIG_PATH", path.toUtf8());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QVERIFY(
      file.write(
          R"({"schemaVersion":1,"enabled":true,"verifiedConfigHash":"old","monitor":{"id":"display","name":"Monitor"},"routes":[{"computerName":"server","inputLabel":"HDMI","inputValue":17},{"computerName":"client","inputLabel":"DisplayPort","inputValue":15}]})"
      ) > 0
  );
  file.close();

  QString error;
  const auto loaded = MonitorSwitchingConfig::load(&error);
  QVERIFY2(error.isEmpty(), qPrintable(error));
  QCOMPARE(loaded.schemaVersion, MonitorSwitchingConfig::SchemaVersion);
  QVERIFY(!loaded.enabled);
  QVERIFY(loaded.activeConfigHash.isEmpty());
  QCOMPARE(loaded.routes.size(), 2);
  qunsetenv("TELEPORT_MONITOR_CONFIG_PATH");
}

void MonitorSwitchingConfigTests::rejectsInvalidJsonStructure()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto path = directory.filePath(QStringLiteral("monitor-switching.json"));
  qputenv("TELEPORT_MONITOR_CONFIG_PATH", path.toUtf8());
  QFile file(path);
  QVERIFY(file.open(QIODevice::WriteOnly));
  QVERIFY(file.write("{\"schemaVersion\":1,\"enabled\":true,\"verifiedConfigHash\":7}") > 0);
  file.close();

  QString error;
  const auto loaded = MonitorSwitchingConfig::load(&error);
  QVERIFY(!error.isEmpty());
  QVERIFY(!loaded.enabled);
  qunsetenv("TELEPORT_MONITOR_CONFIG_PATH");
}

QTEST_MAIN(MonitorSwitchingConfigTests)
#include "MonitorSwitchingConfigTests.moc"
