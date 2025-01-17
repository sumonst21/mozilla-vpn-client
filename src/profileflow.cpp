/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "leakdetector.h"
#include "logger.h"
#include "mozillavpn.h"
#include "profileflow.h"
#include "models/subscriptiondata.h"
#include "tasks/getsubscriptiondetails/taskgetsubscriptiondetails.h"
#include "taskscheduler.h"
#include "telemetry/gleansample.h"

#include <QJsonDocument>
#include <QJsonObject>

namespace {
Logger logger(LOG_MODEL, "ProfileFlow");
}

ProfileFlow::ProfileFlow() { MVPN_COUNT_CTOR(ProfileFlow); }

ProfileFlow::~ProfileFlow() { MVPN_COUNT_DTOR(ProfileFlow); }

void ProfileFlow::setState(State state) {
  logger.debug() << "Set state" << state;

  if (state == StateError) {
    ErrorHandler::instance()->errorHandle(ErrorHandler::RemoteServiceError);
  }

  m_state = state;
  emit stateChanged(m_state);

  emit MozillaVPN::instance()->recordGleanEventWithExtraKeys(
      GleanSample::profileFlowStateChanged,
      {{"state", QVariant::fromValue(state).toString()}});
}

// Only used for testing and debugging the re-authentication flow
void ProfileFlow::setForceReauthFlow(bool forceReauthFlow) {
  logger.debug() << "Set force re-authentication:" << forceReauthFlow;
  m_forceReauthFlow = forceReauthFlow;
}

void ProfileFlow::start() {
  logger.debug() << "Start profile flow";

  reset();

  setState(StateLoading);

  TaskGetSubscriptionDetails* task = new TaskGetSubscriptionDetails(
      m_forceReauthFlow
          ? TaskGetSubscriptionDetails::ForceAuthenticationFlow
          : TaskGetSubscriptionDetails::RunAuthenticationFlowIfNeeded,
      ErrorHandler::PropagateError);

  connect(task, &TaskGetSubscriptionDetails::needsAuthentication, this,
          [this, task] {
            if (task == m_currentTask || m_forceReauthFlow) {
              logger.debug() << "Needs authentication";
              setState(StateAuthenticationNeeded);

              // Reset forcing the re-auth flow
              setForceReauthFlow(false);
            }
          });

  connect(task, &TaskGetSubscriptionDetails::operationCompleted, this,
          [this, task](bool status) {
            if (task != m_currentTask) {
              return;
            }

            m_currentTask = nullptr;
            setState(status ? StateReady : StateError);
          });

  connect(MozillaVPN::instance(), &MozillaVPN::stateChanged, this, [this]() {
    MozillaVPN* vpn = MozillaVPN::instance();
    Q_ASSERT(vpn);
    if (vpn->state() != MozillaVPN::StateMain) {
      reset();
    }
  });

  TaskScheduler::scheduleTask(task);
  m_currentTask = task;
}

void ProfileFlow::reset() {
  logger.debug() << "Reset profile flow";

  if (m_state != StateInitial) {
    MozillaVPN* vpn = MozillaVPN::instance();
    Q_ASSERT(vpn);
    vpn->cancelReauthentication();

    m_currentTask = nullptr;
    setState(StateInitial);
  }
}
