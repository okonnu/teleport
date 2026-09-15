/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/DisplayInputControllerFactory.h"

#include "platform/IDisplayInputController.h"

#if defined(Q_OS_MACOS) && defined(__arm64__)
#include "platform/OSXDisplayInputController.h"
#endif

namespace {
class UnsupportedDisplayInputController : public IDisplayInputController
{
public:
  DisplayDiscoveryResult discoverMonitors() override
  {
    return {DisplayInputStatus::UnsupportedPlatform, QStringLiteral("DDC is not supported on this platform yet."), {}};
  }

  DisplayInputResult readInput(const QString &) override
  {
    return {DisplayInputStatus::UnsupportedPlatform, QStringLiteral("DDC is not supported on this platform yet."), {}};
  }

  DisplayInputResult writeInput(const QString &, uint16_t) override
  {
    return {DisplayInputStatus::UnsupportedPlatform, QStringLiteral("DDC is not supported on this platform yet."), {}};
  }
};
} // namespace

std::unique_ptr<IDisplayInputController> createDisplayInputController()
{
#if defined(Q_OS_MACOS) && defined(__arm64__)
  return std::make_unique<OSXDisplayInputController>();
#else
  return std::make_unique<UnsupportedDisplayInputController>();
#endif
}
