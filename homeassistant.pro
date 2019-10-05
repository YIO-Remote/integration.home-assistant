TEMPLATE        = lib
CONFIG         += plugin
QT             += websockets core quick
HEADERS         = homeassistant.h \
                  ../remote-software/sources/integrations/integration.h \
                  ../remote-software/sources/integrations/integrationinterface.h
SOURCES         = homeassistant.cpp
TARGET          = homeassistant
DESTDIR         = ../remote-software/plugins

#DISTFILES += homeassistant.json

# install
unix {
    target.path = /usr/lib
    INSTALLS += target
}
