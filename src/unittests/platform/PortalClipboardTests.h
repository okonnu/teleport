/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/Log.h"

#include <QTest>

class PortalClipboardTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void formatMimeTypesHandlesEmptyLists();
  void pickSupportedMimeUsesPreferenceOrder();
  void textEncodingPassesBytesThrough();
  void bitmapEncodingRoundTripsPixels();
  void bitmapEncodingRejectsMalformedData();

private:
  Log m_log;
};
