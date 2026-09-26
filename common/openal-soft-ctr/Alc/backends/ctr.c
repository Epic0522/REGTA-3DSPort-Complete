/**
 * OpenAL cross platform audio library
 * Copyright (C) 2010 by Chris Robinson
 * This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the
 *  Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 *  Boston, MA  02111-1307, USA.
 * Or go to http://www.gnu.org/copyleft/lgpl.html
 */

#include "config.h"

#include <3ds.h>
#include <stdlib.h>
#include "alMain.h"
#include "AL/al.h"
#include "AL/alc.h"

#define BYTES_PER_SAMPLE	4
#define NUM_WAVEBUFFERS		3
#define SAMPLES_PER_BUF        1024
#define BYTES_PER_BUF	    	(SAMPLES_PER_BUF * BYTES_PER_SAMPLE)
#define SAMPLERATE             NDSP_SAMPLE_RATE

typedef struct {
    volatile int killNow;
    ALvoid *thread;
    ndspWaveBuf waveBuf[NUM_WAVEBUFFERS];
    uint8_t *buffer;
} ctr_data;


static const ALCchar ctrDevice[] = "CTR:DSP";

Handle BufferIsReady;

void
CtrProcWakeMeUpInside(void *arg)
{
    svcSignalEvent(BufferIsReady);
}

static ALuint
CtrProc(ALvoid *ptr)
{
    ALCdevice *Device = (ALCdevice*)ptr;
    ctr_data *data = (ctr_data*)Device->ExtraData;
    ndspWaveBuf *waveBuf = data->waveBuf;
    int wbi = 0;

    while(!data->killNow && Device->Connected)
    {
	ALubyte *WritePtr = waveBuf[wbi].data_vaddr;
	if(waveBuf[wbi].status == NDSP_WBUF_PLAYING ||
	   waveBuf[wbi].status == NDSP_WBUF_QUEUED){
	    svcWaitSynchronization(BufferIsReady, U64_MAX);
	}else if (waveBuf[wbi].status == NDSP_WBUF_FREE ||
	          waveBuf[wbi].status == NDSP_WBUF_DONE){
	    aluMixData(Device, waveBuf[wbi].data_vaddr, SAMPLES_PER_BUF);
	    DSP_FlushDataCache(waveBuf[wbi].data_vaddr, BYTES_PER_BUF);
	    ndspChnWaveBufAdd(0, &waveBuf[wbi]);
	    wbi = (wbi + 1) % NUM_WAVEBUFFERS;
        }
    }

    return 0;
}

static ALCenum ctr_open_playback(ALCdevice *device, const ALCchar *deviceName)
{
    int i;
    ctr_data *data;

    if(!deviceName)
        deviceName = ctrDevice;
    else if(strcmp(deviceName, ctrDevice) != 0)
        return ALC_INVALID_VALUE;

    data = (ctr_data*)calloc(1, sizeof(*data));
    data->killNow = 0;

    device->szDeviceName = strdup(deviceName);
    device->ExtraData = data;

    device->Frequency   = SAMPLERATE;
    device->UpdateSize  = SAMPLES_PER_BUF;
    device->NumUpdates  = NUM_WAVEBUFFERS;
    device->FmtType     = DevFmtShort;
    device->FmtChans    = DevFmtStereo;

    ndspInit();
    ndspSetOutputMode(NDSP_OUTPUT_STEREO);
    ndspChnSetRate(0, SAMPLERATE);
    ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);

    float mix[12] =
	{ 1.0, 1.0, 0.0, 0.0,
	  0.0, 0.0, 0.0, 0.0,
	  0.0, 0.0, 0.0, 0.0
	};

    ndspChnSetMix(0, mix);
    
    data->buffer = linearAlloc(NUM_WAVEBUFFERS * BYTES_PER_BUF);
    memset(data->waveBuf, 0, sizeof(data->waveBuf));
    for (i = 0; i < NUM_WAVEBUFFERS; i++){
	data->waveBuf[i].data_vaddr = &data->buffer[BYTES_PER_BUF * i];
	data->waveBuf[i].nsamples = SAMPLES_PER_BUF;
    }

    SetDefaultChannelOrder(device);

    svcCreateEvent(&BufferIsReady,0);
    ndspSetCallback(CtrProcWakeMeUpInside, NULL);
    
    return ALC_NO_ERROR;
}

static void ctr_close_playback(ALCdevice *device)
{
    ctr_data *data = (ctr_data*)device->ExtraData;
    ndspSetCallback(NULL, NULL);
    ndspChnWaveBufClear(0);
    ndspExit();
    /* ndspExit joins the callback thread before its event is closed. */
    svcCloseHandle(BufferIsReady);
    BufferIsReady = 0;
    linearFree(data->buffer);
    free(data);
    device->ExtraData = NULL;
}

static ALCboolean ctr_reset_playback(ALCdevice *device)
{
    SetDefaultWFXChannelOrder(device);
    return ALC_TRUE;
}

static ALCboolean ctr_start_playback(ALCdevice *device)
{
    ctr_data *data = (ctr_data*)device->ExtraData;

    data->thread = StartThread(CtrProc, device);
    if(data->thread == NULL)
        return ALC_FALSE;

    return ALC_TRUE;
}

static void ctr_stop_playback(ALCdevice *device)
{
    ctr_data *data = (ctr_data*)device->ExtraData;

    if(!data->thread)
        return;

    data->killNow = 1;
    /* HOME/power-off can suspend DSP callbacks. Wake the mixer ourselves so
     * joining it cannot wait forever for a callback which will never arrive. */
    svcSignalEvent(BufferIsReady);
    StopThread(data->thread);
    data->thread = NULL;

    data->killNow = 0;
}

static const BackendFuncs ctr_funcs = {
    ctr_open_playback,
    ctr_close_playback,
    ctr_reset_playback,
    ctr_start_playback,
    ctr_stop_playback,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL,
    NULL
};

ALCboolean alc_ctr_init(BackendFuncs *func_list)
{
    *func_list = ctr_funcs;
    return ALC_TRUE;
}

void alc_ctr_deinit(void)
{
}

void alc_ctr_probe(enum DevProbe type)
{
    switch(type)
    {
        case ALL_DEVICE_PROBE:
            AppendAllDeviceList(ctrDevice);
            break;
        case CAPTURE_DEVICE_PROBE:
            break;
    }
}
