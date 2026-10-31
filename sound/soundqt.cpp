/**************************************************************************
*   QSSTV-NG: Qt Multimedia sound interface                               *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
***************************************************************************/

/*
  Audio is handled by Qt Multimedia (QAudioInput/QAudioOutput), the same way as WSJT-X does.
  On Linux Qt Multimedia talks to PulseAudio or PipeWire (pipewire-pulse), so every
  sink/source can be selected by name instead of the whole ALSA device being taken over.

  The Qt Multimedia objects need an event loop, so they live in a separate thread (audioThread).
  Data is exchanged with the sound thread (soundBase::run) through two thread safe fifo's.
*/

#include "soundqt.h"
#include "configparams.h"
#include "logging.h"

#include <QAudioInput>
#include <QAudioOutput>
#include <QMetaObject>
#include <string.h>
#include <algorithm>


qtAudioFifo::qtAudioFifo(qint64 capacity, bool padWithSilence)
{
  buf.resize(capacity);
  readPos=0;
  used=0;
  padSilence=padWithSilence;
  open(QIODevice::ReadWrite|QIODevice::Unbuffered);
}

qint64 qtAudioFifo::put(const char *data, qint64 len, int timeoutMs)
{
  qint64 total=0;
  qint64 cap=buf.size();
  QMutexLocker locker(&mutex);
  while(total<len)
    {
      while(used==cap)
        {
          if(!spaceAvailable.wait(&mutex,timeoutMs)) return total;
        }
      qint64 chunk=qMin(len-total,cap-used);
      qint64 writePos=(readPos+used)%cap;
      qint64 first=qMin(chunk,cap-writePos);
      memcpy(buf.data()+writePos,data+total,first);
      memcpy(buf.data(),data+total+first,chunk-first);
      used+=chunk;
      total+=chunk;
    }
  return total;
}

qint64 qtAudioFifo::take(char *data, qint64 len)
{
  QMutexLocker locker(&mutex);
  return takeLocked(data,len);
}

qint64 qtAudioFifo::takeLocked(char *data, qint64 len)
{
  qint64 cap=buf.size();
  qint64 n=qMin(len,used);
  qint64 first=qMin(n,cap-readPos);
  memcpy(data,buf.constData()+readPos,first);
  memcpy(data+first,buf.constData(),n-first);
  readPos=(readPos+n)%cap;
  used-=n;
  if(n>0) spaceAvailable.wakeAll();
  if(used==0) emptied.wakeAll();
  return n;
}

qint64 qtAudioFifo::count()
{
  QMutexLocker locker(&mutex);
  return used;
}

void qtAudioFifo::clear()
{
  QMutexLocker locker(&mutex);
  readPos=0;
  used=0;
  spaceAvailable.wakeAll();
  emptied.wakeAll();
}

bool qtAudioFifo::waitEmpty(int timeoutMs)
{
  QMutexLocker locker(&mutex);
  while(used>0)
    {
      if(!emptied.wait(&mutex,timeoutMs)) return false;
    }
  return true;
}

qint64 qtAudioFifo::bytesAvailable() const
{
  QMutexLocker locker(&mutex);
  return used+QIODevice::bytesAvailable();
}

qint64 qtAudioFifo::readData(char *data, qint64 maxlen)
{
  QMutexLocker locker(&mutex);
  qint64 n=takeLocked(data,maxlen);
  if(padSilence && (n<maxlen))
    {
      // keep the output stream running, an idle stream would need to be restarted
      memset(data+n,0,maxlen-n);
      n=maxlen;
    }
  return n;
}

qint64 qtAudioFifo::writeData(const char *data, qint64 len)
{
  QMutexLocker locker(&mutex);
  qint64 cap=buf.size();
  qint64 n=len;
  if(n>cap)
    {
      data+=n-cap;
      n=cap;
    }
  if(used+n>cap)
    {
      // overrun: drop the oldest data
      qint64 drop=used+n-cap;
      readPos=(readPos+drop)%cap;
      used-=drop;
    }
  qint64 writePos=(readPos+used)%cap;
  qint64 first=qMin(n,cap-writePos);
  memcpy(buf.data()+writePos,data,first);
  memcpy(buf.data(),data+first,n-first);
  used+=n;
  return len;
}



soundQt::soundQt()
{
  audioInput=nullptr;
  audioOutput=nullptr;
  captureFifo=nullptr;
  playbackFifo=nullptr;
  inChannels=MONOCHANNEL;
  outputLatencyMs=0;
  audioContext=new QObject;
  audioContext->moveToThread(&audioThread);
  audioThread.setObjectName("qtaudio");
  audioThread.start(QThread::HighPriority);
}

soundQt::~soundQt()
{
  closeDevices();
  audioThread.quit();
  audioThread.wait();
  delete audioContext;
}

bool soundQt::init(int samplerate)
{
  bool ok=false;
  closeDevices();
  sampleRate=samplerate;
  QMetaObject::invokeMethod(audioContext,[this,&ok]{ok=openDevices();},Qt::BlockingQueuedConnection);
  soundDriverOK=ok;
  return ok;
}

void soundQt::closeDevices()
{
  soundDriverOK=false;
  QMetaObject::invokeMethod(audioContext,[this]{releaseDevices();},Qt::BlockingQueuedConnection);
}

// runs in audioThread
void soundQt::releaseDevices()
{
  if(audioInput!=nullptr)
    {
      audioInput->stop();
      delete audioInput;
      audioInput=nullptr;
    }
  if(audioOutput!=nullptr)
    {
      audioOutput->stop();
      delete audioOutput;
      audioOutput=nullptr;
    }
  delete captureFifo;
  captureFifo=nullptr;
  delete playbackFifo;
  playbackFifo=nullptr;
}

QAudioDeviceInfo soundQt::findDevice(QString name, QAudio::Mode mode)
{
  if(name.isEmpty() || (name=="default"))
    {
      if(mode==QAudio::AudioInput) return QAudioDeviceInfo::defaultInputDevice();
      return QAudioDeviceInfo::defaultOutputDevice();
    }
  foreach(const QAudioDeviceInfo &dev,QAudioDeviceInfo::availableDevices(mode))
    {
      if(dev.deviceName()==name) return dev;
    }
  return QAudioDeviceInfo();
}

// runs in audioThread
bool soundQt::openDevices()
{
  QAudioDeviceInfo inDev=findDevice(qtInputAudioDevice,QAudio::AudioInput);
  QAudioDeviceInfo outDev=findDevice(qtOutputAudioDevice,QAudio::AudioOutput);
  if(inDev.isNull())
    {
      errorHandler("Input audio device not found:",qtInputAudioDevice);
      return false;
    }
  if(outDev.isNull())
    {
      errorHandler("Output audio device not found:",qtOutputAudioDevice);
      return false;
    }

  QAudioFormat format;
  format.setSampleRate(sampleRate);
  format.setSampleSize(16);
  format.setCodec("audio/pcm");
  format.setByteOrder(QAudioFormat::LittleEndian);
  format.setSampleType(QAudioFormat::SignedInt);

  // capture: stereo if possible (left and right are mixed to mono), otherwise mono
  inChannels=STEREOCHANNEL;
  format.setChannelCount(inChannels);
  if(!inDev.isFormatSupported(format))
    {
      inChannels=MONOCHANNEL;
      format.setChannelCount(inChannels);
      if(!inDev.isFormatSupported(format))
        {
          errorHandler(QString("16 bit %1 Hz capture not supported by").arg(sampleRate),inDev.deviceName());
          return false;
        }
    }
  captureFifo=new qtAudioFifo((qint64)BUFFERSIZE*2*sizeof(qint16)*inChannels,false);
  audioInput=new QAudioInput(inDev,format);
  audioInput->setBufferSize(PERIODSIZE*sizeof(qint16)*inChannels);
  audioInput->start(captureFifo);
  if(audioInput->error()!=QAudio::NoError)
    {
      errorHandler("Unable to open",inDev.deviceName());
      releaseDevices();
      return false;
    }

  // playback: always stereo
  format.setChannelCount(STEREOCHANNEL);
  if(!outDev.isFormatSupported(format))
    {
      errorHandler(QString("16 bit %1 Hz stereo playback not supported by").arg(sampleRate),outDev.deviceName());
      releaseDevices();
      return false;
    }
  playbackFifo=new qtAudioFifo((qint64)BUFFERSIZE*sizeof(SOUNDFRAME),true);
  audioOutput=new QAudioOutput(outDev,format);
  audioOutput->setBufferSize(2*PERIODSIZE*sizeof(SOUNDFRAME));
  audioOutput->start(playbackFifo);
  if(audioOutput->error()!=QAudio::NoError)
    {
      errorHandler("Unable to open",outDev.deviceName());
      releaseDevices();
      return false;
    }
  outputLatencyMs=(1000*(qint64)audioOutput->bufferSize())/(sampleRate*(qint64)sizeof(SOUNDFRAME));
  addToLog(QString("Qt audio: input %1 (%2 ch), output %3, output latency %4 ms")
           .arg(inDev.deviceName()).arg(inChannels).arg(outDev.deviceName()).arg(outputLatencyMs),LOGSOUND);
  return true;
}

int soundQt::read(int &countAvailable)
{
  int i;
  if(!soundDriverOK) return 0;
  qint64 frameBytes=sizeof(qint16)*inChannels;
  countAvailable=captureFifo->count()/frameBytes;
  if(countAvailable<DOWNSAMPLESIZE) return 0;
  captureFifo->take((char *)tempRXBuffer,DOWNSAMPLESIZE*frameBytes);
  if(inChannels==STEREOCHANNEL)
    {
      // mono radio audio may arrive on left, right or both channels: mix them
      for(i=0;i<DOWNSAMPLESIZE;i++)
        {
          tempRXBuffer[i]=(qint16)(((int)tempRXBuffer[2*i]+(int)tempRXBuffer[2*i+1])/2);
        }
    }
  return DOWNSAMPLESIZE;
}

int soundQt::write(uint numFrames)
{
  uint i;
  quint32 smp;
  if(!soundDriverOK) return 0;
  if(!pttToneOtherChannel)
    {
      // same signal on left and right channel (synthesizer puts it on the left, or right if swapped)
      for(i=0;i<numFrames;i++)
        {
          if(swapChannel) smp=tempTXBuffer[i]>>16;
          else smp=tempTXBuffer[i]&0xFFFF;
          tempTXBuffer[i]=smp|(smp<<16);
        }
    }
  qint64 len=numFrames*sizeof(SOUNDFRAME);
  // blocks until there is room in the fifo, this paces the playback like a blocking ALSA write
  qint64 written=playbackFifo->put((const char *)tempTXBuffer,len,2000);
  if(written!=len)
    {
      errorHandler("Sound write error",QString("Frames written = %1").arg(written/sizeof(SOUNDFRAME)));
    }
  return written/sizeof(SOUNDFRAME);
}

void soundQt::waitPlaybackEnd()
{
  if(!soundDriverOK) return;
  playbackFifo->waitEmpty(2000);
  msleep(outputLatencyMs);
}

void soundQt::flushCapture()
{
  if(!soundDriverOK) return;
  captureFifo->clear();
}

void soundQt::flushPlayback()
{
  if(!soundDriverOK) return;
  playbackFifo->clear();
}


void getQtAudioCardList(QStringList &inputList, QStringList &outputList)
{
  inputList.clear();
  outputList.clear();
  inputList.append("default");
  outputList.append("default");
  foreach(const QAudioDeviceInfo &dev,QAudioDeviceInfo::availableDevices(QAudio::AudioInput))
    {
      if(!inputList.contains(dev.deviceName())) inputList.append(dev.deviceName());
    }
  foreach(const QAudioDeviceInfo &dev,QAudioDeviceInfo::availableDevices(QAudio::AudioOutput))
    {
      if(!outputList.contains(dev.deviceName())) outputList.append(dev.deviceName());
    }
  // PipeWire/PulseAudio devices (alsa_input.usb-..., bluez_...) first, raw ALSA devices (hw:..., front:...) and plugins after them
  auto pulseFirst=[](const QString &a,const QString &b)
  {
    bool pa=(a=="default") || (a.contains('.') && !a.contains(':'));
    bool pb=(b=="default") || (b.contains('.') && !b.contains(':'));
    return pa && !pb;
  };
  std::stable_sort(inputList.begin(),inputList.end(),pulseFirst);
  std::stable_sort(outputList.begin(),outputList.end(),pulseFirst);
}
