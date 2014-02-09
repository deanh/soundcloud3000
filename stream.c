#include <pthread.h>
#include <string.h>
#include <stdio.h>

#include "soundcloud3000.h"

static int portaudio_callback(const void *input_buffer,
                              void *output_buffer,
                              unsigned long frames_per_buffer,
                              const PaStreamCallbackTimeInfo* timeinfo,
                              PaStreamCallbackFlags status_flags,
                              void *user_data )
{
    Stream *stream = user_data;
    stream_read(stream, output_buffer, sizeof(float) * frames_per_buffer * 2);
    return 0;
}

static void *run_thread(void *ptr)
{
    Stream *stream = ptr;
    char stream_url[4096];
    
    Response *response = http_request_url(stream->url);

    if (http_read_body(response) < 0 && response->status != 302) {
        fprintf(stderr, "request failed %s", stream->url); 
        return NULL;
    }

    free_response(response);

    if (http_header(response, "Location", stream_url) < 0) {
        fprintf(stderr, "expected redirect %s", stream->url); 
        return NULL;
    }

    response = http_request_url(stream_url);

    mpg123_open_fd(stream->mpg123, response->fd);
    
    free_response(response);

    Pa_StartStream(stream->pa_stream);

    return NULL;
}

Stream *stream_open(const char *url)
{
    int err;
    Stream *stream = malloc(sizeof(Stream));
    int frames_per_buffer = 512;

    stream->url = url;

    fprintf(stderr, "stream_open: %s\n", url);

    if ((stream->mpg123 = mpg123_new(NULL, &err)) == NULL) {
        fprintf(stderr, "%s", mpg123_plain_strerror(err));
        return NULL;
    }
    
    mpg123_param(stream->mpg123, MPG123_VERBOSE, 2, 0);
    mpg123_param(stream->mpg123, MPG123_ADD_FLAGS, MPG123_FORCE_FLOAT, 0.);

    err = Pa_OpenDefaultStream(&stream->pa_stream,
                               0,           /* no input channels */
                               2,           /* stereo output */
                               paFloat32,   /* 32 bit floating point output */
                               44100,       /* sample rate*/
                               frames_per_buffer,
                               portaudio_callback,
                               (void*) stream);

    if (err != paNoError) {
        fprintf(stderr, "%s", Pa_GetErrorText(err));
        stream_close(stream);
        return NULL;
    }

    pthread_create(&stream->thread, NULL, run_thread, (void *) stream);
    
    return stream;
}

int stream_read(Stream *stream, void *buffer, size_t buffer_size)
{
    size_t done;
    int err = 0;

    err = mpg123_read(stream->mpg123, buffer, buffer_size, &done);

    if (err != MPG123_OK && err != MPG123_NEW_FORMAT) {
        fprintf(stderr, "%s", mpg123_plain_strerror(err));
    }

    return err;
}

void stream_close(Stream *stream)
{
    mpg123_close(stream->mpg123);
    mpg123_delete(stream->mpg123);
    Pa_CloseStream(stream->pa_stream);

    pthread_cancel(stream->thread);

    free(stream);
}
