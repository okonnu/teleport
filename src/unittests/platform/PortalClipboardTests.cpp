/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "PortalClipboardTests.h"

#include "platform/PortalClipboard.h"

#include <QBuffer>
#include <QColor>
#include <QImage>

using deskflow::PortalClipboard;

void PortalClipboardTests::formatMimeTypesHandlesEmptyLists()
{
  const char *empty[] = {nullptr};

  QCOMPARE(PortalClipboard::formatMimeTypes(nullptr), QByteArrayLiteral("(none)"));
  QCOMPARE(PortalClipboard::formatMimeTypes(empty), QByteArrayLiteral("(none)"));
}

void PortalClipboardTests::pickSupportedMimeUsesPreferenceOrder()
{
  const char *available[] = {"text/plain", "image/png", "text/plain;charset=utf-8", nullptr};

  const auto *mime = PortalClipboard::pickSupportedMime(available);

  QVERIFY(mime != nullptr);
  QCOMPARE(QByteArray(mime->mime), QByteArrayLiteral("image/png"));
  QCOMPARE(mime->format, IClipboard::Format::Bitmap);
}

void PortalClipboardTests::textEncodingPassesBytesThrough()
{
  const QByteArray text = QByteArrayLiteral("first line\nsecond line\n");

  const auto encoded = PortalClipboard::encodeFormat(IClipboard::Format::Text, text);
  const auto decoded = PortalClipboard::decodeFormat(IClipboard::Format::Text, encoded);

  QCOMPARE(encoded, text);
  QCOMPARE(decoded, text);
}

void PortalClipboardTests::bitmapEncodingRoundTripsPixels()
{
  QImage source(2, 1, QImage::Format_ARGB32);
  source.setPixelColor(0, 0, QColor(12, 34, 56, 255));
  source.setPixelColor(1, 0, QColor(78, 90, 123, 255));

  QByteArray bmp;
  QBuffer buffer(&bmp);
  QVERIFY(buffer.open(QIODevice::WriteOnly));
  QVERIFY(source.save(&buffer, "BMP"));
  QVERIFY(bmp.startsWith("BM"));

  constexpr qsizetype bmpFileHeaderSize = 14;
  const auto png = PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, bmp.mid(bmpFileHeaderSize));
  QVERIFY(!png.isEmpty());

  const auto roundTripDib = PortalClipboard::decodeFormat(IClipboard::Format::Bitmap, png);
  QVERIFY(!roundTripDib.isEmpty());
  const auto roundTripPng = PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, roundTripDib);

  QImage result;
  QVERIFY(result.loadFromData(roundTripPng, "PNG"));
  QCOMPARE(result.size(), source.size());
  QCOMPARE(result.pixelColor(0, 0), source.pixelColor(0, 0));
  QCOMPARE(result.pixelColor(1, 0), source.pixelColor(1, 0));
}

void PortalClipboardTests::bitmapEncodingRejectsMalformedData()
{
  const QByteArray malformed(8, '\0');

  QVERIFY(PortalClipboard::encodeFormat(IClipboard::Format::Bitmap, malformed).isEmpty());
}

QTEST_MAIN(PortalClipboardTests)
