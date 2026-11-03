// decodertest stub: logging goes to stderr, filtered by the categories given on the command line
#ifndef LOGGING_H
#define LOGGING_H
#include <QString>
#include <QDebug>
#include "loggingparams.h"

#define errorOut() qDebug()
void harnessLog(const char *func, const QString &msg, int category);
#define addToLog(x,y) harnessLog(__func__,x,y)

class logFile
{
public:
  void addToAux(QString) {}
};
#endif
