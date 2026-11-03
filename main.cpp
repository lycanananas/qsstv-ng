/**************************************************************************
*   Copyright (C) 2000-2019 by Johan Maes                                 *
*   on4qz@telenet.be                                                      *
*   http://users.telenet.be/on4qz                                         *
*                                                                         *
*   This program is free software; you can redistribute it and/or modify  *
*   it under the terms of the GNU General Public License as published by  *
*   the Free Software Foundation; either version 2 of the License, or     *
*   (at your option) any later version.                                   *
*                                                                         *
*   This program is distributed in the hope that it will be useful,       *
*   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
*   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
*   GNU General Public License for more details.                          *
*                                                                         *
*   You should have received a copy of the GNU General Public License     *
*   along with this program; if not, write to the                         *
*   Free Software Foundation, Inc.,                                       *
*   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
***************************************************************************/

#include <QApplication>

#include <QtGui>
#include "appglobal.h"
#include "mainwindow.h"
#include <QPixmap>
#include <QSplashScreen>
#include <QTimer>
#include <QSettings>
#include <clocale>
#include <QDir>
#include <QFileInfo>
#include <QVersionNumber>
#include "dispatcher.h"


QSplashScreen *splash;

// When no settings exist yet (~/.config/QSSTV-NG/qsstv-ng.conf), copy them from the newest
// versioned QSSTV-NG settings file of earlier builds (~/.config/QSSTV-NG/qsstv-ng_<version>.conf),
// or else from the original QSSTV (~/.config/ON4QZ/qsstv_9.0.conf).
static void migrateSettings()
{
  QSettings qSettings;
  if(!qSettings.allKeys().isEmpty()) return;

  QFileInfo current(qSettings.fileName());
  QString prefix=APPLICATION+"_";  // "qsstv-ng_"
  QString bestFile;
  QVersionNumber bestVersion;
  foreach(const QFileInfo &fi,current.dir().entryInfoList(QStringList() << prefix+"*."+current.suffix(),QDir::Files))
    {
      if(fi.absoluteFilePath()==current.absoluteFilePath()) continue;
      QVersionNumber v=QVersionNumber::fromString(fi.completeBaseName().mid(prefix.length()));
      if(v.isNull()) continue;
      if(bestFile.isEmpty() || (v>bestVersion))
        {
          bestVersion=v;
          bestFile=fi.absoluteFilePath();
        }
    }

  QScopedPointer<QSettings> oldSettings;
  if(!bestFile.isEmpty()) oldSettings.reset(new QSettings(bestFile,QSettings::NativeFormat));
  else oldSettings.reset(new QSettings(LEGACYORGANIZATION,LEGACYAPPLICATION));
  foreach(const QString &key,oldSettings->allKeys()) qSettings.setValue(key,oldSettings->value(key));
  qSettings.sync();
}

int main( int argc, char ** argv )
{

  int result;
  QTimer tm;
  tm.setSingleShot(true);

  QCoreApplication::setOrganizationName(ORGANIZATION);
  QCoreApplication::setApplicationName(APPLICATION);
  QApplication app( argc, argv );
  // QApplication sets the C locale from the system, with e.g. pl_PL the decimal separator becomes ','
  // and hamlib reads "1.18" from rigctld as 1 (SWR, ALC, power). Qt formats numbers itself.
  setlocale(LC_NUMERIC,"C");
  // window <-> launcher association: Wayland app_id and _KDE_NET_WM_DESKTOP_FILE
  // (WM_CLASS can't be used, its class part is the application name which also names the settings file)
  QGuiApplication::setDesktopFileName("qsstv-ng");
  migrateSettings();
  QPixmap pixmap(":/icons/qsstvsplash.png");
  QSplashScreen splash(pixmap,Qt::WindowStaysOnTopHint);

  splashPtr=&splash;
#ifdef QT_NO_DEBUG
  splash.show();
#endif
  QFont f;
  f.setBold(true);
  f.setPixelSize(20);
  splashPtr->setFont(f);
  splashStr="\n\n\n";
  splashStr+=QString( "Starting %1").arg(qsstvVersion).rightJustified(25,' ')+"\n";
  splash.showMessage (splashStr,Qt::AlignLeft,Qt::white);
  tm.start(100);
  globalInit();
  mainWindowPtr=new mainWindow;
  mainWindowPtr->setWindowIcon(QPixmap(":/icons/qsstv.png"));
  while(1)
  {
    app.processEvents();
    if(!tm.isActive()) break;
   }
  mainWindowPtr->init(); // this must follow show() because window has to be drawn first to determine fftframe window size
  mainWindowPtr->hide();
  tm.start(100);
  while(1)
  {
    app.processEvents();
    if(!tm.isActive()) break;
   }
  splash.finish(mainWindowPtr);
  mainWindowPtr->show();
  mainWindowPtr->startRunning();
  result=app.exec();
  globalEnd();
  return result;
}

