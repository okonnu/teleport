/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "common/MonitorProfileDatabase.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class MonitorProfileDatabaseTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void bundledDatabase();
  void rejectsInvalidDatabase();
  void installsUploadedDatabase();
};

void MonitorProfileDatabaseTests::bundledDatabase()
{
  const auto path = QFINDTESTDATA("../../apps/res/monitor-profiles.json");
  QVERIFY2(!path.isEmpty(), "Could not find the bundled monitor profile database.");
  QString error;
  const auto database = MonitorProfileDatabase::loadFile(path, &error);
  QVERIFY2(database.isValid(), qPrintable(error));
  QVERIFY(database.profiles().size() >= 113);

  const auto profile = database.find({QStringLiteral("SAM"), 0x7052, QStringLiteral("LC49G95T")});
  QVERIFY(profile);
  QCOMPARE(profile->id, QStringLiteral("SAM-7052"));
  QCOMPARE(profile->inputs.size(), 3);
  QCOMPARE(profile->inputs.at(0).writeValue, 17);
  QCOMPARE(profile->inputs.at(0).readValues, QList<int>({1}));
}

void MonitorProfileDatabaseTests::rejectsInvalidDatabase()
{
  QString error;
  const auto database = MonitorProfileDatabase::fromJson(
      R"({"schemaVersion":2,"databaseVersion":"bad","profiles":[]})", &error
  );
  QVERIFY(!database.isValid());
  QVERIFY(!error.isEmpty());
}

void MonitorProfileDatabaseTests::installsUploadedDatabase()
{
  QTemporaryDir directory;
  QVERIFY(directory.isValid());
  const auto sourcePath = directory.filePath(QStringLiteral("upload.json"));
  const auto destinationPath = directory.filePath(QStringLiteral("installed.json"));
  QFile source(sourcePath);
  QVERIFY(source.open(QIODevice::WriteOnly));
  QVERIFY(
      source.write(
          R"({"schemaVersion":1,"databaseVersion":"user-1","profiles":[{"id":"TST-0001","manufacturerId":"TST","productId":1,"modelNames":["Test"],"revision":1,"source":"test","inputs":[{"id":"hdmi","label":"HDMI","writeValue":17,"readValues":[1]}]}]})"
      ) > 0
  );
  source.close();

  qputenv("TELEPORT_MONITOR_PROFILE_DATABASE_PATH", destinationPath.toUtf8());
  QString error;
  QVERIFY2(MonitorProfileDatabase::installUserDatabase(sourcePath, &error), qPrintable(error));
  const auto installed = MonitorProfileDatabase::loadFile(destinationPath, &error);
  QVERIFY2(installed.isValid(), qPrintable(error));
  QCOMPARE(installed.databaseVersion(), QStringLiteral("user-1"));
  qunsetenv("TELEPORT_MONITOR_PROFILE_DATABASE_PATH");
}

QTEST_MAIN(MonitorProfileDatabaseTests)
#include "MonitorProfileDatabaseTests.moc"
