/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Teleport Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "gui/dialogs/MonitorSwitchingDialog.h"

#include "common/MonitorProfileDatabase.h"
#include "gui/config/ServerConfig.h"
#include "platform/DisplayInputControllerFactory.h"
#include "platform/IDisplayInputController.h"

#include <QApplication>
#include <QComboBox>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
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
  InputColumn,
  StatusColumn,
  ColumnCount
};

constexpr int kMonitorIdRole = Qt::UserRole;
constexpr int kMonitorNameRole = Qt::UserRole + 1;
constexpr int kMonitorManufacturerRole = Qt::UserRole + 2;
constexpr int kMonitorProductRole = Qt::UserRole + 3;
constexpr int kMonitorModelRole = Qt::UserRole + 4;
constexpr int kInputIdRole = Qt::UserRole + 5;
constexpr int kInputReadValuesRole = Qt::UserRole + 6;
constexpr int kInputLabelRole = Qt::UserRole + 7;

QString resultText(const DisplayInputResult &result)
{
  return result.message.isEmpty() ? QStringLiteral("Unknown DDC error.") : result.message;
}

QVariantList readValuesVariant(const QList<uint16_t> &values)
{
  QVariantList result;
  for (const auto value : values)
    result.append(value);
  return result;
}
} // namespace

MonitorSwitchingDialog::MonitorSwitchingDialog(
    QWidget *parent, const ServerConfig &serverConfig, const QStringList &connectedClients,
    std::unique_ptr<IDisplayInputController> controller, WaitFunction waitFunction, ConfirmServerFunction confirmServer,
    ConfirmInputFunction confirmInput
)
    : QDialog(parent),
      m_serverConfig(serverConfig),
      m_connectedClients(connectedClients),
      m_controller(controller ? std::move(controller) : createDisplayInputController()),
      m_config(MonitorSwitchingConfig::load()),
      m_waitFunction(std::move(waitFunction)),
      m_confirmServer(std::move(confirmServer)),
      m_confirmInput(std::move(confirmInput))
{
  setWindowTitle(tr("Monitor Switching"));
  setMinimumSize(820, 480);

  auto *layout = new QVBoxLayout(this);
  auto *description = new QLabel(
      tr("Select the monitor input used by each computer. Teleport uses a matching monitor profile when available "
         "and falls back to guided DDC testing for unknown monitors."),
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
  auto *importDatabaseButton = new QPushButton(tr("Import profile database..."), monitorRow);
  monitorRowLayout->addWidget(m_monitorCombo, 1);
  monitorRowLayout->addWidget(refreshButton);
  monitorRowLayout->addWidget(importDatabaseButton);
  monitorLayout->addRow(tr("Shared monitor:"), monitorRow);
  layout->addLayout(monitorLayout);

  m_routeTable = new QTableWidget(this);
  m_routeTable->setColumnCount(ColumnCount);
  m_routeTable->setHorizontalHeaderLabels({tr("Use"), tr("Computer"), tr("Role"), tr("Monitor input"), tr("Last test")}
  );
  m_routeTable->horizontalHeader()->setSectionResizeMode(ComputerColumn, QHeaderView::Stretch);
  m_routeTable->horizontalHeader()->setSectionResizeMode(InputColumn, QHeaderView::Stretch);
  m_routeTable->horizontalHeader()->setSectionResizeMode(StatusColumn, QHeaderView::Stretch);
  m_routeTable->verticalHeader()->hide();
  layout->addWidget(m_routeTable, 1);

  m_statusLabel = new QLabel(this);
  m_statusLabel->setWordWrap(true);
  layout->addWidget(m_statusLabel);

  auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
  m_disableButton = buttons->addButton(tr("Disable monitor switching"), QDialogButtonBox::ActionRole);
  m_testButton = buttons->addButton(tr("Test assignments and enable"), QDialogButtonBox::ActionRole);
  layout->addWidget(buttons);

  connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
  connect(refreshButton, &QPushButton::clicked, this, &MonitorSwitchingDialog::refreshMonitors);
  connect(importDatabaseButton, &QPushButton::clicked, this, &MonitorSwitchingDialog::importProfileDatabase);
  connect(m_monitorCombo, &QComboBox::currentIndexChanged, this, &MonitorSwitchingDialog::monitorSelectionChanged);
  connect(m_routeTable, &QTableWidget::itemChanged, this, &MonitorSwitchingDialog::configurationEdited);
  connect(m_testButton, &QPushButton::clicked, this, &MonitorSwitchingDialog::runTestsAndEnable);
  connect(m_disableButton, &QPushButton::clicked, this, &MonitorSwitchingDialog::disableMonitorSwitching);

  m_profileDatabase = MonitorProfileDatabase::loadActive(&m_profileDatabaseError);
  m_loading = true;
  refreshMonitors();
  populateComputers();
  m_loading = false;
  refreshInputSources();
  if (m_config.enabled && !m_config.profileId.isEmpty() &&
      (!m_matchedProfile || m_matchedProfile->id != m_config.profileId ||
       m_matchedProfile->revision != m_config.profileRevision)) {
    saveDisabledConfiguration();
  }
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
    const auto index = m_monitorCombo->count() - 1;
    m_monitorCombo->setItemData(index, monitor.id, kMonitorIdRole);
    m_monitorCombo->setItemData(index, monitor.name, kMonitorNameRole);
    m_monitorCombo->setItemData(index, monitor.manufacturerId, kMonitorManufacturerRole);
    m_monitorCombo->setItemData(index, monitor.productId, kMonitorProductRole);
    m_monitorCombo->setItemData(index, monitor.modelName, kMonitorModelRole);
  }

  int selectedIndex = m_monitorCombo->findData(previousId, kMonitorIdRole);
  if (selectedIndex < 0 && !previousId.isEmpty()) {
    m_monitorCombo->addItem(tr("%1 (not currently detected)").arg(previousName));
    selectedIndex = m_monitorCombo->count() - 1;
    m_monitorCombo->setItemData(selectedIndex, previousId, kMonitorIdRole);
    m_monitorCombo->setItemData(selectedIndex, previousName, kMonitorNameRole);
    m_monitorCombo->setItemData(selectedIndex, m_config.monitorManufacturerId, kMonitorManufacturerRole);
    m_monitorCombo->setItemData(selectedIndex, m_config.monitorProductId, kMonitorProductRole);
    m_monitorCombo->setItemData(selectedIndex, m_config.monitorModelName, kMonitorModelRole);
  }
  m_monitorCombo->setCurrentIndex(std::max(0, selectedIndex));
  m_loading = wasLoading;
  if (!m_loading)
    monitorSelectionChanged();
}

void MonitorSwitchingDialog::refreshInputSources()
{
  m_inputSources.clear();
  m_inputSourceMessage.clear();
  m_matchedProfile.reset();
  const auto monitorId = m_monitorCombo->currentData(kMonitorIdRole).toString();
  if (!monitorId.isEmpty()) {
    const MonitorProfileIdentity identity{
        m_monitorCombo->currentData(kMonitorManufacturerRole).toString(),
        m_monitorCombo->currentData(kMonitorProductRole).toInt(),
        m_monitorCombo->currentData(kMonitorModelRole).toString(),
    };
    m_matchedProfile = m_profileDatabase.find(identity);
    if (m_matchedProfile) {
      for (const auto &input : m_matchedProfile->inputs) {
        QList<uint16_t> readValues;
        for (const auto value : input.readValues)
          readValues.append(static_cast<uint16_t>(value));
        m_inputSources.append(
            {static_cast<uint16_t>(input.writeValue), input.label, input.id, std::move(readValues)}
        );
      }
      m_inputSourceMessage = tr("Matched monitor profile %1 from database %2.")
                                 .arg(m_matchedProfile->id, m_profileDatabase.databaseVersion());
    } else {
      const auto result = m_controller->discoverInputSources(monitorId);
      if (result.succeeded()) {
        m_inputSources = result.sources;
        m_inputSourceMessage = result.message;
      } else {
        m_inputSourceMessage =
            result.message.isEmpty() ? tr("The monitor input list could not be read.") : result.message;
      }
    }
  }

  const bool wasLoading = m_loading;
  m_loading = true;
  for (int row = 0; row < m_routeTable->rowCount(); ++row) {
    auto *inputCombo = qobject_cast<QComboBox *>(m_routeTable->cellWidget(row, InputColumn));
    if (!inputCombo)
      continue;
    const auto previousData = inputCombo->currentData();
    int previousValue = previousData.isValid() ? previousData.toInt() : -1;
    QString previousLabel = inputCombo->currentText();
    QString previousInputId = inputCombo->currentData(kInputIdRole).toString();
    const auto computerName = m_routeTable->item(row, ComputerColumn)->text();
    if (previousValue < 0) {
      if (const auto existing = m_config.routeForComputer(computerName)) {
        previousValue = existing->inputValue;
        previousLabel = existing->inputLabel;
        previousInputId = existing->inputId;
      }
    }

    inputCombo->clear();
    inputCombo->addItem(tr("Select a monitor input"), -1);
    for (const auto &source : std::as_const(m_inputSources)) {
      inputCombo->addItem(source.displayName(), source.value);
      const auto index = inputCombo->count() - 1;
      inputCombo->setItemData(index, source.id, kInputIdRole);
      inputCombo->setItemData(index, readValuesVariant(source.readValues), kInputReadValuesRole);
      inputCombo->setItemData(index, source.name, kInputLabelRole);
    }
    int selectedIndex = previousInputId.isEmpty() ? -1 : inputCombo->findData(previousInputId, kInputIdRole);
    if (selectedIndex < 0)
      selectedIndex = inputCombo->findData(previousValue);
    if (selectedIndex < 0 && previousValue >= 0 && !m_matchedProfile) {
      const auto label = previousLabel.isEmpty() ? tr("Saved monitor input [DDC %1]").arg(previousValue)
                                                 : tr("%1 (not reported by monitor)").arg(previousLabel);
      inputCombo->addItem(label, previousValue);
      selectedIndex = inputCombo->count() - 1;
      inputCombo->setItemData(selectedIndex, previousInputId, kInputIdRole);
      inputCombo->setItemData(selectedIndex, previousLabel, kInputLabelRole);
    }
    inputCombo->setCurrentIndex(std::max(0, selectedIndex));
  }
  m_loading = wasLoading;
  updateStatus();
}

void MonitorSwitchingDialog::importProfileDatabase()
{
  const auto path = QFileDialog::getOpenFileName(
      this, tr("Import monitor profile database"), {}, tr("JSON files (*.json);;All files (*)")
  );
  if (path.isEmpty())
    return;

  QString error;
  if (!MonitorProfileDatabase::installUserDatabase(path, &error)) {
    QMessageBox::critical(
        this, tr("Monitor profile database"), tr("The database could not be imported: %1").arg(error)
    );
    return;
  }
  m_profileDatabase = MonitorProfileDatabase::loadActive(&m_profileDatabaseError);
  refreshInputSources();
  saveDisabledConfiguration();
  QMessageBox::information(
      this, tr("Monitor profile database"),
      tr("Database %1 was imported. Review the monitor input assignments before enabling switching.")
          .arg(m_profileDatabase.databaseVersion())
  );
}

void MonitorSwitchingDialog::monitorSelectionChanged()
{
  if (m_loading)
    return;
  refreshInputSources();
  saveDisabledConfiguration();
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

    auto *inputCombo = new QComboBox(m_routeTable);
    inputCombo->addItem(tr("Select a monitor input"), -1);
    m_routeTable->setCellWidget(row, InputColumn, inputCombo);
    auto *statusItem = new QTableWidgetItem(tr("Not tested"));
    statusItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    m_routeTable->setItem(row, StatusColumn, statusItem);

    if (const auto existing = m_config.routeForComputer(screen.name())) {
      useItem->setCheckState(Qt::Checked);
      if (existing->inputValue >= 0)
        inputCombo->addItem(existing->inputLabel, existing->inputValue);
      inputCombo->setCurrentIndex(existing->inputValue >= 0 ? 1 : 0);
      updateRouteStatus(*existing);
    }
    connect(inputCombo, &QComboBox::currentIndexChanged, this, &MonitorSwitchingDialog::configurationEdited);
  }
  m_routeTable->resizeColumnToContents(UseColumn);
  m_routeTable->resizeColumnToContents(RoleColumn);
}

MonitorSwitchingConfig MonitorSwitchingDialog::configFromUi() const
{
  MonitorSwitchingConfig config;
  config.monitorId = m_monitorCombo->currentData(kMonitorIdRole).toString();
  config.monitorName = m_monitorCombo->currentData(kMonitorNameRole).toString();
  config.monitorManufacturerId = m_monitorCombo->currentData(kMonitorManufacturerRole).toString();
  const auto productId = m_monitorCombo->currentData(kMonitorProductRole);
  config.monitorProductId = productId.isValid() ? productId.toInt() : -1;
  config.monitorModelName = m_monitorCombo->currentData(kMonitorModelRole).toString();
  if (m_matchedProfile) {
    config.profileId = m_matchedProfile->id;
    config.profileRevision = m_matchedProfile->revision;
  }

  for (int row = 0; row < m_routeTable->rowCount(); ++row) {
    if (m_routeTable->item(row, UseColumn)->checkState() != Qt::Checked)
      continue;
    MonitorInputRoute route;
    route.computerName = m_routeTable->item(row, ComputerColumn)->text();
    const auto *inputCombo = qobject_cast<QComboBox *>(m_routeTable->cellWidget(row, InputColumn));
    route.inputId = inputCombo->currentData(kInputIdRole).toString();
    route.inputLabel = inputCombo->currentData(kInputLabelRole).toString().trimmed();
    if (route.inputLabel.isEmpty())
      route.inputLabel = inputCombo->currentText().trimmed();
    const auto inputValue = inputCombo->currentData();
    route.inputValue = inputValue.isValid() ? inputValue.toInt() : -1;
    for (const auto &value : inputCombo->currentData(kInputReadValuesRole).toList())
      route.expectedReadValues.append(value.toInt());
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

bool MonitorSwitchingDialog::testRoute(
    MonitorSwitchingConfig &config, MonitorInputRoute &route, const MonitorInputRoute &serverRoute
)
{
  route.lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  if (!m_connectedClients.contains(route.computerName)) {
    route.lastTestStatus = QStringLiteral("failed");
    route.lastTestMessage = tr("The client disconnected before testing began.");
    m_testError = route.lastTestMessage;
    return false;
  }
  const auto writeResult = m_controller->writeInput(config.monitorId, static_cast<uint16_t>(route.inputValue));
  bool completedPreview = false;
  DisplayInputResult readResult{DisplayInputStatus::ReadFailure, tr("DDC readback was unavailable."), {}};
  if (writeResult.succeeded()) {
    const bool stabilized = waitWithProgress(
        tr("Testing %1 for %2. Watch for the expected computer.").arg(route.inputLabel, route.computerName), 2000
    );
    if (stabilized)
      readResult = m_controller->readInput(config.monitorId);
    completedPreview = stabilized && waitWithProgress(tr("Keeping the assigned input visible."), 3000);
  }

  if (!restoreServerInput(config.monitorId, serverRoute, route)) {
    m_testError = route.lastTestMessage;
    return false;
  }
  if (!writeResult.succeeded()) {
    route.lastTestStatus = QStringLiteral("failed");
    route.lastTestMessage = resultText(writeResult);
    m_testError = route.lastTestMessage;
    return false;
  }
  if (!completedPreview) {
    route.lastTestStatus = QStringLiteral("failed");
    route.lastTestMessage = tr("The assignment test was canceled.");
    m_testError = route.lastTestMessage;
    return false;
  }
  if (!m_connectedClients.contains(route.computerName)) {
    route.lastTestStatus = QStringLiteral("failed");
    route.lastTestMessage = tr("The client disconnected during testing.");
    m_testError = route.lastTestMessage;
    return false;
  }
  if (readResult.succeeded() && (!readResult.value || *readResult.value != route.inputValue)) {
    route.lastTestStatus = QStringLiteral("failed");
    route.lastTestMessage = tr("The monitor readback did not match the assigned input.");
    m_testError = route.lastTestMessage;
    return false;
  }
  if (!readResult.succeeded() && readResult.status != DisplayInputStatus::ReadUnsupported &&
      readResult.status != DisplayInputStatus::ReadFailure) {
    route.lastTestStatus = QStringLiteral("failed");
    route.lastTestMessage = resultText(readResult);
    m_testError = route.lastTestMessage;
    return false;
  }

  const bool confirmed =
      m_confirmInput ? m_confirmInput(route)
                     : QMessageBox::question(
                           this, tr("Confirm monitor assignment"),
                           tr("Did %1 appear while %2 was being tested?").arg(route.computerName, route.inputLabel),
                           QMessageBox::Yes | QMessageBox::No, QMessageBox::No
                       ) == QMessageBox::Yes;
  if (!confirmed) {
    route.lastTestStatus = QStringLiteral("failed");
    route.lastTestMessage = tr("The assigned computer was not visually confirmed.");
    m_testError = route.lastTestMessage;
    return false;
  }

  route.lastTestStatus = readResult.succeeded() ? QStringLiteral("verified") : QStringLiteral("verified visually");
  route.lastTestMessage = readResult.succeeded() ? tr("Verified by DDC readback and visual confirmation.")
                                                 : tr("Verified visually because DDC readback was unavailable.");
  return true;
}

bool MonitorSwitchingDialog::testAssignments(MonitorSwitchingConfig &config)
{
  m_testError.clear();
  const auto serverName = m_serverConfig.getServerName();
  auto serverRoute = std::ranges::find(config.routes, serverName, &MonitorInputRoute::computerName);
  Q_ASSERT(serverRoute != config.routes.end());

  serverRoute->lastTestedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
  const auto currentInput = m_controller->readInput(config.monitorId);
  if (!currentInput.succeeded() || !currentInput.value) {
    serverRoute->lastTestStatus = QStringLiteral("failed");
    serverRoute->lastTestMessage = resultText(currentInput);
    m_testError = tr("Teleport could not read the current server input, so it cannot safely run the tests.");
    return false;
  }
  if (*currentInput.value != serverRoute->inputValue) {
    serverRoute->lastTestStatus = QStringLiteral("failed");
    serverRoute->lastTestMessage = tr("The selected server input does not match the monitor's current input.");
    m_testError = tr("Select the monitor input currently showing the server, then run the tests again.");
    return false;
  }
  serverRoute->lastTestStatus = QStringLiteral("verified");
  serverRoute->lastTestMessage = tr("Verified as the current server input with DDC readback.");
  const auto recoveryRoute = *serverRoute;

  for (auto &route : config.routes) {
    if (route.computerName == serverName)
      continue;
    if (!testRoute(config, route, recoveryRoute))
      return false;
  }
  return true;
}

void MonitorSwitchingDialog::runTestsAndEnable()
{
  auto config = configFromUi();
  const auto serverName = m_serverConfig.getServerName();
  const auto validationError = config.validate(serverName, configuredComputerNames());
  if (!validationError.isEmpty()) {
    QMessageBox::warning(this, tr("Monitor switching setup"), validationError);
    return;
  }

  if (m_matchedProfile) {
    const auto activatedAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    for (auto &route : config.routes) {
      route.lastTestStatus = QStringLiteral("database profile");
      route.lastTestMessage = tr("Enabled from bundled or imported monitor profile %1 without DDC testing.")
                                  .arg(m_matchedProfile->id);
      route.lastTestedAt = activatedAt;
    }
    config.activeConfigHash = config.configurationHash();
    config.enabled = true;
    QString error;
    if (!config.save(&error)) {
      QMessageBox::critical(this, tr("Monitor switching setup"), tr("Could not save the configuration: %1").arg(error));
      return;
    }
    m_config = config;
    m_loading = true;
    populateComputers();
    m_loading = false;
    refreshInputSources();
    updateStatus();
    QMessageBox::information(
        this, tr("Monitor switching enabled"),
        tr("Monitor switching was enabled using profile %1. No DDC test commands were sent.")
            .arg(m_matchedProfile->id)
    );
    Q_EMIT configurationEnabled();
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
  const bool passed = testAssignments(config);

  m_config = config;
  if (passed) {
    m_config.activeConfigHash = m_config.configurationHash();
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
        this, tr("Monitor switching enabled"), tr("All selected monitor input assignments were verified.")
    );
    Q_EMIT configurationEnabled();
  } else {
    QMessageBox::warning(
        this, tr("Monitor switching not enabled"),
        m_testError.isEmpty() ? tr("At least one monitor input assignment failed testing.") : m_testError
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
  if (m_config.enabled && m_config.isActiveConfigurationValid()) {
    m_statusLabel->setText(
        m_config.profileId.isEmpty()
            ? tr("Monitor switching is enabled with tested input assignments.")
            : tr("Monitor switching is enabled using monitor profile %1.").arg(m_config.profileId)
    );
    m_testButton->setText(m_matchedProfile ? tr("Enable monitor switching") : tr("Test assignments again"));
    m_disableButton->setEnabled(true);
  } else {
    auto status = m_matchedProfile
                      ? tr("Automatic monitor switching is off. Assign an input to each computer, then enable it.")
                      : tr("Automatic monitor switching is off. Assign and test a monitor input for each computer.");
    if (!m_discoveryError.isEmpty())
      status.append(tr(" Monitor discovery failed: %1").arg(m_discoveryError));
    if (!m_inputSourceMessage.isEmpty())
      status.append(tr(" %1").arg(m_inputSourceMessage));
    if (!m_profileDatabaseError.isEmpty())
      status.append(tr(" Profile database warning: %1").arg(m_profileDatabaseError));
    m_statusLabel->setText(status);
    m_testButton->setText(m_matchedProfile ? tr("Enable monitor switching") : tr("Test assignments and enable"));
    m_disableButton->setEnabled(false);
  }
}
