#include "appglobal.h"
#include "logging.h"
#include "soundbase.h"
#include <QPixmap>
#include <QCursor>


const QString MAJORVERSION  = "10.0";
const QString CONFIGVERSION = "10.0";
const QString MINORVERSION  = ".0";
#ifdef GITCOMMIT
const QString GITVERSION = QString(" (%1)").arg(GITCOMMIT);
#else
const QString GITVERSION;
#endif
const QString LOGVERSION = ("qsstv-ng."+MAJORVERSION+MINORVERSION+".log");
const QString ORGANIZATION = "QSSTV-NG";
// also the X11 WM_CLASS of the windows, it must match the name of qsstv-ng.desktop
const QString APPLICATION  = "qsstv-ng";
const QString LEGACYORGANIZATION = "ON4QZ";
const QString LEGACYAPPLICATION  = "qsstv_9.0"; // settings of the original QSSTV
const QString PROJECTURL = "https://github.com/lycanananas/qsstv-ng";
const QString qsstvVersion=QString("QSSTV-NG " + MAJORVERSION+MINORVERSION+GITVERSION);
const QString shortName=QString("QSSTV-NG");


QSplashScreen *splashPtr;
QString splashStr;

mainWindow *mainWindowPtr;
soundBase *soundIOPtr;
logFile *logFilePtr;
configDialog *configDialogPtr;


ftpThread *notifyRXIntfPtr;
ftpThread *hybridTxIntfPtr;
ftpThread *notifyTXIntfPtr;
ftpThread *onlineStatusIntfPtr;
ftpThread *hybridRxIntfPtr;
ftpThread *saveImageIntfPtr;




dispatcher *dispatcherPtr;
QStatusBar *statusBarPtr;
rxWidget *rxWidgetPtr;
txWidget *txWidgetPtr;
galleryWidget *galleryWidgetPtr;
waterfallText *waterfallPtr;
rigControl *rigControllerPtr;
xmlInterface *xmlIntfPtr;
logBook *logBookPtr;
fileWatcher *fileWatcherPtr;


int fftNumBlocks=2;
bool useHybrid;
bool inStartup;






etransmissionMode transmissionModeIndex;  // SSTV , DRM



QPixmap *greenPXMPtr;
QPixmap *redPXMPtr;

#ifndef QT_NO_DEBUG
scopeView *scopeViewerData;
scopeView *scopeViewerSyncNarrow;
scopeView *scopeViewerSyncWide;
#endif


void globalInit()
{
  logFilePtr=new logFile();
  logFilePtr->open(LOGVERSION);
  QSettings qSettings;
  qSettings.beginGroup("MAIN");
  logFilePtr->readSettings();
  greenPXMPtr=new QPixmap(16,16);
  greenPXMPtr->fill(Qt::green);
  redPXMPtr=new QPixmap(16,16);
  redPXMPtr->fill(Qt::red);
  qSettings.endGroup();
}

void globalEnd(void)
{
  logFilePtr->writeSettings();
  logFilePtr->close();
}

