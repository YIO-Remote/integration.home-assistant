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

#include "math.h"
#include "yio-interface/entities/blindinterface.h"
#include "yio-interface/entities/climateinterface.h"
#include "yio-interface/entities/lightinterface.h"
#include "yio-interface/entities/mediaplayerinterface.h"

IntegrationInterface::~IntegrationInterface() {}

void HomeAssistantPlugin::create(const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api,
                                 QObject *configObj) {
    QMap<QObject *, QVariant> returnData;

    QVariantList data;
    QString      mdns;

    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == "mdns") {
            mdns = iter.value().toString();
        } else if (iter.key() == "data") {
            data = iter.value().toList();
        }
    }

    for (int i = 0; i < data.length(); i++) {
        HomeAssistantBase *ha = new HomeAssistantBase(m_log, this);
        ha->setup(data[i].toMap(), entities, notifications, api, configObj);

        QVariantMap d = data[i].toMap();
        d.insert("mdns", mdns);
        d.insert("type", config.value("type").toString());
        returnData.insert(ha, d);
    }

    emit createDone(returnData);
}

HomeAssistantBase::HomeAssistantBase(QLoggingCategory &log, QObject *parent) : m_log(log) { this->setParent(parent); }

HomeAssistantBase::~HomeAssistantBase() {
    if (m_thread.isRunning()) {
        m_thread.exit();
        m_thread.wait(5000);
    }
}

void HomeAssistantBase::setup(const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api,
                              QObject *configObj) {
    Integration::setup(config, entities);

    // crate a new instance and pass on variables
    HomeAssistantThread *HAThread =
        new HomeAssistantThread(config, entities, notifications, api, configObj, this, m_log);

    // move to thread
    HAThread->moveToThread(&m_thread);

    // connect signals and slots
    QObject::connect(&m_thread, &QThread::finished, HAThread, &QObject::deleteLater);

    QObject::connect(this, &HomeAssistantBase::connectSignal, HAThread, &HomeAssistantThread::connect);
    QObject::connect(this, &HomeAssistantBase::disconnectSignal, HAThread, &HomeAssistantThread::disconnect);
    QObject::connect(this, &HomeAssistantBase::sendCommandSignal, HAThread, &HomeAssistantThread::sendCommand);

    QObject::connect(HAThread, &HomeAssistantThread::stateChanged, this, &HomeAssistantBase::stateHandler);

    m_thread.start();
}

void HomeAssistantBase::connect() { emit connectSignal(); }

void HomeAssistantBase::disconnect() { emit disconnectSignal(); }

void HomeAssistantBase::sendCommand(const QString &type, const QString &entity_id, int command, const QVariant &param) {
    emit sendCommandSignal(type, entity_id, command, param);
}

// FIXME use enum
void HomeAssistantBase::stateHandler(int state) {
    if (state == 0) {
        setState(CONNECTED);
    } else if (state == 1) {
        setState(CONNECTING);
    } else if (state == 2) {
        setState(DISCONNECTED);
    }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// HOME ASSISTANT THREAD CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

HomeAssistantThread::HomeAssistantThread(const QVariantMap &config, QObject *entities, QObject *notifications,
                                         QObject *api, QObject *configObj, Integration *baseObj, QLoggingCategory &log)
    : m_log(log) {
    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == "data") {
            QVariantMap map = iter.value().toMap();
            m_ip = map.value("ip").toString();
            m_token = map.value("token").toString();
        } else if (iter.key() == "id") {
            m_id = iter.value().toString();
        }
    }
    m_entities = qobject_cast<EntitiesInterface *>(entities);
    m_notifications = qobject_cast<NotificationsInterface *>(notifications);
    m_api = qobject_cast<YioAPIInterface *>(api);
    m_config = qobject_cast<ConfigInterface *>(configObj);
    m_baseObj = baseObj;

    m_webSocketId = 4;

    m_wsReconnectTimer = new QTimer(this);

    m_wsReconnectTimer->setSingleShot(true);
    m_wsReconnectTimer->setInterval(2000);
    m_wsReconnectTimer->stop();

    m_webSocket = new QWebSocket;
    m_webSocket->setParent(this);

    QObject::connect(m_webSocket, SIGNAL(textMessageReceived(const QString &)), this,
                     SLOT(onTextMessageReceived(const QString &)));
    QObject::connect(m_webSocket, SIGNAL(error(QAbstractSocket::SocketError)), this,
                     SLOT(onError(QAbstractSocket::SocketError)));
    QObject::connect(m_webSocket, SIGNAL(stateChanged(QAbstractSocket::SocketState)), this,
                     SLOT(onStateChanged(QAbstractSocket::SocketState)));

    QObject::connect(m_wsReconnectTimer, SIGNAL(timeout()), this, SLOT(onTimeout()));
}

void HomeAssistantThread::onTextMessageReceived(const QString &message) {
    QJsonParseError parseerror;
    QJsonDocument   doc = QJsonDocument::fromJson(message.toUtf8(), &parseerror);
    if (parseerror.error != QJsonParseError::NoError) {
        qCCritical(m_log) << "JSON error:" << parseerror.errorString();
        return;
    }
    QVariantMap map = doc.toVariant().toMap();

    QString m = map.value("error").toString();
    if (m.length() > 0) {
        qCCritical(m_log) << "Message error:" << m;
    }

    QString type = map.value("type").toString();
    int     id = map.value("id").toInt();

    if (type == "auth_required") {
        QString auth = QString("{ \"type\": \"auth\", \"access_token\": \"%1\" }\n").arg(m_token);
        m_webSocket->sendTextMessage(auth);
        return;
    }

    if (type == "auth_ok") {
        qCInfo(m_log) << "Authentication successful";
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // FETCH STATES
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        m_webSocket->sendTextMessage("{\"id\": 2, \"type\": \"get_states\"}\n");
    }

    if (id == 2) {
        QVariantList list = map.value("result").toList();
        for (int i = 0; i < list.length(); i++) {
            QVariantMap result = list.value(i).toMap();
            updateEntity(result.value("entity_id").toString(), result);
        }

        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        // SUBSCRIBE TO EVENTS IN HOME ASSISTANT
        ////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
        m_webSocket->sendTextMessage(
            "{\"id\": 3, \"type\": \"subscribe_events\", \"event_type\": \"state_changed\"}\n");
    }

    if (type == "result" && id == 3) {
        setState(0);
        qCDebug(m_log) << "Subscribed to state changes";

        // remove notifications that we don't need anymore as the integration is connected
        m_notifications->remove("Cannot connect to Home Assistant.");
    }

    if (id == m_webSocketId) {
        qCDebug(m_log) << "Command successful";
    }

    if (type == "event" && id == 3) {
        QVariantMap data = map.value("event").toMap().value("data").toMap();
        QVariantMap newState = data.value("new_state").toMap();
        updateEntity(data.value("entity_id").toString(), newState);
    }
}

void HomeAssistantThread::onStateChanged(QAbstractSocket::SocketState state) {
    if (state == QAbstractSocket::UnconnectedState && !m_userDisconnect) {
        qCDebug(m_log) << "State changed to 'Unconnected': starting reconnect";
        setState(2);
        m_wsReconnectTimer->start();
    }
}

void HomeAssistantThread::onError(QAbstractSocket::SocketError error) {
    qCWarning(m_log) << error << m_webSocket->errorString();
    if (m_webSocket->isValid()) {
        m_webSocket->close();
    }
    setState(2);
    m_wsReconnectTimer->start();
}

void HomeAssistantThread::onTimeout() {
    if (m_tries == 3) {
        m_wsReconnectTimer->stop();
        qCCritical(m_log) << "Cannot connect to Home Assistant: retried 3 times connecting to" << m_ip;
        QObject *param = m_baseObj;
        m_notifications->add(
            true, tr("Cannot connect to Home Assistant."), tr("Reconnect"),
            [](QObject *param) {
                Integration *i = qobject_cast<Integration *>(param);
                i->connect();
            },
            param);

        disconnect();
        m_tries = 0;
    } else {
        m_webSocketId = 4;
        if (m_state != 1) {
            setState(1);
        }

        QString url = QString("ws://").append(m_ip).append("/api/websocket");
        qCDebug(m_log) << "Reconnection attempt" << m_tries + 1 << "to HomeAssistant server:" << url;
        m_webSocket->open(QUrl(url));

        m_tries++;
    }
}

void HomeAssistantThread::webSocketSendCommand(const QString &domain, const QString &service, const QString &entity_id,
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
        d.insert("entity_id", QVariant(entity_id));
        map.insert("service_data", d);
    } else {
        data->insert("entity_id", QVariant(entity_id));
        map.insert("service_data", *data);
    }
    QJsonDocument doc = QJsonDocument::fromVariant(map);
    QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
    m_webSocket->sendTextMessage(message);
}

int HomeAssistantThread::convertBrightnessToPercentage(float value) {
    return static_cast<int>(round(value / 255 * 100));
}

void HomeAssistantThread::updateEntity(const QString &entity_id, const QVariantMap &attr) {
    EntityInterface *entity = m_entities->getEntityInterface(entity_id);
    if (entity) {
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
    }
}

void HomeAssistantThread::updateLight(EntityInterface *entity, const QVariantMap &attr) {
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

void HomeAssistantThread::updateBlind(EntityInterface *entity, const QVariantMap &attr) {
    QVariantMap attributes;

    // state
    if (attr.value("state").toString() == "open") {
        entity->setState(BlindDef::OPEN);
        //        attributes.insert("state", true);
    } else {
        //        attributes.insert("state", false);
    }

    // position
    if (entity->isSupported(BlindDef::F_POSITION)) {
        //        attributes.insert("position", attr.value("attributes").toMap().value("current_position").toInt());
        entity->updateAttrByIndex(BlindDef::POSITION,
                                  attr.value("attributes").toMap().value("current_position").toInt());
    }

    //    m_entities->update(entity->entity_id(), attributes);
}

void HomeAssistantThread::updateMediaPlayer(EntityInterface *entity, const QVariantMap &attr) {
    QVariantMap attributes;

    // state
    if (attr.value("state").toString() == "off") {
        //        attributes.insert("state", 0);
        //        entity->setState(MediaPlayerDef::OFF);
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::OFF);
    } else if (attr.value("state").toString() == "on") {
        //        attributes.insert("state", 1);
        //        entity->setState(MediaPlayerDef::ON);
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::ON);
    } else if (attr.value("state").toString() == "idle") {
        //        attributes.insert("state", 2);
        //        entity->setState(MediaPlayerDef::IDLE);
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::IDLE);
    } else if (attr.value("state").toString() == "playing") {
        //        attributes.insert("state", 3);
        //        entity->setState(MediaPlayerDef::PLAYING);
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::PLAYING);
    } else {
        //        attributes.insert("state", 0);
        //        entity->setState(MediaPlayerDef::OFF);
        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::OFF);
    }

    QVariantMap haAttr = attr.value("attributes").toMap();
    // source
    if (entity->isSupported(MediaPlayerDef::F_SOURCE) && haAttr.contains("source")) {
        attributes.insert("source", haAttr.value("source").toString());
    }

    // volume
    if (entity->isSupported(MediaPlayerDef::F_VOLUME_SET) && haAttr.contains("volume_level")) {
        attributes.insert("volume", static_cast<int>(round(haAttr.value("volume_level").toDouble() * 100)));
    }

    // media type
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_TYPE) && haAttr.contains("media_content_type")) {
        attributes.insert("mediaType", haAttr.value("media_content_type").toString());
    }

    // media image
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_IMAGE) && haAttr.contains("entity_picture")) {
        QString url = haAttr.value("entity_picture").toString();
        QString fullUrl = "";
        if (url.contains("http")) {
            fullUrl = url;
        } else {
            fullUrl = QString("http://").append(m_ip).append(url);
        }
        attributes.insert("mediaImage", fullUrl);
    }

    // media title
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_TITLE) && haAttr.contains("media_title")) {
        attributes.insert("mediaTitle", haAttr.value("media_title").toString());
    }

    // media artist
    if (entity->isSupported(MediaPlayerDef::F_MEDIA_ARTIST) && haAttr.contains("media_artist")) {
        attributes.insert("mediaArtist", haAttr.value("media_artist").toString());
    }

    m_entities->update(entity->entity_id(), attributes);
}

void HomeAssistantThread::updateClimate(EntityInterface *entity, const QVariantMap &attr) {
    // state
    if (attr.value("state").toString() == "off") {
        entity->setState(ClimateDef::OFF);
    } else if (attr.value("state").toString() == "heat") {
        entity->setState(ClimateDef::HEAT);
    } else if (attr.value("state").toString() == "cool") {
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

void HomeAssistantThread::setState(int state) {
    m_state = state;
    emit stateChanged(state);
}

void HomeAssistantThread::connect() {
    m_userDisconnect = false;

    setState(1);

    // reset the reconnnect trial variable
    m_tries = 0;

    // turn on the websocket connection
    if (m_webSocket->isValid()) {
        m_webSocket->close();
    }

    QString url = QString("ws://").append(m_ip).append("/api/websocket");
    qCDebug(m_log) << "Connecting to HomeAssistant server:" << url;
    m_webSocket->open(QUrl(url));
}

void HomeAssistantThread::disconnect() {
    m_userDisconnect = true;
    qCDebug(m_log) << "Disconnecting from HomeAssistant";

    // turn of the reconnect try
    m_wsReconnectTimer->stop();

    // turn off the socket
    m_webSocket->close();

    setState(2);
}

void HomeAssistantThread::sendCommand(const QString &type, const QString &entity_id, int command,
                                      const QVariant &param) {
    if (type == "light") {
        if (command == LightDef::C_TOGGLE) {
            webSocketSendCommand(type, "toggle", entity_id, nullptr);
        } else if (command == LightDef::C_ON) {
            webSocketSendCommand(type, "turn_on", entity_id, nullptr);
        } else if (command == LightDef::C_OFF) {
            webSocketSendCommand(type, "turn_off", entity_id, nullptr);
        } else if (command == LightDef::C_BRIGHTNESS) {
            QVariantMap data;
            data.insert("brightness_pct", param);
            webSocketSendCommand(type, "turn_on", entity_id, &data);
        } else if (command == LightDef::C_COLOR) {
            QColor       color = param.value<QColor>();
            QVariantMap  data;
            QVariantList list;
            list.append(color.red());
            list.append(color.green());
            list.append(color.blue());
            data.insert("rgb_color", list);
            webSocketSendCommand(type, "turn_on", entity_id, &data);
        }
    }
    if (type == "blind") {
        if (command == BlindDef::C_OPEN) {
            webSocketSendCommand("cover", "open_cover", entity_id, nullptr);
        } else if (command == BlindDef::C_CLOSE) {
            webSocketSendCommand("cover", "close_cover", entity_id, nullptr);
        } else if (command == BlindDef::C_STOP) {
            webSocketSendCommand("cover", "stop_cover", entity_id, nullptr);
        } else if (command == BlindDef::C_POSITION) {
            QVariantMap data;
            data.insert("position", param);
            webSocketSendCommand("cover", "set_cover_position", entity_id, &data);
        }
    }
    if (type == "media_player") {
        if (command == MediaPlayerDef::C_VOLUME_SET) {
            QVariantMap data;
            data.insert("volume_level", param.toDouble() / 100);
            webSocketSendCommand(type, "volume_set", entity_id, &data);
        } else if (command == MediaPlayerDef::C_PLAY || command == MediaPlayerDef::C_PAUSE) {
            webSocketSendCommand(type, "media_play_pause", entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_PREVIOUS) {
            webSocketSendCommand(type, "media_previous_track", entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_NEXT) {
            webSocketSendCommand(type, "media_next_track", entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_TURNON) {
            webSocketSendCommand(type, "turn_on", entity_id, nullptr);
        } else if (command == MediaPlayerDef::C_TURNOFF) {
            webSocketSendCommand(type, "turn_off", entity_id, nullptr);
        }
    }
    if (type == "climate") {
        if (command == ClimateDef::C_ON) {
            webSocketSendCommand(type, "turn_on", entity_id, nullptr);
        } else if (command == ClimateDef::C_OFF) {
            webSocketSendCommand(type, "turn_off", entity_id, nullptr);
        } else if (command == ClimateDef::C_TARGET_TEMPERATURE) {
            QVariantMap data;
            data.insert("temperature", param.toDouble());
            webSocketSendCommand(type, "set_temperature", entity_id, &data);
        } else if (command == ClimateDef::C_HEAT) {
            QVariantMap data;
            data.insert("hvac_mode", "heat");
            webSocketSendCommand(type, "set_hvac_mode", entity_id, &data);
        } else if (command == ClimateDef::C_COOL) {
            QVariantMap data;
            data.insert("hvac_mode", "cool");
            webSocketSendCommand(type, "set_hvac_mode", entity_id, &data);
        }
    }
}
