#ifndef RIGMETERWIDGET_H
#define RIGMETERWIDGET_H

#include <QWidget>
#include <QTimer>

class QSlider;
class QLabel;
class QProgressBar;
class QToolButton;

// status bar widget: RX/TX audio level and the transmit meters (PWR, SWR, ALC) read via hamlib
class rigMeterWidget : public QWidget
{
  Q_OBJECT
public:
  explicit rigMeterWidget(QWidget *parent = nullptr);
  void setTransmitting(bool tx);

private slots:
  void slotRxLevel(int level); // 0.1 dB
  void slotTxLevel(int level);
  void slotOverdrive(bool on);
  void slotPoll();

private:
  void clearMeters();
  QSlider *rxSlider;
  QSlider *txSlider;
  QLabel *rxLabel;
  QLabel *txLabel;
  QToolButton *odButton;
  QProgressBar *pwrBar;
  QProgressBar *alcBar;
  QProgressBar *swrBar;
  QTimer pollTimer;
  double maxPower;
};

#endif // RIGMETERWIDGET_H
