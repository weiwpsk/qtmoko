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

#include "neoaudioplugin.h"

#include <QDir>
#include <QDebug>
#include <QProcess>
#include <QAudioState>
#include <QAudioStateInfo>
#include <QValueSpaceItem>
#include <QtopiaIpcAdaptor>
#include <QtopiaIpcEnvelope>

#include <qplugin.h>
#include <qtopialog.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#ifdef QTOPIA_BLUETOOTH
#include <QBluetoothAudioGateway>
#endif

// For GTA04 audio system docs please check:
//     http://projects.goldelico.com/p/gta04-kernel/page/Sound/

/* This is version that this file was programmed against:

Basic

amixer set 'DAC1 Analog' off
amixer set 'DAC2 Analog' on
amixer set 'Codec Operation Mode' 'Option 1 (audio)'

Headset

echo 1 >/sys/devices/virtual/gpio/gpio55/value
amixer set 'Headset' 2
amixer set 'HeadsetL Mixer AudioL1' off
amixer set 'HeadsetL Mixer AudioL2' on
amixer set 'HeadsetL Mixer Voice' off
amixer set 'HeadsetR Mixer AudioR1' off
amixer set 'HeadsetR Mixer AudioR2' on
amixer set 'HeadsetR Mixer Voice' off
aplay /usr/share/sounds/alsa/Front_Center.wav 
amixer set 'HeadsetL Mixer AudioL2' off
amixer set 'HeadsetR Mixer AudioR2' off

Vibrasound

amixer set Vibra Audio
amixer set 'Vibra H-bridge direction' 'Positive polarity'
amixer set 'Vibra H-bridge mode' 'Audio data MSB'
amixer set 'Vibra Mux' AudioL2
aplay /usr/share/sounds/alsa/Front_Center.wav 
amixer set Vibra 'Local vibrator'

Earpiece

amixer set Earpiece 100%
amixer set 'Earpiece Mixer AudioL2' on
amixer set 'Earpiece Mixer AudioR2' off
amixer set 'Earpiece Mixer Voice' off
aplay /usr/share/sounds/alsa/Front_Center.wav 
amixer set 'Earpiece Mixer AudioL2' off

Handsfree

amixer set HandsfreeL on
amixer set HandsfreeR on
amixer set 'HandsfreeL Mux' AudioL2
amixer set 'HandsfreeR Mux' AudioR2
aplay /usr/share/sounds/alsa/Front_Center.wav 
amixer set HandsfreeL off
amixer set HandsfreeR off

internal Microphone

amixer set 'Analog' 5
amixer set TX1 'Analog'
amixer set 'TX1 Digital' 12
amixer set 'Analog Left AUXL' nocap
amixer set 'Analog Right AUXR' nocap
amixer set 'Analog Left Main Mic' cap
amixer set 'Analog Left Headset Mic' nocap
arecord | tee record.wav | aplay

Headset Mic

echo 0 >echo $state >/sys/devices/virtual/gpio/gpio23/value  # switch off video out
amixer set 'Analog' 5
amixer set 'Analog Left AUXL' nocap
amixer set 'Analog Right AUXR' nocap
amixer set 'Analog Left Main Mic' nocap
amixer set 'Analog Left Headset Mic' cap
arecord | tee record.wav | aplay

AUX-In

echo 0 >/sys/devices/virtual/gpio/gpio55/value
amixer set 'Analog' 5
amixer set 'Analog Left AUXL' cap
amixer set 'Analog Right AUXR' cap
amixer set 'Analog Left Main Mic' nocap
amixer set 'Analog Left Headset Mic' nocap
arecord | tee record.wav | aplay

*/

enum Gta04AudioScenario
{
    Scenario_Handsfree,         // stereo output from internal GTA04 speaker + internal MIC
    Scenario_Earpiece,          // default for GSM calls, ouput from earpiece + internal MIC
    Scenario_Headset,           // external headphones
    Scenario_GSMBluetooth,      // for gsm calls
};

static bool amixerSet(QStringList & args)
{
    qLog(AudioState) << "amixer set " << args;

    args.insert(0, "-q");           // make it quiet
    args.insert(0, "set");
    int ret = QProcess::execute("amixer", args);
    if (ret != 0) {
        qWarning() << "amixer returned " << ret;
    }
    return ret == 0;
}

static bool writeToFile(const char *filename, const char *val, int len)
{
    qLog(AudioState) << "echo " << val << " > " << filename;

    QFile f(filename);
    if (!f.open(QIODevice::WriteOnly)) {
        qWarning() << "failed to open " << filename << ", error is: " <<
            f.errorString();
        return false;
    }
    int written = f.write(val, len);
    f.close();
    if (written == len) {
        return true;
    }
    qWarning() << "failed to write to " << filename << ", error is: " <<
        f.errorString();
    return false;
}

// ========================================================================= //
class HandsfreeAudioState : public QAudioState
{
    Q_OBJECT
public:
    HandsfreeAudioState(bool isPhone, QObject * parent = 0);

    QAudioStateInfo info() const;
     QAudio::AudioCapabilities capabilities() const;

    bool isAvailable() const;
    bool enter(QAudio::AudioCapability capability);
    bool leave();

private:
     QAudioStateInfo m_info;
    bool m_isPhone;
};

HandsfreeAudioState::HandsfreeAudioState(bool isPhone, QObject * parent):
QAudioState(parent), m_isPhone(isPhone)
{
    if (isPhone) {
        m_info.setDomain("Phone");
        m_info.setProfile("PhoneSpeaker");
        m_info.setPriority(150);
        
    } else {
        m_info.setDomain("Media");
        m_info.setProfile("MediaSpeaker");
        m_info.setPriority(100);
    }
    m_info.setDisplayName(tr("Speaker"));
}

QAudioStateInfo HandsfreeAudioState::info() const
{
    return m_info;
}

QAudio::AudioCapabilities HandsfreeAudioState::capabilities()const
{
    return QAudio::OutputOnly;
}

bool HandsfreeAudioState::isAvailable() const
{
    return true;
}

bool HandsfreeAudioState::enter(QAudio::AudioCapability)
{
    qLog(AudioState) << "HandsfreeAudioState::enter()" << "isPhone" <<
        m_isPhone;

    return
        amixerSet(QStringList() << "HandsfreeL" << "on") &&
        amixerSet(QStringList() << "HandsfreeR" << "on") &&
        amixerSet(QStringList() << "HandsfreeL Mux" << "AudioL2") &&
        amixerSet(QStringList() << "HandsfreeR Mux" << "AudioR2");
}

bool HandsfreeAudioState::leave()
{
    qLog(AudioState) << "HandsfreeAudioState::leave()";

    return amixerSet(QStringList() << "HandsfreeL" << "off")
        && amixerSet(QStringList() << "HandsfreeR" << "off");
}

// ========================================================================= //
class EarpieceAudioState : public QAudioState
{
    Q_OBJECT
public:
    EarpieceAudioState(bool isPhone, QObject * parent = 0);

    QAudioStateInfo info() const;
    QAudio::AudioCapabilities capabilities() const;

    bool isAvailable() const;
    bool enter(QAudio::AudioCapability capability);
    bool leave();

private:
    QAudioStateInfo m_info;
    bool m_isPhone;
};

EarpieceAudioState::EarpieceAudioState(bool isPhone, QObject * parent):
QAudioState(parent), m_isPhone(isPhone)
{
    if(isPhone) {
        m_info.setDomain("Phone");
        m_info.setProfile("PhoneEarpiece");
        m_info.setPriority(100);
    } else {
        m_info.setDomain("Media");
        m_info.setProfile("MediaEarpiece");
        m_info.setPriority(150);
    }
    m_info.setDisplayName(tr("Earpiece"));
}

QAudioStateInfo EarpieceAudioState::info() const
{
    return m_info;
}

QAudio::AudioCapabilities EarpieceAudioState::capabilities()const
{
    return QAudio::OutputOnly;
}

bool EarpieceAudioState::isAvailable() const
{
    return true;
}

bool EarpieceAudioState::enter(QAudio::AudioCapability)
{
    qLog(AudioState) << "EarpieceAudioState::enter()" << "isPhone" << m_isPhone;

    return amixerSet(QStringList() << "Earpiece" << "100%") &&
        amixerSet(QStringList() << "Earpiece Mixer AudioL2" << "on") &&
        amixerSet(QStringList() << "Earpiece Mixer AudioR2" << "off") &&
        amixerSet(QStringList() << "Earpiece Mixer Voice" << "off");
}

bool EarpieceAudioState::leave()
{
    return amixerSet(QStringList() << "Earpiece Mixer AudioL2" << "off");
}

// ========================================================================= //
class HeadsetAudioState : public QAudioState
{
    Q_OBJECT
public:
    HeadsetAudioState(bool isPhone, QObject * parent = 0);

    QAudioStateInfo info() const;
    QAudio::AudioCapabilities capabilities() const;

    bool isAvailable() const;
    bool enter(QAudio::AudioCapability capability);
    bool leave();

private slots:
    void onHeadsetModified();

private:
    QAudioStateInfo m_info;
    bool m_isPhone;
    QValueSpaceItem *m_headset;
};

HeadsetAudioState::HeadsetAudioState(bool isPhone, QObject * parent):
QAudioState(parent), m_isPhone(isPhone)
{
    if (m_isPhone) {
        m_info.setDomain("Phone");
        m_info.setProfile("PhoneHeadset");
    } else {
        m_info.setDomain("Media");
        m_info.setProfile("MediaHeadset");
    }

    m_info.setDisplayName(tr("Headphones"));
    m_info.setPriority(50);

    m_headset =
        new QValueSpaceItem("/Hardware/Accessories/PortableHandsfree/Present",
                            this);
    connect(m_headset, SIGNAL(contentsChanged()), SLOT(onHeadsetModified()));
}

QAudioStateInfo HeadsetAudioState::info() const
{
    return m_info;
}

QAudio::AudioCapabilities HeadsetAudioState::capabilities()const
{
    return QAudio::InputOnly | QAudio::OutputOnly;
}

void HeadsetAudioState::onHeadsetModified()
{
    bool avail = m_headset->value().toBool();

    qLog(AudioState) << __PRETTY_FUNCTION__ << avail;

    if (!avail) {
        QtopiaIpcEnvelope e("QPE/AudioVolumeManager/NeoVolumeService",
                            "setAmp(bool)");
        e << true;              //turn on the speaker amp
    } else {
        QtopiaIpcEnvelope e("QPE/AudioVolumeManager/NeoVolumeService",
                            "setAmp(bool)");
        e << false;             //turn of the speaker amp
    }

    emit availabilityChanged(avail);
}

bool HeadsetAudioState::isAvailable() const
{
    return m_headset->value().toBool();
}

bool HeadsetAudioState::enter(QAudio::AudioCapability)
{
    qLog(AudioState) << "HEarpieceAudioState::enter()" << "isPhone" <<
        m_isPhone;

    return writeToFile("/sys/devices/virtual/gpio/gpio55/value", "1", 1) &&
        amixerSet(QStringList() << "Headset" << "2") &&
        amixerSet(QStringList() << "HeadsetL Mixer AudioL1" << "off") &&
        amixerSet(QStringList() << "HeadsetL Mixer AudioL2" << "on") &&
        amixerSet(QStringList() << "HeadsetL Mixer Voice" << "off") &&
        amixerSet(QStringList() << "HeadsetR Mixer AudioR1" << "off") &&
        amixerSet(QStringList() << "HeadsetR Mixer AudioR2" << "on") &&
        amixerSet(QStringList() << "HeadsetR Mixer Voice" << "off");
}

bool HeadsetAudioState::leave()
{
    return amixerSet(QStringList() << "HeadsetL Mixer AudioL2" << "off") &&
        amixerSet(QStringList() << "HeadsetR Mixer AudioR2" << "off");
}

#ifdef QTOPIA_BLUETOOTH
class BluetoothAudioState : public QAudioState
{
    Q_OBJECT
public:
    explicit BluetoothAudioState(bool isPhone, QObject * parent = 0);
    virtual ~ BluetoothAudioState();

    virtual QAudioStateInfo info() const;
    virtual QAudio::AudioCapabilities capabilities() const;

    virtual bool isAvailable() const;
    virtual bool enter(QAudio::AudioCapability capability);
    virtual bool leave();

private slots:
    void bluetoothAudioStateChanged();
    void headsetDisconnected();
    void headsetConnected();

private:
    bool resetCurrAudioGateway();

private:
    QList < QBluetoothAudioGateway * >m_audioGateways;
    bool m_isPhone;
    QBluetoothAudioGateway *m_currAudioGateway;
    QAudioStateInfo m_info;
    bool m_isActive;
    bool m_isAvail;
};

BluetoothAudioState::BluetoothAudioState(bool isPhone, QObject * parent):
QAudioState(parent),
m_isPhone(isPhone), m_currAudioGateway(0), m_isActive(false), m_isAvail(false)
{
    QBluetoothAudioGateway *hf =
        new QBluetoothAudioGateway("BluetoothHandsfree");
    m_audioGateways.append(hf);
    qLog(AudioState) << "Handsfree audio gateway: " << hf;

    QBluetoothAudioGateway *hs = new QBluetoothAudioGateway("BluetoothHeadset");
    m_audioGateways.append(hs);
    qLog(AudioState) << "Headset audio gateway: " << hs;

    for (int i = 0; i < m_audioGateways.size(); ++i) {
        QBluetoothAudioGateway *gateway = m_audioGateways.at(i);
        connect(gateway, SIGNAL(audioStateChanged()),
                SLOT(bluetoothAudioStateChanged()));
        connect(gateway, SIGNAL(headsetDisconnected()),
                SLOT(headsetDisconnected()));
        connect(gateway, SIGNAL(connectResult(bool, QString)),
                SLOT(headsetConnected()));
        connect(gateway, SIGNAL(newConnection(QBluetoothAddress)),
                SLOT(headsetConnected()));
    }

    if (isPhone) {
        m_info.setDomain("Phone");
        m_info.setProfile("PhoneBluetoothHeadset");
        m_info.setPriority(25);
    } else {
        m_info.setDomain("Media");
        m_info.setProfile("MediaBluetoothHeadset");
        m_info.setPriority(150);
    }

    m_info.setDisplayName(tr("Bluetooth Headset"));
    m_isAvail = resetCurrAudioGateway();
}

BluetoothAudioState::~BluetoothAudioState()
{
    qDeleteAll(m_audioGateways);
}

bool BluetoothAudioState::resetCurrAudioGateway()
{
    for (int i = 0; i < m_audioGateways.size(); ++i) {
        QBluetoothAudioGateway *gateway = m_audioGateways.at(i);
        if (gateway->isConnected()) {
            m_currAudioGateway = gateway;
            qLog(AudioState) << "Returning audiogateway to be:" <<
                m_currAudioGateway;
            return true;
        }
    }

    qLog(AudioState) << "No current audio gateway found";
    return false;
}

void BluetoothAudioState::bluetoothAudioStateChanged()
{
    qLog(AudioState) << "BluetoothAudioState::bluetoothAudioStateChanged()" <<
        m_isActive << m_currAudioGateway;

    if (m_isActive && (m_currAudioGateway || resetCurrAudioGateway())) {
        if (!m_currAudioGateway->audioEnabled()) {
            emit doNotUseHint();
        }
    } else if (!m_isActive && (m_currAudioGateway || resetCurrAudioGateway())) {
        if (m_currAudioGateway->audioEnabled()) {
            emit useHint();
        }
    }
}

void BluetoothAudioState::headsetConnected()
{
    if (!m_isAvail && resetCurrAudioGateway()) {
        m_isAvail = true;
        emit availabilityChanged(true);
    }
}

void BluetoothAudioState::headsetDisconnected()
{
    if (!resetCurrAudioGateway()) {
        m_isAvail = false;
        emit availabilityChanged(false);
    }
}

QAudioStateInfo BluetoothAudioState::info() const
{
    return m_info;
}

QAudio::AudioCapabilities BluetoothAudioState::capabilities()const
{

    if (m_isPhone) {
        return QAudio::OutputOnly;
    } else {
        return QAudio::OutputOnly | QAudio::InputOnly;
    }
}

bool BluetoothAudioState::isAvailable() const
{
    return m_isAvail;
}

bool BluetoothAudioState::enter(QAudio::AudioCapability capability)
{
    Q_UNUSED(capability)

        qLog(AudioState) << "BluetoothAudioState::enter()" << "isPhone" <<
        m_isPhone;

    if (m_currAudioGateway || resetCurrAudioGateway()) {
        m_currAudioGateway->connectAudio();
        //m_isActive = setAudioScenario(Scenario_GSMBluetooth);
    }

    return m_isActive;
}

bool BluetoothAudioState::leave()
{
    if (m_currAudioGateway || resetCurrAudioGateway()) {
        m_currAudioGateway->releaseAudio();
    }

    m_isActive = false;

    return true;
}
#endif

class NeoAudioPluginPrivate
{
public:
    QList < QAudioState * >m_states;
};

NeoAudioPlugin::NeoAudioPlugin(QObject * parent):
QAudioStatePlugin(parent)
{
    m_data = new NeoAudioPluginPrivate;

    m_data->m_states.push_back(new HandsfreeAudioState(false, this));           // ringtones, mp3 etc..
    m_data->m_states.push_back(new HandsfreeAudioState(true, this));            // loud gsm
    m_data->m_states.push_back(new EarpieceAudioState(this));                   // default for gsm calls
    m_data->m_states.push_back(new HeadsetAudioState(false, this));             // audio in headphones
    m_data->m_states.push_back(new HeadsetAudioState(true, this));              // gsm in headphones

#ifdef QTOPIA_BLUETOOTH
    // Can play media through bluetooth. Can record through bluetooth as well.
    m_data->m_states.push_back(new BluetoothAudioState(false, this));
    m_data->m_states.push_back(new BluetoothAudioState(true, this));
#endif

    // Basic initialization
    amixerSet(QStringList() << "DAC1 Analog" << "off");
    amixerSet(QStringList() << "DAC2 Analog" << "on");
    amixerSet(QStringList() << "Codec Operation Mode" << "Option 1 (audio)");
}

NeoAudioPlugin::~NeoAudioPlugin()
{
    qDeleteAll(m_data->m_states);

    delete m_data;
}

QList < QAudioState * >NeoAudioPlugin::statesProvided()const
{
    return m_data->m_states;
}

Q_EXPORT_PLUGIN2(neoaudio_plugin, NeoAudioPlugin)
#include "neoaudioplugin.moc"
