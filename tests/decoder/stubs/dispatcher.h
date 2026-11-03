// decodertest stub: receives the image start/end events of the decoder
#ifndef DISPATCHER_H
#define DISPATCHER_H
#include "dispatchevents.h"
#include "appglobal.h"
#include <QApplication>

class dispatcher : public QObject
{
  Q_OBJECT
public:
  void customEvent(QEvent *e) override;
};
#endif
