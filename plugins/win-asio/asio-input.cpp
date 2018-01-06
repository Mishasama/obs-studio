/*
Copyright (C) 2017 by pkv <pkv.stream@gmail.com>, andersama <???>

Based on Pulse Input plugin by Leonhard Oelke.

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#pragma once

#include <util/bmem.h>
#include <util/platform.h>
#include <util/threading.h>
#include <obs-module.h>
#include <vector>
#include <stdio.h>
#include <string>
#include <windows.h>

#include "RtAudio.h"


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

#define blog(level, msg, ...) blog(level, "asio-input: " msg, ##__VA_ARGS__)

#define NSEC_PER_SEC  1000000000LL
#define NSEC_PER_MSEC 1000000L

#define TEST_RUN_TIME  20.0		// run for 20 seconds

#define TEXT_FIRST_CHANNEL              obs_module_text("FirstChannel")
#define TEXT_LAST_CHANNEL               obs_module_text("LastChannel")
//#define CHANNEL_FORMAT  "channel_format"
//#define TEXT_CHANNEL_FORMAT             obs_module_text("ChannelFormat")
//#define TEXT_CHANNEL_FORMAT_NONE        obs_module_text("ChannelFormat.None")
//#define TEXT_CHANNEL_FORMAT_MONO        obs_module_text("ChannelFormat.Mono")
//#define TEXT_CHANNEL_FORMAT_2_0CH       obs_module_text("ChannelFormat.2_0ch")
//#define TEXT_CHANNEL_FORMAT_2_1CH       obs_module_text("ChannelFormat.2_1ch")
//#define TEXT_CHANNEL_FORMAT_4_0CH       obs_module_text("ChannelFormat.4_0ch")
//#define TEXT_CHANNEL_FORMAT_4_1CH       obs_module_text("ChannelFormat.4_1ch")
//#define TEXT_CHANNEL_FORMAT_5_1CH       obs_module_text("ChannelFormat.5_1ch")
//#define TEXT_CHANNEL_FORMAT_7_1CH       obs_module_text("ChannelFormat.7_1ch")
#define TEXT_BUFFER_SIZE                obs_module_text("BufferSize")
#define TEXT_BUFFER_64_SAMPLES          obs_module_text("64_samples")
#define TEXT_BUFFER_128_SAMPLES         obs_module_text("128_samples")
#define TEXT_BUFFER_256_SAMPLES         obs_module_text("256_samples")
#define TEXT_BUFFER_512_SAMPLES         obs_module_text("512_samples")
#define TEXT_BUFFER_1024_SAMPLES        obs_module_text("1024_samples")
#define TEXT_BITDEPTH                   obs_module_text("BitDepth")

#define OPEN_ASIO_SETTINGS "open_asio_settings"
#define CLOSE_ASIO_SETTINGS "close_asio_settings"
#define OPEN_ASIO_TEXT obs_module_text("OpenAsioDriverSettings")
#define CLOSE_ASIO_TEXT obs_module_text("CloseAsioDriverSettings")


struct asio_data {
	obs_source_t *source;

	/*asio device and info */
	const char *device;
	uint8_t device_index;
	RtAudio::DeviceInfo info;

	audio_format BitDepth; // 16bit or 32 bit
	int SampleRate;          //44100 or 48000 Hz
//	uint8_t *buffer;         //stores the audio data
	uint16_t BufferSize;     // number of samples in buffer
	uint64_t first_ts;       //first timestamp

	/* channels info */
	unsigned int channels; //total number of input channels
	unsigned int output_channels; // number of output channels (not used)
//	speaker_layout speakers;

	/* Allow custom capture of contiguous channels;
	 * FirstChannel and LastChannel can be identical (mono capture);
	 * FirstChannel takes its value between 0 and (channels - 1);
	 * LastChannel >= FirstChannel and LastChannel < channels .
	 */
	uint8_t FirstChannel; // index of the first channel which will be captured
	uint8_t LastChannel; // index of the last channel which will be captured
};

/* global RtAudio */
RtAudio adc;

/* ======================================================================= */
/* conversion between rtaudio and obs */

enum audio_format rtasio_to_obs_audio_format(RtAudioFormat format)
{
	switch (format) {
	case RTAUDIO_SINT16:   return AUDIO_FORMAT_16BIT;
	case RTAUDIO_SINT32:   return AUDIO_FORMAT_32BIT;
	case RTAUDIO_FLOAT32:  return AUDIO_FORMAT_FLOAT;
	default:               break;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

int BitDepth(audio_format format) {
	switch (format) {
	case AUDIO_FORMAT_16BIT:   return 16;
	case AUDIO_FORMAT_32BIT:   return 32;
	case AUDIO_FORMAT_FLOAT:   return 32;
	default:                   return 32;
	}
}

RtAudioFormat obs_to_rtasio_audio_format(audio_format format)
{
	switch (format) {
	case AUDIO_FORMAT_16BIT:   return RTAUDIO_SINT16;
	// obs doesn't have 24 bit
	case AUDIO_FORMAT_32BIT:   return RTAUDIO_SINT32;
	case AUDIO_FORMAT_FLOAT:   return RTAUDIO_FLOAT32;
	default:                   break;
	}
	// default to 32 bit samples for best quality
	return RTAUDIO_SINT32;
}

enum speaker_layout asio_channels_to_obs_speakers(unsigned int channels)
{
	switch (channels) {
	case 1:   return SPEAKERS_MONO;
	case 2:   return SPEAKERS_STEREO;
	case 3:   return SPEAKERS_2POINT1;
	case 4:   return SPEAKERS_QUAD;
	case 5:   return SPEAKERS_4POINT1;
	case 6:   return SPEAKERS_5POINT1;
	/* no layout for 7 channels */
	case 8:   return SPEAKERS_7POINT1;
	}

	return SPEAKERS_UNKNOWN;
}

/*****************************************************************************/
//get device info
RtAudio::DeviceInfo get_device_info(const char *device) {
	RtAudio::DeviceInfo info;
	unsigned int numOfDevices = adc.getDeviceCount();
	for (uint8_t i = 0; i<numOfDevices; i++) {
		info = adc.getDeviceInfo(i);
		if (info.probed == true && strcmp(device, info.name.c_str()) == 0) {
			break;
		}
	}
	return info;
}

// get the device index
uint8_t get_device_index(const char *device) {
	RtAudio::DeviceInfo info;
	uint8_t device_index = 0;
	unsigned int numOfDevices = adc.getDeviceCount();
	for (uint8_t i = 0; i<numOfDevices; i++) {
		info = adc.getDeviceInfo(i);
		if (info.probed == true && strcmp(device, info.name.c_str()) == 0) {
			device_index = i;
			break;
		}
	}
	return device_index;
}

/*****************************************************************************/
//void asio_deinit(struct asio_data *data);
void asio_update(void *vptr, obs_data_t *settings);
void asio_destroy(void *vptr);

//creates the device list
void fill_out_devices(obs_property_t *list) {

	RtAudio::DeviceInfo info;
	//for debug purposes or helpful ui things maybe later*
	std::vector<RtAudio::DeviceInfo> asioDeviceInfo;
	int numOfDevices = adc.getDeviceCount();
	char** names = new char*[numOfDevices];
	blog(LOG_INFO,"ASIO Devices: %i\n", numOfDevices);
	// Scan through devices for various capabilities
	for (int i = 0; i<numOfDevices; i++) {
		info = adc.getDeviceInfo(i);
		asioDeviceInfo.push_back(info);
		blog(LOG_INFO, "device  %i = %s and probed.true is %i\n", i, info.name.c_str(), info.probed);
		blog(LOG_INFO, ": maximum input channels = %i\n", info.inputChannels);
		blog(LOG_INFO, ": maximum output channels = %i\n", info.outputChannels);
		std::string test = info.name;
		char* cstr = new char[test.length() + 1];
		strcpy(cstr, test.c_str());
		names[i] = cstr;
	}

	//add devices to list 
	for (int i = 0; i < numOfDevices; i++) {
//		const char dev_id = static_cast<char>(i);
		blog(LOG_INFO, "list ASIO Devices: %i\n", numOfDevices);
		blog(LOG_INFO, "list: device  %i = %s \n", i, names[i]);
		obs_property_list_add_string(list, names[i], names[i]);
	}
}

//creates list of input channels
static bool fill_out_channels(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	const char* device = obs_data_get_string(settings, "device_id");
	RtAudio::DeviceInfo info;
	unsigned int input_channels;

	//get the device info
	info = get_device_info(device);
	input_channels = info.inputChannels;

	for (unsigned int i = 0; i < input_channels; i++) {
		std::string channel_numbering(device);
		char** names = new char*[32];
		std::string test = info.name + " " + std::to_string(i);
		char* cstr = new char[test.length() + 1];
		strcpy(cstr, test.c_str());
		names[i] = cstr;
		obs_property_list_add_int(list, names[i], i);
	}
	return true;
}

//creates list of input sample rates supported by the device
static bool fill_out_sample_rates(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	const char* device = obs_data_get_string(settings, "device_id");
	RtAudio::DeviceInfo info;
	unsigned int input_channels;

	//get the device info
	info = get_device_info(device);
	std::vector<unsigned int> sampleRates;
	sampleRates = info.sampleRates;
	size_t sampleRatesNb = sampleRates.size();
	for (unsigned int i = 0; i < sampleRatesNb; i++) {
		std::string rate = std::to_string(sampleRates[i]);
		char* cstr = new char[rate.length() + 1];
		strcpy(cstr, rate.c_str());
		obs_property_list_add_int(list, cstr, sampleRates[i]);
	}
	return true;
}

//create list of supported audio formats
static bool fill_out_bit_depths(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	const char* device = obs_data_get_string(settings, "device_id");
	RtAudio::DeviceInfo info;
	unsigned int input_channels;

	//get the device info
	info = get_device_info(device);
	RtAudioFormat nativeBitdepths;
	nativeBitdepths = info.nativeFormats;
	if (nativeBitdepths & 0x2) {
		obs_property_list_add_int(list, "16 bit", AUDIO_FORMAT_16BIT);
	}
	if (nativeBitdepths & 0x8) {
		obs_property_list_add_int(list, "32 bit", AUDIO_FORMAT_32BIT);
	}
	if (nativeBitdepths & 0x10) {
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT);
	}

	return true;
}

static bool asio_device_changed(obs_properties_t *props,
	obs_property_t *list, obs_data_t *settings)
{
	const char *curDeviceId = obs_data_get_string(settings, "device_id");
	obs_property_t *first_channel = obs_properties_get(props, "first channel");
	obs_property_t *last_channel = obs_properties_get(props, "last channel");
	obs_property_t *sample_rate = obs_properties_get(props, "sample rate");
	obs_property_t *bit_depth = obs_properties_get(props, "bit depth");

	obs_property_list_clear(first_channel);
	obs_property_list_clear(last_channel);
	obs_property_list_clear(sample_rate);
	obs_property_list_clear(bit_depth);

	size_t itemCount = obs_property_list_item_count(list);
	bool itemFound = false;

	for (size_t i = 0; i < itemCount; i++) {
		const char *DeviceId = obs_property_list_item_string(list, i);
		if (strcmp(DeviceId, curDeviceId) == 0) {
			itemFound = true;
			break;
		}
	}

	if (!itemFound) {
		obs_property_list_insert_string(list, 0, " ", curDeviceId);
		obs_property_list_item_disable(list, 0, true);
	}
	obs_property_set_modified_callback(first_channel, fill_out_channels);
	obs_property_set_modified_callback(last_channel, fill_out_channels);
	obs_property_set_modified_callback(sample_rate, fill_out_sample_rates);
	obs_property_set_modified_callback(bit_depth, fill_out_bit_depths);

	return true;
}

int create_asio_buffer(void *outputBuffer, void *inputBuffer, unsigned int nBufferFrames,
	double streamTime, RtAudioStreamStatus status, void *userData) {
	unsigned int i;
	asio_data *data = (asio_data *)userData;

	if (data->BitDepth == AUDIO_FORMAT_UNKNOWN /*||
		data->speakers == SPEAKERS_UNKNOWN*/) {
		return 0;
	}

	uint8_t *buffer;
	uint8_t *inputBuf = (uint8_t *)inputBuffer;
	int recorded_channels = data->LastChannel - data->FirstChannel + 1; //number of channels recorded
	
	blog(LOG_INFO, "testing!");
	/* buffer in Bytes =
	 * number of frames in buffer x number of channels x bitdepth / 8
	 *                                                 
	 * buffer per channel in Bytes =
	 * number of frames in buffer x bitdepth / 8
	 */
	int BitDepthBytes = BitDepth(data->BitDepth) / 8;
	size_t bufSizePerChannelBytes = nBufferFrames * BitDepthBytes;
	size_t bufSizeBytes = bufSizePerChannelBytes * recorded_channels;
	buffer = (uint8_t *)malloc(bufSizeBytes);
	if (!buffer) {
		blog(LOG_INFO, "Buffer allocation failed!");
		return 0;
	}

	if (nBufferFrames > data->BufferSize) {
		blog(LOG_INFO, "Buffer is too small! %i > %i", nBufferFrames, data->BufferSize);
	}
	else if (nBufferFrames < data->BufferSize) {
		blog(LOG_INFO, "Buffer is too big! %i < %i", nBufferFrames, data->BufferSize);
	}
	else {
		blog(LOG_INFO, "Buffer is just right: %i", nBufferFrames);
	}

	if (status) {
		blog(LOG_INFO, "Stream overflow detected!");
		return 0;
	}

	/* Write planar audio data to asio_data buffer.
	 * For interleaved, we would have to loop over the frames
	 * and discard the unwanted samples by looping over the channels. 2 loops.
	 * With planar, we have a single loop over the channels.
	 * The buffer starts at channel 0 but we keep only from FirstChannel to LastChannel.
	 */
	for (i = data->FirstChannel; i<=data->LastChannel; i++) {
		memcpy(buffer + (i - data->FirstChannel)*bufSizePerChannelBytes, 
				inputBuf + i * bufSizePerChannelBytes, bufSizePerChannelBytes);
	}

	struct obs_source_audio out;
	out.data[0] = buffer;

	/* audio data passed to obs in planar format */
	if (data->BitDepth == AUDIO_FORMAT_16BIT) {
		out.format = AUDIO_FORMAT_16BIT_PLANAR;
	} else if (data->BitDepth == AUDIO_FORMAT_32BIT) {
		out.format = AUDIO_FORMAT_32BIT_PLANAR;
	} else if (data->BitDepth == AUDIO_FORMAT_FLOAT) {
		out.format = AUDIO_FORMAT_FLOAT_PLANAR;
	}

	if (recorded_channels == 7) {
		blog(LOG_ERROR, "OBS does not support 7 channels; defaulting to 8 channels");
		out.speakers = SPEAKERS_7POINT1; // probably won't work ; FIXME: need to memcpy one silent channel
	}
	else {
		out.speakers = asio_channels_to_obs_speakers(recorded_channels);
	}
	out.samples_per_sec = data->SampleRate;
	out.frames = nBufferFrames;//data->BufferSize;
	out.timestamp = os_gettime_ns() - ((nBufferFrames * NSEC_PER_SEC) / data->SampleRate);

	if (!data->first_ts) {
		data->first_ts = out.timestamp;
	}

	if (out.timestamp > data->first_ts) {
		obs_source_output_audio(data->source, &out);
	}

	return 0;
}

void asio_init(struct asio_data *data)
{
	// number of channels which will be captured
	int recorded_channels = data->LastChannel - data->FirstChannel + 1;

	unsigned int deviceNumber = adc.getDeviceCount();
	if (deviceNumber < 1) {
		blog(LOG_INFO,"\nNo audio devices found!\n");
	}
	RtAudio::StreamParameters parameters;
	parameters.deviceId = data->device_index? data->device_index:0;
	parameters.nChannels = recorded_channels? recorded_channels:1;
	parameters.firstChannel = 0;  //first channel passed to the buffer; this is not the first channel captured
	unsigned int sampleRate = data->SampleRate ? data->SampleRate:48000;
	unsigned int bufferFrames = data->BufferSize? data->BufferSize:256; // default is 256 frames
	RtAudioFormat audioFormat = obs_to_rtasio_audio_format(data->BitDepth? data->BitDepth: AUDIO_FORMAT_FLOAT);
	RtAudio::StreamOptions options;
	options.flags = RTAUDIO_NONINTERLEAVED;
	if (adc.isStreamOpen()) {
		//stream might not be runnning*
	}
	else {
		try {
			adc.openStream(NULL, &parameters, audioFormat, sampleRate, &bufferFrames, &create_asio_buffer, data, &options);
		}
		catch (RtAudioError& e) {
			e.printMessage();
			blog(LOG_INFO, "error caught in openStream\n");
			blog(LOG_INFO, "error type number is %i\n", e.getType());
			blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
			goto cleanup;
		}
	}
	if (adc.isStreamRunning()) {
		//
	} else {
		try {
			adc.startStream();
		}
		catch (RtAudioError& e) {
			e.printMessage();
			blog(LOG_INFO, "error caught in startStream\n");
			blog(LOG_INFO, "error type number is %i\n", e.getType());
			blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
			goto cleanup;
		}
	}

	return;
cleanup:
	try {
		adc.stopStream();
	}
	catch (RtAudioError& e) {
		e.printMessage();
		blog(LOG_ERROR, "error caught in stopStream");
		blog(LOG_INFO, "error type number is %i\n", e.getType());
		blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
	}
	if (adc.isStreamOpen())
		adc.closeStream();
	
}

static void * asio_create(obs_data_t *settings, obs_source_t *source)
{
	struct asio_data *data = new asio_data;

	data->source = source;
	data->first_ts = 0;
	data->device = NULL;

	asio_update(data, settings);

	if (obs_data_get_string(settings, "device_id")) {
		asio_init(data);
	}

	return data;
}

void asio_destroy(void *vptr)
{
	struct asio_data *data = (asio_data *)vptr;

	try {
		adc.stopStream();
	}
	catch (RtAudioError& e) {
		e.printMessage();
		blog(LOG_INFO, "error caught in asio_destroy()\n");
		blog(LOG_INFO, "error type number is %i\n", e.getType());
		blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
	}

	if (adc.isStreamOpen()) {
		adc.closeStream();
	}

	delete data;
}

/* set all settings to asio_data struct */
void asio_update(void *vptr, obs_data_t *settings)
{
	struct asio_data *data =(asio_data *)vptr;
	const char *device;
	unsigned int rate;
	speaker_layout ChannelFormat;
	audio_format BitDepth;
	uint16_t BufferSize;
	unsigned int channels;
	uint8_t FirstChannel;
	uint8_t LastChannel;
	RtAudio::DeviceInfo info;
	bool reset = false;

	device = obs_data_get_string(settings, "device_id");
	//blog(LOG_INFO, "device in settings is %s\n", device);
	//blog(LOG_INFO, "device in asio data is %s\n", data->device);
	try {
		if (device == NULL) {
			reset = true;
		} else if (data->device == NULL) {
			data->device = bstrdup(device);
			reset = true;
		} else {
			if (strcmp(device, data->device) != 0) {
				data->device = bstrdup(device);
				reset = true;
			}
		}
	}
	catch(...) {
		blog(LOG_INFO, "Initializing asio device");
	}
	
	info = get_device_info(device);
	
	rate = (int)obs_data_get_int(settings, "sample rate");
	if (data->SampleRate != (int)rate) {
		data->SampleRate = (int)rate;
		reset = true;
	}

	/*ChannelFormat = (speaker_layout)obs_data_get_int(settings, CHANNEL_FORMAT);
	if (data->speakers != ChannelFormat) {
		data->speakers = ChannelFormat;
		reset = true;
	}*/

	BitDepth = (audio_format)obs_data_get_int(settings,"bit depth");
	if (data->BitDepth != BitDepth) {
		data->BitDepth = BitDepth;
		reset = true;
	}

	BufferSize = obs_data_get_int(settings, "buffer");
	if (data->BufferSize != BufferSize) {
		data->BufferSize = BufferSize;
		reset = true;
	}

	FirstChannel = (uint8_t)obs_data_get_int(settings, "first channel");
	if (FirstChannel != data->FirstChannel) {
		data->FirstChannel = FirstChannel;
		reset = true;
	}
	LastChannel = (uint8_t)obs_data_get_int(settings, "last channel");
	if (LastChannel != data->LastChannel) {
		data->LastChannel = LastChannel;
		reset = true;
	}

	data->channels = info.inputChannels;
	channels = data->channels;
	data->output_channels = info.outputChannels;
	data->device_index = get_device_index(device);

	// check channels and swap if necessary
	if (FirstChannel > channels || LastChannel > channels || FirstChannel < 0 ||
			LastChannel < 0) {
		blog(LOG_ERROR, "Invalid number of channels");
	} else {
		data->channels = channels;
		if (FirstChannel <= LastChannel) {
			data->FirstChannel = FirstChannel;
			data->LastChannel = LastChannel;
		} else {
			data->FirstChannel = LastChannel;
			data->LastChannel = FirstChannel;
		}
	}
	if (reset && adc.isStreamOpen()) {
		if (adc.isStreamRunning()) {
			try {
				adc.stopStream();
			}
			catch (RtAudioError& e) {
				e.printMessage();
				blog(LOG_INFO, "error caught in asio_update()\n");
				blog(LOG_INFO, "error type number is %i\n", e.getType());
				blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
			}
		}
		adc.closeStream();
		asio_init(data);
	}
	else if(reset) {
		asio_init(data);
	}
}

const char * asio_get_name(void *unused)
{
	UNUSED_PARAMETER(unused);
	return obs_module_text("asioInput");
}

void asio_get_defaults(obs_data_t *settings)
{
//	obs_data_set_default_string(settings, "device_id", "default");
	obs_data_set_default_int(settings, "sample rate", 48000);
//	obs_data_set_default_int(settings, CHANNEL_FORMAT, SPEAKERS_MONO);
	obs_data_set_default_int(settings, "bit depth", AUDIO_FORMAT_FLOAT);
	obs_data_set_default_int(settings, "first channel", 0);
	obs_data_set_default_int(settings, "last channel", 0);
//	obs_data_set_default_int(settings, "buffer", 256);
}

obs_properties_t * asio_get_properties(void *unused)
{
	obs_properties_t *props;
	obs_property_t *devices;
	obs_property_t *rate;
//	obs_property_t *channel_layout;
	obs_property_t *first_channel;
	obs_property_t *last_channel;
	obs_property_t *bit_depth;
	obs_property_t *buffer_size;

	UNUSED_PARAMETER(unused);

	props = obs_properties_create();
	devices = obs_properties_add_list(props, "device_id",
			obs_module_text("Device"), OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback(devices, asio_device_changed);
	fill_out_devices(devices);
//	obs_property_list_add_string(devices, "Default", "default");

	first_channel = obs_properties_add_list(props, "first channel",
			TEXT_FIRST_CHANNEL, OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_INT);
	obs_property_set_modified_callback(first_channel, fill_out_channels);

	last_channel = obs_properties_add_list(props, "last channel",
			TEXT_LAST_CHANNEL, OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_INT);
	obs_property_set_modified_callback(last_channel, fill_out_channels);

	rate = obs_properties_add_list(props, "sample rate",
			obs_module_text("SampleRate"), OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(rate, "44100 Hz", 44100);
	obs_property_list_add_int(rate, "48000 Hz", 48000);

	//channel_layout = obs_properties_add_list(props, CHANNEL_FORMAT,
	//		TEXT_CHANNEL_FORMAT, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_NONE,
	//		SPEAKERS_UNKNOWN);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_MONO,
	//	SPEAKERS_MONO);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_2_0CH,
	//		SPEAKERS_STEREO);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_2_1CH,
	//		SPEAKERS_2POINT1);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_4_0CH,
	//		SPEAKERS_QUAD);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_4_1CH,
	//		SPEAKERS_4POINT1);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_5_1CH,
	//		SPEAKERS_5POINT1);
	//obs_property_list_add_int(channel_layout, TEXT_CHANNEL_FORMAT_7_1CH,
	//		SPEAKERS_7POINT1);

	bit_depth = obs_properties_add_list(props, "bit depth",
			TEXT_BITDEPTH, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(bit_depth, "16 bit", AUDIO_FORMAT_16BIT);
	obs_property_list_add_int(bit_depth, "32 bit", AUDIO_FORMAT_32BIT);
	obs_property_list_add_int(bit_depth, "32 bit float", AUDIO_FORMAT_FLOAT);

	buffer_size = obs_properties_add_list(props, "buffer", TEXT_BUFFER_SIZE,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(buffer_size, TEXT_BUFFER_64_SAMPLES, 64);
	obs_property_list_add_int(buffer_size, TEXT_BUFFER_128_SAMPLES, 128);
	obs_property_list_add_int(buffer_size, TEXT_BUFFER_256_SAMPLES, 256);
	obs_property_list_add_int(buffer_size, TEXT_BUFFER_512_SAMPLES, 512);
	obs_property_list_add_int(buffer_size, TEXT_BUFFER_1024_SAMPLES, 1024);

	//obs_properties_add_button(props, OPEN_ASIO_SETTINGS, OPEN_ASIO_TEXT,
	//		open_settings_button_clicked);
	//obs_properties_add_button(props, CLOSE_ASIO_SETTINGS, CLOSE_ASIO_TEXT, close_editor_button_clicked);
	//obs_property_set_visible(obs_properties_get(props, CLOSE_ASIO_SETTINGS), false);
	return props;
}

bool obs_module_load(void)
{
	struct obs_source_info asio_input_capture = {};
	asio_input_capture.id             = "asio_input_capture";
	asio_input_capture.type           = OBS_SOURCE_TYPE_INPUT;
	asio_input_capture.output_flags   = OBS_SOURCE_AUDIO;
	asio_input_capture.create         = asio_create;
	asio_input_capture.destroy        = asio_destroy;
	asio_input_capture.update         = asio_update;
	asio_input_capture.get_defaults   = asio_get_defaults;
	asio_input_capture.get_name       = asio_get_name;
	asio_input_capture.get_properties = asio_get_properties;

	obs_register_source(&asio_input_capture);
	return true;
}