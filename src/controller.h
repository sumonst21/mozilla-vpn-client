/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef CONTROLLER_H
#define CONTROLLER_H

#include "models/server.h"
#include "ipaddress.h"
#include "pinghelper.h"

#include <QElapsedTimer>
#include <QHostAddress>
#include <QList>
#include <QObject>
#include <QTimer>
#include <QDateTime>

#include <functional>

class ControllerImpl;
class MozillaVPN;

class HopConnection {
 public:
  HopConnection() {}

  Server m_server;
  int m_hopindex = 0;
  QList<IPAddress> m_allowedIPAddressRanges;
  QStringList m_excludedAddresses;
  QStringList m_vpnDisabledApps;
  QHostAddress m_dnsServer;
};

class Controller final : public QObject {
  Q_OBJECT
  Q_DISABLE_COPY_MOVE(Controller)

 public:
  enum State {
    StateInitializing,
    StateOff,
    StateConnecting,
    StateConfirming,
    StateOn,
    StateDisconnecting,
    StateSwitching,
  };
  Q_ENUM(State)

 private:
  Q_PROPERTY(State state READ state NOTIFY stateChanged)
  Q_PROPERTY(qint64 time READ time NOTIFY timeChanged)
  Q_PROPERTY(QString currentLocalizedCityName READ currentLocalizedCityName
                 NOTIFY stateChanged)
  Q_PROPERTY(QString switchingLocalizedCityName READ switchingLocalizedCityName
                 NOTIFY stateChanged)
  Q_PROPERTY(
      int connectionRetry READ connectionRetry NOTIFY connectionRetryChanged);
  Q_PROPERTY(bool enableDisconnectInConfirming READ enableDisconnectInConfirming
                 NOTIFY enableDisconnectInConfirmingChanged);

 public:
  Controller();
  ~Controller();

  void initialize();

  State state() const;

  Q_INVOKABLE void changeServer(const QString& countryCode, const QString& city,
                                const QString& entryCountryCode = QString(),
                                const QString& entryCity = QString());

  Q_INVOKABLE void logout();

  qint64 time() const;

  QString currentLocalizedCityName() const;

  QString switchingLocalizedCityName() const;

  bool silentSwitchServers();

  void updateRequired();

  void getBackendLogs(std::function<void(const QString& logs)>&& callback);

  void cleanupBackendLogs();

  void getStatus(
      std::function<void(const QString& serverIpv4Gateway,
                         const QString& deviceIpv4Address, uint64_t txBytes,
                         uint64_t rxBytes)>&& callback);

  int connectionRetry() const { return m_connectionRetry; }

  bool enableDisconnectInConfirming() const {
    return m_enableDisconnectInConfirming;
  }

  void backendFailure();
  void serverUnavailable();
  void setCooldownForAllServersInACity(const QString& countryCode,
                                       const QString& cityCode);

  void captivePortalPresent();
  void captivePortalGone();

 public slots:
  // These 2 methods activate/deactivate the VPN. Return true if a signal will
  // be emitted at the end of the operation.
  bool activate();
  bool deactivate();

  Q_INVOKABLE void quit();

 private slots:
  void connected(const QString& pubkey);
  void disconnected();
  void timerTimeout();
  void implInitialized(bool status, bool connected,
                       const QDateTime& connectionDate);
  void statusUpdated(const QString& serverIpv4Gateway,
                     const QString& deviceIpv4Address, uint64_t txBytes,
                     uint64_t rxBytes);
  void handshakeTimeout();

 signals:
  void stateChanged();
  void timeChanged();
  void readyToQuit();
  void readyToUpdate();
  void readyToBackendFailure();
  void readyToServerUnavailable(bool pingReceived);
  void connectionRetryChanged();
  void enableDisconnectInConfirmingChanged();
  void silentSwitchDone();
  void activationBlockedForCaptivePortal();
  void handshakeFailed(const QString& serverHostname);

 private:
  void setState(State state);

  void maybeEnableDisconnectInConfirming();

  bool processNextStep();
  QList<IPAddress> getAllowedIPAddressRanges(const Server& server);
  QStringList getExcludedAddresses(const Server& server);

  void activateInternal(bool forceDNSPort = false);
  void activateNext();

  void clearRetryCounter();
  void clearConnectedTime();
  void resetConnectedTime();

 private:
  State m_state = StateInitializing;

  QTimer m_timer;

  bool m_portalDetected = false;

  QDateTime m_connectedTimeInUTC;

  PingHelper m_ping_canary;
  bool m_ping_received = false;

  QScopedPointer<ControllerImpl> m_impl;

  QString m_currentCountryCode;
  QString m_currentCity;

  QString m_switchingExitCountry;
  QString m_switchingExitCity;
  QString m_switchingEntryCountry;
  QString m_switchingEntryCity;

  QTimer m_connectingTimer;
  QTimer m_handshakeTimer;
  bool m_enableDisconnectInConfirming = false;

  enum NextStep {
    None,
    Quit,
    Update,
    Disconnect,
    BackendFailure,
    ServerUnavailable,
  };

  NextStep m_nextStep = None;

  int m_connectionRetry = 0;

  QList<HopConnection> m_activationQueue;

  QList<std::function<void(const QString& serverIpv4Gateway,
                           const QString& deviceIpv4Address, uint64_t txBytes,
                           uint64_t rxBytes)>>
      m_getStatusCallbacks;
};

#endif  // CONTROLLER_H
