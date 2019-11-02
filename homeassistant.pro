TEMPLATE        = lib
CONFIG         += plugin
QT             += websockets core quick

include(../remote-software/qmake-target-platform.pri)
include(../remote-software/qmake-destination-path.pri)

HEADERS         = homeassistant.h \
                  ../remote-software/sources/integrations/integration.h \
                  ../remote-software/sources/integrations/integrationinterface.h
SOURCES         = homeassistant.cpp
TARGET          = homeassistant

# Configure destination path by "Operating System/Compiler/Processor Architecture/Build Configuration"
DESTDIR = $$PWD/../binaries/$$DESTINATION_PATH/plugins
OBJECTS_DIR = $$PWD/build/$$DESTINATION_PATH/obj
MOC_DIR = $$PWD/build/$$DESTINATION_PATH/moc
RCC_DIR = $$PWD/build/$$DESTINATION_PATH/qrc
UI_DIR = $$PWD/build/$$DESTINATION_PATH/ui

#DISTFILES += homeassistant.json

# install
unix {
    target.path = /usr/lib
    INSTALLS += target
}
