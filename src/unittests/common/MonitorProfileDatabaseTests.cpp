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
  void modelNameMatchesAllProfiles();
  void mergePreservesModelVariants();
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

  const auto profiles = database.findAllByModelName(QStringLiteral("LC49G95T"));
  QCOMPARE(profiles.size(), 1);
  QCOMPARE(profiles.first().id, QStringLiteral("SAM-7052"));
  QCOMPARE(profiles.first().inputs.size(), 3);
  QCOMPARE(profiles.first().inputs.at(0).writeValue, 17);
  QCOMPARE(profiles.first().inputs.at(0).readValues, QList<int>({1}));
}

void MonitorProfileDatabaseTests::modelNameMatchesAllProfiles()
{
  const auto path = QFINDTESTDATA("../../apps/res/monitor-profiles.json");
  QString error;
  const auto database = MonitorProfileDatabase::loadFile(path, &error);
  QVERIFY2(database.isValid(), qPrintable(error));

  const auto samsung = database.findAllByModelName(QStringLiteral(" lc49-g95t "));
  QCOMPARE(samsung.size(), 1);
  QCOMPARE(samsung.first().id, QStringLiteral("SAM-7052"));

  const auto s2721 = database.findAllByModelName(QStringLiteral("dell s2721dgfa"));
  QCOMPARE(s2721.size(), 2);
  QCOMPARE(s2721.at(0).id, QStringLiteral("DEL41D9"));
  QCOMPARE(s2721.at(1).id, QStringLiteral("DEL41DA"));

  QCOMPARE(database.findAllByModelName(QStringLiteral("Dell U3219Q")).size(), 4);
  QCOMPARE(database.findAllByModelName(QStringLiteral("Dell Ultrasharp u3011")).size(), 2);
  QVERIFY(database.findAllByModelName(QStringLiteral("Unknown monitor")).isEmpty());
}

void MonitorProfileDatabaseTests::mergePreservesModelVariants()
{
  QString error;
  auto database = MonitorProfileDatabase::fromJson(
      R"({"schemaVersion":1,"databaseVersion":"base","profiles":[{"id":"TST-0001","manufacturerId":"TST","productId":1,"modelNames":["Same Model"],"revision":1,"source":"test","inputs":[{"id":"hdmi","label":"HDMI","writeValue":17,"readValues":[17]}]}]})",
      &error
  );
  QVERIFY2(database.isValid(), qPrintable(error));
  const auto overrides = MonitorProfileDatabase::fromJson(
      R"({"schemaVersion":1,"databaseVersion":"override","profiles":[{"id":"TST-0002","manufacturerId":"TST","productId":2,"modelNames":["Same Model"],"revision":1,"source":"test","inputs":[{"id":"dp","label":"DisplayPort","writeValue":15,"readValues":[15]}]}]})",
      &error
  );
  QVERIFY2(overrides.isValid(), qPrintable(error));

  database.merge(overrides);
  const auto matches = database.findAllByModelName(QStringLiteral("same-model"));
  QCOMPARE(matches.size(), 2);
  QCOMPARE(matches.at(0).id, QStringLiteral("TST-0001"));
  QCOMPARE(matches.at(1).id, QStringLiteral("TST-0002"));
}

void MonitorProfileDatabaseTests::rejectsInvalidDatabase()
{
  QString error;
  const auto database =
      MonitorProfileDatabase::fromJson(R"({"schemaVersion":2,"databaseVersion":"bad","profiles":[]})", &error);
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
