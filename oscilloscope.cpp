#include "oscilloscope.h"

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include "QAudioSink"
#endif

Oscilloscope::Oscilloscope(QObject *parent) : QThread(parent)
{
    //set border data
    //bufferFrame.resize(32 * this->channelCount);
    //for(int i = 0; i < 8; i++)
    //{
    //    bufferFrame[i * channelCount + channelX] = char(i * 32);
    //    bufferFrame[i * channelCount + channelY] = char(0);
    //    bufferFrame[(8 + i) * channelCount + channelX] = char(255);
    //    bufferFrame[(8 + i) * channelCount + channelY] = char(i * 32);
    //    bufferFrame[(16 + i) * channelCount + channelX] = char((7 - i) * 32);
    //    bufferFrame[(16 + i) * channelCount + channelY] = char(255);
    //    bufferFrame[(24 + i) * channelCount + channelX] = char(0);
    //    bufferFrame[(24 + i) * channelCount + channelY] = char((7 - i) * 32);
    //}
    //bufferFrame.resize(256 * 4 * this->channelCount);
    //for(int i = 0; i < 256; i++)
    //{
    //    bufferFrame[i * channelCount + channelX] = char(i);
    //    bufferFrame[i * channelCount + channelY] = char(0);
    //    bufferFrame[(256 + i) * channelCount + channelX] = char(255);
    //    bufferFrame[(256 + i) * channelCount + channelY] = char(i);
    //    bufferFrame[(256 * 2 + i) * channelCount + channelX] = char(255 - i);
    //    bufferFrame[(256 * 2 + i) * channelCount + channelY] = char(255);
    //    bufferFrame[(256 * 3 + i) * channelCount + channelX] = char(0);
    //    bufferFrame[(256 * 3 + i) * channelCount + channelY] = char(255 - i);
    //}

    //setting output format
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    format.setSampleFormat(QAudioFormat::UInt8);
#else
    format.setCodec("audio/pcm");
    format.setSampleSize(8);
    format.setSampleType(QAudioFormat::UnSignedInt);
    format.setByteOrder(QAudioFormat::LittleEndian);
#endif
}

Oscilloscope::~Oscilloscope()
{
    stop();
}

int Oscilloscope::set(QAudioDevice audioDevice, int sampleRate, int channelCount, int channelX, int channelY, int fps)
{
    this->audioDevice = audioDevice;
    this->sampleRate = sampleRate;
    format.setSampleRate(sampleRate);
    this->channelCount = channelCount;
    format.setChannelCount(channelCount);
    this->channelX = channelX;
    this->channelY = channelY;
    if(fps == 0)
        this->fps = 1;
    else
        this->fps = fps;
    setBuffer();
    return isFormatSupported();
}

int Oscilloscope::setAudioDevice(const QAudioDevice audioDevice)
{
    this->audioDevice = audioDevice;
    return isFormatSupported();
}

int Oscilloscope::setSampleRate(int sampleRate)
{
    this->sampleRate = sampleRate;
    format.setSampleRate(sampleRate);
    setBuffer();
    return isFormatSupported();
}

int Oscilloscope::setChannelCount(int channelCount)
{
    this->channelCount = channelCount;
    format.setChannelCount(channelCount);
    setBuffer();
    return isFormatSupported();
}

void Oscilloscope::setChannelX(int channelX)
{
    this->channelX = channelX;
}

void Oscilloscope::setChannelY(int channelY)
{
    this->channelY = channelY;
}

void Oscilloscope::setFPS(int fps)
{
    if(fps == 0)
        this->fps = 1;
    else
        this->fps = fps;
    setBuffer();
}

void Oscilloscope::setBuffer()  //reallocate buffer memory according to parameters
{
    bufferMaxSize = this->sampleRate / this->fps * this->channelCount;  //the maximum value is equal to the amount of data at the sampling rate for one frame of time
    if(buffer)
        delete buffer;
    buffer = new char[bufferMaxSize];
    if(bufferRefresh)
        delete buffer;
    bufferRefresh = new char[bufferMaxSize];
}

void Oscilloscope::setPoints(const QVector<Point> points)   //calculate the buffer based on the point data, and temporarily store it in bufferRefresh and another thread will copy it when it is refreshed next time
{
    for(int i = 0; i < points.length() && i * channelCount < bufferMaxSize; i++)
    {
        bufferRefresh[i * channelCount + channelX] = char(points.at(i).x);
        bufferRefresh[i * channelCount + channelY] = char(-1 - points.at(i).y);
    }

    bufferRefreshLen = points.length() * channelCount;
    if(bufferRefreshLen > bufferMaxSize) bufferRefreshLen = bufferMaxSize;

    refresh = true; //set the need to refresh the marker and wait for the thread to be refreshed
}

int Oscilloscope::isFormatSupported()
{
    return audioDevice.isFormatSupported(format);
}

void Oscilloscope::run()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    auto output = new QAudioSink(this->audioDevice, format);
#else
    auto output = new QAudioOutput(this->audioDevice, format);
#endif
    if(output->bufferSize() < bufferMaxSize * 2) output->setBufferSize(bufferMaxSize * 2);  //if the audio buffer is less than twice the maximum buffer, expand it
    auto device = output->start();

    stateStart = true;
    while(1)
    {
        //exit detection
        if(stopMe)
        {
            output->stop();
            delete output;
            stopMe = false;
            stateStart = false;
            return;
        }

        //if need to refresh, refresh first
        if(refresh)
        {
            refresh = false;
            memcpy(buffer, bufferRefresh, size_t(bufferRefreshLen));
            bufferLen = bufferRefreshLen;
        }

        //output
        if(device && output && bufferLen)
        {
            if((output->bufferSize() - output->bytesFree()) * 10 / channelCount * fps / sampleRate < 10 && //if the remaining data playable time is less than the inverse of fps (*10 is to not want to use floating point numbers)
                    output->bytesFree() > bufferLen)    //and the remaining buffer is larger than the size of the data to be written (this does not normally happen, but here is just a precaution)
            {
                //device->write(bufferFrame, bufferFrame.length()); //输出边框
                device->write(buffer, bufferLen);
            }
            else    //otherwise, it means that the buffer time must be greater than the countdown of fps, so you can take a break
                QThread::msleep(1000 / fps / 10);  //rest a tenth of the frame time
        }

    }
}

int Oscilloscope::stop(int time)
{
    if(stateStart)
    {
        stopMe = true;

        int i = 0;
        while(stateStart)
        {
            QCoreApplication::processEvents();
            i++;
            if(time && (i > time)) return 1;
        }
    }
    return 0;
}

bool Oscilloscope::state()
{
    return stateStart;
}
