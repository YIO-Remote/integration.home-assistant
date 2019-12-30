/******************************************************************************
 *
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

#ifndef HOMEASSISTANT_H
#define HOMEASSISTANT_H

#include <QColor>
#include <QLoggingCategory>
#include <QObject>
#include <QString>
#include <QThread>
#include <QTimer>
#include <QVariant>
#include <QtWebSockets/QWebSocket>

#include "../remote-software/sources/configinterface.h"
#include "../remote-software/sources/entities/entitiesinterface.h"
#include "../remote-software/sources/entities/entityinterface.h"
#include "../remote-software/sources/integrations/integration.h"
#include "../remote-software/sources/integrations/plugininterface.h"
#include "../remote-software/sources/notificationsinterface.h"
#include "../remote-software/sources/yioapiinterface.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// HOME ASSISTANT FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class HomeAssistantPlugin : public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "YIO.PluginInterface" FILE "homeassistant.json")
    Q_INTERFACES(PluginInterface)

 public:
    explicit HomeAssistantPlugin() : m_log("homeassistant") {}

    void create(const QVariantMap& config, QObject* entities, QObject* notifications, QObject* api,
                QObject* configObj) override;
    void setLogEnabled(QtMsgType msgType, bool enable) override { m_log.setEnabled(msgType, enable); }

 private:
    QLoggingCategory m_log;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// HOME ASSISTANT BASE CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class HomeAssistantBase : public Integration {
    Q_OBJECT

 public:
    explicit HomeAssistantBase(QLoggingCategory& log, QObject* parent);
    virtual ~HomeAssistantBase();

    Q_INVOKABLE void setup(const QVariantMap& config, QObject* entities, QObject* notifications, QObject* api,
                           QObject* configObj);
    Q_INVOKABLE void connect();
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void sendCommand(const QString& type, const QString& entity_id, int command, const QVariant& param);

 signals:
    void connectSignal();
    void disconnectSignal();
    void sendCommandSignal(const QString& type, const QString& entity_id, int command, const QVariant& param);

 public slots:
    void stateHandler(int state);

 private:
    QThread           m_thread;
    QLoggingCategory& m_log;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// HOME ASSISTANT THREAD CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class HomeAssistantThread : public QObject {
    Q_OBJECT

 public:
    HomeAssistantThread(const QVariantMap& config, QObject* entities, QObject* notifications, QObject* api,
                        QObject* configObj, QLoggingCategory& log);

 signals:
    void stateChanged(int state);

 public slots:
    void connect();
    void disconnect();

    void sendCommand(const QString& type, const QString& entity_id, int command, const QVariant& param);

    void onTextMessageReceived(const QString& message);
    void onStateChanged(QAbstractSocket::SocketState state);
    void onError(QAbstractSocket::SocketError error);

    void onTimeout();

 private:
    void webSocketSendCommand(const QString& domain, const QString& service, const QString& entity_id,
                              QVariantMap* data);
    int  convertBrightnessToPercentage(float value);

    void updateEntity(const QString& entity_id, const QVariantMap& attr);
    void updateLight(EntityInterface* entity, const QVariantMap& attr);
    void updateBlind(EntityInterface* entity, const QVariantMap& attr);
    void updateMediaPlayer(EntityInterface* entity, const QVariantMap& attr);
    void updateClimate(EntityInterface* entity, const QVariantMap& attr);

    void setState(int state);

    EntitiesInterface*      m_entities;
    NotificationsInterface* m_notifications;
    YioAPIInterface*        m_api;
    ConfigInterface*        m_config;

    QString m_id;

    QString           m_ip;
    QString           m_token;
    QWebSocket*       m_socket;
    QTimer*           m_websocketReconnect;
    int               m_tries;
    int               m_webSocketId;
    bool              m_userDisconnect = false;
    QLoggingCategory& m_log;
    int               m_state = 0;
};

#endif  // HOMEASSISTANT_H
