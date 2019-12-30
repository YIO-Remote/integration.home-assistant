TEMPLATE        = lib
CONFIG         += plugin
QT             += websockets core quick

REMOTE_SRC = $$(YIO_SRC)
isEmpty(REMOTE_SRC) {
    REMOTE_SRC = $$clean_path($$PWD/../remote-software)
    warning("Environment variables YIO_SR not defined! Using '$$REMOTE_SRC' for remote-software project.")
} else {
    REMOTE_SRC = $$(YIO_SRC)/remote-software
    message("YIO_SRC is set: using '$$REMOTE_SRC' for remote-software project.")
}

include($$REMOTE_SRC/qmake-target-platform.pri)
include($$REMOTE_SRC/qmake-destination-path.pri)

HEADERS         = homeassistant.h \
                  $$REMOTE_SRC/sources/integrations/integration.h \
                  $$REMOTE_SRC/sources/integrations/plugininterface.h
SOURCES         = homeassistant.cpp
TARGET          = homeassistant

# Configure destination path. DESTDIR is set in qmake-destination-path.pri
DESTDIR = $$DESTDIR/plugins
OBJECTS_DIR = $$PWD/build/$$DESTINATION_PATH/obj
MOC_DIR = $$PWD/build/$$DESTINATION_PATH/moc
RCC_DIR = $$PWD/build/$$DESTINATION_PATH/qrc
UI_DIR = $$PWD/build/$$DESTINATION_PATH/ui

# install
unix {
    target.path = /usr/lib
    INSTALLS += target
}

# === start TRANSLATION section =======================================
lupdate_only{
SOURCES = homeassistant.h \
          homeassistant.cpp
}

TRANSLATIONS = translations/bg_BG.ts \
               translations/cs_CZ.ts \
               translations/da_DK.ts \
               translations/de_DE.ts \
               translations/el_GR.ts \
               translations/en_US.ts \
               translations/es_ES.ts \
               translations/et_EE.ts \
               translations/fi_FI.ts \
               translations/fr_CA.ts \
               translations/fr_FR.ts \
               translations/ga_IE.ts \
               translations/hr_HR.ts \
               translations/hu_HU.ts \
               translations/it_IT.ts \
               translations/lt_LT.ts \
               translations/lv_LV.ts \
               translations/mt_MT.ts \
               translations/nl_NL.ts \
               translations/no_NO.ts \
               translations/pl_PL.ts \
               translations/pt_BR.ts \
               translations/pt_PT.ts \
               translations/ro_RO.ts \
               translations/sk_SK.ts \
               translations/sl_SI.ts \
               translations/sv_SE.ts
