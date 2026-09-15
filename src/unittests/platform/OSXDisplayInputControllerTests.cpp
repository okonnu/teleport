/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXDisplayInputController.h"

#include <QtTest>

class OSXDisplayInputControllerTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void writePackets();
  void readPacket();
  void parseReply();
  void retryBehavior();
};

void OSXDisplayInputControllerTests::writePackets()
{
  QCOMPARE(OSXDisplayInputController::makeWritePacket(1).toHex(), QByteArray("8403600001d9"));
  QCOMPARE(OSXDisplayInputController::makeWritePacket(16).toHex(), QByteArray("8403600010c8"));
}

void OSXDisplayInputControllerTests::readPacket()
{
  QCOMPARE(OSXDisplayInputController::makeReadPacket().toHex(), QByteArray("8201608d"));
}

void OSXDisplayInputControllerTests::parseReply()
{
  QByteArray reply = QByteArray::fromHex("6e880200600000ff001000");
  uint8_t checksum = 0x50;
  for (int index = 0; index < reply.size() - 1; ++index)
    checksum ^= static_cast<uint8_t>(reply.at(index));
  reply[reply.size() - 1] = static_cast<char>(checksum);
  QCOMPARE(OSXDisplayInputController::parseReadReply(reply), std::optional<uint16_t>(16));

  reply[reply.size() - 1] ^= 1;
  QVERIFY(!OSXDisplayInputController::parseReadReply(reply));
}

void OSXDisplayInputControllerTests::retryBehavior()
{
  int attempts = 0;
  QVERIFY(OSXDisplayInputController::runWithRetries([&attempts] { return ++attempts == 3; }));
  QCOMPARE(attempts, 3);

  attempts = 0;
  QVERIFY(!OSXDisplayInputController::runWithRetries([&attempts] {
    ++attempts;
    return false;
  }));
  QCOMPARE(attempts, 5);
}

QTEST_MAIN(OSXDisplayInputControllerTests)
#include "OSXDisplayInputControllerTests.moc"
