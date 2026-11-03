#include "syncprocessor.h"
#include "appglobal.h"
#include "sstvparam.h"
#include "configparams.h"
#include "dispatchevents.h"
#include "dispatcher.h"
#include "modes/modes.h"

#ifndef QT_NO_DEBUG
#include "scope/scopeview.h"
#endif

#define LINETOLERANCEMODEDETECT 0.008
#define LINETOLERANCEINSYNC 0.008
#define FILTERDELAYCORRECTION  +9
//#define MINVOLUME 80
#define MINSYNCWIDTHRATIO 0.8
#define MAXSYNCWIDTHRATIO 2


#define NUMBEROFSENSITIVITIES 4

//#define DISABLERETRACE
//#define DISABLEDETECT
#ifdef DISABLENARROW
#pragma message "NARROW DETECTION DISABLED"
#endif


ssenitivity sensitivityArray[NUMBEROFSENSITIVITIES]=
{
  // minMatchedLines maxLineDistanceModeDetect maxLineDistanceInSync onRatio offRatio minVolume maxTempOutOfSyncLines maxOutOfSyncLines;
  {         4,                2,                       7,              0.5,    0.3,   2000,        10, 15},
  {         4,                2,                       7,              0.5  ,  0.3,   1000,        10, 20},
  {         4,                2,                       7,              0.5,    0.3,   100,         15, 30},
  {         4,                2,                       7 ,             0.5,    0.3,   500,         15, 30}
};




const QString syncStateStr[syncProcessor::SYNCVALID+1]=
{
  "SYNCOFF",
  "SYNCUP",
  "SYNCVALID"
};

const QString stateStr[syncProcessor::RETRACEWAIT+1]=
{
  "Mode detect",
  "In Sync",
  "Sync Lost New Mode",
  "Sync Lost False syncs",
  "Sync Lost Missing Lines",
  "Sync Lost",
  "Retrace Wait"
};

/*!
 * \brief syncProcessor::syncProcessor constructor
 *
 * Process the received sync pulses. When a sync pulse is detected, the samplecounter reference is stored in the syncArray.
 * Concurrent with this processing, the VIS decoding and FSKid decoding is done.
 *
 * \param narrow if true then this instance will use narrow settings, else wide settings
 * \param parent
 */



syncProcessor::syncProcessor(bool narrow, QObject *parent) :  QObject(parent),streamDecode(narrow)
{
  detectNarrow=narrow;
  currentModePtr=NULL;
  visMode=NOTVALID;
  maxLineSamples=getMaxLineSamples();
  //  syncFound=false;
  if(!detectNarrow)
  {
    connect(streamDecode.getFskDecoderPtr(),SIGNAL(callReceived(QString)),SLOT(slotNewCall(QString)));
    connect(streamDecode.getVisDecoderPtr(),SIGNAL(visCodeWideDetected(int,uint)),SLOT(slotVisCodeDetected(int,uint)));
  }
  else
  {
    connect(streamDecode.getVisDecoderPtr(),SIGNAL(visCodeNarrowDetected(int,uint)),SLOT(slotVisCodeDetected(int,uint)));
  }
}

syncProcessor::~syncProcessor()
{
  int i;
  for(i=0;i<=ENDNARROW;i++)
  {
    matchArray[i].clear();
  }
  if(currentModePtr!=NULL) delete currentModePtr;
}

void syncProcessor::reset()
{
  sampleCounter=0;
  init();
  streamDecode.reset();

#ifndef QT_NO_DEBUG
  scopeViewerSyncNarrow->setCurveName("SYNC VOL",SCDATA1);
  scopeViewerSyncNarrow->setCurveName("INPUT VOL",SCDATA2);
  scopeViewerSyncNarrow->setCurveName("SYNC STATE",SCDATA3);
  scopeViewerSyncNarrow->setCurveName("FREQ",SCDATA4);
  scopeViewerSyncNarrow->setAxisTitles("Samples","int","State");

  scopeViewerSyncWide->setCurveName("SYNC VOL",SCDATA1);
  scopeViewerSyncWide->setCurveName("INPUT VOL",SCDATA2);
  scopeViewerSyncWide->setCurveName("SYNC STATE",SCDATA3);
  scopeViewerSyncWide->setCurveName("FREQ",SCDATA4);
  scopeViewerSyncWide->setAxisTitles("Samples","int","State");
#endif

}

/**
 * @brief initialize sync processor
 * idxStart and idxEnd are set for narrow or wide modes in Auto mode, else idxStart=idxEnd= selected mode
 * enableSyncDetection is set to false for AVT modes.
 *
 */
void syncProcessor::init()
{
  enableSyncDetection=true;
  //  syncAvg=0;

  if(detectNarrow)
  {
    idxStart=STARTNARROW;
    idxEnd=ENDNARROW;
  }
  else
  {
    idxStart=STARTWIDE;
    idxEnd=ENDWIDE;
  }

  if(sstvModeIndexRx!=0)
  {
    //      if((sstvModeIndexRx>=idxStart) && (sstvModeIndexRx<=idxEnd))
    {
      idxEnd=idxStart=(esstvMode)(sstvModeIndexRx-1);
      if((idxStart>=AVT24)&&(idxStart<=AVT94))
      {
        enableSyncDetection=false;
      }
    }
  }
  visMode=NOTVALID; // and reset the visMode
  syncProcesState=MODEDETECT;
  appliedTrimPpm=rxSlantTrimPpm.loadRelaxed();
  modifiedClock=rxClock*(1.+appliedTrimPpm*1e-6)/SUBSAMPLINGFACTOR;
  syncArrayIndex=0;
  syncArray[0].init();
  retraceFlag=false;
  syncState=SYNCOFF;
  displaySyncEvent* ce;
  ce = new displaySyncEvent(0); // reset sync vuMeter
  QApplication::postEvent(dispatcherPtr, ce);
  addToLog("init called",LOGSYNCSTATE);
  clearMatchArray();
  currentMode=NOTVALID;
  lineTolerance=LINETOLERANCEMODEDETECT;
  minMatchedLines=sensitivityArray[sensitivity].minMatchedLines;
}

/**
 * @brief process the data buffer to extract syncs
 *
 *
 * It calls extract sync and the streamDecode to extarct FSKid and VIS Code
 *
 */


void syncProcessor::process()
{
#ifdef DISABLENARROW
  if(!detectNarrow)
  {
#endif

    streamDecode.process(freqPtr,sampleCounter); // only VIS and FSK if not narrow
    extractSync();
#ifdef DISABLENARROW
  }
#endif

#ifndef QT_NO_DEBUG
  if(detectNarrow)
  {
    scopeViewerSyncNarrow->addData(SCDATA1,syncVolumePtr,sampleCounter,RXSTRIPE);
    //      scopeViewerSyncNarrow->addData(SCDATA2,inputVolumePtr,sampleCounter,RXSTRIPE);
    scopeViewerSyncNarrow->addData(SCDATA2,inputVolumePtr,sampleCounter,RXSTRIPE);
    scopeViewerSyncNarrow->addData(SCDATA3,syncStateBuffer,sampleCounter,RXSTRIPE);
    scopeViewerSyncNarrow->addData(SCDATA4,freqPtr,sampleCounter,RXSTRIPE);
  }
  else
  {
    scopeViewerSyncWide->addData(SCDATA1,syncVolumePtr,sampleCounter,RXSTRIPE);
    //      scopeViewerSyncWide->addData(SCDATA2,syncAvgPtr,sampleCounter,RXSTRIPE);
    scopeViewerSyncWide->addData(SCDATA2,inputVolumePtr,sampleCounter,RXSTRIPE);
    scopeViewerSyncWide->addData(SCDATA3,syncStateBuffer,sampleCounter,RXSTRIPE);
    scopeViewerSyncWide->addData(SCDATA4,freqPtr,sampleCounter,RXSTRIPE);
  }
#endif
}


/**
 * @brief extract the syncs from the data
 *
 * The function fills in the syncArray en calls validateSync.
 * If syncProcesState==INSYNC then the out of sync is tracked
 * The last check is to see if we received a valid VIS code
 *
 */

void syncProcessor::extractSync()
{
  int i;
  for(i=0;i<RXSTRIPE;i++)
  {
    switch(syncState)
    {
    case SYNCVALID:
    case SYNCOFF:
      if(inputVolumePtr[i]<sensitivityArray[sensitivity].minVolume) break;
      if(syncVolumePtr[i]>sensitivityArray[sensitivity].onRatio*inputVolumePtr[i])
      {
        syncArray[syncArrayIndex].start=sampleCounter+i;
        syncArray[syncArrayIndex].startVolume=syncVolumePtr[i];
        switchSyncState(SYNCACTIVE,sampleCounter+i);
      }
      break;
    case SYNCACTIVE:
      if(inputVolumePtr[i]<sensitivityArray[sensitivity].minVolume)
      {
        switchSyncState(SYNCOFF,sampleCounter+i);
        break;
      }
      if(syncVolumePtr[i]<sensitivityArray[sensitivity].offRatio*inputVolumePtr[i])
      {
        syncArray[syncArrayIndex].end=sampleCounter+i;
        if(validateSync())
        {
          switchSyncState(SYNCVALID,sampleCounter+i);
        }
        else
        {
          switchSyncState(SYNCOFF,sampleCounter+i);
        }
      }
      break;
    }
#ifndef QT_NO_DEBUG
    syncStateBuffer[i]=(unsigned char)syncState*STATESCALER;
#endif
  }

  if((syncProcesState==INSYNC) && (enableSyncDetection))
  {
    trackHold();
  }
  if((visMode>=AVT24)&&(visMode<=AVT94))
  {
    currentMode=visMode;
    enableSyncDetection=false;
    createModeBase();
    visMode=NOTVALID;
    switchProcessState(INSYNC);
  }
}

/**
 * @brief validate sync and build up syncArray
 *
 * The function evaluates the sync pulse width and sets the retrace flag
 * *
 * \retval false if no valid sync and no further processing is neeeded
 *         true if a new syncArray entry is added and further processing is necessary
 */


bool syncProcessor::validateSync()
{
  bool result;
#ifndef DISABLERETRACE
  if(syncArray[syncArrayIndex].diffStartEnd()>=MINRETRACEWIDTH)
  {
    syncArray[syncArrayIndex].retrace=true; // simply set retrace true
    result=true;
  }
  else if(syncArray[syncArrayIndex].diffStartEnd()>=MINRETRACEWIDTH/4)
  {
    // merging pulses into a retrace is only done while hunting: during a fade or QRM the fragments
    // of noise would otherwise end the image that is being received
    if((syncProcesState!=INSYNC) && (syncArrayIndex>2)
       && (syncArray[syncArrayIndex].end-syncArray[syncArrayIndex-1].start)>=MINRETRACEWIDTH
       && (syncArray[syncArrayIndex].start-syncArray[syncArrayIndex-1].end)<=MINRETRACEWIDTH/5)
    {
      syncArray[syncArrayIndex-1].end=syncArray[syncArrayIndex].end;
      syncArray[syncArrayIndex-1].retrace=true;
      syncArray[syncArrayIndex-1].diffStartEnd(); // just calculate the width;
      syncArrayIndex--;
    }
    result=true;
  }
  else if((syncArray[syncArrayIndex].diffStartEnd()>=0.004*SAMPLERATE) && (syncArray[syncArrayIndex].diffStartEnd()<=0.025*SAMPLERATE))
#else
  if((syncArray[syncArrayIndex].diffStartEnd()>=0.004*SAMPLERATE) && (syncArray[syncArrayIndex].diffStartEnd()<=0.025*SAMPLERATE))
#endif
  {
    syncArray[syncArrayIndex].retrace=false;
    //      addToLog(QString("index %1, mid:=%2").arg(syncArrayIndex).arg(syncArray[syncArrayIndex].mid),LOGSYNCACCEPTED);
    result=true;
  }
  else
  {
    //      addToLog(QString("Sync rejected:%1 end:%2 width %3").arg(syncArray[syncArrayIndex].start)
    //               .arg(syncArray[syncArrayIndex].end).arg(syncArray[syncArrayIndex].diffStartEnd()),LOGSYNCREJECTED);
    result=false;
  }

  if(result)
  {
    checkSyncArray();
    switch(syncProcesState)
    {
    case MODEDETECT:
      if(findMatch())
      {
        visMode=NOTVALID; //reset visMode;
#ifndef DISABLEDETECT
        //we have a new mode
        if(!createModeBase())
        {
          addToLog("Error creating modeBase",LOGALL);
          result=false;
        }
        else
        {
          lineTolerance=LINETOLERANCEINSYNC;
          // when we have S1,S2 or SDX then we have to set the syncposition at the beginning of the green line
          //          syncPosition=currentModePtr->adjustSyncPosition(syncArray[0].end)- FILTERDELAYCORRECTION; // type 1 sync end
          unsigned int syncCorrected;;
          syncWidth=getSyncWidth(currentMode ,modifiedClock);

          if(syncArray[0].retrace)
          {
            syncCorrected=syncArray[0].end;
          }
          else
          {
            syncCorrected=(syncArray[0].start+syncArray[0].end)/2+syncWidth/2;
          }

          syncPosition=currentModePtr->adjustSyncPosition(syncCorrected,syncArray[0].retrace)+FILTERDELAYCORRECTION; // type 2 sync end
          tempOutOfSync=false;
          trackInit(syncCorrected);
          switchProcessState(INSYNC);
          addToLog(QString("Mode detected: %1, sync at %2").arg(getSSTVModeNameShort(currentMode)).arg(syncPosition),LOGSYNCEVAL);
        }
#endif
      }
      break;
    case INSYNC:
      if(enableSyncDetection)
      {
        if(retraceFlag) switchProcessState(RETRACEWAIT);
        else trackAdd(syncArray[syncArrayIndex]);
      }
      break;
    case SYNCLOSTNEWMODE:
    case SYNCLOSTFALSESYNC:
    case SYNCLOSTMISSINGLINES:
    case SYNCLOST:
      addToLog("synclost detected",LOGSYNCSTATE);
      break;
    case RETRACEWAIT:
      break;

    }
    syncArrayIndex++;
    syncArray[syncArrayIndex].init();
  }
  return result;
}


void syncProcessor::slotNewCall(QString call)
{
  emit callReceived(call);
  // the FSK ID follows the image: only use it to end an image that is (almost) complete,
  // an ID decoded from noise in the middle of the image must not restart the reception
  if((syncProcesState==INSYNC) && (currentModePtr!=NULL)
     && (currentModePtr->receivedLines()>=(currentModePtr->imageLines()*9)/10))
  {
    retraceFlag=true;
  }
}

void syncProcessor::slotVisCodeDetected(int mode,uint visSampleCounter)
{
  if(syncProcesState==INSYNC) holdVis=true;
  if((mode>=idxStart) && (mode<=idxEnd))
  {
    visMode=(esstvMode)mode;
    if((visMode>=AVT24)&&(visMode<=AVT94))
    {
      enableSyncDetection=false;
    }
    addToLog(QString("viscode %1 accepted").arg(visMode),LOGSYNCACCEPTED);
    idxStart=idxEnd=(esstvMode)mode;
    minMatchedLines=3;
    visEndCounter=visSampleCounter;
  }
  else
  {
    visMode=NOTVALID;
  }
}

/**
 * @brief syncProcessor::calculateLineNumber
 * @param fromIdx
 * @param toIdx
 */

void  syncProcessor::calculateLineNumber(uint fromIdx,uint toIdx)
{
  quint16 lnbr;
  double fract;
  lineCompare(samplesPerLine,0,toIdx,lnbr,fract);
  syncArray[toIdx].length=syncArray[toIdx].end-syncArray[fromIdx].end;
  syncArray[toIdx].lineNumber=lnbr;
  lastUpdatedSync=toIdx;
}

void syncProcessor::checkSyncArray()
{

  if(syncArray[syncArrayIndex].retrace)
  {
    syncArray[0]=syncArray[syncArrayIndex];
    syncArrayIndex=0;
    //      syncPosition=syncArray[0].end + FILTERDELAYCORRECTION;
    addToLog(QString("Found retrace: start:%1 end:%2  width %3").arg(syncArray[0].start).arg(syncArray[0].end).arg(syncArray[0].end-syncArray[0].start),LOGSYNCACCEPTED);
    if(syncProcesState==INSYNC)
    {
      retraceFlag=true;
      syncProcesState=RETRACEWAIT;
    }
  }

  if(syncArrayIndex>=(MAXSYNCENTRIES-1))
  {
    //shift syncArray
    dropTop();
  }
  addToLog(QString("Found sync: start:%1 end:%2 width %3 at %4").arg(syncArray[syncArrayIndex].start).arg(syncArray[syncArrayIndex].end).arg(syncArray[syncArrayIndex].end-syncArray[syncArrayIndex].start).arg(syncArrayIndex),LOGSYNCACCEPTED);
}


/**
 * @brief syncProcessor::findMatch
 The syncArray is filled with valid synPositions.
 idxStart and idxEnd indicate the sstv modes to compare the sync pulse intervals with.


* @return true if match

*/


bool syncProcessor::findMatch()
{
  int i,j,k,m;
  int fs;
  uint minTotLines=9999;
  double minFract=1;
  int idx=-1;
  QList<modeMatchList *> changeList;
  QList <int> modeList;
  QList<uint> totalLinesList;
  QList<double> totalFractList;

  if(enableSyncDetection)
  {
    //      addToLog (QString(" checking match for syncArrayIndex %1 at %2").arg(syncArrayIndex).arg(syncArray[syncArrayIndex].end),LOGSYNCMATCH);
    for(i=idxStart;i<=idxEnd;i++)
    {
      syncWidth=getSyncWidth((esstvMode)i ,modifiedClock);
      if(addToMatch((esstvMode)i))
      {
        for(j=0;j<matchArray[i].count();j++)
        {
          if(matchArray[i][j]->count()>=(int)minMatchedLines)
          {
            fs=0;
            for(k=0;k<matchArray[i][j]->count()-1;k++)
            {
              fs+=matchArray[i][j]->at(k)->to-matchArray[i][j]->at(k)->from-1;
            }
            if (fs<20)
            {
              changeList.append(matchArray[i][j]);
              modeList.append(i);
            }
          }

        }
      }
    }
    for (m=0;m<changeList.count();m++)
    {
      totalLinesList.append(calcTotalLines( changeList.at(m)));
      totalFractList.append(calcTotalFract( changeList.at(m)));
    }
    for (m=0;m<changeList.count();m++)
    {
      if((minTotLines>=totalLinesList.at(m)) && (totalFractList.at(m)<=minFract))
      {
        idx=m;
        minTotLines=totalLinesList.at(m);
        minFract=totalFractList.at(m);
      }
    }


    if(idx>=0)
    {
      if(visMode!=NOTVALID)
      {
        currentMode=visMode;
      }
      else
      {
        currentMode=(esstvMode)modeList.at(idx);
      }

      activeChainPtr=changeList.at(idx);
      samplesPerLine=getLineLength(currentMode,modifiedClock);
      cleanupMatchArray();
      return true;
    }
    return false;
  }

  return false;
}


uint syncProcessor::calcTotalLines(modeMatchList *mlPtr)
{
  int i;
  uint lines=0;
  for(i=0;i<mlPtr->count();i++)
  {
    lines+=mlPtr->at(i)->lineSpacing;
  }
  return lines;
}

double syncProcessor::calcTotalFract(modeMatchList *mlPtr)
{

  int i;
  double fract=0;
  for(i=0;i<mlPtr->count();i++)
  {
    fract+=mlPtr->at(i)->fraction;
  }
  return fract;

}

/**
 * @brief syncProcessor::addToMatch
 * @param mode sstv mode to be evaluated
 * @return true if added to chain
 */

bool syncProcessor::addToMatch(esstvMode mode)
{
  int  i;
  
  if(syncArrayIndex<1) return false;
  for(i=syncArrayIndex-1;i>=0;i--)
  {

    if(addToChain(mode,i))
    {
      return true;
    }
  }
  return false;
}






bool syncProcessor::addToChain(esstvMode mode,  uint fromIdx)
{
  int i;
  double fract;
  quint16 lineSpacing;

  samplesPerLine=getLineLength(mode,modifiedClock);
  
  if(!lineCompare(samplesPerLine,fromIdx,syncArrayIndex,lineSpacing,fract))
  {
    return false;
  }


  if((syncArray[syncArrayIndex].diffStartEnd()<syncWidth*MINSYNCWIDTHRATIO)
     || (syncArray[syncArrayIndex].diffStartEnd()>syncWidth*MAXSYNCWIDTHRATIO)
     || (syncArray[fromIdx].diffStartEnd()<syncWidth*MINSYNCWIDTHRATIO))
  {

    return false;
  }

  if (syncArray[fromIdx].diffStartEnd()>syncWidth*MAXSYNCWIDTHRATIO)
  {
    if(fromIdx!=0 || !syncArray[0].retrace)
    {
      return false;
    }
  }
  if(syncProcesState==MODEDETECT)
  {
    if(lineSpacing>sensitivityArray[sensitivity].maxLineDistanceModeDetect)
    {
      return false;
    }
  }

  bool found=false;
  if(matchArray[mode].count()==0) // we don't have a chain yet
  {
    matchArray[mode].append(new modeMatchList);
    matchArray[mode][0]->append(new smatchEntry(fromIdx,syncArrayIndex,lineSpacing,fract,syncArray[fromIdx].end,syncArray[syncArrayIndex].end));
    //      addToLog(QString("Match: mode=%1,new chain=%2 syncIndex=%3 end=%4").arg(getSSTVModeNameShort(mode)).arg(matchArray[mode].count()-1).arg(syncArrayIndex).arg(syncArray[syncArrayIndex].end),LOGSYNCMATCH);
  }
  else
  {
    // can we append this to an existing chain?
    for(i=0;i<matchArray[mode].count();i++)
    {
      if(matchArray[mode][i]->last()->to==fromIdx)
      {
        if((syncProcesState==INSYNC) && (i==0) )
        {
          currentModeMatchChanged=true;
        }
        matchArray[mode][i]->append(new smatchEntry(fromIdx,syncArrayIndex,lineSpacing,fract,syncArray[fromIdx].end,syncArray[syncArrayIndex].end));
        //              addToLog(QString("Match: mode=%1,chain=%2 syncIndex=%3 end=%4").arg(getSSTVModeNameShort(mode)).arg(i).arg(syncArrayIndex).arg(syncArray[syncArrayIndex].end),LOGSYNCMATCH);
        found=true;
        break;
      }
    }
    if(!found)
    {
      matchArray[mode].append(new modeMatchList);
      matchArray[mode].last()->append(new smatchEntry(fromIdx,syncArrayIndex,lineSpacing,fract,syncArray[fromIdx].end,syncArray[syncArrayIndex].end));
      //          addToLog(QString("Match: mode=%1,new chain=%2 syncIndex=%3 end=%4").arg(getSSTVModeNameShort(mode)).arg(matchArray[mode].count()-1).arg(syncArrayIndex).arg(syncArray[syncArrayIndex].end),LOGSYNCMATCH);
    }

  }
  return true;
}


void syncProcessor::clearMatchArray()
{
  int i,j;
  for(i=idxStart;i<=idxEnd;i++)
  {
    for(j=0;j<matchArray[i].count();j++)
    {
      removeMatchArrayChain((esstvMode)i,0);
    }
    matchArray[i].clear();
  }
  activeChainPtr=NULL;
  switchProcessState(MODEDETECT);
}

void syncProcessor::removeMatchArrayChain(esstvMode mode,int chainIdx)
{
  int i;
  for(i=0;i<matchArray[mode][chainIdx]->count();i++)
  {
    delete matchArray[mode][chainIdx]->at(i);
  }
  matchArray[mode][chainIdx]->clear();
  delete matchArray[mode][chainIdx];
  matchArray[mode].takeAt(chainIdx);
}

void syncProcessor::cleanupMatchArray()
{
  int i,j;
  //  double fract=0;
  //  quint16 lnbr=0;
  for(i=idxStart;i<=idxEnd;i++)
  {
    for(j=0;j<matchArray[i].count();)
    {
      if(activeChainPtr!=matchArray[i][j])
      {
        removeMatchArrayChain((esstvMode)i,j);
      }
      else
      {
        j++;
      }
    }
  }

  for(i=0;i<activeChainPtr->count();i++)
  {
    syncArray[activeChainPtr->at(i)->from].inUse=true;
    syncArray[activeChainPtr->at(i)->to].inUse=true;
  }
  for(i=0;i<syncArrayIndex;)
  {
    if(!syncArray[i].inUse)
    {
      deleteSyncArrayEntry(i);
    }
    else
    {
      i++;
    }
  }
  for(i=0;i<syncArrayIndex;i++)
  {
    calculateLineNumber(i,i+1);
  }
}

void syncProcessor::dropTop()
{
  deleteSyncArrayEntry(0);
}

void syncProcessor::deleteSyncArrayEntry(uint entry)
{
  int i,j,k;
  modeMatchList *ml;
  //  smatchEntry *tempPtr;
  if(entry>=syncArrayIndex) return;
  // delete or adapt the matchArrays
  for(i=idxStart;i<=idxEnd;i++)   //all modes
  {
    for(j=0;j<matchArray[i].count();) //all chains
    {
      for(k=0;k<matchArray[i][j]->count();)
      {
        ml=matchArray[i][j];
        if (ml->at(k)->from==entry)
        {
          delete matchArray[i][j]->takeAt(k);
        }
        else
        {
          if(ml->at(k)->from>entry)
          {
            ml->at(k)->from--;
          }
          if(ml->at(k)->to>entry)
          {
            ml->at(k)->to--;
          }
          k++;
        }
      }
      if(matchArray[i][j]->count()==0)
      {
        if(activeChainPtr==matchArray[i][j])
        {
          activeChainPtr=NULL;
        }
        delete matchArray[i].takeAt(j);
      }
      else
      {
        j++;
      }
    }
  }
  for(i=entry;i<(int)syncArrayIndex;i++)
  {
    syncArray[i]=syncArray[i+1];
  }
  syncArrayIndex--;
}


/**
 * @brief syncProcessor::recalculateMatchArray
 * \todo recalculate
 */

void syncProcessor::recalculateMatchArray()
{
}


/**
 * @brief syncProcessor::lineCompare
 * @param samPerLine samples per line
 * @param srcIdx from sync array entry
 * @param dstIdx to sync array entry
 * @param lineNumber number of lines
 * @param fraction fractional part
 * @return true if a match for that line (i.e fractional part is smaller than lineTolerance
 */


bool syncProcessor::lineCompare(DSPFLOAT samPerLine, int srcIdx, int dstIdx, quint16 &lineNumber, double &fraction)
{
  double delta;
  delta=(double)(syncArray[dstIdx].end-syncArray[srcIdx].end);

  lineNumber=(delta+samPerLine/2.) /samPerLine;
  fraction=(double)lineNumber-delta/samPerLine;
  if(fraction<0) fraction=-fraction;
  //  if(fraction<lineTolerance)
  //  addToLog(QString("Lnbr: %1, fract: %2, delta: %3 src: %4,dest: %5, OK %6")
  //           .arg(lineNumber).arg(fraction).arg(delta).arg(srcIdx).arg(dstIdx).arg(fraction<lineTolerance)
  //           ,LOGSYNCCOMP);
  return (fraction<lineTolerance);
}

void syncProcessor::resetRetraceFlag()
{
  retraceFlag=false;
  clearMatchArray();
}


bool  syncProcessor::createModeBase()
{
  bool done=false;
  if(currentModePtr) delete currentModePtr;
  currentModePtr=NULL;
  switch (currentMode)
  {
  case M1:
  case M2:
    currentModePtr=new modeGBR(currentMode,RXSTRIPE,false,false);
    break;
  case S1:
  case S2:
  case SDX:
    currentModePtr=new modeGBR2(currentMode,RXSTRIPE,false,false);
    break;
  case R36:
    currentModePtr=new modeRobot1(currentMode,RXSTRIPE,false,false);
    break;
  case R24:
  case R72:
  case MR73:
  case MR90:
  case MR115:
  case MR140:
  case MR175:
  case ML180:
  case ML240:
  case ML280:
  case ML320:
    currentModePtr=new modeRobot2(currentMode,RXSTRIPE,false,false);
    break;
  case SC2_60:
  case SC2_120:
  case SC2_180:
  case P3:
  case P5:
  case P7:
  case MC110N:
  case MC140N:
  case MC180N:
    currentModePtr=new modeRGB(currentMode,RXSTRIPE,false,false);
    break;
  case FAX480:
  case BW8:
  case BW12:
    currentModePtr=new modeBW(currentMode,RXSTRIPE,false,false);
    break;
  case AVT24:
  case AVT90:
  case AVT94:
    currentModePtr=new modeAVT(currentMode,RXSTRIPE,false,false);
    break;
  case PD50:
  case PD90:
  case PD120:
  case PD160:
  case PD180:
  case PD240:
  case PD290:
  case MP73:
  case MP115:
  case MP140:
  case MP175:
    currentModePtr=new modePD(currentMode,RXSTRIPE,false,false);
    break;
  case MP73N:
  case MP110N:
  case MP140N:
    currentModePtr=new modePD(currentMode,RXSTRIPE,false,true);
    break;
  default:
    currentMode=NOTVALID;
    break;
  }
  if (currentMode!=NOTVALID)
  {
    initializeSSTVParametersIndex(currentMode,false);
    QString s=getSSTVModeNameLong(currentMode);
    addToLog(QString("create RX mode: %1").arg(getSSTVModeNameShort(currentMode)),LOGSYNCSTATE);
    currentModePtr->init(modifiedClock);
    startImageRXEvent* ce = new startImageRXEvent(QSize(currentModePtr->imagePixels(),currentModePtr->imageLines()));
    ce->waitFor(&done);
    QApplication::postEvent(dispatcherPtr, ce);
    while(!done)
    {
      QApplication::processEvents();
    }
  }
  return (currentMode!=NOTVALID);
}


/*
  Sync tracking while an image is received.

  The sync pulses lie on a grid: the centre of the sync of line n is at T0 + n*P (P = samples per line).
  T0 and P are fitted (least squares) on the accepted syncs, with the nominal line length as prior for P.
  A sync is only accepted close to its predicted position, so noise and QRM can't pull the model away.
  Missing syncs (fading, dropouts) don't end the image: it is drawn on with the fitted clock (hold)
  until the syncs return on the same grid.
  The image is ended early only when the signal is gone for TRACKMAXHOLD seconds (not in DX), when syncs
  keep coming in another phase (a new transmission) or when a VIS code is received during a hold.
  With auto slant the image is redrawn with the fitted clock when it is about half a pixel off.
*/

#define TRACKSYNCJITTER   3.     // noise of the measured sync position in samples
#define TRACKPRIORPPM     500.   // uncertainty of the nominal clock
#define TRACKMINGATE      0.003  // acceptance window around the predicted sync, fraction of a line
#define TRACKMAXGATE      0.012
#define TRACKMINFIT       8      // accepted syncs needed before the clock is corrected
#define TRACKREDRAWLINES  8      // minimum lines between two redraws
#define TRACKMAXHOLD      30.    // seconds without syncs before the image is given up
#define TRACKRIVALS       5      // syncs on another grid that mean a new transmission
#define TRACKRIVALWINDOW  12     // lines

void syncProcessor::trackInit(unsigned int syncCorrected)
{
  int i;
  QList<uint> idx;
  trackPoints.clear();
  rivalSyncs.clear();
  holdVis=false;
  trackP0=samplesPerLine;
  trackRef=(syncArray[0].start+syncArray[0].end)/2.;
  // the syncs matched during mode detection
  if(activeChainPtr!=NULL)
  {
    for(i=0;i<activeChainPtr->count();i++)
    {
      if(i==0) idx.append(activeChainPtr->at(i)->from);
      idx.append(activeChainPtr->at(i)->to);
    }
  }
  lastAcceptedLine=0;
  foreach(uint k,idx)
  {
    lastValidSyncCounter=syncArray[k].end;
    if(syncArray[k].retrace) continue;
    strackPoint p;
    p.n=round(((double)syncArray[k].end-(double)syncArray[0].end)/trackP0);
    p.t=(syncArray[k].start+syncArray[k].end)/2.-trackRef;
    trackPoints.append(p);
    if(p.n>lastAcceptedLine) lastAcceptedLine=p.n;
  }
  trackFit();
  initSyncCorrected=syncCorrected;
  initRetrace=syncArray[0].retrace;
  // syncCorrected was measured on the first sync (track time 0), after a retrace only the fit is known
  initT0=initRetrace ? trackT0 : 0;
  appliedT0=trackT0;
  appliedP=trackP0;
  lastRedrawLine=lastAcceptedLine;
  addToLog(QString("track init: %1 syncs, line %2, P=%3").arg(trackPoints.count()).arg(lastAcceptedLine).arg(trackP),LOGSLANT);
}

void syncProcessor::trackFit()
{
  // least squares t = T0 + P*n, with prior P = trackP0
  double lambda=(TRACKSYNCJITTER*TRACKSYNCJITTER)/pow(trackP0*TRACKPRIORPPM*1e-6,2);
  double v=TRACKSYNCJITTER*TRACKSYNCJITTER;
  int i,worst;
  for(;;)
  {
    double s0=0,sn=0,snn=0,st=0,snt=0;
    foreach(const strackPoint &p,trackPoints)
    {
      s0+=1;
      sn+=p.n;
      snn+=p.n*p.n;
      st+=p.t;
      snt+=p.n*p.t;
    }
    if(s0==0)
    {
      trackT0=0;
      trackP=trackP0;
      trackVarT0=pow(TRACKMAXGATE*trackP0,2);
      trackCovT0P=0;
      trackVarP=pow(trackP0*TRACKPRIORPPM*1e-6,2);
      return;
    }
    double d=snn+lambda;
    double det=s0*d-sn*sn;
    trackT0=(d*st-sn*(snt+lambda*trackP0))/det;
    trackP=(s0*(snt+lambda*trackP0)-sn*st)/det;
    trackVarT0=v*d/det;
    trackCovT0P=-v*sn/det;
    trackVarP=v*s0/det;
    // drop a sync that doesn't fit (e.g. a false one from mode detection) and fit again
    worst=-1;
    double maxRes=TRACKMINGATE*trackP0;
    for(i=0;i<trackPoints.count();i++)
    {
      double r=fabs(trackPoints.at(i).t-(trackT0+trackPoints.at(i).n*trackP));
      if(r>maxRes)
      {
        maxRes=r;
        worst=i;
      }
    }
    if((worst<0) || (trackPoints.count()<=4)) return;
    trackPoints.removeAt(worst);
  }
}

void syncProcessor::trackAdd(const ssyncEntry &sync)
{
  int i;
  if(sync.retrace) return;
  double width=(double)sync.end-(double)sync.start;
  if((width<syncWidth*MINSYNCWIDTHRATIO) || (width>syncWidth*MAXSYNCWIDTHRATIO)) return;
  double t=(sync.start+sync.end)/2.-trackRef;
  double n=round((t-trackT0)/trackP);
  if(n<0) return;
  double r=t-(trackT0+n*trackP);
  double var=trackVarT0+2*n*trackCovT0P+n*n*trackVarP+TRACKSYNCJITTER*TRACKSYNCJITTER;
  double gate=qBound(TRACKMINGATE*trackP,4*sqrt(var),TRACKMAXGATE*trackP);
  if(fabs(r)>gate)
  {
    addToLog(QString("sync rejected: line %1 residual %2 gate %3").arg(n).arg(r).arg(gate),LOGSYNCREJECTED);
    trackRival(t);
    return;
  }
  // one sync per line, keep the best one
  for(i=0;i<trackPoints.count();i++)
  {
    if(trackPoints.at(i).n==n)
    {
      if(fabs(trackPoints.at(i).t-(trackT0+n*trackP))<=fabs(r)) return;
      trackPoints.removeAt(i);
      break;
    }
  }
  strackPoint p;
  p.n=n;
  p.t=t;
  trackPoints.append(p);
  trackFit();
  rivalSyncs.clear();
  holdVis=false;
  if(n>lastAcceptedLine) lastAcceptedLine=n;
  lastValidSyncCounter=sync.end;
  trackApply();
}

void syncProcessor::trackRival(double t)
{
  // syncs that are not on the grid: a new transmission in another phase when they are regular
  double lastAcceptedT=trackT0+lastAcceptedLine*trackP;
  if(t-lastAcceptedT<3*trackP) return;
  rivalSyncs.append(t);
  while(rivalSyncs.first()<t-TRACKRIVALWINDOW*trackP) rivalSyncs.removeFirst();
  int consistent=0;
  foreach(double o,rivalSyncs)
  {
    double k=(t-o)/trackP;
    if((k>0.5) && (fabs(k-round(k))<LINETOLERANCEINSYNC)) consistent++;
  }
  if(consistent>=TRACKRIVALS)
  {
    addToLog(QString("syncs on another grid: new transmission"),LOGSYNCLOST);
    switchProcessState(SYNCLOST);
  }
}

void syncProcessor::trackHold()
{
  double now=sampleCounter+RXSTRIPE-trackRef;
  double missing=(now-(trackT0+lastAcceptedLine*trackP))/trackP; // lines since the last accepted sync
  tempOutOfSync=false;
  syncQuality=qBound(0,(int)round(11-missing),10);
  if(holdVis && (missing>=2))
  {
    addToLog(QString("VIS code during hold: new transmission"),LOGSYNCLOST);
    switchProcessState(SYNCLOST);
    return;
  }
  if((sensitivity!=(NUMBEROFSENSITIVITIES-1)) && (missing*trackP>TRACKMAXHOLD*modifiedClock))
  {
    addToLog(QString("no syncs for %1 lines").arg(missing),LOGSYNCLOST);
    switchProcessState(SYNCLOST);
  }
}

void syncProcessor::trackApply()
{
  if(!autoSlantAdjust) return;
  if((trackPoints.count()<TRACKMINFIT) || (lastAcceptedLine<TRACKMINFIT)) return;
  double lines=SSTVTable[currentMode].numberOfDataLines;
  double pixel=trackP/(4.*currentModePtr->imagePixels());
  // difference at the end of the image between the fitted and the drawn grid
  double drift=fabs(trackT0-appliedT0)+fabs(trackP-appliedP)*lines;
  if(drift<pixel/2) return;
  if((lastAcceptedLine-lastRedrawLine<TRACKREDRAWLINES) && (drift<4*pixel)) return;
  appliedT0=trackT0;
  appliedP=trackP;
  lastRedrawLine=lastAcceptedLine;
  modifiedClock=trackP*lines/SSTVTable[currentMode].imageTime;
  samplesPerLine=trackP;
  syncPosition=currentModePtr->adjustSyncPosition(initSyncCorrected+(long)round(appliedT0-initT0),initRetrace)+FILTERDELAYCORRECTION;
  newClock=true;
  addToLog(QString("new clock %1 (%2 ppm), T0 %3, drift %4, line %5")
           .arg(modifiedClock).arg((trackP/trackP0-1)*1e6,0,'f',1).arg(trackT0,0,'f',1).arg(drift,0,'f',1).arg(lastAcceptedLine),LOGSLANT);
}


/**
 * @brief apply a changed manual slant correction (rxSlantTrimPpm) to the clock
 * @return true if an image is being received and has to be redrawn with the new clock (hasNewClock() is set)
 */
bool syncProcessor::updateClockTrim()
{
  int trim=rxSlantTrimPpm.loadRelaxed();
  if(trim==appliedTrimPpm) return false;
  double ratio=(1.+trim*1e-6)/(1.+appliedTrimPpm*1e-6);
  modifiedClock*=ratio;
  appliedTrimPpm=trim;
  addToLog(QString("manual slant %1 ppm, clock %2").arg(trim).arg(modifiedClock),LOGSLANT);
  if((syncProcesState!=INSYNC) || (currentMode==NOTVALID)) return false;
  if((currentMode>=AVT24) && (currentMode<=AVT94)) return false;
  trackP0*=ratio;
  appliedP*=ratio;
  samplesPerLine=appliedP;
  syncPosition=currentModePtr->adjustSyncPosition(initSyncCorrected+(long)round(appliedT0-initT0),initRetrace)+FILTERDELAYCORRECTION;
  newClock=true;
  return true;
}

void syncProcessor::switchSyncState(esyncState newState,quint32 sampleCntr)
{
  Q_UNUSED(sampleCntr)
  if(syncState!=newState)
  {
    addToLog(QString("switching from %1 to %2 at %3").arg(syncStateStr[syncState]).arg(syncStateStr[newState]).arg(sampleCntr),LOGSYNCSTATE);
    syncState=newState;
  }
}

void syncProcessor::switchProcessState(esyncProcessState  newState)
{
  addToLog(QString("syncProcessState %1 to %2").arg(stateStr[syncProcesState]).arg(stateStr[newState]),LOGSYNCPROCESSSTATE);
  if((newState==SYNCLOSTFALSESYNC) || (newState==SYNCLOSTNEWMODE)||(newState==SYNCLOSTMISSINGLINES) || (newState==SYNCLOST))
  {
    newState=SYNCLOST;
  }
  syncProcesState=newState;
}

#ifndef QT_NO_DEBUG
void syncProcessor::setOffset(unsigned int dataScopeOffset)
{
  xOffset=dataScopeOffset;
  scopeViewerSyncNarrow->setOffset(xOffset);
  scopeViewerSyncWide->setOffset(xOffset);
}

void syncProcessor::clear()
{
  scopeViewerSyncNarrow->clear();
  scopeViewerSyncWide->clear();
  xOffset=0;
  scopeViewerSyncNarrow->setOffset(xOffset);
  scopeViewerSyncWide->setOffset(xOffset);
}


#endif
