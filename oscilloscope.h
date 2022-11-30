#ifndef OSCILLOSCOPE_H
#define OSCILLOSCOPE_H

#include <QThread>
#include <QCoreApplication>
#include <QVector>
#include <QByteArray>
#include <QtEndian>
#include <QAudioOutput>
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QAudioDevice>
#else
#include <QAudioDeviceInfo>
using QAudioDevice = QAudioDeviceInfo;
#endif
#include <QAudioFormat>

struct Point {
    quint8 x = 0;
    quint8 y = 0;
};

class Oscilloscope : public QThread
{
    Q_OBJECT
public:
    explicit Oscilloscope(QObject *parent = nullptr);
    ~Oscilloscope();

    int set(QAudioDevice audioDevice, int sampleRate, int channelCount, int channelX, int channelY, int fps);
    int setAudioDevice(const QAudioDevice audioDevice);
    int setSampleRate(int sampleRate);
    int setChannelCount(int channelCount);
    void setChannelX(int channelX);
    void setChannelY(int channelY);
    void setFPS(int fps);
    void setPoints(const QVector<Point> points);
    void run();
    int stop(int time = 0);
    bool state();

signals:

public slots:

private:
    void setBuffer();  //reallocate buffer memory
    int isFormatSupported();

    bool stopMe = false;
    bool stateStart = false;

    QAudioDevice audioDevice;
    int sampleRate;
    int channelCount;
    int channelX;
    int channelY;
    int fps;
    QAudioFormat format;

    int bufferMaxSize = 0;
    char* buffer = nullptr;
    int bufferLen = 0;
    char* bufferRefresh = nullptr;
    int bufferRefreshLen = 0;
    QAtomicInteger<bool> refresh = false;       //if refresh == true, the thread will copy the contents of bufferRefresh to the buffer using the bufferRefreshLen length of bufferRefresh
    //QByteArray bufferFrame;   //frame

};

#endif // OSCILLOSCOPE_H
