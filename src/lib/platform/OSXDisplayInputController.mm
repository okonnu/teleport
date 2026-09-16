/*
 * Deskflow -- mouse and keyboard sharing utility
 * Adapted from AppleSiliconDDC: https://github.com/waydabber/AppleSiliconDDC
 * SPDX-FileCopyrightText: (C) 2021 Istvan T.
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: MIT
 */

#include "platform/OSXDisplayInputController.h"

#include <QRegularExpression>

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>
#include <unistd.h>

#include <utility>

typedef CFTypeRef IOAVService;

extern "C" IOAVService IOAVServiceCreateWithService(CFAllocatorRef allocator, io_service_t service);
extern "C" IOReturn IOAVServiceReadI2C(
    IOAVService service, uint32_t chipAddress, uint32_t offset, void *outputBuffer, uint32_t outputBufferSize
);
extern "C" IOReturn IOAVServiceWriteI2C(
    IOAVService service, uint32_t chipAddress, uint32_t dataAddress, void *inputBuffer, uint32_t inputBufferSize
);

namespace {
constexpr uint8_t kDdcAddress = 0x37;
constexpr uint8_t kDataAddress = 0x51;
constexpr uint8_t kInputSelect = 0x60;
constexpr uint8_t kCapabilitiesRequest = 0xf3;
constexpr uint8_t kCapabilitiesReply = 0xe3;
constexpr int kMaximumDdcPacketSize = 39;
constexpr int kMaximumCapabilitiesSize = 4096;

struct RegistryDisplay
{
  QString id;
  QString name;
  QString connectionName;
  QString manufacturerId;
  int productId = -1;
};

uint8_t checksum(uint8_t initial, const QByteArray &data, int lastIndex)
{
  auto result = initial;
  for (int index = 0; index <= lastIndex; ++index)
    result ^= static_cast<uint8_t>(data.at(index));
  return result;
}

QString stringProperty(io_registry_entry_t entry, CFStringRef key, IOOptionBits options = 0)
{
  const auto value = IORegistryEntryCreateCFProperty(entry, key, kCFAllocatorDefault, options);
  if (!value)
    return {};

  QString result;
  if (CFGetTypeID(value) == CFStringGetTypeID()) {
    const auto string = static_cast<CFStringRef>(value);
    const auto length = CFStringGetLength(string);
    const auto maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
    QByteArray buffer(maximum, 0);
    if (CFStringGetCString(string, buffer.data(), maximum, kCFStringEncodingUTF8))
      result = QString::fromUtf8(buffer.constData());
  }
  CFRelease(value);
  return result;
}

QString dictionaryString(CFDictionaryRef dictionary, CFStringRef key)
{
  const auto value = CFDictionaryGetValue(dictionary, key);
  if (!value || CFGetTypeID(value) != CFStringGetTypeID())
    return {};
  const auto string = static_cast<CFStringRef>(value);
  const auto length = CFStringGetLength(string);
  const auto maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  QByteArray buffer(maximum, 0);
  return CFStringGetCString(string, buffer.data(), maximum, kCFStringEncodingUTF8)
             ? QString::fromUtf8(buffer.constData())
             : QString();
}

int dictionaryNumber(CFDictionaryRef dictionary, CFStringRef key)
{
  const auto value = CFDictionaryGetValue(dictionary, key);
  if (!value || CFGetTypeID(value) != CFNumberGetTypeID())
    return -1;
  int result = -1;
  return CFNumberGetValue(static_cast<CFNumberRef>(value), kCFNumberIntType, &result) ? result : -1;
}

RegistryDisplay displayProperties(io_registry_entry_t entry)
{
  RegistryDisplay display;
  io_string_t path{};
  if (IORegistryEntryGetPath(entry, kIOServicePlane, path) == KERN_SUCCESS)
    display.id = QString::fromUtf8(path);

  const auto attributesValue = IORegistryEntryCreateCFProperty(
      entry, CFSTR("DisplayAttributes"), kCFAllocatorDefault, kIORegistryIterateRecursively
  );
  if (!attributesValue || CFGetTypeID(attributesValue) != CFDictionaryGetTypeID()) {
    if (attributesValue)
      CFRelease(attributesValue);
    return display;
  }

  const auto attributes = static_cast<CFDictionaryRef>(attributesValue);
  const auto productValue = CFDictionaryGetValue(attributes, CFSTR("ProductAttributes"));
  if (productValue && CFGetTypeID(productValue) == CFDictionaryGetTypeID()) {
    const auto product = static_cast<CFDictionaryRef>(productValue);
    const auto nameValue = CFDictionaryGetValue(product, CFSTR("ProductName"));
    if (nameValue && CFGetTypeID(nameValue) == CFStringGetTypeID()) {
      const auto name = static_cast<CFStringRef>(nameValue);
      const auto length = CFStringGetLength(name);
      const auto maximum = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
      QByteArray buffer(maximum, 0);
      if (CFStringGetCString(name, buffer.data(), maximum, kCFStringEncodingUTF8))
        display.name = QString::fromUtf8(buffer.constData());
    }
  }
  CFRelease(attributesValue);

  const auto metadataValue = IORegistryEntryCreateCFProperty(
      entry, CFSTR("Metadata"), kCFAllocatorDefault, kIORegistryIterateRecursively
  );
  if (metadataValue && CFGetTypeID(metadataValue) == CFDictionaryGetTypeID()) {
    const auto metadata = static_cast<CFDictionaryRef>(metadataValue);
    display.manufacturerId = dictionaryString(metadata, CFSTR("ManufacturerName")).toUpper();
    display.productId = dictionaryNumber(metadata, CFSTR("ProductID"));
  }
  if (metadataValue)
    CFRelease(metadataValue);

  const auto transportValue =
      IORegistryEntryCreateCFProperty(entry, CFSTR("Transport"), kCFAllocatorDefault, kIORegistryIterateRecursively);
  if (transportValue && CFGetTypeID(transportValue) == CFDictionaryGetTypeID()) {
    const auto transport = static_cast<CFDictionaryRef>(transportValue);
    display.connectionName = dictionaryString(transport, CFSTR("Downstream"));
    if (display.connectionName.isEmpty())
      display.connectionName = dictionaryString(transport, CFSTR("Upstream"));
  }
  if (transportValue)
    CFRelease(transportValue);
  return display;
}

template <typename Callback> void forEachExternalService(Callback callback)
{
  const auto root = IORegistryGetRootEntry(kIOMainPortDefault);
  if (root == IO_OBJECT_NULL)
    return;

  io_iterator_t iterator = IO_OBJECT_NULL;
  if (IORegistryEntryCreateIterator(root, kIOServicePlane, kIORegistryIterateRecursively, &iterator) != KERN_SUCCESS) {
    IOObjectRelease(root);
    return;
  }

  RegistryDisplay current;
  while (const auto entry = IOIteratorNext(iterator)) {
    io_name_t nameBuffer{};
    if (IORegistryEntryGetName(entry, nameBuffer) == KERN_SUCCESS) {
      const QString entryName = QString::fromUtf8(nameBuffer);
      if (entryName.contains(QStringLiteral("AppleCLCD2")) ||
          entryName.contains(QStringLiteral("IOMobileFramebufferShim"))) {
        current = displayProperties(entry);
      } else if (entryName.contains(QStringLiteral("DCPAVServiceProxy")) &&
                 stringProperty(entry, CFSTR("Location")) == QStringLiteral("External") && !current.id.isEmpty()) {
        const auto serviceDisplay = displayProperties(entry);
        if (current.manufacturerId.isEmpty())
          current.manufacturerId = serviceDisplay.manufacturerId;
        if (current.productId < 0)
          current.productId = serviceDisplay.productId;
        if (!callback(current, entry)) {
          IOObjectRelease(entry);
          break;
        }
      }
    }
    IOObjectRelease(entry);
  }

  IOObjectRelease(iterator);
  IOObjectRelease(root);
}

IOAVService findService(const QString &monitorId)
{
  IOAVService result = nullptr;
  forEachExternalService([&](const RegistryDisplay &display, io_service_t entry) {
    if (display.id != monitorId)
      return true;
    result = IOAVServiceCreateWithService(kCFAllocatorDefault, entry);
    return false;
  });
  return result;
}

std::optional<RegistryDisplay> findDisplay(const QString &monitorId)
{
  std::optional<RegistryDisplay> result;
  forEachExternalService([&](const RegistryDisplay &display, io_service_t) {
    if (display.id != monitorId)
      return true;
    result = display;
    return false;
  });
  return result;
}

DisplayInputResult unsupportedResult()
{
  return {DisplayInputStatus::UnsupportedPlatform, QStringLiteral("Direct DDC currently requires Apple Silicon."), {}};
}
} // namespace

QByteArray OSXDisplayInputController::makeReadPacket()
{
  QByteArray packet;
  packet.append(static_cast<char>(0x82));
  packet.append(static_cast<char>(0x01));
  packet.append(static_cast<char>(kInputSelect));
  packet.append('\0');
  packet[packet.size() - 1] = static_cast<char>(checksum(kDdcAddress << 1, packet, packet.size() - 2));
  return packet;
}

QByteArray OSXDisplayInputController::makeWritePacket(uint16_t inputValue)
{
  QByteArray packet;
  packet.append(static_cast<char>(0x84));
  packet.append(static_cast<char>(0x03));
  packet.append(static_cast<char>(kInputSelect));
  packet.append(static_cast<char>(inputValue >> 8));
  packet.append(static_cast<char>(inputValue & 0xff));
  packet.append('\0');
  packet[packet.size() - 1] = static_cast<char>(checksum((kDdcAddress << 1) ^ kDataAddress, packet, packet.size() - 2));
  return packet;
}

QByteArray OSXDisplayInputController::makeCapabilitiesPacket(uint16_t offset)
{
  QByteArray packet;
  packet.append(static_cast<char>(0x84));
  packet.append(static_cast<char>(0x03));
  packet.append(static_cast<char>(kCapabilitiesRequest));
  packet.append(static_cast<char>(offset >> 8));
  packet.append(static_cast<char>(offset & 0xff));
  packet.append('\0');
  packet[packet.size() - 1] = static_cast<char>(checksum((kDdcAddress << 1) ^ kDataAddress, packet, packet.size() - 2));
  return packet;
}

std::optional<uint16_t> OSXDisplayInputController::parseReadReply(const QByteArray &reply)
{
  if (reply.size() != 11 || static_cast<uint8_t>(reply.at(3)) != 0 ||
      static_cast<uint8_t>(reply.at(4)) != kInputSelect ||
      checksum(0x50, reply, reply.size() - 2) != static_cast<uint8_t>(reply.back())) {
    return std::nullopt;
  }
  return static_cast<uint16_t>((static_cast<uint8_t>(reply.at(8)) << 8) | static_cast<uint8_t>(reply.at(9)));
}

std::optional<DdcCapabilitiesFragment> OSXDisplayInputController::parseCapabilitiesReply(const QByteArray &reply)
{
  if (reply.size() < 6)
    return std::nullopt;
  const int dataLength = static_cast<uint8_t>(reply.at(1)) & 0x7f;
  const int packetLength = dataLength + 3;
  if (dataLength < 3 || packetLength > reply.size() || static_cast<uint8_t>(reply.at(2)) != kCapabilitiesReply ||
      checksum(0x50, reply, packetLength - 2) != static_cast<uint8_t>(reply.at(packetLength - 1))) {
    return std::nullopt;
  }

  return DdcCapabilitiesFragment{
      (static_cast<uint8_t>(reply.at(3)) << 8) | static_cast<uint8_t>(reply.at(4)), reply.mid(5, dataLength - 3)
  };
}

QList<uint16_t> OSXDisplayInputController::parseInputValues(const QByteArray &capabilities)
{
  const auto text = QString::fromLatin1(capabilities);
  const QRegularExpression inputFeature(
      QStringLiteral("60\\s*\\(([^()]*)\\)"), QRegularExpression::CaseInsensitiveOption
  );
  const auto featureMatch = inputFeature.match(text);
  if (!featureMatch.hasMatch())
    return {};

  QList<uint16_t> values;
  const QRegularExpression hexValue(QStringLiteral("[0-9a-fA-F]{2}"));
  auto valueMatches = hexValue.globalMatch(featureMatch.captured(1));
  while (valueMatches.hasNext()) {
    bool converted = false;
    const auto value = valueMatches.next().captured().toUShort(&converted, 16);
    if (converted && !values.contains(value))
      values.append(value);
  }
  return values;
}

QString OSXDisplayInputController::inputSourceName(uint16_t inputValue)
{
  switch (inputValue) {
  case 0x01:
    return QStringLiteral("VGA 1");
  case 0x02:
    return QStringLiteral("VGA 2");
  case 0x03:
    return QStringLiteral("DVI 1");
  case 0x04:
    return QStringLiteral("DVI 2");
  case 0x05:
    return QStringLiteral("Composite 1");
  case 0x06:
    return QStringLiteral("Composite 2");
  case 0x07:
    return QStringLiteral("S-Video 1");
  case 0x08:
    return QStringLiteral("S-Video 2");
  case 0x09:
    return QStringLiteral("Tuner 1");
  case 0x0a:
    return QStringLiteral("Tuner 2");
  case 0x0b:
    return QStringLiteral("Tuner 3 or USB-C");
  case 0x0c:
    return QStringLiteral("Component 1");
  case 0x0d:
    return QStringLiteral("Component 2");
  case 0x0e:
    return QStringLiteral("Component 3");
  case 0x0f:
    return QStringLiteral("DisplayPort 1");
  case 0x10:
    return QStringLiteral("DisplayPort 2");
  case 0x11:
    return QStringLiteral("HDMI 1");
  case 0x12:
    return QStringLiteral("HDMI 2");
  case 0x1b:
    return QStringLiteral("USB-C 1");
  case 0x1c:
    return QStringLiteral("USB-C 2");
  default:
    return QStringLiteral("Monitor input");
  }
}

bool OSXDisplayInputController::runWithRetries(const std::function<bool()> &operation)
{
  for (int attempt = 0; attempt < 5; ++attempt) {
    if (operation())
      return true;
  }
  return false;
}

DisplayDiscoveryResult OSXDisplayInputController::discoverMonitors()
{
#if defined(__arm64__)
  QList<DisplayMonitor> monitors;
  forEachExternalService([&](const RegistryDisplay &display, io_service_t) {
    const DisplayMonitor monitor{
        display.id, display.name.isEmpty() ? QStringLiteral("External display") : display.name,
        display.manufacturerId, display.productId, display.name
    };
    if (!monitors.contains(monitor))
      monitors.append(monitor);
    return true;
  });
  return {DisplayInputStatus::Success, {}, monitors};
#else
  return {DisplayInputStatus::UnsupportedPlatform, QStringLiteral("Direct DDC currently requires Apple Silicon."), {}};
#endif
}

DisplayInputSourcesResult OSXDisplayInputController::discoverInputSources(const QString &monitorId)
{
#if defined(__arm64__)
  const auto service = findService(monitorId);
  if (!service)
    return {DisplayInputStatus::MonitorNotFound, QStringLiteral("The selected monitor is no longer connected."), {}};

  QByteArray capabilities;
  uint16_t offset = 0;
  bool complete = false;
  while (capabilities.size() < kMaximumCapabilitiesSize) {
    std::optional<DdcCapabilitiesFragment> fragment;
    const bool fragmentRead = runWithRetries([&] {
      const auto packetTemplate = makeCapabilitiesPacket(offset);
      auto packet = packetTemplate;
      IOReturn writeResult = kIOReturnError;
      for (int cycle = 0; cycle < 2; ++cycle) {
        usleep(10000);
        writeResult = IOAVServiceWriteI2C(
            service, kDdcAddress, kDataAddress, packet.data(), static_cast<uint32_t>(packet.size())
        );
      }
      if (writeResult != kIOReturnSuccess)
        return false;

      usleep(50000);
      QByteArray reply(kMaximumDdcPacketSize, 0);
      if (IOAVServiceReadI2C(service, kDdcAddress, kDataAddress, reply.data(), static_cast<uint32_t>(reply.size())) !=
          kIOReturnSuccess) {
        return false;
      }
      fragment = parseCapabilitiesReply(reply);
      return fragment && fragment->offset == offset;
    });
    if (!fragmentRead)
      break;
    if (fragment->data.isEmpty()) {
      complete = true;
      break;
    }
    capabilities.append(fragment->data);
    offset = static_cast<uint16_t>(offset + fragment->data.size());
  }
  CFRelease(service);

  auto inputValues = complete ? parseInputValues(capabilities) : QList<uint16_t>{};
  QString message;
  if (inputValues.isEmpty()) {
    inputValues = {0x01, 0x02, 0x03, 0x04, 0x0f, 0x10, 0x11, 0x12, 0x1b, 0x1c};
    message = QStringLiteral(
        "The monitor did not report its input list. Standard DDC inputs are shown and must be verified."
    );
  }

  const auto currentInput = readInput(monitorId);
  if (currentInput.succeeded() && currentInput.value && !inputValues.contains(*currentInput.value))
    inputValues.prepend(*currentInput.value);
  const auto display = findDisplay(monitorId);

  QList<DisplayInputSource> sources;
  for (const auto inputValue : std::as_const(inputValues)) {
    auto name = inputSourceName(inputValue);
    if (currentInput.succeeded() && currentInput.value && inputValue == *currentInput.value) {
      if (display && !display->connectionName.isEmpty())
        name = QStringLiteral("%1 (current server input)").arg(display->connectionName);
      else
        name = QStringLiteral("%1 (current server input)").arg(name);
    }
    sources.append({inputValue, name});
  }
  return {DisplayInputStatus::Success, message, sources};
#else
  Q_UNUSED(monitorId)
  return {DisplayInputStatus::UnsupportedPlatform, QStringLiteral("Direct DDC currently requires Apple Silicon."), {}};
#endif
}

DisplayInputResult OSXDisplayInputController::readInput(const QString &monitorId)
{
#if defined(__arm64__)
  const auto service = findService(monitorId);
  if (!service)
    return {DisplayInputStatus::MonitorNotFound, QStringLiteral("The selected monitor is no longer connected."), {}};

  std::optional<uint16_t> inputValue;
  const bool readSucceeded = runWithRetries([&] {
    const auto packetTemplate = makeReadPacket();
    auto packet = packetTemplate;
    IOReturn writeResult = kIOReturnError;
    for (int cycle = 0; cycle < 2; ++cycle) {
      usleep(10000);
      writeResult =
          IOAVServiceWriteI2C(service, kDdcAddress, kDataAddress, packet.data(), static_cast<uint32_t>(packet.size()));
    }
    if (writeResult == kIOReturnSuccess) {
      usleep(50000);
      QByteArray reply(11, 0);
      if (IOAVServiceReadI2C(service, kDdcAddress, kDataAddress, reply.data(), static_cast<uint32_t>(reply.size())) ==
          kIOReturnSuccess) {
        if (const auto value = parseReadReply(reply)) {
          inputValue = value;
          return true;
        }
      }
    }
    usleep(20000);
    return false;
  });
  CFRelease(service);
  if (readSucceeded)
    return {DisplayInputStatus::Success, QStringLiteral("DDC input read succeeded."), inputValue};
  return {DisplayInputStatus::ReadFailure, QStringLiteral("The monitor did not return a valid DDC input value."), {}};
#else
  Q_UNUSED(monitorId)
  return unsupportedResult();
#endif
}

DisplayInputResult OSXDisplayInputController::writeInput(const QString &monitorId, uint16_t inputValue)
{
#if defined(__arm64__)
  const auto service = findService(monitorId);
  if (!service)
    return {DisplayInputStatus::MonitorNotFound, QStringLiteral("The selected monitor is no longer connected."), {}};

  const bool writeSucceeded = runWithRetries([&] {
    const auto packetTemplate = makeWritePacket(inputValue);
    auto packet = packetTemplate;
    IOReturn result = kIOReturnError;
    for (int cycle = 0; cycle < 2; ++cycle) {
      usleep(10000);
      result =
          IOAVServiceWriteI2C(service, kDdcAddress, kDataAddress, packet.data(), static_cast<uint32_t>(packet.size()));
    }
    if (result == kIOReturnSuccess)
      return true;
    usleep(20000);
    return false;
  });
  CFRelease(service);
  if (writeSucceeded)
    return {DisplayInputStatus::Success, QStringLiteral("DDC input write succeeded."), inputValue};
  return {DisplayInputStatus::WriteFailure, QStringLiteral("The monitor rejected the DDC input command."), {}};
#else
  Q_UNUSED(monitorId)
  Q_UNUSED(inputValue)
  return unsupportedResult();
#endif
}
