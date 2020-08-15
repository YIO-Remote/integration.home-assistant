/******************************************************************************
 *
 # Copyright (C) 2020 Markus Zehnder <business@markuszehnder.ch>
 * Copyright (C) 2019 Marton Borzak <hello@martonborzak.com>
 * Copyright (C) 2019 Christian Riedl <ric@rts.co.at>
 *
 * This file is part of the YIO-Remote software project.
 *
 * YIO-Remote software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * YIO-Remote software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with YIO-Remote software. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *****************************************************************************/

#include "homeassistant.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QtDebug>

#include "homeassistant_supportedfeatures.h"
#include "math.h"
#include "yio-interface/entities/blindinterface.h"
#include "yio-interface/entities/climateinterface.h"
#include "yio-interface/entities/lightinterface.h"
#include "yio-interface/entities/mediaplayerinterface.h"
#include "yio-interface/entities/switchinterface.h"
#include "yio-interface/entities/remoteinterface.h"

HomeAssistantPlugin::HomeAssistantPlugin() : Plugin("homeassistant", USE_WORKER_THREAD) {}

Integration *HomeAssistantPlugin::createIntegration(const QVariantMap &config, EntitiesInterface *entities,
                                                    NotificationsInterface *notifications, YioAPIInterface *api,
                                                    ConfigInterface *configObj) {
    qCInfo(m_logCategory) << "Creating HomeAssistant integration plugin" << PLUGIN_VERSION;

    return new HomeAssistant(config, entities, notifications, api, configObj, this);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// HOME ASSISTANT THREAD CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HomeAssistant::HomeAssistant(const QVariantMap &config, EntitiesInterface *entities,
                             NotificationsInterface *notifications, YioAPIInterface *api, ConfigInterface *configObj,
                             Plugin *plugin)
    : Integration(config, entities, notifications, api, configObj, plugin) {
    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == Integration::OBJ_DATA) {
            QVariantMap map = iter.value().toMap();
            m_ip            = map.value(Integration::KEY_DATA_IP).toString();
            m_token         = map.value(Integration::KEY_DATA_TOKEN).toString();
            m_ssl           = map.value(Integration::KEY_DATA_SSL).toBool();
            m_ignoreSsl     = map.value(Integration::KEY_DATA_SSL_IGNORE).toBool();
            m_url           = QString(m_ssl ? "wss://" : "ws://").append(m_ip).append("/api/websocket");
        }
    }

    qRegisterMetaType<QAbstractSocket::SocketState>();

    // FIXME magic number
    m_webSocketId = 4;

    m_wsReconnectTimer = new QTimer(this);
    m_wsReconnectTimer->setSingleShot(true);
    m_wsReconnectTimer->setInterval(2000);
    m_wsReconnectTimer->stop();

    m_webSocket = new QWebSocket;
    m_webSocket->setParent(this);

    QObject::connect(m_webSocket, &QWebSocket::textMessageReceived, this, &HomeAssistant::onTextMessageReceived);
    QObject::connect(m_webSocket, static_cast<void (QWebSocket::*)(QAbstractSocket::SocketError)>(&QWebSocket::error),
                     this, &HomeAssistant::onError);
    QObject::connect(m_webSocket, &QWebSocket::stateChanged, this, &HomeAssistant::onStateChanged);
    QObject::connect(m_webSocket, &QWebSocket::sslErrors, this, &HomeAssistant::onSslError);

    QObject::connect(m_wsReconnectTimer, &QTimer::timeout, this, &HomeAssistant::onTimeout);

    // set up timer to check heartbeat
    m_heartbeatTimer->setInterval(m_heartbeatCheckInterval);
    QObject::connect(m_heartbeatTimer, &QTimer::timeout, this, &HomeAssistant::onHeartbeat);

    // set up hearbeat timeout timer
    m_heartbeatTimeoutTimer->setSingleShot(true);
    m_heartbeatTimeoutTimer->setInterval(m_heartbeatCheckInterval / 2);
    QObject::connect(m_heartbeatTimeoutTimer, &QTimer::timeout, this, &HomeAssistant::onHeartbeatTimeout);
}

void HomeAssistant::onTextMessageReceived(const QString &message) {
    QJsonParseError parseerror;
    QJsonDocument   doc = QJsonDocument::fromJson(message.toUtf8(), &parseerror);
    if (parseerror.error != QJsonParseError::NoError) {
        qCCritical(m_logCategory) << "JSON error:" << parseerror.errorString();
        return;
    }
    QVariantMap map = doc.toVariant().toMap();

    QString m = map.value("error").toMap().value("message").toString();
    if (m.length() > 0) {
        qCCritical(m_logCategory) << "Message error:" << m;
    }

    QString type = map.value("type").toString();
    int     id   = map.value("id").toInt();

    if (type == "auth_required") {
        QString auth = QString("{ \"type\": \"auth\", \"access_token\": \"%1\" }\n").arg(m_token);
        m_webSocket->sendTextMessage(auth);
        return;
    }

    if (type == "auth_ok") {
        qCInfo(m_logCategory) << "Authentication successful";
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // FETCH STATES
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        m_webSocket->sendTextMessage("{\"id\": 2, \"type\": \"get_states\"}\n");
    }

    if (type == "auth_invalid") {
        qCCritical(m_logCategory) << "Invalid authentication";
        disconnect();
        // try again after a couple of seconds
        m_wsReconnectTimer->start();
        return;
    }

    // FIXME magic number!
    if (id == 2) {
        QVariantList list = map.value("result").toList();
        for (int i = 0; i < list.length(); i++) {
            QVariantMap result = list.value(i).toMap();

            // append the list of available entities
            QString type = result.value("entity_id").toString().split(".")[0];
            // rename type to match our own naming system
            if (type == "cover") {
                type = "blind";
            } else if (type == "input_boolean") {
                type = "switch";
            }
            // add entity to allAvailableEntities list
            addAvailableEntity(
                result.value("entity_id").toString(), type, integrationId(),
                result.value("attributes").toMap().value("friendly_name").toString(),
                supportedFeatures(type, result.value("attributes").toMap().value("supported_features").toInt()));

            // update the entity
            updateEntity(result.value("entity_id").toString(), result);
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // SUBSCRIBE TO EVENTS IN HOME ASSISTANT
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        m_webSocket->sendTextMessage(
            "{\"id\": 3, \"type\": \"subscribe_events\", \"event_type\": \"state_changed\"}\n");
    }

    // FIXME magic number!
    if (type == "result" && id == 3) {
        setState(CONNECTED);
        qCDebug(m_logCategory) << "Subscribed to state changes";

        // remove notifications that we don't need anymore as the integration is connected
        m_notifications->remove("Cannot connect to Home Assistant.");

        m_heartbeatTimer->start();
    }

    if (id == m_webSocketId) {
        qCDebug(m_logCategory) << "Command successful";
    }

    // FIXME magic number!
    if (type == "event" && id == 3) {
        QVariantMap data     = map.value("event").toMap().value("data").toMap();
        QVariantMap newState = data.value("new_state").toMap();
        updateEntity(data.value("entity_id").toString(), newState);
    }

    // heartbeat
    if (type == "pong") {
        qCDebug(m_logCategory) << "Got heartbeat!";
        m_heartbeatTimeoutTimer->stop();
    }
}

void HomeAssistant::onStateChanged(QAbstractSocket::SocketState state) {
    if (state == QAbstractSocket::UnconnectedState && !m_userDisconnect) {
        qCDebug(m_logCategory) << "State changed to 'Unconnected': starting reconnect";

        // turn off heartbeat
        m_heartbeatTimer->stop();
        m_heartbeatTimeoutTimer->stop();

        if (m_webSocket->isValid()) {
            m_webSocket->close();
        }
        setState(DISCONNECTED);
        m_wsReconnectTimer->start();
    }
}

void HomeAssistant::onError(QAbstractSocket::SocketError error) {
    qCWarning(m_logCategory) << error << m_webSocket->errorString();

    // turn off heartbeat
    m_heartbeatTimer->stop();
    m_heartbeatTimeoutTimer->stop();

    if (m_webSocket->isValid()) {
        m_webSocket->close();
    }
    setState(DISCONNECTED);
    m_wsReconnectTimer->start();
}

void HomeAssistant::onTimeout() {
    if (m_tries == 3) {
        disconnect();

        qCCritical(m_logCategory) << "Cannot connect to Home Assistant: retried 3 times connecting to" << m_ip;
        QObject *param = this;

        m_notifications->add(
            true, tr("Cannot connect to ").append(friendlyName()).append("."), tr("Reconnect"),
            [](QObject *param) {
                Integration *i = qobject_cast<Integration *>(param);
                i->connect();
            },
            param);

        m_tries = 0;
    } else {
        // FIXME magic number
        m_webSocketId = 4;
        if (m_state != CONNECTING) {
            setState(CONNECTING);
        }

        qCDebug(m_logCategory) << "Reconnection attempt" << m_tries + 1 << "to HomeAssistant server:" << m_url;
        m_webSocket->open(QUrl(m_url));

        m_tries++;
    }
}

void HomeAssistant::onSslError(QList<QSslError>) {
    if (m_ignoreSsl) {
        qCDebug(m_logCategory) << "Ignoring SSL errors.";
        m_webSocket->ignoreSslErrors();
    } else {
        qCWarning(m_logCategory) << "SSL certificate error";
        m_notifications->add(
            true,
            tr("SSL certificate validation error. Please check your certificate. ").append(friendlyName()).append("."));
    }
}

void HomeAssistant::webSocketSendCommand(const QString &domain, const QString &service, const QString &entityId,
                                         QVariantMap *data) {
    // sends a command to home assistant
    m_webSocketId++;

    QVariantMap map;
    map.insert("id", QVariant(m_webSocketId));
    map.insert("type", QVariant("call_service"));
    map.insert("domain", QVariant(domain));
    map.insert("service", QVariant(service));

    if (data == nullptr) {
        QVariantMap d;
        d.insert("entity_id", QVariant(entityId));
        map.insert("service_data", d);
    } else {
        data->insert("entity_id", QVariant(entityId));
        map.insert("service_data", *data);
    }
    QJsonDocument doc     = QJsonDocument::fromVariant(map);
    QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
    m_webSocket->sendTextMessage(message);
}

int HomeAssistant::convertBrightnessToPercentage(float value) { return static_cast<int>(round(value / 255 * 100)); }

void HomeAssistant::updateEntity(const QString &entity_id, const QVariantMap &attr) {
    QList<EntityInterface*> entities = m_entities->getByIntegration(integrationId());

    foreach(EntityInterface *entity, entities) {
        if (entity->entity_id().startsWith(entity_id)) {
            if (entity->type() == "light") {
                updateLight(entity, attr);
            }
            if (entity->type() == "blind") {
                updateBlind(entity, attr);
            }
            if (entity->type() == "media_player") {
                updateMediaPlayer(entity, attr);
            }
            if (entity->type() == "climate") {
                updateClimate(entity, attr);
            }
            if (entity->type() == "switch") {
                updateSwitch(entity, attr);
            }
        }
    }
}

void HomeAssistant::updateLight(EntityInterface *entity, const QVariantMap &attr) {
    // state
    if (attr.value("state").toString() == "on") {
        entity->setState(LightDef::ON);
    } else {
        entity->setState(LightDef::OFF);
    }

    QVariantMap haAttr = attr.value("attributes").toMap();
    // brightness
    if (entity->isSupported(LightDef::F_BRIGHTNESS)) {
        if (haAttr.contains("brightness")) {
            entity->updateAttrByIndex(LightDef::BRIGHTNESS,
                                      convertBrightnessToPercentage(haAttr.value("brightness").toInt()));
        } else {
            entity->updateAttrByIndex(LightDef::BRIGHTNESS, 0);
        }
    }

    // color
    if (entity->isSupported(LightDef::F_COLOR)) {
        QVariant     color = haAttr.value("rgb_color");
        QVariantList cl(color.toList());
        char         buffer[10];
        snprintf(buffer, sizeof(buffer), "#%02X%02X%02X", cl.value(0).toInt(), cl.value(1).toInt(),
                 cl.value(2).toInt());
        entity->updateAttrByIndex(LightDef::COLOR, buffer);
    }

    // color temp
    if (entity->isSupported(LightDef::F_COLORTEMP)) {
        // FIXME implement me!
    }
}

void HomeAssistant::updateBlind(EntityInterface *entity, const QVariantMap &attr) {
    QVariantMap attributes;

    // state
    if (attr.value("state").toString() == "open") {
        entity->setState(BlindDef::OPEN);
    } else {
        entity->setState(BlindDef::CLOSED);
    }

    // position
    if (entity->isSupported(BlindDef::F_POSITION)) {
        entity->updateAttrByIndex(BlindDef::POSITION,
                                  100 - attr.value("attributes").toMap().value("current_position").toInt());
    }
}

void HomeAssistant::updateMediaPlayer(EntityInterface *entity, const QVariantMap &attr) {
    // state
    QString state = attr.value("state").toString();
    if (state == "off") {
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::OFF);
    } else if (state == "on") {
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::ON);
    } else if (state == "idle") {
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::IDLE);
    } else if (state == "playing") {
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::PLAYING);
    } else {
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::OFF);
    }

    QVariantMap haAttr = attr.value("attributes").toMap();
    // source
    if (entity->isSupported(MediaPlayerDef::F_SOURCE) && haAttr.contains("source")) {
        entity->updateAttrByIndex(MediaPlayerDef::SOURCE, haAttr.value("source").toString());
    }

    // volume
    if (entity->isSupported(MediaPlayerDef::F_VOLUME_SET) && haAttr.contains("volume_level")) {
        entity->updateAttrByIndex(MediaPlayerDef::VOLUME,
                                  static_cast<int>(round(haAttr.value("volume_level").toDouble() * 100)));
    }

    // media type
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_TYPE) && haAttr.contains("media_content_type")) {
        entity->updateAttrByIndex(MediaPlayerDef::MEDIATYPE, haAttr.value("media_content_type").toString());
    }

    // media image
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_IMAGE) && haAttr.contains("entity_picture")) {
        QString url     = haAttr.value("entity_picture").toString();
        QString fullUrl = "";
        if (url.contains("http")) {
            fullUrl = url;
        } else {
            fullUrl = QString("http://").append(m_ip).append(url);
        }
        entity->updateAttrByIndex(MediaPlayerDef::MEDIAIMAGE, fullUrl);
    }

    // media title
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_TITLE) && haAttr.contains("media_title")) {
        entity->updateAttrByIndex(MediaPlayerDef::MEDIATITLE, haAttr.value("media_title").toString());
    }

    // media artist
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_ARTIST) && haAttr.contains("media_artist")) {
        entity->updateAttrByIndex(MediaPlayerDef::MEDIAARTIST, haAttr.value("media_artist").toString());
    }
}

void HomeAssistant::updateClimate(EntityInterface *entity, const QVariantMap &attr) {
    // state
    QString state = attr.value("state").toString();
    if (state == "off") {
        entity->setState(ClimateDef::OFF);
    } else if (state == "heat") {
        entity->setState(ClimateDef::HEAT);
    } else if (state == "cool") {
        entity->setState(ClimateDef::COOL);
    }

    // current temperature
    if (entity->isSupported(ClimateDef::F_TEMPERATURE) &&
        attr.value("attributes").toMap().contains("current_temperature")) {
        entity->updateAttrByIndex(ClimateDef::TEMPERATURE,
                                  attr.value("attributes").toMap().value("current_temperature").toDouble());
    }

    // target temperature
    if (entity->isSupported(ClimateDef::F_TARGET_TEMPERATURE) &&
        attr.value("attributes").toMap().contains("temperature")) {
        entity->updateAttrByIndex(ClimateDef::TARGET_TEMPERATURE,
                                  attr.value("attributes").toMap().value("temperature").toDouble());
    }

    // max and min temperatures
    if (entity->isSupported(ClimateDef::F_TEMPERATURE_MAX) && attr.value("attributes").toMap().contains("max_temp")) {
        entity->updateAttrByIndex(ClimateDef::TEMPERATURE_MAX,
                                  attr.value("attributes").toMap().value("max_temp").toDouble());
    }

    if (entity->isSupported(ClimateDef::F_TEMPERATURE_MIN) && attr.value("attributes").toMap().contains("min_temp")) {
        entity->updateAttrByIndex(ClimateDef::TEMPERATURE_MIN,
                                  attr.value("attributes").toMap().value("min_temp").toDouble());
    }
}

void HomeAssistant::updateSwitch(EntityInterface *entity, const QVariantMap &attr) {
    // state
    if (attr.value("state").toString() == "on") {
        entity->setState(SwitchDef::ON);
    } else {
        entity->setState(SwitchDef::OFF);
    }
}

void HomeAssistant::connect() {
    m_userDisconnect = false;

    setState(CONNECTING);

    // reset the reconnnect trial variable
    m_tries = 0;

    // turn on the websocket connection
    if (m_webSocket->isValid()) {
        m_webSocket->close();
    }

    qCDebug(m_logCategory) << "Connecting to HomeAssistant server:" << m_url;
    m_webSocket->open(QUrl(m_url));
}

void HomeAssistant::disconnect() {
    m_userDisconnect = true;
    qCDebug(m_logCategory) << "Disconnecting from HomeAssistant";

    // turn of the reconnect try
    m_wsReconnectTimer->stop();

    // turn off heartbeat
    m_heartbeatTimer->stop();
    m_heartbeatTimeoutTimer->stop();

    // turn off the socket
    if (m_webSocket->isValid()) {
        m_webSocket->close();
    }

    setState(DISCONNECTED);
}

void HomeAssistant::enterStandby() {
    qCDebug(m_logCategory) << "Entering standby";
    m_heartbeatTimer->stop();
    m_heartbeatTimeoutTimer->stop();
}

void HomeAssistant::leaveStandby() { m_heartbeatTimer->start(); }

void HomeAssistant::sendCommand(const QString &type, const QString &entity_id, int command, const QVariant &param) {
    const QString ha_entity_id = entity_id.left(entity_id.indexOf('+'));

    if (type == "light") {
        if (command == LightDef::C_TOGGLE) {
            webSocketSendCommand(type, "toggle", ha_entity_id, nullptr);
        } else if (command == LightDef::C_ON) {
            webSocketSendCommand(type, "turn_on", ha_entity_id, nullptr);
        } else if (command == LightDef::C_OFF) {
            webSocketSendCommand(type, "turn_off", ha_entity_id, nullptr);
        } else if (command == LightDef::C_BRIGHTNESS) {
            QVariantMap data;
            data.insert("brightness_pct", param);
            webSocketSendCommand(type, "turn_on", ha_entity_id, &data);
        } else if (command == LightDef::C_COLOR) {
            QColor       color = param.value<QColor>();
            QVariantMap  data;
            QVariantList list;
            list.append(color.red());
            list.append(color.green());
            list.append(color.blue());
            data.insert("rgb_color", list);
            webSocketSendCommand(type, "turn_on", ha_entity_id, &data);
        }
    } else if (type == "blind") {
        if (command == BlindDef::C_OPEN) {
            webSocketSendCommand("cover", "open_cover", ha_entity_id, nullptr);
        } else if (command == BlindDef::C_CLOSE) {
            webSocketSendCommand("cover", "close_cover", ha_entity_id, nullptr);
        } else if (command == BlindDef::C_STOP) {
            webSocketSendCommand("cover", "stop_cover", ha_entity_id, nullptr);
        } else if (command == BlindDef::C_POSITION) {
            QVariantMap data;
            data.insert("position", param);
            webSocketSendCommand("cover", "set_cover_position", ha_entity_id, &data);
        }
    } else if (type == "media_player") {
        if (command == MediaPlayerDef::C_VOLUME_SET) {
            QVariantMap data;
            data.insert("volume_level", param.toDouble() / 100);
            webSocketSendCommand(type, "volume_set", ha_entity_id, &data);
        } else if (command == MediaPlayerDef::C_PLAY || command == MediaPlayerDef::C_PAUSE) {
            webSocketSendCommand(type, "media_play_pause", ha_entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_PREVIOUS) {
            webSocketSendCommand(type, "media_previous_track", ha_entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_NEXT) {
            webSocketSendCommand(type, "media_next_track", ha_entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_TURNON) {
            webSocketSendCommand(type, "turn_on", ha_entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_TURNOFF) {
            webSocketSendCommand(type, "turn_off", ha_entity_id, nullptr);
        }
    } else if (type == "climate") {
        if (command == ClimateDef::C_ON) {
            webSocketSendCommand(type, "turn_on", ha_entity_id, nullptr);
        } else if (command == ClimateDef::C_OFF) {
            webSocketSendCommand(type, "turn_off", ha_entity_id, nullptr);
        } else if (command == ClimateDef::C_TARGET_TEMPERATURE) {
            QVariantMap data;
            data.insert("temperature", param.toDouble());
            webSocketSendCommand(type, "set_temperature", ha_entity_id, &data);
        } else if (command == ClimateDef::C_HEAT) {
            QVariantMap data;
            data.insert("hvac_mode", "heat");
            webSocketSendCommand(type, "set_hvac_mode", ha_entity_id, &data);
        } else if (command == ClimateDef::C_COOL) {
            QVariantMap data;
            data.insert("hvac_mode", "cool");
            webSocketSendCommand(type, "set_hvac_mode", ha_entity_id, &data);
        }
    } else if (type == "switch") {
        QString haType = ha_entity_id.split(".")[0];
        if (command == SwitchDef::C_ON) {
            webSocketSendCommand(haType, "turn_on", ha_entity_id, nullptr);
        } else if (command == SwitchDef::C_OFF) {
            webSocketSendCommand(haType, "turn_off", ha_entity_id, nullptr);
        }
    } else if (type == "remote") {
        EntityInterface *entity = m_entities->getEntityInterface(entity_id);
        RemoteInterface *remoteInterface = static_cast<RemoteInterface *>(entity->getSpecificInterface());
        QVariantList commands = remoteInterface->commands();
        QStringList remoteCodes = findRemoteCodes(entity->getCommandName(command), commands);
        QString remoteDevice = findRemoteDevice(entity->getCommandName(command), commands);

        if (remoteCodes.length() > 0) {
            QVariantMap data;

            if (remoteDevice.length() > 0) {
                data.insert("device", remoteDevice);
            }

            data.insert("command", remoteCodes);
            webSocketSendCommand(type, "send_command", ha_entity_id, &data);
        }
    }
}

QString HomeAssistant::findRemoteDevice(const QString &feature, const QVariantList &list) {
    for (int i = 0; i < list.length(); i++) {
        QVariantMap map = list[i].toMap();
        if (map.value("button_map").toString() == feature) {
            return map.value("device").toString();
        }
    }

    return "";
}

QStringList HomeAssistant::findRemoteCodes(const QString &feature, const QVariantList &list) {
    QStringList r;

    for (int i = 0; i < list.length(); i++) {
        QVariantMap map = list[i].toMap();
        if (map.value("button_map").toString() == feature) {
            r += map.value("code").toString().split(',');
        }
    }

    return r;
}

void HomeAssistant::onHeartbeat() {
    qCDebug(m_logCategory) << "Sending hearbeat request";
    m_webSocketId++;
    QString msg = QString("{ \"id\": \"%1\", \"type\": \"ping\" }\n").arg(m_webSocketId);
    if (m_webSocket->isValid()) {
        m_webSocket->sendTextMessage(msg);
    }
    m_heartbeatTimeoutTimer->start();
}

void HomeAssistant::onHeartbeatTimeout() {
    disconnect();

    QObject *param = this;
    m_notifications->add(
        true, tr("Connection lost to ").append(friendlyName()).append("."), tr("Reconnect"),
        [](QObject *param) {
            Integration *i = qobject_cast<Integration *>(param);
            i->connect();
        },
        param);
}

QStringList HomeAssistant::supportedFeatures(const QString &entityType, const int &supportedFeatures) {
    QStringList features;

    if (entityType == "light") {
        if (supportedFeatures & LightFeatures::SUPPORT_BRIGHTNESS) {
            features.append("BRIGHTNESS");
        }
        if (supportedFeatures & LightFeatures::SUPPORT_COLOR) {
            features.append("COLOR");
        }
        if (supportedFeatures & LightFeatures::SUPPORT_COLOR_TEMP) {
            features.append("COLORTEMP");
        }
    } else if (entityType == "blind") {
        if (supportedFeatures & BlindFeatures::SUPPORT_OPEN) {
            features.append("OPEN");
        }
        if (supportedFeatures & BlindFeatures::SUPPORT_CLOSE) {
            features.append("CLOSE");
        }
        if (supportedFeatures & BlindFeatures::SUPPORT_STOP) {
            features.append("STOP");
        }
        if (supportedFeatures & BlindFeatures::SUPPORT_SET_POSITION) {
            features.append("POSITION");
        }
    } else if (entityType == "climate") {
        features.append("TEMPERATURE");
        if (supportedFeatures & ClimateFeatures::SUPPORT_TARGET_TEMPERATURE) {
            features.append("TARGET_TEMPERATURE");
        }
        if (supportedFeatures & ClimateFeatures::SUPPORT_TARGET_TEMPERATURE_RANGE) {
            features.append("TEMPERATURE_MIN");
            features.append("TEMPERATURE_MAX");
        }
    } else if (entityType == "media_player") {
        features.append("APP_NAME");
        features.append("MEDIA_ALBUM");
        features.append("MEDIA_ARTIST");
        features.append("MEDIA_IMAGE");
        features.append("MEDIA_TITLE");
        features.append("MEDIA_TYPE");

        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_PAUSE) {
            features.append("PAUSE");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_SEEK) {
            features.append("SEEK");
            features.append("MEDIA_DURATION");
            features.append("MEDIA_POSITION");
            features.append("MEDIA_PROGRESS");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_VOLUME_SET) {
            features.append("VOLUME_SET");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_VOLUME_MUTE) {
            features.append("MUTE");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_PREVIOUS_TRACK) {
            features.append("PREVIOUS");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_NEXT_TRACK) {
            features.append("NEXT");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_TURN_ON) {
            features.append("TURN_ON");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_TURN_OFF) {
            features.append("TURN_OFF");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_VOLUME_STEP) {
            features.append("VOLUME_DOWN");
            features.append("VOLUME_UP");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_SELECT_SOURCE) {
            features.append("SOURCE");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_STOP) {
            features.append("STOP");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_PLAY) {
            features.append("PLAY");
        }
        if (supportedFeatures & MediaPlayerFeatures::SUPPORT_SHUFFLE_SET) {
            features.append("SHUFFLE");
        }
    }
    return features;
}
