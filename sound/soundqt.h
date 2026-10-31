#ifndef SOUNDQT_H
#define SOUNDQT_H

#include "soundbase.h"

#include <QAudioDeviceInfo>
#include <QAudioFormat>
#include <QIODevice>
#include <QMutex>
#include <QWaitCondition>
#include <QThread>

class QAudioInput;
class QAudioOutput;

void getQtAudioCardList(QStringList &inputList, QStringList &outputList);


// thread safe byte fifo, used as the QIODevice between Qt Multimedia and the sound thread
class qtAudioFifo : public QIODevice
{
public:
  qtAudioFifo(qint64 capacity, bool padWithSilence);
  qint64 put(const char *data, qint64 len, int timeoutMs); // blocking until space available or timeout
  qint64 take(char *data, qint64 len);
  qint64 count();
  void clear();
  bool waitEmpty(int timeoutMs);
  bool isSequential() const override {return true;}
  qint64 bytesAvailable() const override;

protected:
  qint64 readData(char *data, qint64 maxlen) override;  // called by QAudioOutput (pull mode)
  qint64 writeData(const char *data, qint64 len) override; // called by QAudioInput (push into fifo)

private:
  qint64 takeLocked(char *data, qint64 len);
  QByteArray buf;
  qint64 readPos;
  qint64 used;
  bool padSilence;
  mutable QMutex mutex;
  QWaitCondition spaceAvailable;
  QWaitCondition emptied;
};


class soundQt:public soundBase
{
public:
  soundQt();
  ~soundQt();
  bool init(int samplerate);
  int read(int &countAvailable);
  int write(uint numFrames);

protected:
  void flushCapture();
  void flushPlayback();
  void closeDevices();
  void waitPlaybackEnd();

private:
  QAudioDeviceInfo findDevice(QString name,QAudio::Mode mode);
  bool openDevices();
  void releaseDevices();
  QThread audioThread;
  QObject *audioContext;   // lives in audioThread, all Qt Multimedia objects are created/deleted there
  QAudioInput *audioInput;
  QAudioOutput *audioOutput;
  qtAudioFifo *captureFifo;
  qtAudioFifo *playbackFifo;
  int inChannels;
  int outputLatencyMs;
};

#endif // SOUNDQT_H
