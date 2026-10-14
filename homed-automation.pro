include(../homed-common/homed-common.pri)
include(../homed-common/homed-parser.pri)
include(../homed-common/homed-sun.pri)

HEADERS += \
    action.h \
    automation.h \
    condition.h \
    controller.h \
    runner.h \
    telegram.h \
    trigger.h

SOURCES += \
    action.cpp \
    automation.cpp \
    condition.cpp \
    controller.cpp \
    runner.cpp \
    telegram.cpp \
    trigger.cpp

DISTFILES += $$files(deploy/data/usr/share/homed-automation/*, true)

deploy.files = deploy/data/usr/share/homed-automation/*
deploy.path = /usr/share/homed-automation

INSTALLS += deploy
