/**
 * FreeRDP: A Remote Desktop Protocol Implementation
 * Audio Output Virtual Channel
 *
 * Copyright 2012 Laxmikant Rashinkar <LK.Rashinkar@gmail.com>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <winpr/crt.h>

#include <freerdp/types.h>
#include <freerdp/codec/dsp.h>
#include <freerdp/utils/svc_plugin.h>

#include <AudioToolbox/AudioToolbox.h>
#include <AudioToolbox/AudioQueue.h>

#include "rdpsnd_main.h"

#define AQ_NUM_BUFFERS		10
#define AQ_BUF_SIZE		(32 * 1024)

struct rdpsnd_mac_plugin
{
	rdpsndDevicePlugin device;

	BOOL isOpen;
	BOOL isPlaying;
	
	UINT32 latency;
	AUDIO_FORMAT format;
	int audioBufferIndex;
    
	AudioQueueRef audioQueue;
	AudioStreamBasicDescription audioFormat;
	AudioQueueBufferRef audioBuffers[AQ_NUM_BUFFERS];
};
typedef struct rdpsnd_mac_plugin rdpsndMacPlugin;

static void mac_audio_queue_output_cb(void* inUserData, AudioQueueRef inAQ, AudioQueueBufferRef inBuffer)
{
	
}

static void rdpsnd_mac_set_format(rdpsndDevicePlugin* device, AUDIO_FORMAT* format, int latency)
{
	rdpsndMacPlugin* mac = (rdpsndMacPlugin*) device;
	
	mac->latency = (UINT32) latency;
	CopyMemory(&(mac->format), format, sizeof(AUDIO_FORMAT));
	
	switch (format->wFormatTag)
	{
		case WAVE_FORMAT_ALAW:
			mac->audioFormat.mFormatID = kAudioFormatALaw;
			break;
			
		case WAVE_FORMAT_MULAW:
			mac->audioFormat.mFormatID = kAudioFormatULaw;
			break;
			
		case WAVE_FORMAT_PCM:
			mac->audioFormat.mFormatID = kAudioFormatLinearPCM;
			break;
			
		case WAVE_FORMAT_GSM610:
			mac->audioFormat.mFormatID = kAudioFormatMicrosoftGSM;
			break;
			
		default:
			break;
	}
	
	mac->audioFormat.mSampleRate = format->nSamplesPerSec;
	mac->audioFormat.mFormatFlags = kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked;
	mac->audioFormat.mFramesPerPacket = 1;
	mac->audioFormat.mChannelsPerFrame = format->nChannels;
	mac->audioFormat.mBitsPerChannel = format->wBitsPerSample;
	mac->audioFormat.mBytesPerFrame = (format->wBitsPerSample * format->nChannels) / 8;
	mac->audioFormat.mBytesPerPacket = format->nBlockAlign;
	
	rdpsnd_print_audio_format(format);
}

static void rdpsnd_mac_open(rdpsndDevicePlugin* device, AUDIO_FORMAT* format, int latency)
{
	int index;
	OSStatus status;
    
	rdpsndMacPlugin* mac = (rdpsndMacPlugin*) device;
	
	if (mac->isOpen)
		return;
    
	mac->audioBufferIndex = 0;
    
	device->SetFormat(device, format, 0);
    
	status = AudioQueueNewOutput(&(mac->audioFormat),
				     mac_audio_queue_output_cb, mac,
				     NULL, NULL, 0, &(mac->audioQueue));
	
	if (status != 0)
	{
		fprintf(stderr, "AudioQueueNewOutput failure\n");
		return;
	}
    
	for (index = 0; index < AQ_NUM_BUFFERS; index++)
	{
		status = AudioQueueAllocateBuffer(mac->audioQueue, AQ_BUF_SIZE, &mac->audioBuffers[index]);
		
		if (status != 0)
		{
			fprintf(stderr, "AudioQueueAllocateBuffer failed\n");
		}
	}
    
	mac->isOpen = TRUE;
}

static void rdpsnd_mac_close(rdpsndDevicePlugin* device)
{
	rdpsndMacPlugin* mac = (rdpsndMacPlugin*) device;
	
	if (mac->isOpen)
	{
		mac->isOpen = FALSE;
		
		AudioQueueStop(mac->audioQueue, true);
		AudioQueueDispose(mac->audioQueue, true);
		
		mac->isPlaying = FALSE;
	}
}

static void rdpsnd_mac_free(rdpsndDevicePlugin* device)
{
	
}

static BOOL rdpsnd_mac_format_supported(rdpsndDevicePlugin* device, AUDIO_FORMAT* format)
{
        if (format->wFormatTag == WAVE_FORMAT_PCM)
        {
                return TRUE;
        }
        else if (format->wFormatTag == WAVE_FORMAT_ALAW)
        {
                return FALSE;
        }
        else if (format->wFormatTag == WAVE_FORMAT_MULAW)
        {
                return FALSE;
        }
        else if (format->wFormatTag == WAVE_FORMAT_GSM610)
        {
                return FALSE;
        }
	
	return FALSE;
}

static void rdpsnd_mac_set_volume(rdpsndDevicePlugin* device, UINT32 value)
{
	
}

static void rdpsnd_mac_start(rdpsndDevicePlugin* device)
{
	rdpsndMacPlugin* mac = (rdpsndMacPlugin*) device;
	
	if (!mac->isPlaying)
	{
		OSStatus status;
		
		if (!mac->audioQueue)
			return;
		
		status = AudioQueueStart(mac->audioQueue, NULL);
		
		if (status != 0)
		{
			printf("AudioQueueStart failed\n");
		}
		
		mac->isPlaying = TRUE;
	}
}

static void rdpsnd_mac_play(rdpsndDevicePlugin* device, BYTE* data, int size)
{
	int length;
	AudioQueueBufferRef audioBuffer;
	rdpsndMacPlugin* mac = (rdpsndMacPlugin*) device;
    
	fprintf(stderr, "WavePlay\n");
	
	if (!mac->isOpen)
		return;

	audioBuffer = mac->audioBuffers[mac->audioBufferIndex];
    
	length = size > AQ_BUF_SIZE ? AQ_BUF_SIZE : size;
    
	CopyMemory(audioBuffer->mAudioData, data, length);
	audioBuffer->mAudioDataByteSize = length;
    
	AudioQueueEnqueueBuffer(mac->audioQueue, audioBuffer, 0, 0);
    
	mac->audioBufferIndex++;

	if (mac->audioBufferIndex >= AQ_NUM_BUFFERS)
		mac->audioBufferIndex = 0;
	
	device->Start(device);
}

#ifdef STATIC_CHANNELS
#define freerdp_rdpsnd_client_subsystem_entry	mac_freerdp_rdpsnd_client_subsystem_entry
#endif

int freerdp_rdpsnd_client_subsystem_entry(PFREERDP_RDPSND_DEVICE_ENTRY_POINTS pEntryPoints)
{
	rdpsndMacPlugin* mac;
    
	mac = (rdpsndMacPlugin*) malloc(sizeof(rdpsndMacPlugin));
	
	if (mac)
	{
		ZeroMemory(mac, sizeof(rdpsndMacPlugin));
	
		mac->device.Open = rdpsnd_mac_open;
		mac->device.FormatSupported = rdpsnd_mac_format_supported;
		mac->device.SetFormat = rdpsnd_mac_set_format;
		mac->device.SetVolume = rdpsnd_mac_set_volume;
		mac->device.Play = rdpsnd_mac_play;
		mac->device.Start = rdpsnd_mac_start;
		mac->device.Close = rdpsnd_mac_close;
		mac->device.Free = rdpsnd_mac_free;

		pEntryPoints->pRegisterRdpsndDevice(pEntryPoints->rdpsnd, (rdpsndDevicePlugin*) mac);
	}

	return 0;
}
