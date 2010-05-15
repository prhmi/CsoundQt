/*
    Copyright (C) 2008, 2009 Andres Cabrera
    mantaraya36@gmail.com

    This file is part of QuteCsound.

    QuteCsound is free software; you can redistribute it
    and/or modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    QuteCsound is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with Csound; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA
    02111-1307 USA
*/

#include "csoundengine.h"
#include "widgetlayout.h"
//#include "curve.h"

#include "console.h"
#include "qutescope.h"  // Needed for passing the ud to the scope for display data
#include "qutegraph.h"  // Needed for passing the ud to the graph for display data

#ifdef Q_OS_WIN32
#include <unistd.h> // for usleep()
#endif

CsoundEngine::CsoundEngine()
{
  // Initialize user data pointer passed to Csound
  ud = new CsoundUserData();
  ud->PERF_STATUS = 0;
  ud->cs = this;
  ud->threaded = true;
  ud->csound = NULL;
  ud->perfThread = 0;
  ud->mouseValues.resize(6); // For _MouseX _MouseY _MouseRelX _MouseRelY _MouseBut1 and _MouseBut2 channels

  pFields = (MYFLT *) calloc(QCS_EVENTS_MAX_PFIELDS, sizeof(MYFLT)); // Maximum number of p-fields for events

  m_recording = false;
  bufferSize = 4096;
  recBuffer = (MYFLT *) calloc(bufferSize, sizeof(MYFLT));

#ifndef QCS_DESTROY_CSOUND
  // Create only once
  ud->csound=csoundCreate(0);
#endif

//  csoundPreCompile(ud->csound);  // Precompile once, to preload dynamic libs (making first run faster)
//  csoundCleanup(ud->csound);
//
//#ifndef QCS_DESTROY_CSOUND
//  csoundDestroy(ud->csound);
//#endif

  eventQueue.resize(QCS_MAX_EVENTS);
  eventTimeStamps.resize(QCS_MAX_EVENTS);
  eventQueueSize = 0;

//  qTimer = new QTimer(this);
  closing = 0;
//  qTimer.setSingleShot(true);
//
//  connect(&qTimer, SIGNAL(timeout()), this, SLOT(dispatchQueues()));
  refreshTime = QCS_QUEUETIMER_DEFAULT_TIME;  // Eventually allow this to be changed
}

CsoundEngine::~CsoundEngine()
{
//  qDebug() << "CsoundEngine::~CsoundEngine() ";
  stop();
  freeze();
  disconnect(this, 0,0,0);
  while (closing == 1) {
    qApp->processEvents();
    usleep(10000);
  }
#ifndef QCS_DESTROY_CSOUND
  csoundDestroy(ud->csound);
#endif
//  flushMessageQueue();
//  free(ud);
//  consoles.clear();
//  for (int i = 0; i < consoles.size(); i++) {
//  }
  free(pFields);
  delete ud;
  delete recBuffer;
}

void CsoundEngine::messageCallbackNoThread(CSOUND *csound,
                                          int /*attr*/,
                                          const char *fmt,
                                          va_list args)
{
  CsoundUserData *ud = (CsoundUserData *) csoundGetHostData(csound);
  QString msg;
  msg = msg.vsprintf(fmt, args);
  for (int i = 0; i < ud->cs->consoles.size(); i++) {
    ud->cs->consoles[i]->appendMessage(msg);
    ud->cs->consoles[i]->scrollToEnd();
  }
}

void CsoundEngine::messageCallbackThread(CSOUND *csound,
                                          int /*attr*/,
                                          const char *fmt,
                                          va_list args)
{

  CsoundUserData *ud = (CsoundUserData *) csoundGetHostData(csound);
  QString msg;
  msg = msg.vsprintf(fmt, args);
  ud->cs->queueMessage(msg);
}

void CsoundEngine::outputValueCallback (CSOUND *csound,
                                     const char *channelName,
                                     MYFLT value)
{
  // Called by the csound running engine when 'outvalue' opcode is used
  // To pass data from Csound to QuteCsound
  CsoundUserData *ud = (CsoundUserData *) csoundGetHostData(csound);
//  if (ud->cs->isRunning()) {
    QString name = QString(channelName);
    ud->cs->perfMutex.lock();
    if (name.startsWith('$')) {
      QString channelName = name;
      channelName.chop(name.size() - (int) value + 1);
      QString sValue = name;
      sValue = sValue.right(name.size() - (int) value);
      channelName.remove(0,1);
      ud->cs->passOutString(channelName, sValue);
    }
    else {
      ud->cs->passOutValue(name, value);
    }
    ud->cs->perfMutex.unlock();
//  }
}

void CsoundEngine::inputValueCallback(CSOUND *csound,
                                     const char *channelName,
                                     MYFLT *value)
{
  // Called by the csound running engine when 'invalue' opcode is used
  // To pass data from qutecsound to Csound
  CsoundUserData *ud = (CsoundUserData *) csoundGetHostData(csound);
//  if (ud->cs->isRunning()) {
    QString name = QString(channelName);
    ud->cs->perfMutex.lock();
    if (name.startsWith('$')) { // channel is a string channel
      char *string = (char *) value;
      // FIMXE: check string length
      QString newValue = ud->wl->getStringForChannel(name.mid(1));
      strcpy(string, newValue.toLocal8Bit());
    }
    else {  // Not a string channel
      //FIXME check if mouse tracking is active, and move this from here
      if (name == "_MouseX") {
        *value = (MYFLT) ud->mouseValues[0];
      }
      else if (name == "_MouseY") {
        *value = (MYFLT) ud->mouseValues[1];
      }
      else if(name == "_MouseRelX") {
        *value = (MYFLT) ud->mouseValues[2];
      }
      else if(name == "_MouseRelY") {
        *value = (MYFLT) ud->mouseValues[3];
      }
      else if(name == "_MouseBut1") {
        *value = (MYFLT) ud->mouseValues[4];
      }
      else if(name == "_MouseBut2") {
        *value = (MYFLT) ud->mouseValues[5];
      }
      else {
        *value = (MYFLT) ud->wl->getValueForChannel(name);
      }
    }
    ud->cs->perfMutex.unlock();
//  }
}

void CsoundEngine::makeGraphCallback(CSOUND *csound, WINDAT *windat, const char *name)
{
  CsoundUserData *ud = (CsoundUserData *) csoundGetHostData(csound);
  // Csound reuses windat, so it is not guaranteed to be unique
//  qDebug() << "CsoundEngine::makeGraph() " << windat << "  " << name;
  ud->wl->appendCurve(windat);
//  windat->windid = (uintptr_t) curve;
//   qDebug("CsoundEngine::makeGraphCallback %i", windat->windid);
}

void CsoundEngine::drawGraphCallback(CSOUND *csound, WINDAT *windat)
{
  CsoundUserData *udata = (CsoundUserData *) csoundGetHostData(csound);
  // This callback paints data on curves
//  qDebug("CsoundEngine::drawGraph()");
  udata->wl->updateCurve(windat);
}

void CsoundEngine::killGraphCallback(CSOUND *csound, WINDAT *windat)
{
  // When is this callback called??
  qDebug() << "CsoundEngine::killGraphCallback";
  CsoundUserData *udata = (CsoundUserData *) csoundGetHostData(csound);
  udata->wl->killCurve(windat);
}

int CsoundEngine::exitGraphCallback(CSOUND *csound)
{
//  qDebug("CsoundEngine::exitGraphCallback()");
  CsoundUserData *udata = (CsoundUserData *) csoundGetHostData(csound);
  return udata->wl->killCurves(csound);
}

int CsoundEngine::keyEventCallback(void *userData,
                                 void *p,
                                 unsigned int type)
{
  if (type != CSOUND_CALLBACK_KBD_EVENT)
    return 1;
  CsoundUserData *ud = (CsoundUserData *) userData;
//  WidgetLayout *wl = (WidgetLayout *) ud->wl;
  int *value = (int *) p;
  int key = ud->cs->popKeyPressEvent();
  if (key >= 0) {
    *value = key;
//    qDebug() << "Pressed: " << key;
  }
  else {
    key = ud->cs->popKeyReleaseEvent();
    if (key >= 0) {
      *value = key;
//       qDebug() << "Released: " << key;
    }
  }
  return 0;
}

// void CsoundEngine::ioCallback (CSOUND *csound,
//                              const char *channelName,
//                              MYFLT *value,
//                              int channelType
//                             )
// {
//   qDebug() << "qutecsound::ioCallback";
//   if (channelType & CSOUND_INPUT_CHANNEL) { // is Input Channel
//     if (channelType & CSOUND_CONTROL_CHANNEL) {
//       inputValueCallback(csound, channelName, value);
//     }
//     else if (channelType & CSOUND_AUDIO_CHANNEL) {
//     }
//     else if (channelType & CSOUND_STRING_CHANNEL) {
//     }
//   }
//   else if (channelType & CSOUND_OUTPUT_CHANNEL) { // Is output channel
//     if (channelType & CSOUND_CONTROL_CHANNEL) {
//       outputValueCallback(csound, channelName, *value);
//     }
//     else if (channelType & CSOUND_AUDIO_CHANNEL) {
//     }
//     else if (channelType & CSOUND_STRING_CHANNEL) {
//     }
//   }
// }

void CsoundEngine::csThread(void *data)
{
  CsoundUserData* udata = (CsoundUserData*)data;
  udata->outputBuffer = csoundGetSpout(udata->csound);
  for (int i = 0; i < udata->outputBufferSize*udata->numChnls; i++) {
    udata->audioOutputBuffer.put(udata->outputBuffer[i]/ udata->zerodBFS);
  }
//  udata->wl->getValues(&udata->channelNames,
//                       &udata->values,
//                       &udata->stringValues);
  if (!udata->useInvalue) {
//    CsoundChannelListEntry **channelList;
//    int numChannels = csoundListChannels(udata->csound, channelList);
//    csoundDeleteChannelList(udata->csound, *channelList);
//    for (int j = 0; j < numChannels; j++) {
//      qDebug() << (char*) channelList[j] << "--";
//    }
//    writeWidgetValues(udata);
//    readWidgetValues(udata);
  }
  udata->cs->processEventQueue();  // FIXME: This function locks a mutex, not ideal here
  (udata->ksmpscount)++;
}

void CsoundEngine::readWidgetValues(CsoundUserData *ud)
{
  MYFLT* pvalue;
//   CsoundChannelListEntry **lst;
//   CsoundChannelListEntry *chan;
//   int num = csoundListChannels(ud->csound, lst);
//   for (int i = 0; i < num; i++) {
//     chan = lst[i];
//     if (chan) {  //Not sure why this check is needed here....
//       if (chan->type & (CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL)) {
//         if(csoundGetChannelPtr(ud->csound, &pvalue, chan->name, 0) == 0) {
//           *pvalue = (MYFLT) ud->qcs->values[i];
//         }
//       }
//       else if (chan->type & (CSOUND_INPUT_CHANNEL | CSOUND_STRING_CHANNEL)) {
//         if(csoundGetChannelPtr(ud->csound, &pvalue, chan->name, 0) == 0) {
//           char *string = (char *) pvalue;
//           strcpy(string, ud->qcs->stringValues[i].toStdString().c_str());
//         }
//       }
//     }
//   }
//   csoundDeleteChannelList(ud->csound, *lst);

  for (int i = 0; i < ud->channelNames.size(); i++) {
    if(csoundGetChannelPtr(ud->csound, &pvalue, ud->channelNames[i].toStdString().c_str(),
        CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
      *pvalue = (MYFLT) ud->values[i];
    }
    if(csoundGetChannelPtr(ud->csound, &pvalue, ud->channelNames[i].toStdString().c_str(),
       CSOUND_INPUT_CHANNEL | CSOUND_STRING_CHANNEL) == 0) {
      char *string = (char *) pvalue;
      strcpy(string, ud->stringValues[i].toStdString().c_str());
    }
  }
  //FIXME check if mouse tracking is active
  if(csoundGetChannelPtr(ud->csound, &pvalue, "_MouseX",
        CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
      *pvalue = (MYFLT) ud->mouseValues[0];
  }
  else if(csoundGetChannelPtr(ud->csound, &pvalue, "_MouseY",
        CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
      *pvalue = (MYFLT) ud->mouseValues[1];
  }
  else if(csoundGetChannelPtr(ud->csound, &pvalue, "_MouseRelX",
        CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
      *pvalue = (MYFLT) ud->mouseValues[2];
  }
  else if(csoundGetChannelPtr(ud->csound, &pvalue, "_MouseRelY",
        CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
      *pvalue = (MYFLT) ud->mouseValues[3];
  }
  else if(csoundGetChannelPtr(ud->csound, &pvalue, "_MouseBut1",
        CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
      *pvalue = (MYFLT) ud->mouseValues[4];
  }
  else if(csoundGetChannelPtr(ud->csound, &pvalue, "_MouseBut2",
        CSOUND_INPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
      *pvalue = (MYFLT) ud->mouseValues[5];
  }
}

void CsoundEngine::writeWidgetValues(CsoundUserData *ud)
{
//   qDebug("qutecsound::writeWidgetValues");
   MYFLT* pvalue;
   for (int i = 0; i < ud->channelNames.size(); i++) {
     if (ud->channelNames[i] != "") {
       if(csoundGetChannelPtr(ud->csound, &pvalue, ud->channelNames[i].toStdString().c_str(),
          CSOUND_OUTPUT_CHANNEL | CSOUND_CONTROL_CHANNEL) == 0) {
         ud->wl->setValue(i,*pvalue);
       }
       else if(csoundGetChannelPtr(ud->csound, &pvalue, ud->channelNames[i].toStdString().c_str(),
         CSOUND_OUTPUT_CHANNEL | CSOUND_STRING_CHANNEL) == 0) {
         ud->wl->setValue(i,QString((char *)pvalue));
       }
     }
   }
}

//
//void CsoundEngine::setFiles(QString fileName1, QString fileName2)
//{
//  m_fileName1 = fileName1;
//  m_fileName2 = fileName2;
//}

//void CsoundEngine::setCsoundOptions(const CsoundOptions &options)
//{
//  m_options = options;
//}

void CsoundEngine::setWidgetLayout(WidgetLayout *wl)
{
  ud->wl = wl;
//  connect(wl, SIGNAL(destroyed()), this, SLOT(widgetLayoutDestroyed()));
  dispatchQueues(); // starts queue dispatcher timer
}

void CsoundEngine::setThreaded(bool threaded)
{
  m_threaded = threaded;
}

void CsoundEngine::useInvalue(bool use)
{
  ud->useInvalue = use;
}

void CsoundEngine::enableWidgets(bool enable)
{
  ud->enableWidgets = enable;
}

void CsoundEngine::setInitialDir(QString initialDir)
{
  m_initialDir = initialDir;
}

void CsoundEngine::freeze()
{
//  qDebug() << "CsoundEngine::freeze";
  engineMutex.lock();
  if (closing != -1) {
    closing = 1;
  }
  engineMutex.unlock();
}

void CsoundEngine::registerConsole(ConsoleWidget *c)
{
  consoles.append(c);
}

void CsoundEngine::unregisterConsole(ConsoleWidget *c)
{
  int index = consoles.indexOf(c);
  if (index >= 0 )
    consoles.remove(index);
}

QList<QPair<int, QString> > CsoundEngine::getErrorLines()
{
  QList<QPair<int, QString> > list;
  if (consoles.size() > 0) {
    QList<int> lines = consoles[0]->errorLines;
    QStringList texts = consoles[0]->errorTexts;
    for (int i = 0; i < lines.size(); i++) {
      list.append(QPair<int, QString>(lines[i], texts[i]));
    }
  }
  return list;
}

void CsoundEngine::setConsoleBufferSize(int size)
{
  qDebug() << "CsoundEngine::setConsoleBufferSize " << size;
  m_consoleBufferSize = size;
}

void CsoundEngine::keyPressForCsound(QString key)
{
  keyMutex.lock();
  keyPressBuffer << key;
  keyMutex.unlock();
}

void CsoundEngine::keyReleaseForCsound(QString key)
{
//   qDebug() << "keyReleaseForCsound " << key;
  keyMutex.lock();
  keyReleaseBuffer << key;
  keyMutex.unlock();
}

void CsoundEngine::registerScope(QuteScope *scope)
{
  scope->setUd(ud);
}

void CsoundEngine::registerGraph(QuteGraph *graph)
{
  graph->setUd(ud);
}

//void CsoundEngine::unregisterScope(QuteScope *scope)
//{
//  // TODO is it necessary to unregiter scopes?
//  qDebug() << "CsoundEngine::unregisterScope not implemented";
//}

int CsoundEngine::popKeyPressEvent()
{
  keyMutex.lock();
  int value = -1;
  if (!keyPressBuffer.isEmpty()) {
    value = (int) keyPressBuffer.takeFirst()[0].toAscii();
  }
  keyMutex.unlock();
  return value;
}

int CsoundEngine::popKeyReleaseEvent()
{
  keyMutex.lock();
  int value = -1;
  if (!keyReleaseBuffer.isEmpty()) {
    value = (int) keyReleaseBuffer.takeFirst()[0].toAscii() + 0x10000;
  }
  keyMutex.unlock();
  return value;
}

void CsoundEngine::processEventQueue()
{
  // This function should only be called when Csound is running
  eventMutex.lock();
  while (eventQueueSize > 0) {
    eventQueueSize--;
    if (ud->perfThread != 0) {
      //ScoreEvent is not working
//      ud->perfThread->ScoreEvent(0, type, eventElements.size(), pFields);
//      qDebug() << "CsoundEngine::processEventQueue()" << eventQueue[eventQueueSize];
       ud->perfThread
           ->InputMessage(eventQueue[eventQueueSize].toStdString().c_str());
    }
    else {
      char type = eventQueue[eventQueueSize][0].unicode();
      QStringList eventElements = eventQueue[eventQueueSize].remove(0,1).split(" ",QString::SkipEmptyParts);
      // eventElements.size() should never be larger than QCS_EVENTS_MAX_PFIELDS
      for (int j = 0; j < eventElements.size(); j++) {
        pFields[j] = (MYFLT) eventElements[j].toDouble();
      }
      qDebug("type %c line: %s", type, eventQueue[eventQueueSize].toStdString().c_str());
      csoundScoreEvent(ud->csound, type, pFields, eventElements.size());
    }
  }
  eventMutex.unlock();
}

void CsoundEngine::passOutValue(QString channelName, double value)
{
  ud->wl->newValue(QPair<QString, double>(channelName, value));
}

void CsoundEngine::passOutString(QString channelName, QString value)
{
//   qDebug() << "qutecsound::queueOutString";
  ud->wl->newValue(QPair<QString, QString>(channelName, value));
}

int CsoundEngine::play(CsoundOptions *options)
{
//  qDebug() << "CsoundEngine::play";
  if (!isRunning()) {
    m_options = *options;
    return runCsound();
  }
  else {
    if (ud->threaded) { // TODO is this action correct?
      ud->perfThread->Play();
    }
    return 0;
  }
}

void CsoundEngine::stop()
{
  stopRecording();
  stopCsound();
  emit stopSignal();
}

void CsoundEngine::pause()
{
  if (isRunning() && ud->perfThread->GetStatus() == 0) {
    ud->perfThread->Pause();
  }
}

int CsoundEngine::startRecording(int sampleformat, QString fileName)
{
  const int channels=ud->numChnls;
  const int sampleRate=ud->sampleRate;
  int format = SF_FORMAT_WAV;
  switch (sampleformat) {
      case 0:
    format |= SF_FORMAT_PCM_16;
    break;
      case 1:
    format |= SF_FORMAT_PCM_24;
    break;
      case 2:
    format |= SF_FORMAT_FLOAT;
    break;
  }
  qDebug("start recording: %s", fileName.toStdString().c_str());
  outfile = new SndfileHandle(fileName.toStdString().c_str(), SFM_WRITE, format, channels, sampleRate);
  // clip instead of wrap when converting floats to ints
  outfile->command(SFC_SET_CLIPPING, NULL, SF_TRUE);
  samplesWritten = 0;
  m_recording = true;

  recordTimer.singleShot(20, this, SLOT(recordBuffer()));
  return 0;
}

void CsoundEngine::stopRecording()
{
  m_recording = false;  // Will be processed on next record buffer
}

void CsoundEngine::queueEvent(QString eventLine, int delay)
{
  // TODO: implement delayed events
//   qDebug("CsoundEngine::queueEvent %s", eventLine.toStdString().c_str());
  if (!isRunning()) {
    queueMessage(tr("Csound is not running! Event ignored.\n"));
    return;
  }
  if (eventQueueSize < QCS_MAX_EVENTS) {
    eventMutex.lock();
    eventQueue[eventQueueSize] = eventLine;
    eventQueueSize++;
    eventMutex.unlock();
  }
  else {
    qDebug("Warning: event queue full, event not processed");
  }
}

int CsoundEngine::runCsound()
{
#ifdef MACOSX_PRE_SNOW
  //Remember menu bar to set it after FLTK grabs it
  menuBarHandle = GetMenuBar();
#endif
  eventQueueSize = 0; //Flush events gathered while idle
  ud->audioOutputBuffer.allZero();

  QDir::setCurrent(m_options.fileName1);
  ud->threaded = m_threaded;
  for (int i = 0; i < consoles.size(); i++) {
    consoles[0]->reset();
  }

#ifdef QCS_DESTROY_CSOUND
  ud->csound=csoundCreate(0);
#endif

  // Message Callbacks must be set before compile, otherwise some information is missed
  if (ud->threaded) {
    csoundSetMessageCallback(ud->csound, &CsoundEngine::messageCallbackThread);
  }
  else {
    csoundSetMessageCallback(ud->csound, &CsoundEngine::messageCallbackNoThread);
  }
  //    QString oldOpcodeDir = "";
  if (m_options.opcodedirActive) {
    // csoundGetEnv must be called after Compile or Precompile,
    // But I need to set OPCODEDIR before compile....
    //      char *name = 0;
    //      csoundGetEnv(csound,name);
    //      oldOpcodeDir = QString(name);
    //      qDebug() << oldOpcodeDir;
    csoundSetGlobalEnv("OPCODEDIR", m_options.opcodedir.toLocal8Bit());
  }
#ifdef Q_OS_MAC
  else {
#ifdef USE_DOUBLES
    QString opcodedir = m_initialDir + "/QuteCsound.app/Contents/Frameworks/CsoundLib64.framework/Resources/Opcodes";
    QString stdopcode = opcodedir + "/libstdopcod.dylib";
#else
    QString opcodedir = m_initialDir + "/QuteCsound.app/Contents/Frameworks/CsoundLib.framework/Resources/Opcodes";
    QString stdopcode = opcodedir + "/libstdopcod.dylib";
//    qDebug() << opcodedir;
//    qDebug() << stdopcode;
#endif
    // TODO is this check robust enough? what if the standard library is not used? is it likely it is not?
    if (QFile::exists(stdopcode)) {
      csoundSetGlobalEnv("OPCODEDIR", opcodedir.toLocal8Bit());
    }
  }
#endif
  csoundReset(ud->csound);
  csoundSetHostData(ud->csound, (void *) ud);
  csoundPreCompile(ud->csound);  //Need to run PreCompile to create the FLTK_Flags global variable

  if (csoundCreateGlobalVariable(ud->csound, "FLTK_Flags", sizeof(int)) != CSOUND_SUCCESS) {
    return -3;
  }
  if (m_options.enableFLTK) {
    // disable FLTK graphs, but allow FLTK widgets
    *((int*) csoundQueryGlobalVariable(ud->csound, "FLTK_Flags")) = 4;
  }
  else {
    //       qDebug("play() FLTK Disabled");
    *((int*) csoundQueryGlobalVariable(ud->csound, "FLTK_Flags")) = 3;
  }

  csoundSetIsGraphable(ud->csound, true);
  csoundSetMakeGraphCallback(ud->csound, &CsoundEngine::makeGraphCallback);
  csoundSetDrawGraphCallback(ud->csound, &CsoundEngine::drawGraphCallback);
  csoundSetKillGraphCallback(ud->csound, &CsoundEngine::killGraphCallback);
  csoundSetExitGraphCallback(ud->csound, &CsoundEngine::exitGraphCallback);

  if (ud->enableWidgets) {
    if (ud->useInvalue) {
      csoundSetInputValueCallback(ud->csound, &CsoundEngine::inputValueCallback);
      csoundSetOutputValueCallback(ud->csound, &CsoundEngine::outputValueCallback);
    }
    else {
      // Not really sure that this is worth the trouble, as it
      // is used only with chnsend and chnrecv which are deprecated
      //         qDebug() << "csoundSetChannelIOCallback";
      //         csoundSetChannelIOCallback(csound, &qutecsound::ioCallback);
    }
  }
  else {
    csoundSetInputValueCallback(ud->csound, NULL);
    csoundSetOutputValueCallback(ud->csound, NULL);
  }
  csoundSetCallback(ud->csound,
                    &CsoundEngine::keyEventCallback,
                    (void *) ud, CSOUND_CALLBACK_KBD_EVENT);

  char **argv;
  argv = (char **) calloc(33, sizeof(char*));
  int argc = m_options.generateCmdLine(argv);
  ud->result=csoundCompile(ud->csound,argc,argv);
  if (ud->result!=CSOUND_SUCCESS) {
    qDebug() << "Csound compile failed! "  << ud->result;
    flushMessageQueue();
    for (int i = 0; i < argc; i++) {
      qDebug() << argv[i];
      free(argv[i]);
    }
    free(argv);
    emit (errorLines(getErrorLines()));
    return -3;
  }
  ud->zerodBFS = csoundGet0dBFS(ud->csound);
  ud->sampleRate = csoundGetSr(ud->csound);
  ud->numChnls = csoundGetNchnls(ud->csound);
  ud->outputBufferSize = csoundGetKsmps(ud->csound);
  ud->ksmpscount = 0;

  //TODO is something here necessary to work with doubles?
  //     PUBLIC int csoundGetSampleFormat(CSOUND *);
  //     PUBLIC int csoundGetSampleSize(CSOUND *);
  if (ud->threaded) {
    ud->perfThread = new CsoundPerformanceThread(ud->csound);
    ud->perfThread->SetProcessCallback(CsoundEngine::csThread, (void*)ud);
    //      qDebug() << "qutecsound::runCsound perfThread->Play";
    ud->perfThread->Play();
  } /*if (ud->thread)*/
  else { // Run in the same thread
    ud->PERF_STATUS = 1;
    while(ud->PERF_STATUS == 1 && csoundPerformKsmps(ud->csound)==0) {
      processEventQueue();
      CsoundEngine::csThread(ud);
      qApp->processEvents(); // Must process events last to avoid stopping and calling csThread invalidly
    }
    ud->PERF_STATUS = 0;
    csoundStop(ud->csound);
    csoundSetMessageCallback(ud->csound, 0); // Does this fix the messages that appear when closing QCS?
#ifdef QCS_DESTROY_CSOUND
  csoundDestroy(ud->csound);
#else
    csoundCleanup(ud->csound);
#endif
    flushMessageQueue();  // To flush pending queues
#ifdef MACOSX_PRE_SNOW
    // Put menu bar back
    SetMenuBar(menuBarHandle);
#endif
  }
  for (int i = 0; i < argc; i++) {
    free(argv[i]);
  }
  free(argv);
  //    if (oldOpcodeDir != "") {
  //      csoundSetGlobalEnv("OPCODEDIR", oldOpcodeDir.toLocal8Bit());
  //    }

  return 0;
}

void CsoundEngine::stopCsound()
{
//  qDebug() << "CsoundEngine::stopCsound()";
  if (ud->threaded) {
//    perfThread->ScoreEvent(0, 'e', 0, 0);
    if (ud->perfThread != 0) {
      ud->perfThread->Stop();
      qDebug() << "CsoundEngine::stopCsound() stopped";
      ud->perfThread->Join();
      qDebug() << "CsoundEngine::stopCsound() joined";
      delete ud->perfThread;
      ud->perfThread = 0;
      flushMessageQueue();
    }
  } /*if (ud->threaded)*/
  else {  // in same thread
    if (ud->PERF_STATUS == 1) {
      ud->PERF_STATUS = -1;
      while (ud->PERF_STATUS == -1) { // Wait until performance has stopped
        usleep(100000);
        qApp->processEvents();
      }
    }
  }
#ifdef MACOSX_PRE_SNOW
// Put menu bar back
  SetMenuBar(menuBarHandle);
#endif
#ifdef QCS_DESTROY_CSOUND
  csoundDestroy(ud->csound);
#endif
}

void CsoundEngine::dispatchQueues()
{
//   qDebug("qutecsound::dispatchQueues()");
  if (!engineMutex.tryLock(2)) {
    QTimer::singleShot(refreshTime, this, SLOT(dispatchQueues()));
    return;
  }
  if (closing == 1) {
    closing = -1;
    engineMutex.unlock();
    return;
  }
  ud->wl->getMouseValues(&ud->mouseValues);
  ud->wl->refreshWidgets();
  int counter = 0;
  while ((m_consoleBufferSize <= 0 || counter++ < m_consoleBufferSize)) {
    messageMutex.lock();
    if (messageQueue.isEmpty()) {
      messageMutex.unlock();
      break;
    }
    QString msg = messageQueue.takeFirst();
    messageMutex.unlock();
    for (int i = 0; i < ud->cs->consoles.size(); i++) {
      ud->cs->consoles[i]->appendMessage(msg);
      ud->cs->consoles[i]->scrollToEnd();
    }
    ud->wl->appendMessage(msg);
    ud->wl->refreshConsoles();  // Scroll to end of text all console widgets
  }
  messageMutex.lock();
  if (!messageQueue.isEmpty() && m_consoleBufferSize > 0 && counter >= m_consoleBufferSize) {
    messageQueue.clear();
    messageQueue << "\nQUTECSOUND: Message buffer overflow. Messages discarded!\n";
  }
  messageMutex.unlock();

  if (ud->threaded && ud->perfThread) {
    if (ud->perfThread->GetStatus() > 0) {
//      qDebug() << "CsoundEngine::dispatchQueues() perf finished";
      stop();
    }
  }
  engineMutex.unlock();
  QTimer::singleShot(refreshTime, this, SLOT(dispatchQueues()));
}

void CsoundEngine::queueMessage(QString message)
{
  if (closing != 1) {
    messageMutex.lock(); // FIXME this is still occasionally causing threading issues as Csound uses it unexpectedly
    messageQueue << message;
    messageMutex.unlock();
  }
}

void CsoundEngine::clearMessageQueue()
{
  messageMutex.lock();
  messageQueue.clear();
  messageMutex.unlock();
}

void CsoundEngine::flushMessageQueue()
{
  foreach (QString msg, messageQueue) { //Flush pending messages
    for (int i = 0; i < ud->cs->consoles.size(); i++) {
      ud->cs->consoles[i]->appendMessage(msg);
    }
    ud->wl->appendMessage(msg);
  }
  for (int i = 0; i < ud->cs->consoles.size(); i++) {
    ud->cs->consoles[i]->scrollToEnd();
  }
  qApp->processEvents();
}

void CsoundEngine::recordBuffer()
{
  if (m_recording) {
    if (ud->audioOutputBuffer.copyAvailableBuffer(recBuffer, bufferSize)) {
      int samps = outfile->write(recBuffer, bufferSize);
      samplesWritten += samps;
    }
    else {
//       qDebug("qutecsound::recordBuffer() : Empty Buffer!");
    }
    recordTimer.singleShot(20, this, SLOT(recordBuffer()));
  }
  else { //Stop recording
    delete outfile;
    qDebug("Recording stopped. Written %li samples", samplesWritten);
  }
}

bool CsoundEngine::isRunning()
{
  if (ud->threaded) {
    if (ud->perfThread != 0 && ud->perfThread->GetStatus() == 0)
      return true;
    else
      return false;
  }
  else {
    return (ud->PERF_STATUS == 1);
  }
}

bool CsoundEngine::isRecording()
{
  return m_recording;
}

