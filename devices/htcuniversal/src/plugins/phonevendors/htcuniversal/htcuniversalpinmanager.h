/****************************************************************************
 **
 ** Copyright (C) 2000-2007 TROLLTECH ASA. All rights reserved.
 **
 ** This file is part of the Opensource Edition of the Qtopia Toolkit.
 **
 ** This software is licensed under the terms of the GNU General Public
 ** License (GPL) version 2.
 **
 ** See http://www.trolltech.com/gpl/ for GPL licensing information.
 **
 ** Contact info@trolltech.com if any conditions of this licensing are
 ** not clear to you.
 **
 **
 **
 ** This file is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE
 ** WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 **
 ****************************************************************************/

#ifndef HTCUNIVERSALPINMANAGER_H
#define HTCUNIVERSALPINMANAGER_H

#include <qmodempinmanager.h>

class HtcuniversalPinManager : public QModemPinManager
{
  Q_OBJECT
    public:
  HtcuniversalPinManager(QModemService *service);
  ~HtcuniversalPinManager();

 protected:
  bool emptyPinIsReady() const;

};

#endif /* HTCUNIVERSALPINMANAGER_H */
