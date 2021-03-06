/****************************************************************************
**
** This file is part of the Qt Extended Opensource Package.
**
** Copyright (C) 2009 Trolltech ASA.
**
** Contact: Qt Extended Information (info@qtextended.org)
**
** This file may be used under the terms of the GNU General Public License
** version 2.0 as published by the Free Software Foundation and appearing
** in the file LICENSE.GPL included in the packaging of this file.
**
** Please review the following information to ensure GNU General Public
** Licensing requirements will be met:
**     http://www.fsf.org/licensing/licenses/info/GPLv2.html.
**
**
****************************************************************************/

#ifndef WAVDECODER_H
#define WAVDECODER_H

#include <qtopiamedia/media.h>
#include <QMediaDecoder>

class QMediaDevice;
class WavDecoderPrivate;

class WavDecoder : public QMediaDecoder
{
    Q_OBJECT

public:
    WavDecoder();
    ~WavDecoder();

    QMediaDevice::Info const& dataType() const;

    bool connectToInput(QMediaDevice* input);
    void disconnectFromInput(QMediaDevice* input);

    void start();
    void stop();
    void pause();

    quint64 length();
    bool seek(qint64 ms);

    void setVolume(int volume);
    int volume();

    void setMuted(bool mute);
    bool isMuted();

private:
    qint64 readData(char *data, qint64 maxlen);
    qint64 writeData(const char *data, qint64 len);

    WavDecoderPrivate* d;
};

#endif
