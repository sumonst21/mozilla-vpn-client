/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "servercountrymodel.h"
#include "collator.h"
#include "leakdetector.h"
#include "logger.h"
#include "models/feature.h"
#include "servercountry.h"
#include "serverdata.h"
#include "serveri18n.h"
#include "settingsholder.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>

namespace {
Logger logger(LOG_MODEL, "ServerCountryModel");
}

ServerCountryModel::ServerCountryModel() {
  MVPN_COUNT_CTOR(ServerCountryModel);
}

ServerCountryModel::~ServerCountryModel() {
  MVPN_COUNT_DTOR(ServerCountryModel);
}

bool ServerCountryModel::fromSettings() {
  SettingsHolder* settingsHolder = SettingsHolder::instance();
  Q_ASSERT(settingsHolder);

  logger.debug() << "Reading the server list from settings";

  const QByteArray json = settingsHolder->servers();
  if (json.isEmpty() || !fromJsonInternal(json)) {
    return false;
  }

  m_rawJson = json;
  return true;
}

bool ServerCountryModel::fromJson(const QByteArray& s) {
  logger.debug() << "Reading from JSON";

  if (!s.isEmpty() && m_rawJson == s) {
    logger.debug() << "Nothing has changed";
    return true;
  }

  if (!fromJsonInternal(s)) {
    return false;
  }

  m_rawJson = s;
  emit changed();
  return true;
}

bool ServerCountryModel::fromJsonInternal(const QByteArray& s) {
  beginResetModel();

  m_rawJson = "";
  m_countries.clear();
  m_servers.clear();

  QJsonDocument doc = QJsonDocument::fromJson(s);
  if (!doc.isObject()) {
    return false;
  }

  QJsonObject obj = doc.object();

  QJsonValue countries = obj.value("countries");
  if (!countries.isArray()) {
    return false;
  }

  QJsonArray countriesArray = countries.toArray();
  for (const QJsonValue& countryValue : countriesArray) {
    if (!countryValue.isObject()) {
      return false;
    }

    QJsonObject countryObj = countryValue.toObject();

    ServerCountry country;
    if (!country.fromJson(countryObj)) {
      return false;
    }

    if (country.cities().isEmpty()) {
      continue;
    }

    m_countries.append(country);

    QJsonValue cities = countryObj.value("cities");
    if (!cities.isArray()) {
      return false;
    }

    QJsonArray cityArray = cities.toArray();
    for (const QJsonValue& cityValue : cityArray) {
      if (!cityValue.isObject()) {
        return false;
      }
      QJsonObject cityObj = cityValue.toObject();
      QJsonValue servers = cityObj.value("servers");
      if (!servers.isArray()) {
        return false;
      }

      QJsonArray serverArray = servers.toArray();
      for (const QJsonValue& serverValue : serverArray) {
        Server server;
        if (!server.fromJson(serverValue.toObject())) {
          return false;
        }
        m_servers[server.publicKey()] = server;
      }
    }
  }

  sortCountries();

  endResetModel();

  return true;
}

QHash<int, QByteArray> ServerCountryModel::roleNames() const {
  QHash<int, QByteArray> roles;
  roles[NameRole] = "name";
  roles[LocalizedNameRole] = "localizedName";
  roles[CodeRole] = "code";
  roles[CitiesRole] = "cities";
  return roles;
}

QVariant ServerCountryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() >= m_countries.length()) {
    return QVariant();
  }

  switch (role) {
    case NameRole:
      return QVariant(m_countries.at(index.row()).name());

    case LocalizedNameRole: {
      const ServerCountry& country = m_countries.at(index.row());
      return QVariant(
          ServerI18N::translateCountryName(country.code(), country.name()));
    }

    case CodeRole:
      return QVariant(m_countries.at(index.row()).code());

    case CitiesRole: {
      const ServerCountry& country = m_countries.at(index.row());

      QList<QVariant> list;
      for (const ServerCity& city : country.cities()) {
        list.append(QVariant::fromValue(&city));
      }

      return QVariant(list);
    }

    default:
      return QVariant();
  }
}

int ServerCountryModel::cityConnectionScore(const QString& countryCode,
                                            const QString& cityCode) const {
  for (const ServerCountry& country : m_countries) {
    if (country.code() != countryCode) {
      continue;
    }

    for (const ServerCity& city : country.cities()) {
      if (city.code() != cityCode) {
        continue;
      }

      return cityConnectionScore(city);
    }

    // No such city was found.
    return NoData;
  }

  // No such country was found.
  return NoData;
}

int ServerCountryModel::cityConnectionScore(const ServerCity& city) const {
  qint64 now = QDateTime::currentSecsSinceEpoch();
  int score = Poor;
  int activeServerCount = 0;
  uint32_t sumLatencyMsec = 0;
  for (const QString& pubkey : city.servers()) {
    const Server& server = m_servers[pubkey];
    if (server.cooldownTimeout() <= now) {
      sumLatencyMsec += server.latency();
      activeServerCount++;
    }
  }

  // Ensure there is at least one reachable server.
  if (activeServerCount == 0) {
    return Unavailable;
  }

  // If the feature is disabled, we have no data to return.
  if (!Feature::get(Feature::Feature_serverConnectionScore)->isSupported()) {
    return NoData;
  }
  // In the unlikely event that the sum of the latencies is zero, then we
  // haven't actually measured anything and have nothing to report.
  if (sumLatencyMsec == 0) {
    return NoData;
  }

  // Increase the score if the location has less than 100ms of latency.
  if ((sumLatencyMsec / activeServerCount) < 100) {
    score++;
  }

  // Increase the score if the location has 6 or more servers.
  if (activeServerCount >= 6) {
    score++;
  }

  if (score > Good) {
    score = Good;
  }
  return score;
}

bool ServerCountryModel::pickIfExists(const QString& countryCode,
                                      const QString& cityCode,
                                      ServerData& data) const {
  logger.debug() << "Checking if a server exists"
                 << logger.sensitive(countryCode) << logger.sensitive(cityCode);

  for (const ServerCountry& country : m_countries) {
    if (country.code() == countryCode) {
      for (const ServerCity& city : country.cities()) {
        if (city.code() == cityCode) {
          data.update(country.code(), city.name());
          return true;
        }
      }
      break;
    }
  }

  return false;
}

QStringList ServerCountryModel::pickRandom() {
  logger.debug() << "Choosing a random server";

  QStringList serverTuple;

  quint32 countryId =
      QRandomGenerator::global()->generate() % m_countries.length();
  const ServerCountry& country = m_countries[countryId];

  quint32 cityId =
      QRandomGenerator::global()->generate() % country.cities().length();
  const ServerCity& city = country.cities().at(cityId);

  serverTuple.append(country.code());
  serverTuple.append(city.name());
  serverTuple.append(
      ServerI18N::translateCityName(country.code(), city.name()));
  return serverTuple;
}

void ServerCountryModel::pickRandom(ServerData& data) const {
  logger.debug() << "Choosing a random server";

  quint32 countryId =
      QRandomGenerator::global()->generate() % m_countries.length();
  const ServerCountry& country = m_countries[countryId];

  quint32 cityId =
      QRandomGenerator::global()->generate() % country.cities().length();
  const ServerCity& city = country.cities().at(cityId);

  data.update(country.code(), city.name());
}

bool ServerCountryModel::pickByIPv4Address(const QString& ipv4Address,
                                           ServerData& data) const {
  logger.debug() << "Choosing a server with addres:"
                 << logger.sensitive(ipv4Address);

  for (const ServerCountry& country : m_countries) {
    for (const ServerCity& city : country.cities()) {
      for (const QString& pubkey : city.servers()) {
        const Server server = m_servers.value(pubkey);
        if (server.ipv4AddrIn() == ipv4Address) {
          data.update(country.code(), city.name());
          return true;
        }
      }
    }
  }

  return false;
}

bool ServerCountryModel::exists(ServerData& data) const {
  logger.debug() << "Check if the server is still valid.";
  Q_ASSERT(data.initialized());

  for (const ServerCountry& country : m_countries) {
    if (country.code() == data.exitCountryCode()) {
      for (const ServerCity& city : country.cities()) {
        if (data.exitCityName() == city.name()) {
          return true;
        }
      }

      break;
    }
  }

  return false;
}

const QList<Server> ServerCountryModel::servers(const ServerData& data) const {
  QList<Server> results;

  for (const ServerCountry& country : m_countries) {
    if (country.code() == data.exitCountryCode()) {
      for (const QString& pubkey : country.servers(data)) {
        if (m_servers.contains(pubkey)) {
          results.append(m_servers.value(pubkey));
        }
      }
    }
  }

  return results;
}

const QString ServerCountryModel::countryName(
    const QString& countryCode) const {
  for (const ServerCountry& country : m_countries) {
    if (country.code() == countryCode) {
      return country.name();
    }
  }

  return QString();
}

const QString ServerCountryModel::localizedCountryName(
    const QString& countryCode) const {
  const QString name = countryName(countryCode);
  return ServerI18N::translateCountryName(countryCode, name);
}

QString ServerCountryModel::getLocalizedCountryName(
    const QString& countryCode) {
  return localizedCountryName(countryCode);
}

void ServerCountryModel::retranslate() {
  beginResetModel();
  sortCountries();
  endResetModel();
}

void ServerCountryModel::setServerLatency(const QString& publicKey,
                                          unsigned int msec) {
  if (m_servers.contains(publicKey)) {
    m_servers[publicKey].setLatency(msec);
  }
}

void ServerCountryModel::setServerCooldown(const QString& publicKey,
                                           unsigned int duration) {
  if (m_servers.contains(publicKey)) {
    m_servers[publicKey].setCooldownTimeout(duration);
  }
}

void ServerCountryModel::setCooldownForAllServersInACity(
    const QString& countryCode, const QString& cityCode,
    unsigned int duration) {
  logger.debug() << "Set cooldown for all servers for: "
                 << logger.sensitive(countryCode) << logger.sensitive(cityCode);

  for (const ServerCountry& country : m_countries) {
    if (country.code() == countryCode) {
      for (const ServerCity& city : country.cities()) {
        if (city.code() == cityCode) {
          for (const QString& pubkey : city.servers()) {
            setServerCooldown(pubkey, duration);
          }
          break;
        }
      }
      break;
    }
  }
}

namespace {

bool sortCountryCallback(const ServerCountry& a, const ServerCountry& b,
                         Collator* collator) {
  Q_ASSERT(collator);
  return collator->compare(
             ServerI18N::translateCountryName(a.code(), a.name()),
             ServerI18N::translateCountryName(b.code(), b.name())) < 0;
}

}  // anonymous namespace

void ServerCountryModel::sortCountries() {
  Collator collator;
  std::sort(m_countries.begin(), m_countries.end(),
            std::bind(sortCountryCallback, std::placeholders::_1,
                      std::placeholders::_2, &collator));

  for (ServerCountry& country : m_countries) {
    country.sortCities();
  }
}
