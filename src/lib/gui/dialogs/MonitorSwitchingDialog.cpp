/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/dialogs/MonitorSwitchingDialog.h"

#include "gui/config/ServerConfig.h"
#include "platform/DisplayInputControllerFactory.h"
#include "platform/IDisplayInputController.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QMessageBox>
#include <QProgressDialog>
#include <QPushButton>
#include <QTableWidget>
#include <QThread>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {
enum RouteColumn
{
  UseColumn,
  ComputerColumn,
  RoleColumn,
  LabelColumn,
  DetectedColumn,
  StatusColumn,
  ColumnCount
};

constexpr int kMonitorIdRole = Qt::UserRole;
constexpr int kMonitorNameRole = Qt::UserRole + 1;
constexpr int kDetectedInputRole = Qt::UserRole + 2;

QString resultText(const DisplayInputResult &result)
{
  return result.message.isEmpty() ? QStringLiteral("Unknown DDC error.") : result.message;
}
} // namespace

MonitorSwitchingDialog::MonitorSwitchingDialog(
    QWidget *parent, const ServerConfig &serverConfig, const QStringList &connectedClients,
    std::unique_ptr<IDisplayInputController> controller, WaitFunction waitFunction, ConfirmServerFunction confirmServer,
    IdentifyInputFunction identifyInput
)
    : QDialog(parent),
      m_serverConfig(serverConfig),
      m_connectedClients(connectedClients),
      m_controller(controller ? std::move(controller) : createDisplayInputController()),
      m_config(MonitorSwitchingConfig::load()),
      m_waitFunction(std::move(waitFunction)),
      m_confirmServer(std::move(confirmServer)),
      m_identifyInput(std::move(identifyInput))
{
  setWindowTitle(tr("Monitor Switching"));
  setMinimumSize(820, 480);

  auto *layout = new QVBoxLayout(this);
  auto *description = new QLabel(
      tr("Select the computers that share this monitor. Teleport safely tests the monitor inputs, asks which "
         "computer appeared, and saves the detected mappings automatically."),
      this
  );
  description->setWordWrap(true);
  layout->addWidget(description);

  auto *monitorLayout = new QFormLayout;
  auto *monitorRow = new QWidget(this);
  auto *monitorRowLayout = new QHBoxLayout(monitorRow);
  monitorRowLayout->setContentsMargins(0, 0, 0, 0);
  m_monitorCombo = new QComboBox(monitorRow);
  auto *refreshButton = new QPushButton(tr("Refresh"), monitorRow);
  monitorRowLayout->addWidget(m_monitorCombo, 1);
  monitorRowLayout->addWidget(refreshButton);
  monitorLayout->addRow(tr("Shared monitor:"), monitorRow);
  layout->addLayout(monitorLayout);

  m_routeTable = new QTableWidget(this);
  m_routeTable->setColumnCount(ColumnCount);
  m_routeTable->setHorizontalHeaderLabels(
      {tr("Use"), tr("Computer"), tr("Role"), tr("Input label"), tr("Detected input"), tr("Last test")}
  );
  m_routeTable->horizontalHeader()->setSectionResizeMode(ComputerColumn, QHeaderView::Stretch);
  m_routeTable->horizontalHeader()->setSectionResizeMode(LabelColumn, QHeaderView::Stretch);
  m_routeTable->horizontalHeader()->setSectionResizeMode(StatusColumn, QHeaderView::Stretch);
  m_routeTable->verticalHeader()->hide();
  layout->addWidget(m_routeTable, 1);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  m_disableButton = buttons->addButton(tr("Disable monitor switching"), QDialogButtonBox::ActionRole);
  m_testButton = buttons->addButton(tr("Detect inputs and enable"), QDialogButtonBox::ActionRole);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(refreshButton, &QPushButton::clicked, this, &MonitorSwitchingDialog::refreshMonitors);
  connect(m_monitorCombo, &QComboBox::currentIndexChanged, this, &MonitorSwitchingDialog::configurationEdited);
  connect(m_routeTable, &QTableWidget::itemChanged, this, &MonitorSwitchingDialog::configurationEdited);
  connect(m_testButton, &QPushButton::clicked, this, &MonitorSwitchingDialog::runTestsAndEnable);
  connect(m_disableButton, &QPushButton::clicked, this, &MonitorSwitchingDialog::disableMonitorSwitching);

  m_loading = true;
  refreshMonitors();
  populateComputers();
  m_loading = false;
  if (!m_config.routes.isEmpty() &&
      !m_config.validate(m_serverConfig.getServerName(), configuredComputerNames()).isEmpty()) {
    saveDisabledConfiguration();
  }
  updateStatus();
}

MonitorSwitchingDialog::~MonitorSwitchingDialog() = default;

void MonitorSwitchingDialog::setConnectedClients(const QStringList &connectedClients)
{
  m_connectedClients = connectedClients;
}

void MonitorSwitchingDialog::refreshMonitors()
{
  const auto previousId = m_loading ? m_config.monitorId : m_monitorCombo->currentData(kMonitorIdRole).toString();
  const auto previousName = m_loading ? m_config.monitorName : m_monitorCombo->currentData(kMonitorNameRole).toString();
  const auto discovery = m_controller->discoverMonitors();
  m_discoveryError = discovery.succeeded() ? QString() : resultText({discovery.status, discovery.message, {}});

  const bool wasLoading = m_loading;
  m_loading = true;
  m_monitorCombo->clear();
  m_monitorCombo->addItem(tr("Select a monitor"));
  for (const auto &monitor : discovery.monitors) {
    m_monitorCombo->addItem(monitor.name);
    m_monitorCombo->setItemData(m_monitorCombo->count() - 1, monitor.id, kMonitorIdRole);
    m_monitorCombo->setItemData(m_monitorCombo->count() - 1, monitor.name, kMonitorNameRole);
  }

  int selectedIndex = m_monitorCombo->findData(previousId, kMonitorIdRole);
  if (selectedIndex < 0 && !previousId.isEmpty()) {
    m_monitorCombo->addItem(tr("%1 (not currently detected)").arg(previousName));
    selectedIndex = m_monitorCombo->count() - 1;
    m_monitorCombo->setItemData(selectedIndex, previousId, kMonitorIdRole);
    m_monitorCombo->setItemData(selectedIndex, previousName, kMonitorNameRole);
  }
  m_monitorCombo->setCurrentIndex(std::max(0, selectedIndex));
  m_loading = wasLoading;
  if (!m_loading)
    configurationEdited();
}

void MonitorSwitchingDialog::populateComputers()
{
  m_routeTable->setRowCount(0);
  const auto serverName = m_serverConfig.getServerName();
  for (const auto &screen : m_serverConfig.screens()) {
    if (screen.isNull())
      continue;

    const int row = m_routeTable->rowCount();
    m_routeTable->insertRow(row);
    auto *useItem = new QTableWidgetItem;
    useItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsUserCheckable);
    useItem->setCheckState(Qt::Unchecked);
    m_routeTable->setItem(row, UseColumn, useItem);

    auto *computerItem = new QTableWidgetItem(screen.name());
    computerItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_routeTable->setItem(row, ComputerColumn, computerItem);

    const bool isServer = screen.name() == serverName;
    auto *roleItem = new QTableWidgetItem(isServer ? tr("Server") : tr("Client"));
    roleItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_routeTable->setItem(row, RoleColumn, roleItem);

    auto *labelItem = new QTableWidgetItem;
    m_routeTable->setItem(row, LabelColumn, labelItem);
    auto *detectedItem = new QTableWidgetItem(tr("Not detected"));
    detectedItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    detectedItem->setData(kDetectedInputRole, -1);
    m_routeTable->setItem(row, DetectedColumn, detectedItem);
    auto *statusItem = new QTableWidgetItem(tr("Not tested"));
    statusItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_routeTable->setItem(row, StatusColumn, statusItem);

    if (const auto existing = m_config.routeForComputer(screen.name())) {
      useItem->setCheckState(Qt::Checked);
      labelItem->setText(existing->inputLabel);
      detectedItem->setText(existing->inputValue >= 0 ? tr("Detected") : tr("Not detected"));
      detectedItem->setData(kDetectedInputRole, existing->inputValue);
      updateRouteStatus(*existing);
    }
  }
  m_routeTable->resizeColumnToContents(UseColumn);
  m_routeTable->resizeColumnToContents(RoleColumn);
  m_routeTable->resizeColumnToContents(DetectedColumn);
}

MonitorSwitchingConfig MonitorSwitchingDialog::configFromUi() const
{
  MonitorSwitchingConfig config;
  config.monitorId = m_monitorCombo->currentData(kMonitorIdRole).toString();
  config.monitorName = m_monitorCombo->currentData(kMonitorNameRole).toString();

  for (int row = 0; row < m_routeTable->rowCount(); ++row) {
    if (m_routeTable->item(row, UseColumn)->checkState() != Qt::Checked)
      continue;
    MonitorInputRoute route;
    route.computerName = m_routeTable->item(row, ComputerColumn)->text();
    route.inputLabel = m_routeTable->item(row, LabelColumn)->text().trimmed();
    route.inputValue = m_routeTable->item(row, DetectedColumn)->data(kDetectedInputRole).toInt();
    if (const auto previous = m_config.routeForComputer(route.computerName)) {
      route.lastTestStatus = previous->lastTestStatus;
      route.lastTestMessage = previous->lastTestMessage;
      route.lastTestedAt = previous->lastTestedAt;
    }
    config.routes.append(route);
  }
  return config;
}

QStringList MonitorSwitchingDialog::configuredComputerNames() const
{
  QStringList names;
  for (const auto &screen : m_serverConfig.screens()) {
    if (!screen.isNull())
      names.append(screen.name());
  }
  return names;
}

void MonitorSwitchingDialog::configurationEdited()
{
  if (m_loading)
    return;
  saveDisabledConfiguration();
}

void MonitorSwitchingDialog::saveDisabledConfiguration()
{
  m_config = configFromUi();
  m_config.invalidate();
  QString error;
  if (!m_config.save(&error)) {
    m_statusLabel->setText(tr("Could not save monitor switching configuration: %1").arg(error));
    return;
  }
  updateStatus();
}

bool MonitorSwitchingDialog::waitWithProgress(const QString &message, int milliseconds)
{
  if (m_waitFunction)
    return m_waitFunction(message, milliseconds);

  QProgressDialog progress(message, tr("Cancel"), 0, milliseconds, this);
  progress.setWindowModality(Qt::WindowModal);
  progress.setMinimumDuration(0);
  for (int elapsed = 0; elapsed < milliseconds; elapsed += 100) {
    progress.setValue(elapsed);
    QApplication::processEvents();
    if (progress.wasCanceled())
      return false;
    QThread::msleep(100);
  }
  progress.setValue(milliseconds);
  return true;
}

bool MonitorSwitchingDialog::restoreServerInput(
    const QString &monitorId, const MonitorInputRoute &serverRoute, MonitorInputRoute &testedRoute
)
{
  const auto restoreResult = m_controller->writeInput(monitorId, static_cast<uint16_t>(serverRoute.inputValue));
  if (!restoreResult.succeeded()) {
    testedRoute.lastTestStatus = QStringLiteral("failed");
    testedRoute.lastTestMessage = tr("The server input could not be restored: %1").arg(resultText(restoreResult));
    QMessageBox::critical(
        this, tr("Monitor restore failed"),
        tr("Teleport could not restore the server input. Use the physical monitor controls to return to the server. "
           "Monitor switching remains disabled.\n\n%1")
            .arg(resultText(restoreResult))
    );
    return false;
  }

  waitWithProgress(tr("Restoring the server input."), 2000);
  const auto restoredInput = m_controller->readInput(monitorId);
  if (restoredInput.succeeded()) {
    if (restoredInput.value && *restoredInput.value == static_cast<uint16_t>(serverRoute.inputValue))
      return true;
    testedRoute.lastTestStatus = QStringLiteral("failed");
    testedRoute.lastTestMessage = tr("The server input restore did not pass DDC readback verification.");
  } else if (restoredInput.status == DisplayInputStatus::ReadUnsupported ||
             restoredInput.status == DisplayInputStatus::ReadFailure) {
    const bool confirmed = m_confirmServer ? m_confirmServer()
                                           : QMessageBox::question(
                                                 this, tr("Confirm server input"),
                                                 tr("DDC could not verify the restore. Is this setup window visible "
                                                    "again?"),
                                                 QMessageBox::Yes | QMessageBox::No, QMessageBox::No
                                             ) == QMessageBox::Yes;
    if (confirmed)
      return true;
    testedRoute.lastTestStatus = QStringLiteral("failed");
    testedRoute.lastTestMessage = tr("The server input restore was not visually confirmed.");
  } else {
    testedRoute.lastTestStatus = QStringLiteral("failed");
    testedRoute.lastTestMessage =
        tr("The server input restore could not be verified: %1").arg(resultText(restoredInput));
  }

  QMessageBox::critical(
      this, tr("Monitor restore failed"),
      tr("Teleport could not verify the server input after restoring it. Use the physical monitor controls if "
         "needed. Monitor switching remains disabled.")
  );
  return false;
}

QList<uint16_t> MonitorSwitchingDialog::standardInputValues()
{
  QList<uint16_t> values{0x0f, 0x10, 0x11, 0x12, 0x1b, 0x1c};
  for (uint16_t value = 1; value <= 0x1f; ++value) {
    if (!values.contains(value))
      values.append(value);
  }
  return values;
}

std::optional<QString> MonitorSwitchingDialog::identifyInputComputer(const QStringList &computerNames)
{
  if (m_identifyInput)
    return m_identifyInput(computerNames);

  QStringList choices{tr("No selected computer appeared")};
  choices.append(computerNames);
  bool accepted = false;
  const auto selection = QInputDialog::getItem(
      this, tr("Identify monitor input"),
      tr("The monitor has returned to the server. Which selected computer appeared during the test?"), choices, 0,
      false, &accepted
  );
  if (!accepted)
    return std::nullopt;
  if (selection == choices.first())
    return QString();
  return selection;
}

bool MonitorSwitchingDialog::discoverInputRoutes(MonitorSwitchingConfig &config, const QList<uint16_t> &candidateValues)
{
  m_inputDetectionError.clear();
  const auto serverName = m_serverConfig.getServerName();
  auto serverRouteIt = std::ranges::find(config.routes, serverName, &MonitorInputRoute::computerName);
  Q_ASSERT(serverRouteIt != config.routes.end());

  const auto currentInput = m_controller->readInput(config.monitorId);
  if (!currentInput.succeeded() || !currentInput.value) {
    m_inputDetectionError =
        tr("Teleport could not read the current server input, so it cannot safely restore the monitor during "
           "automatic detection. Check that DDC is enabled on the monitor and connect it directly if a dock or "
           "adapter blocks DDC.");
    serverRouteIt->lastTestStatus = QStringLiteral("failed");
    serverRouteIt->lastTestMessage = resultText(currentInput);
    serverRouteIt->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    return false;
  }

  serverRouteIt->inputValue = *currentInput.value;
  serverRouteIt->lastTestStatus = QStringLiteral("detected");
  serverRouteIt->lastTestMessage = tr("Detected as the current server input with DDC readback.");
  serverRouteIt->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  const auto serverRoute = *serverRouteIt;

  QStringList unmatchedComputers;
  for (auto &route : config.routes) {
    if (route.computerName == serverName)
      continue;
    route.inputValue = -1;
    route.lastTestStatus.clear();
    route.lastTestMessage.clear();
    route.lastTestedAt.clear();
    unmatchedComputers.append(route.computerName);
  }

  for (const auto candidateValue : candidateValues) {
    if (unmatchedComputers.isEmpty())
      break;
    if (candidateValue == static_cast<uint16_t>(serverRoute.inputValue))
      continue;

    for (const auto &computerName : std::as_const(unmatchedComputers)) {
      if (!m_connectedClients.contains(computerName)) {
        auto route = std::ranges::find(config.routes, computerName, &MonitorInputRoute::computerName);
        route->lastTestStatus = QStringLiteral("failed");
        route->lastTestMessage = tr("The client disconnected during input detection.");
        route->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        m_inputDetectionError = tr("%1 disconnected during input detection.").arg(computerName);
        return false;
      }
    }

    auto statusRoute = std::ranges::find(config.routes, unmatchedComputers.first(), &MonitorInputRoute::computerName);
    const auto writeResult = m_controller->writeInput(config.monitorId, candidateValue);
    bool completedPreview = false;
    DisplayInputResult readResult{DisplayInputStatus::ReadFailure, tr("DDC readback was unavailable."), {}};
    if (writeResult.succeeded()) {
      const bool stabilized =
          waitWithProgress(tr("Testing a possible monitor input. Watch for one of the selected computers."), 2000);
      if (stabilized)
        readResult = m_controller->readInput(config.monitorId);
      completedPreview = stabilized && waitWithProgress(tr("Keeping the possible input visible."), 3000);
    }

    if (!restoreServerInput(config.monitorId, serverRoute, *statusRoute)) {
      m_inputDetectionError = statusRoute->lastTestMessage;
      return false;
    }
    if (!writeResult.succeeded())
      continue;
    if (!completedPreview) {
      statusRoute->lastTestStatus = QStringLiteral("failed");
      statusRoute->lastTestMessage = tr("Input detection was canceled.");
      statusRoute->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
      m_inputDetectionError = statusRoute->lastTestMessage;
      return false;
    }
    if (readResult.succeeded() && (!readResult.value || *readResult.value != candidateValue))
      continue;
    if (!readResult.succeeded() && readResult.status != DisplayInputStatus::ReadUnsupported &&
        readResult.status != DisplayInputStatus::ReadFailure) {
      m_inputDetectionError = resultText(readResult);
      return false;
    }

    const auto identifiedComputer = identifyInputComputer(unmatchedComputers);
    if (!identifiedComputer) {
      statusRoute->lastTestStatus = QStringLiteral("failed");
      statusRoute->lastTestMessage = tr("Input detection was canceled.");
      statusRoute->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
      m_inputDetectionError = statusRoute->lastTestMessage;
      return false;
    }
    if (identifiedComputer->isEmpty())
      continue;

    auto detectedRoute = std::ranges::find(config.routes, *identifiedComputer, &MonitorInputRoute::computerName);
    if (detectedRoute == config.routes.end() || !unmatchedComputers.contains(*identifiedComputer))
      continue;
    detectedRoute->inputValue = candidateValue;
    detectedRoute->lastTestStatus =
        readResult.succeeded() ? QStringLiteral("verified") : QStringLiteral("verified visually");
    detectedRoute->lastTestMessage = readResult.succeeded()
                                         ? tr("Detected by DDC readback and visual identification.")
                                         : tr("Detected by visual identification because readback was unavailable.");
    detectedRoute->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    unmatchedComputers.removeAll(*identifiedComputer);
  }

  if (unmatchedComputers.isEmpty())
    return true;

  for (const auto &computerName : std::as_const(unmatchedComputers)) {
    auto route = std::ranges::find(config.routes, computerName, &MonitorInputRoute::computerName);
    route->lastTestStatus = QStringLiteral("failed");
    route->lastTestMessage = tr("No working monitor input was identified.");
    route->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  }
  m_inputDetectionError =
      tr("Teleport could not identify a working monitor input for: %1. Confirm that each computer is awake and "
         "sending video to the selected monitor, then run detection again.")
          .arg(unmatchedComputers.join(QStringLiteral(", ")));
  return false;
}

void MonitorSwitchingDialog::runTestsAndEnable()
{
  auto config = configFromUi();
  const auto serverName = m_serverConfig.getServerName();
  const auto validationError = config.validateSetupSelection(serverName, configuredComputerNames());
  if (!validationError.isEmpty()) {
    QMessageBox::warning(this, tr("Monitor switching setup"), validationError);
    return;
  }

  for (const auto &route : config.routes) {
    if (route.computerName != serverName && !m_connectedClients.contains(route.computerName)) {
      QMessageBox::warning(
          this, tr("Computer not connected"),
          tr("Connect %1 to the Deskflow server before testing its monitor input.").arg(route.computerName)
      );
      return;
    }
  }

  config.invalidate();
  QString error;
  if (!config.save(&error)) {
    QMessageBox::critical(this, tr("Monitor switching setup"), tr("Could not save the configuration: %1").arg(error));
    return;
  }
  const bool passed = discoverInputRoutes(config, standardInputValues()) &&
                      config.validate(serverName, configuredComputerNames()).isEmpty();

  m_config = config;
  if (passed) {
    m_config.verifiedConfigHash = m_config.configurationHash();
    m_config.enabled = true;
  }
  if (!m_config.save(&error)) {
    QMessageBox::critical(this, tr("Monitor switching setup"), tr("Could not save the configuration: %1").arg(error));
    return;
  }

  m_loading = true;
  populateComputers();
  m_loading = false;
  updateStatus();
  if (passed) {
    QMessageBox::information(
        this, tr("Monitor switching enabled"), tr("All selected monitor inputs were detected and verified.")
    );
    Q_EMIT configurationEnabled();
  } else {
    QMessageBox::warning(
        this, tr("Monitor switching not enabled"),
        m_inputDetectionError.isEmpty() ? tr("At least one monitor input could not be detected.")
                                        : m_inputDetectionError
    );
  }
}

void MonitorSwitchingDialog::disableMonitorSwitching()
{
  m_config = configFromUi();
  m_config.invalidate();
  QString error;
  if (!m_config.save(&error)) {
    QMessageBox::critical(this, tr("Monitor switching setup"), tr("Could not save the configuration: %1").arg(error));
    return;
  }
  updateStatus();
}

void MonitorSwitchingDialog::updateRouteStatus(const MonitorInputRoute &route)
{
  for (int row = 0; row < m_routeTable->rowCount(); ++row) {
    if (m_routeTable->item(row, ComputerColumn)->text() != route.computerName)
      continue;
    QString text = route.lastTestStatus.isEmpty() ? tr("Not tested") : route.lastTestStatus;
    if (!route.lastTestedAt.isEmpty())
      text.append(tr(" at %1").arg(route.lastTestedAt));
    m_routeTable->item(row, StatusColumn)->setText(text);
    m_routeTable->item(row, StatusColumn)->setToolTip(route.lastTestMessage);
    return;
  }
}

void MonitorSwitchingDialog::updateStatus()
{
  if (m_config.enabled && m_config.isVerified()) {
    m_statusLabel->setText(tr("Monitor switching is enabled and verified."));
    m_testButton->setText(tr("Detect inputs again"));
    m_disableButton->setEnabled(true);
  } else {
    auto status = tr("Monitor switching is disabled until every selected input passes testing.");
    if (!m_discoveryError.isEmpty())
      status.append(tr(" Monitor discovery failed: %1").arg(m_discoveryError));
    m_statusLabel->setText(status);
    m_testButton->setText(tr("Detect inputs and enable"));
    m_disableButton->setEnabled(false);
  }
}
