#include "rigmeterwidget.h"
#include "appglobal.h"
#include "rigcontrol.h"
#include "soundbase.h"

#include <QHBoxLayout>
#include <QFrame>
#include <QSlider>
#include <QLabel>
#include <QProgressBar>
#include <QSettings>
#include <QPalette>
#include <math.h>

#define POLLINTERVAL 500  // ms
// logarithmic level sliders, position in 0.1 dB, the leftmost position mutes
#define MINLEVEL  -400    // -40 dB
#define RXMAXLEVEL  60    // +6 dB = 200%, above 0 dB (100%) is overdrive
#define TXMAXLEVEL   0    // 0 dB = 100%

// colors are set with the palette, a style sheet changed at runtime resets the font of the widget
#define COLORNORMAL QColor("#5b9bd5")
#define COLORWARN   QColor("#e67e00")
#define COLORALARM  QColor("#d00000")

static QSlider *makeSlider(int maximum,QWidget *parent)
{
  QSlider *sl=new QSlider(Qt::Horizontal,parent);
  sl->setRange(MINLEVEL,maximum);
  sl->setSingleStep(5);
  sl->setPageStep(30);
  sl->setFixedSize(80,18);
  return sl;
}

static double levelToGain(int level)
{
  if(level<=MINLEVEL) return 0;
  return pow(10.,level/200.);
}

static QString levelText(int level)
{
  if(level<=MINLEVEL) return QString("mute");
  return QString("%1%2 dB").arg(level>0 ? "+" : "").arg(level/10.,0,'f',1);
}

// same look as the frequency display
static QFrame *makeFrame(QWidget *parent)
{
  QFrame *fr=new QFrame(parent);
  fr->setFrameShape(QFrame::Box);
  fr->setFrameShadow(QFrame::Raised);
  fr->setLineWidth(2);
  QHBoxLayout *layout=new QHBoxLayout(fr);
  layout->setContentsMargins(4,1,4,1);
  layout->setSpacing(3);
  return fr;
}

static QLabel *makeName(QString name,QWidget *parent)
{
  QLabel *lbl=new QLabel(name,parent);
  QFont f=lbl->font();
  f.setBold(true);
  lbl->setFont(f);
  return lbl;
}

static void setBarColor(QProgressBar *pb,const QColor &color)
{
  QPalette pal=pb->palette();
  pal.setColor(QPalette::Highlight,color);
  pb->setPalette(pal);
}

static void setLabelColor(QLabel *lbl,const QColor &color)
{
  QPalette pal=lbl->palette();
  pal.setColor(QPalette::WindowText,color);
  lbl->setPalette(pal);
}

static QProgressBar *makeBar(QWidget *parent)
{
  QProgressBar *pb=new QProgressBar(parent);
  pb->setRange(0,100);
  pb->setFixedSize(80,18);
  pb->setTextVisible(true);
  pb->setAlignment(Qt::AlignCenter);
  setBarColor(pb,COLORNORMAL);
  return pb;
}

rigMeterWidget::rigMeterWidget(QWidget *parent) : QWidget(parent)
{
  QHBoxLayout *layout=new QHBoxLayout(this);
  layout->setContentsMargins(0,0,0,0);
  layout->setSpacing(4);
  maxPower=0;

  // audio levels
  QFrame *levelFrame=makeFrame(this);
  QHBoxLayout *levelLayout=(QHBoxLayout *)levelFrame->layout();
  rxSlider=makeSlider(RXMAXLEVEL,levelFrame);
  rxSlider->setToolTip(tr("RX audio level, -40 dB to +6 dB, above 0 dB is overdrive"));
  rxLabel=new QLabel(levelFrame);
  rxLabel->setFixedWidth(rxLabel->fontMetrics().horizontalAdvance("-40.0 dB")+6);
  rxLabel->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
  txSlider=makeSlider(TXMAXLEVEL,levelFrame);
  txSlider->setToolTip(tr("TX audio level, -40 dB to 0 dB"));
  txLabel=new QLabel(levelFrame);
  txLabel->setFixedWidth(txLabel->fontMetrics().horizontalAdvance("-40.0 dB")+6);
  txLabel->setAlignment(Qt::AlignRight|Qt::AlignVCenter);
  levelLayout->addWidget(makeName("RX",levelFrame));
  levelLayout->addWidget(rxSlider);
  levelLayout->addWidget(rxLabel);
  levelLayout->addSpacing(6);
  levelLayout->addWidget(makeName("TX",levelFrame));
  levelLayout->addWidget(txSlider);
  levelLayout->addWidget(txLabel);

  // transmit meters
  QFrame *meterFrame=makeFrame(this);
  meterFrame->setToolTip(tr("Transmit meters, read via hamlib CAT while transmitting"));
  QHBoxLayout *meterLayout=(QHBoxLayout *)meterFrame->layout();
  pwrBar=makeBar(meterFrame);
  swrBar=makeBar(meterFrame);
  alcBar=makeBar(meterFrame);
  meterLayout->addWidget(pwrBar);
  meterLayout->addWidget(swrBar);
  meterLayout->addWidget(alcBar);

  layout->addWidget(levelFrame);
  layout->addWidget(meterFrame);
  // fixed size: moving a slider must never resize the status bar
  setSizePolicy(QSizePolicy::Fixed,QSizePolicy::Fixed);

  QSettings qSettings;
  qSettings.beginGroup("LEVELS");
  int rxLevel=qBound(MINLEVEL,qSettings.value("rxLevel",0).toInt(),RXMAXLEVEL);
  int txLevel=qBound(MINLEVEL,qSettings.value("txLevel",0).toInt(),TXMAXLEVEL);
  qSettings.endGroup();
  rxSlider->setValue(rxLevel);
  txSlider->setValue(txLevel);
  slotRxLevel(rxLevel);
  slotTxLevel(txLevel);
  connect(rxSlider,SIGNAL(valueChanged(int)),SLOT(slotRxLevel(int)));
  connect(txSlider,SIGNAL(valueChanged(int)),SLOT(slotTxLevel(int)));

  pollTimer.setInterval(POLLINTERVAL);
  connect(&pollTimer,SIGNAL(timeout()),SLOT(slotPoll()));
  clearMeters();
}

void rigMeterWidget::slotRxLevel(int level)
{
  rxLabel->setText(levelText(level));
  // overdrive warning
  setLabelColor(rxLabel,level>0 ? COLORWARN : txLabel->palette().color(QPalette::WindowText));
  soundBase::setRxGain(levelToGain(level));
  QSettings qSettings;
  qSettings.setValue("LEVELS/rxLevel",level);
}

void rigMeterWidget::slotTxLevel(int level)
{
  txLabel->setText(levelText(level));
  soundBase::setTxGain(levelToGain(level));
  QSettings qSettings;
  qSettings.setValue("LEVELS/txLevel",level);
}

void rigMeterWidget::setTransmitting(bool tx)
{
  if(tx && rigControllerPtr->enabled() && !rigControllerPtr->params()->enableXMLRPC)
    {
      pollTimer.start();
    }
  else
    {
      pollTimer.stop();
      clearMeters();
    }
}

void rigMeterWidget::clearMeters()
{
  pwrBar->setValue(0);
  pwrBar->setFormat("PWR --");
  swrBar->setValue(0);
  swrBar->setFormat("SWR --");
  setBarColor(swrBar,COLORNORMAL);
  alcBar->setValue(0);
  alcBar->setFormat("ALC --");
}

void rigMeterWidget::slotPoll()
{
  double power,swr,alc;
  bool inWatts;
  if(!rigControllerPtr->getTxMeters(power,inWatts,swr,alc))
    {
      // the rig doesn't answer, don't keep blocking the GUI with CAT timeouts during this transmission
      pollTimer.stop();
      return;
    }
  if(power>=0)
    {
      if(inWatts)
        {
          // auto scale: 5, 10, 20, 50, 100 ... W
          static const double scales[]={5,10,20,50,100,200,500,1000,2000};
          for(double sc:scales)
            {
              if((power<=sc) || (sc==2000))
                {
                  if(sc>maxPower) maxPower=sc;
                  break;
                }
            }
          pwrBar->setValue(qRound(100.*power/maxPower));
          pwrBar->setFormat(QString("PWR %1 W").arg(power,0,'f',power<10 ? 1 : 0));
        }
      else
        {
          pwrBar->setValue(qRound(100.*power));
          pwrBar->setFormat(QString("PWR %1%").arg(qRound(100.*power)));
        }
    }
  if(swr>=0)
    {
      // bar scale SWR 1.0 .. 3.0
      swrBar->setValue(qBound(0,qRound(50.*(swr-1.)),100));
      swrBar->setFormat(QString("SWR %1").arg(swr,0,'f',1));
      if(swr>=2.5) setBarColor(swrBar,COLORALARM);
      else if(swr>=1.8) setBarColor(swrBar,COLORWARN);
      else setBarColor(swrBar,COLORNORMAL);
    }
  if(alc>=0)
    {
      alcBar->setValue(qRound(100.*qBound(0.,alc,1.)));
      alcBar->setFormat(QString("ALC %1%").arg(qRound(100.*alc)));
    }
}
