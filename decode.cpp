#include "decode.h"

Decode::Decode(QObject *parent) : QThread(parent)
{

}

Decode::~Decode()
{
    avcodec_free_context(&video_dec_ctx);
    avcodec_free_context(&audio_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_frame_free(&frame);
//    av_frame_free(&video_convert_frame);
    if(video_edge) delete video_edge;
}

void Decode::set(int scaleX, int scaleY, int moveX, int moveY, int edge)
{
    this->scaleX = scaleX;
    this->scaleY = scaleY;
    this->moveX = moveX;
    this->moveY = moveY;
    this->edge = edge;
    refresh = true;
}

void Decode::setScaleX(int scaleX)
{
    this->scaleXrefresh = scaleX;
    refresh = true;
}

void Decode::setScaleY(int scaleY)
{
    this->scaleYrefresh = scaleY;
    refresh = true;
}

void Decode::setMoveX(int moveX)
{
    this->moveXrefresh = moveX;
    refresh = true;
}

void Decode::setMoveY(int moveY)
{
    this->moveYrefresh = moveY;
    refresh = true;
}

void Decode::setEdge(int edge)
{
    this->edgeRefresh = edge;
    refresh = true;
}

void Decode::run()
{
    int ret = 0, got_frame;

    //reading frames from a file
    while (av_read_frame(fmt_ctx, &pkt) >= 0) {
        AVPacket orig_pkt = pkt;
        do {
            ret = decode_packet(&got_frame);
            if (ret < 0)
                break;
            pkt.data += ret;
            pkt.size -= ret;
        } while (pkt.size > 0);
        av_packet_unref(&orig_pkt);
    }
    //refreshing cached frames
    pkt.data = nullptr;
    pkt.size = 0;
    do {
        decode_packet(&got_frame);
    } while (got_frame);
}

bool Decode::isReady()
{
    return ready;
}

int Decode::open(QString filename)
{
    //specify file read format
    if (avformat_open_input(&fmt_ctx, filename.toLatin1().data(), nullptr, nullptr) < 0)
    {
        return 1;   //unable to open source file
    }

    //display input file information (for debugging)
    //    av_dump_format(fmt_ctx, 0, filename.toLatin1().data(), 0);

    //retrieve stream information
    if (avformat_find_stream_info(fmt_ctx, nullptr) < 0)
    {
        return 2;   //can't retrieve stream information
    }

    //open video stream
    video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video_stream_idx >= 0)
    {
        video_stream = fmt_ctx->streams[video_stream_idx];
        //find decoder for video stream
        const AVCodec* video_dec = avcodec_find_decoder(video_stream->codecpar->codec_id);
        if (video_dec)
        {
            //assign codec context to decoder
            video_dec_ctx = avcodec_alloc_context3(video_dec);
            if (video_dec_ctx)
            {
                //copy codec parameters from the input stream to the output codec context
                if (avcodec_parameters_to_context(video_dec_ctx, video_stream->codecpar) >= 0)
                {
                    //start decoder
                    if (avcodec_open2(video_dec_ctx, video_dec, nullptr) >= 0)
                    {
                        //retrieve information in width and height pixel format
                        video_width = video_dec_ctx->width;
                        video_height = video_dec_ctx->height;
                        video_pix_fmt = video_dec_ctx->pix_fmt;
                        //convert colors
//                        video_convert_frame = av_frame_alloc();
//                        if (!video_convert_frame)
//                            return 8;  //unable to assign frames
//                        unsigned char* out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_BGRA, video_width, video_height, 1));
//                        if (!out_buffer)
//                            return 8;  //unable to assign frames
//                        av_image_fill_arrays(video_convert_frame->data, video_convert_frame->linesize, out_buffer, AV_PIX_FMT_BGRA, video_width, video_height, 1);
//                        video_convert_ctx = sws_getContext(video_width, video_height, video_pix_fmt,
//                                                           video_width, video_height, AV_PIX_FMT_BGRA,
//                                                           SWS_BICUBIC, NULL, NULL, NULL);
//                        if(!video_convert_ctx)
//                        {
//                            audio_stream_idx = -1;
//                            return 8;  //unable to set video color conversion context
//                        }
                        //edge detection
                        video_edge = new quint8[256 * 256];
                    }
                    else
                        return 7;   //unable to open video codec
                }
                else
                    return 6;   //unable to copy video codec parameters to the decoder context
            }
            else
                return 5;   //unable to assign codec context
        }
        else
            return 4;   //can't find codec for video streaming
    }
    else
        return 3;   //can't find video stream

    //open audio stream
    audio_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (audio_stream_idx >= 0)
    {
        audio_stream = fmt_ctx->streams[audio_stream_idx];
        //find decoder
        const AVCodec* audio_dec = avcodec_find_decoder(audio_stream->codecpar->codec_id);
        if (audio_dec)
        {
            //assigning codec context
            audio_dec_ctx = avcodec_alloc_context3(audio_dec);
            ;
            if (audio_dec_ctx)
            {
                //copy codec parameters
                if (avcodec_parameters_to_context(audio_dec_ctx, audio_stream->codecpar) >= 0)
                {
                    //start decoder
                    if (avcodec_open2(audio_dec_ctx, audio_dec, nullptr) >= 0)
                    {
                        //audio resampling in 44100 dual-channel 16-bit integer
                        audio_convert_ctx = swr_alloc_set_opts(nullptr,
                                                               AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_U8, 44100,
                                                               int64_t(audio_dec_ctx->channel_layout), audio_dec_ctx->sample_fmt, audio_dec_ctx->sample_rate,
                                                               0, nullptr);
                        //audio_convert_ctx = swr_alloc_set_opts(nullptr, AV_CH_LAYOUT_STEREO, AV_SAMPLE_FMT_S16, 44100, audio_stream->codec->channel_layout, audio_stream->codec->sample_fmt, audio_stream->codec->sample_rate, 0, NULL);    //采样为44100双声道16位整型
                        if(!audio_convert_ctx)
                        {
                            audio_stream_idx = -1;
                            return 15;  //can't set resampling context
                        }
                        swr_init(audio_convert_ctx);
                    }
                    else
                    {
                        audio_stream_idx = -1;
                        return 14;   //unable to open audio codec
                    }
                }
                else
                {
                    audio_stream_idx = -1;
                    return 13;   //unable to copy audio codec parameters
                }
            }
            else
            {
                audio_stream_idx = -1;
                return 12;   //unable to assign codec context
            }
        }
        else
        {
            audio_stream_idx = -1;
            return 11;   //can't find decoder for audio stream
        }
    }
    else
    {
        audio_stream_idx = -1;
        return 10;   //can't find audio stream
    }

    frame = av_frame_alloc();
    if (!frame)
        return 16;  //unable to assign frames

    //initialize the packet, set data to nullptr, let the demuxer fill it 
    av_init_packet(&pkt);
    pkt.data = nullptr;
    pkt.size = 0;

    ready = true;

    return 0;
}

AVRational Decode::fps()
{
    if(video_stream && video_stream->avg_frame_rate.num && video_stream->avg_frame_rate.den)
        return video_stream->avg_frame_rate;
    else
    {
        AVRational ret;
        ret.den = 1;
        ret.num = 30;
        return ret;
    }
}

int Decode::width()
{
    return video_width;
}

int Decode::height()
{
    return video_height;
}

int Decode::decode_packet(int *got_frame)
{
    int ret = 0;
    int decoded = pkt.size;
    *got_frame = 0;

    //check queuelength
    auto fps = this->fps();
    if(video.size() > 4)    //cache 4f, rest a frame if more than 4f are already in the queue
        QThread::msleep(1000 * fps.den / fps.num);
    if (pkt.stream_index == video_stream_idx)
    {
        //decoding frames
        ret = avcodec_send_packet(video_dec_ctx, &pkt);
        if (ret < 0)
        {
            fprintf(stderr, "Error while sending packets for decoding\n");
            return ret;
        }
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(video_dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return ret;
            else if (ret < 0) {
                fprintf(stderr, "Error while decoding\n");
                return ret;
            }

            //output frame information
            //printf("video_frame n:%d coded_n:%d\n",
            //       video_frame_count++, frame->coded_picture_number);
            //convert colors
            //sws_scale(video_convert_ctx,
            //          frame->data, frame->linesize, 0, video_height,
            //          video_convert_frame->data, video_convert_frame->linesize);

            //check refresh
            if(refresh)
            {
                refresh = false;
                scaleX = scaleXrefresh;
                scaleY = scaleYrefresh;
                moveX = moveXrefresh;
                moveY = moveYrefresh;
                edge = edgeRefresh;
            }

            //edge detection
            float temp1[9]={1,0,-1,1,0,-1,1,0,-1};  //template arrays
            float temp2[9]={-1,-1,-1,0,0,0,1,1,1};
            float result1;
            float result2;
            int count = 0;  //total point count
            memset(video_edge, 0, 256 * 256);
            {
                int y = (1 - moveY - video_height / 2) * scaleY / 100 + 256 / 2;
                if(y < 0)
                    y = 0;
                int yMax = (video_height - 1 - moveY - video_height / 2) * scaleY / 100 + 256 / 2;
                if(yMax > 256)
                    yMax = 256;
                for (; y < yMax; y++)
                {
                    int yy = video_height / 2 + (-256 / 2 + y) * 100 / scaleY + moveY;
                    {
                        int x = (1 - moveX - video_width / 2) * scaleX / 100 + 256 / 2;
                        if(x < 0)
                            x = 0;
                        int xMax = (video_width - 1 - moveX - video_width / 2) * scaleX / 100 + 256 / 2;
                        if(xMax > 256)
                            xMax = 256;
                        for(; x < xMax; x++)
                        {
                            int xx = video_width / 2 + (-256 / 2 + x) * 100 / scaleX + moveX;
                            result1 = 0;
                            result2 = 0;
                            for (int ty = 0; ty < 3; ty++)
                            {
                                for (int tx = 0; tx < 3; tx++)
                                {
                                    int z = frame->data[0][(yy - 1 + ty) * frame->linesize[0] + xx - 1 + tx];
                                    result1 += z * temp1[ ty * 3 + tx];
                                    result2 += z * temp2[ ty * 3 + tx];
                                }
                            }
                            result1 = abs(int(result1));
                            result2 = abs(int(result2));
                            if(result1 < result2)
                                result1 = result2;
                            if((result1 > edge))
                            {
                                video_edge[y * 256 + x] = 255;
                                count++;
                            }

                        }
                    }
                }
            }

            //generate QImage
            //QImage image(video_width, video_height, QImage::Format_ARGB32);
            //for (int y = 0; y < video_height; y++)
            //    memcpy(image.scanLine(y), video_convert_frame->data[0] + y * video_convert_frame->linesize[0], video_width * 4);
            QImage image(256, 256, QImage::Format_Grayscale8);
            for (int y = 0; y < 256; y++)
            {
                int yy = video_height / 2 + (-256 / 2 + y) * 100 / scaleY + moveY;
                if(yy < 0 || yy >= video_height)
                    memset(image.scanLine(y), 0, 256);
                else
                    for(int x = 0; x < 256; x++)
                    {
                        int xx = video_width / 2 + (-256 / 2 + x) * 100 / scaleX + moveX;
                        if(xx < 0 || xx >= video_width)
                            image.scanLine(y)[x] = 0;
                        else
                            image.scanLine(y)[x] = frame->data[0][yy * frame->linesize[0] + xx];
                    }
            }
            video.enqueue(image);   //add to the end of the queue
            QImage imageEdge(256, 256, QImage::Format_Grayscale8);
            for (int y = 0; y < 256; y++)
                memcpy(imageEdge.scanLine(y), video_edge + y * 256, 256);
            videoEdge.enqueue(imageEdge);

            //calculate path points
            QVector<Point> points(count);
            int x = 0;
            int y = 0;
            for (int i = 0; i < count; i++)
            {
                int rMax;
                rMax = (x > y) ? x : y;
                rMax = (rMax > 256 - x) ? rMax : 256 - x;
                rMax = (rMax > 256 - y) ? rMax : 256 - y;

                for (int r = 0; r < rMax; r++)
                {
                    int xMin = x - r;
                    int xMax = x + r;
                    int yMin = y - r;
                    int yMax = y + r;
                    bool xMinOut = false;
                    bool xMaxOut = false;
                    bool yMinOut = false;
                    bool yMaxOut = false;
                    if(xMin < 0)
                    {
                        xMin = 0;
                        xMinOut = true;
                    }
                    if(xMax >= 256)
                    {
                        xMax = 255;
                        xMaxOut = true;
                    }
                    if(yMin < 0)
                    {
                        yMin = 0;
                        yMinOut = true;
                    }
                    if(yMax >= 256)
                    {
                        yMax = 255;
                        yMaxOut = true;
                    }
                    //upper edge
                    if(!yMinOut)
                    {
                        for(int xx = xMin; xx < xMax; xx++)
                        {
                            if(video_edge[yMin * 256 + xx] == 255)
                            {
                                points[i].x = x = xx;
                                points[i].y = y = yMin;
                                video_edge[yMin * 256 + xx] = 254; //mark as used
                                goto find;
                            }
                        }
                    }
                    //lower edge
                    if(!yMaxOut)
                    {
                        for(int xx = xMin; xx < xMax; xx++)
                        {
                            if(video_edge[yMax * 256 + xx] == 255)
                            {
                                points[i].x = x = xx;
                                points[i].y = y = yMax;
                                video_edge[yMax * 256 + xx] = 254; //mark as used
                                goto find;
                            }
                        }
                    }
                    //left edge
                    if(!xMinOut)
                    {
                        for(int yy = yMin; yy < yMax; yy++)
                        {
                            if(video_edge[yy * 256 + xMin] == 255)
                            {
                                points[i].x = x = xMin;
                                points[i].y = y = yy;
                                video_edge[yy * 256 + xMin] = 254; //mark as used
                                goto find;
                            }
                        }
                    }
                    //right edge
                    if(!xMaxOut)
                    {
                        for(int yy = yMin; yy < yMax; yy++)
                        {
                            if(video_edge[yy * 256 + xMax] == 255)
                            {
                                points[i].x = x = xMax;
                                points[i].y = y = yy;
                                video_edge[yy * 256 + xMax] = 254; //mark as used
                                goto find;
                            }
                        }
                    }
                }
find:
                ;;
            }
            this->points.enqueue(points);
        }
    }
    else if (pkt.stream_index == audio_stream_idx)  //is audio package
    {
        //decoding audio frames
        ret = avcodec_send_packet(audio_dec_ctx, &pkt);
        if (ret < 0)
        {
            fprintf(stderr, "errer submitting packets to decoder\n");
            return ret;
        }
        while (ret >= 0)
        {
            ret = avcodec_receive_frame(audio_dec_ctx, frame);
            if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
                return ret;
            else if (ret < 0) {
                fprintf(stderr, "error while decoding\n");
                return ret;
            }
            //output frame information
            //printf("audio_frame n:%d nb_samples:%d pts:%s\n",
            //       audio_frame_count++, frame->nb_samples,
            //       av_ts2timestr(frame->pts, &audio_dec_ctx->time_base));

            int data_size = av_get_bytes_per_sample(audio_dec_ctx->sample_fmt);
            if (data_size < 0) {
                //This should not occur, checking just for paranoia
                fprintf(stderr, "unable to calculate data size\n");
                return -1;
            }

            //            for (i = 0; i < frame->nb_samples; i++)
            //                for (ch = 0; ch < audio_dec_ctx->channels; ch++)
            //                    fwrite(frame->data[ch] + data_size*i, 1, data_size, audio_dst_file);


            //            size_t unpadded_linesize = frame->nb_samples * av_get_bytes_per_sample(AVSampleFormat(frame->format));
            /* Write the raw audio data samples of the first plane. This works
             * fine for packed formats (e.g. AV_SAMPLE_FMT_S16). However,
             * most audio decoders output planar audio, which uses a separate
             * plane of audio samples for each channel (e.g. AV_SAMPLE_FMT_S16P).
             * In other words, this code will write only the first audio channel
             * in these cases.
             * You should use libswresample or libavfilter to convert the frame
             * to packed data. */
            //            fwrite(frame->extended_data[0], 1, unpadded_linesize, audio_dst_file);





            //memset(play_buf, sizeof(uint8_t), out_size);
            //            swr_convert(pSwrCtx, &play_buf , out_size, (const uint8_t**)frame->data, frame->nb_samples);
            //            out->write((char*)play_buf, out_size);
            //            QThread::msleep( 20 );




        }
    }


    return decoded;
}
