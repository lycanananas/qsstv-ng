# SSTV decoder test without GUI, see decodertest.cpp
# build: mkdir build && cd build && qmake-qt5 ../decodertest.pro && make

QT += core gui widgets
CONFIG += c++11 console release
CONFIG -= app_bundle
TARGET = decodertest

SRC = $$PWD/../..

# the stubs replace the GUI headers, so they must come first
INCLUDEPATH += $$PWD/stubs $$SRC $$SRC/sstv $$SRC/sstv/modes $$SRC/dsp $$SRC/utils $$SRC/dispatch

SOURCES += decodertest.cpp \
    $$SRC/sstv/sstvrx.cpp \
    $$SRC/sstv/syncprocessor.cpp \
    $$SRC/sstv/visfskid.cpp \
    $$SRC/sstv/sstvparam.cpp \
    $$SRC/sstv/modes/modeavt.cpp \
    $$SRC/sstv/modes/modebase.cpp \
    $$SRC/sstv/modes/modebw.cpp \
    $$SRC/sstv/modes/modegbr.cpp \
    $$SRC/sstv/modes/modegbr2.cpp \
    $$SRC/sstv/modes/modepd.cpp \
    $$SRC/sstv/modes/modergb.cpp \
    $$SRC/sstv/modes/moderobot1.cpp \
    $$SRC/sstv/modes/moderobot2.cpp \
    $$SRC/dsp/filters.cpp \
    $$SRC/dsp/filter.cpp \
    $$SRC/dsp/filterparam.cpp \
    $$SRC/dsp/downsamplefilter.cpp \
    $$SRC/utils/loggingparams.cpp

HEADERS += stubs/dispatcher.h \
    $$SRC/sstv/sstvrx.h \
    $$SRC/sstv/syncprocessor.h \
    $$SRC/sstv/visfskid.h

QMAKE_CXXFLAGS += -Wno-implicit-fallthrough -Wno-psabi
