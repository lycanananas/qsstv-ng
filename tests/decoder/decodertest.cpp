/*
  decodertest: runs a WAV file through the QSSTV-NG SSTV receiver without GUI.

  The real decoder sources (sstvRx, syncProcessor, modes, filters) are compiled, the GUI parts
  are replaced by the headers in stubs/. Received images are saved as PNG, the decoder log is
  printed on stderr.

  usage: decodertest [options] file.wav
    --out DIR            directory for the received images (default .)
    --sensitivity N      0=Low 1=Normal 2=High 3=DX (default 1)
    --autoslant 0|1      auto slant correction (default 1)
    --slant PPM          manual slant correction (default 0)
    --rxclock HZ         calibrated RX clock (default 48000)
    --mode N             0=auto, else SSTV mode index+1 (default 0)
    --mincompletion PCT  minimum completion to keep an image (default 25)
    --log CATS           comma separated log categories (e.g. SLANT,SYNCQLTY,SYNCPROCST,RXFUNC) or ALL
*/

#include "appglobal.h"
#include "configparams.h"
#include "dispatcher.h"
#include "rxwidget.h"
#include "txwidget.h"
#include "synthes.h"
#include "sstvrx.h"
#include "downsamplefilter.h"
#include "modes/modebase.h"

#include <QCoreApplication>
#include <QThread>
#include <QFile>
#include <QDir>
#include <QAtomicInteger>
#include <QSet>
#include <cstdio>

// globals normally owned by the application
double rxClock=BASESAMPLERATE;
double txClock=BASESAMPLERATE;
bool useVOX=false;
QColor imageBackGroundColor(128,128,128);
dispatcher *dispatcherPtr;
rxWidget *rxWidgetPtr;
txWidget *txWidgetPtr;
logFile *logFilePtr;
synthesizer *synthesPtr;

static QAtomicInteger<qint64> samplesDone(0);   // input samples (48 kHz) fed to the decoder
static QSet<int> logCategories;
static bool logAll=false;
static QString outDir=".";
static int imageCount=0;
static QString lastStatus;

static double nowSec()
{
  return samplesDone.loadRelaxed()/(double)BASESAMPLERATE;
}

void harnessLog(const char *func, const QString &msg, int category)
{
  if(!logAll && !logCategories.contains(category)) return;
  fprintf(stderr,"[%8.3f] %-22s %s\n",nowSec(),func,qPrintable(msg));
}

void dispatcher::customEvent(QEvent *e)
{
  switch((int)e->type())
    {
    case startImageRX:
      {
        QSize sz=((startImageRXEvent *)e)->getSize();
        rxWidgetPtr->getImageViewerPtr()->image=QImage(sz,QImage::Format_RGB32);
        rxWidgetPtr->getImageViewerPtr()->image.fill(imageBackGroundColor);
        printf("%8.3f START  %dx%d\n",nowSec(),sz.width(),sz.height());
      }
      break;
    case endSSTVImageRX:
      {
        esstvMode mode=((endImageSSTVRXEvent *)e)->getMode();
        QString modeStr=(mode==NOTVALID) ? QString("invalid") : getSSTVModeNameShort(mode);
        QString fn=QString("%1/%2_%3.png").arg(outDir).arg(++imageCount,2,10,QChar('0')).arg(modeStr);
        if(!rxWidgetPtr->getImageViewerPtr()->image.isNull())
          {
            rxWidgetPtr->getImageViewerPtr()->image.save(fn);
          }
        printf("%8.3f END    %s -> %s\n",nowSec(),qPrintable(modeStr),qPrintable(fn));
      }
      break;
    case rxSSTVStatus:
      {
        QString s=((rxSSTVStatusEvent *)e)->getStr();
        if(s!=lastStatus)
          {
            printf("%8.3f STATUS %s\n",nowSec(),qPrintable(s));
            lastStatus=s;
          }
      }
      break;
    default:
      break;
    }
  fflush(stdout);
  ((baseEvent *)e)->setDone();
}

class decodeThread : public QThread
{
public:
  QVector<qint16> samples;
protected:
  void run() override
  {
    sstvRx rx;
    rx.init();
    downsampleFilter ds(DOWNSAMPLESIZE,true);
    ds.init();
    short int block[DOWNSAMPLESIZE];
    // a few seconds of silence after the file so that the last image is ended
    int total=samples.count()+10*BASESAMPLERATE;
    for(int pos=0;pos+DOWNSAMPLESIZE<=total;pos+=DOWNSAMPLESIZE)
      {
        for(int i=0;i<DOWNSAMPLESIZE;i++)
          {
            block[i]=(pos+i<samples.count()) ? samples.at(pos+i) : 0;
          }
        ds.downSample4(block);
        rx.run(ds.filteredDataPtr(),ds.getVolumePtr());
        samplesDone.storeRelaxed(pos+DOWNSAMPLESIZE);
      }
  }
};

static bool readWav(QString fn,QVector<qint16> &out)
{
  QFile f(fn);
  if(!f.open(QIODevice::ReadOnly)) return false;
  QByteArray d=f.readAll();
  if(d.size()<44 || !d.startsWith("RIFF") || d.mid(8,4)!="WAVE") return false;
  int pos=12;
  int channels=1,rate=0,bits=0;
  while(pos+8<=d.size())
    {
      QByteArray id=d.mid(pos,4);
      quint32 len=*(const quint32 *)(d.constData()+pos+4);
      const char *p=d.constData()+pos+8;
      if(id=="fmt ")
        {
          channels=*(const quint16 *)(p+2);
          rate=*(const quint32 *)(p+4);
          bits=*(const quint16 *)(p+14);
        }
      else if(id=="data")
        {
          if(bits!=16 || rate!=BASESAMPLERATE)
            {
              fprintf(stderr,"need 16 bit %d Hz wav (got %d bit %d Hz)\n",BASESAMPLERATE,bits,rate);
              return false;
            }
          const qint16 *s=(const qint16 *)p;
          int frames=qMin<qint64>(len,d.size()-pos-8)/(2*channels);
          out.resize(frames);
          for(int i=0;i<frames;i++)
            {
              int acc=0;
              for(int c=0;c<channels;c++) acc+=s[i*channels+c];
              out[i]=(qint16)(acc/channels);
            }
          return true;
        }
      pos+=8+len+(len&1);
    }
  return false;
}

int main(int argc,char **argv)
{
  QCoreApplication app(argc,argv);
  QStringList args=app.arguments().mid(1);
  QString wavFile;
  sensitivity=1;
  autoSlantAdjust=true;
  sstvModeIndexRx=(esstvMode)0;
  minCompletion=25;
  for(int i=0;i<args.count();i++)
    {
      QString a=args.at(i);
      QString v=(i+1<args.count()) ? args.at(i+1) : QString();
      if(a=="--out") {outDir=v; i++;}
      else if(a=="--sensitivity") {sensitivity=v.toInt(); i++;}
      else if(a=="--autoslant") {autoSlantAdjust=(v.toInt()!=0); i++;}
      else if(a=="--slant") {rxSlantTrimPpm.storeRelaxed(v.toInt()); i++;}
      else if(a=="--rxclock") {rxClock=v.toDouble(); i++;}
      else if(a=="--mode") {sstvModeIndexRx=(esstvMode)v.toInt(); i++;}
      else if(a=="--mincompletion") {minCompletion=v.toInt(); i++;}
      else if(a=="--log")
        {
          foreach(QString c,v.split(',',Qt::SkipEmptyParts))
            {
              if(c.toUpper()=="ALL") logAll=true;
              for(int l=0;l<NUMDEBUGLEVELS;l++)
                {
                  if(levelStr[l].trimmed().compare(c.trimmed(),Qt::CaseInsensitive)==0) logCategories.insert(l);
                }
            }
          i++;
        }
      else wavFile=a;
    }
  if(wavFile.isEmpty())
    {
      fprintf(stderr,"usage: decodertest [--out DIR] [--sensitivity N] [--autoslant 0|1] [--slant PPM] [--rxclock HZ] [--mode N] [--log CATS] file.wav\n");
      return 2;
    }
  QDir().mkpath(outDir);

  dispatcher disp;
  dispatcherPtr=&disp;
  rxWidget rxw;
  rxWidgetPtr=&rxw;
  txWidget txw;
  txWidgetPtr=&txw;
  logFile lf;
  logFilePtr=&lf;

  decodeThread dt;
  if(!readWav(wavFile,dt.samples))
    {
      fprintf(stderr,"cannot read %s\n",qPrintable(wavFile));
      return 1;
    }
  printf("file %s: %.1f s, sensitivity %d, autoslant %d, slant %d ppm, rxclock %.2f\n",qPrintable(wavFile),
         dt.samples.count()/(double)BASESAMPLERATE,sensitivity,autoSlantAdjust,rxSlantTrimPpm.loadRelaxed(),rxClock);
  QObject::connect(&dt,&QThread::finished,&app,&QCoreApplication::quit);
  dt.start();
  app.exec();
  dt.wait();
  printf("images: %d\n",imageCount);
  return 0;
}
