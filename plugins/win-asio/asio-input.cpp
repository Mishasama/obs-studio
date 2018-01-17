/*
Copyright (C) 2017 by pkv <pkv.stream@gmail.com>, andersama <anderson.john.alexander@gmail.com>

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
#include <bassasio.h>

//#include "RtAudio.h"

//#include <obs-frontend-api.h>
//
//#include <QFileInfo>
//#include <QProcess>
//#include <QLibrary>
//#include <QMainWindow>
//#include <QAction>
//#include <QMessageBox>
//#include <QString>
//#include <QStringList>


OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

#define blog(level, msg, ...) blog(level, "asio-input: " msg, ##__VA_ARGS__)

#define NSEC_PER_SEC  1000000000LL

#define TEXT_BUFFER_SIZE                obs_module_text("BufferSize")
#define TEXT_BUFFER_64_SAMPLES          obs_module_text("64_samples")
#define TEXT_BUFFER_128_SAMPLES         obs_module_text("128_samples")
#define TEXT_BUFFER_256_SAMPLES         obs_module_text("256_samples")
#define TEXT_BUFFER_512_SAMPLES         obs_module_text("512_samples")
#define TEXT_BUFFER_1024_SAMPLES        obs_module_text("1024_samples")
#define TEXT_BITDEPTH                   obs_module_text("BitDepth")


struct asio_data {
	obs_source_t *source;

	/*asio device and info */
	const char *device;
	uint8_t device_index;
	BASS_ASIO_INFO *info;

	audio_format BitDepth;  // 16bit or 32 bit
	double SampleRate;       //44100 or 48000 Hz
	uint16_t BufferSize;     // number of samples in buffer
	uint64_t first_ts;       //first timestamp

	/* channels info */
	DWORD channels; //total number of input channels
	DWORD output_channels; // number of output channels of device (not used)
	DWORD recorded_channels; // number of channels passed from device (including muted) to OBS; is at most 8
	DWORD route[MAX_AUDIO_CHANNELS]; // stores the channel re-ordering info
};


/* ======================================================================= */

/* conversion between BASS_ASIO and obs */

enum audio_format asio_to_obs_audio_format(DWORD format)
{
	switch (format) {
	case BASS_ASIO_FORMAT_16BIT:   return AUDIO_FORMAT_16BIT;
	case BASS_ASIO_FORMAT_32BIT:   return AUDIO_FORMAT_32BIT;
	case BASS_ASIO_FORMAT_FLOAT:   return AUDIO_FORMAT_FLOAT;
	default:                       break;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

enum audio_format get_planar_format(audio_format format)
{
	if (is_audio_planar(format))
		return format;

	switch (format) {
	case AUDIO_FORMAT_U8BIT: return AUDIO_FORMAT_U8BIT_PLANAR;
	case AUDIO_FORMAT_16BIT: return AUDIO_FORMAT_16BIT_PLANAR;
	case AUDIO_FORMAT_32BIT: return AUDIO_FORMAT_32BIT_PLANAR;
	case AUDIO_FORMAT_FLOAT: return AUDIO_FORMAT_FLOAT_PLANAR;
		//should NEVER get here
	default: return AUDIO_FORMAT_UNKNOWN;
	}
}

int bytedepth_format(audio_format format)
{
	return (int)get_audio_bytes_per_channel(format);
}

int bytedepth_format(DWORD format) {
	return bytedepth_format(asio_to_obs_audio_format(format));
}

DWORD obs_to_asio_audio_format(audio_format format)
{
	switch (format) {

	case AUDIO_FORMAT_16BIT:
		return BASS_ASIO_FORMAT_16BIT;
		// obs doesn't have 24 bit
	case AUDIO_FORMAT_32BIT:
		return BASS_ASIO_FORMAT_32BIT;

	case AUDIO_FORMAT_FLOAT:
	default:
		return BASS_ASIO_FORMAT_FLOAT;
	}
	// default to 32 float samples for best quality

}

enum speaker_layout asio_channels_to_obs_speakers(unsigned int channels)
{
	switch (channels) {
	case 1:   return SPEAKERS_MONO;
	case 2:   return SPEAKERS_STEREO;
	case 3:   return SPEAKERS_2POINT1;
	case 4:   return SPEAKERS_4POINT0;
	case 5:   return SPEAKERS_4POINT1;
	case 6:   return SPEAKERS_5POINT1;
		/* no layout for 7 channels */
	case 8:   return SPEAKERS_7POINT1;
	}
	return SPEAKERS_UNKNOWN;
}

/*****************************************************************************/
// get number of output channels
DWORD get_obs_output_channels() {
	// get channel number from output speaker layout set by obs
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	DWORD recorded_channels = get_audio_channels(aoi.speakers);
	return recorded_channels;
}

// get device number
uint8_t getDeviceCount() {
	uint8_t a, count = 0;
	BASS_ASIO_DEVICEINFO info;
	for (a = 0; BASS_ASIO_GetDeviceInfo(a, &info); a++)
		count++;
	return count;
}

//get list of devices (retrieves a list of driver names)
const char **get_list_devices() {
	const char **names;
	BASS_ASIO_SetUnicode(true);
	BASS_ASIO_DEVICEINFO *info;
	for (int i = 0; BASS_ASIO_GetDeviceInfo(i, info); i++) {
		names[i] = info->name;
		blog(LOG_INFO, "device  %i = %s\n", i, info->name);
	}
	return names;
}

// get the device index from a device name : the current index can be retrieved from DWORD BASS_ASIO_GetDevice();
DWORD get_device_index(const char *device) {
	DWORD device_index = 0;
	BASS_ASIO_SetUnicode(true);
	BASS_ASIO_DEVICEINFO *info;
	for (int i = 0; BASS_ASIO_GetDeviceInfo(i, info); i++) {
		if (strcmp(device, info->name)) {
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
	BASS_ASIO_SetUnicode(true);
	BASS_ASIO_DEVICEINFO *info;
	int numOfDevices = getDeviceCount();
	const char** names = get_list_devices();
	blog(LOG_INFO, "ASIO Devices number is : %i\n", numOfDevices);

	for (int i = 0; i < numOfDevices; i++) {
		blog(LOG_INFO, "Number of ASIO Devices: %i\n", numOfDevices);
		blog(LOG_INFO, "device %i  = %s added successfully.\n", i, names[i]);
		obs_property_list_add_string(list, names[i], names[i]);
	}
}

/* Creates list of input channels ; modified so that value -2 means inactive channel.
* This differs from a muted channel in that a muted channel would be passed to obs;
* Here an inactive channel is not passed at all. A muted channel has value -1 and
* is recorded. The user can unmute the channel later.
*/
static bool fill_out_channels_modified(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \n"
			"error number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
			BASS_ASIO_ErrorGetCode());
	}
	// DEBUG: check that the current device in bass thread is the correct one
	// once code is fine the check can be removed
	const char* device = obs_data_get_string(settings, "device_id");
	if (!strcmp(device, info.name)) {
		blog(LOG_ERROR, "Device loaded is not the one in settings\n");
	}
	//get the device info
	DWORD input_channels = info.inputs;
	obs_property_list_clear(list);
	obs_property_list_add_int(list, "mute", -1);
	for (DWORD i = 0; i < input_channels; i++) {
		char** names = new char*[34];
		std::string test = info.name;
		test = test + " " + std::to_string(i);
		char* cstr = new char[test.length() + 1];
		strcpy(cstr, test.c_str());
		names[i] = cstr;
		obs_property_list_add_int(list, names[i], i);
	}
	return true;
}

//creates list of input sample rates supported by the device and OBS (obs supports only 44100 and 48000)
static bool fill_out_sample_rates(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \n"
			"error number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
			BASS_ASIO_ErrorGetCode());
	}
	// DEBUG: check that the current device in bass thread is the correct one
	// once code is fine the check can be removed
	const char* device = obs_data_get_string(settings, "device_id");
	if (!strcmp(device, info.name)) {
		blog(LOG_ERROR, "Device loaded is not the one in settings\n");
	}

	obs_property_list_clear(list);
	//get the device info
	ret = BASS_ASIO_CheckRate(44100);
	if (ret) {
		std::string rate = "44100 Hz";
		char* cstr = new char[rate.length() + 1];
		strcpy(cstr, rate.c_str());
		obs_property_list_add_int(list, cstr, 44100);
	}
	else {
		blog(LOG_INFO, "Device loaded does not support 44100 Hz sample rate\n");
	}
	ret = BASS_ASIO_CheckRate(48000);
	if (ret) {
		std::string rate = "48000 Hz";
		char* cstr = new char[rate.length() + 1];
		strcpy(cstr, rate.c_str());
		obs_property_list_add_int(list, cstr, 48000);
	}
	else {
		blog(LOG_INFO, "Device loaded does not support 48000 Hz sample rate\n");
	}
	return true;
}

//create list of supported audio formats
static bool fill_out_bit_depths(obs_properties_t *props, obs_property_t *list, obs_data_t *settings) {
	BASS_ASIO_INFO info;
	bool ret = BASS_ASIO_GetInfo(&info);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \n"
			"error number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
			BASS_ASIO_ErrorGetCode());
	}
	// DEBUG: check that the current device in bass thread is the correct one
	// once code is fine the check can be removed
	const char* device = obs_data_get_string(settings, "device_id");
	if (!strcmp(device, info.name)) {
		blog(LOG_ERROR, "Device loaded is not the one in settings\n");
	}

	//get the device channel info
	BASS_ASIO_CHANNELINFO *channelInfo;
	ret = BASS_ASIO_ChannelGetInfo(true, 0, channelInfo);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve channel info on the current driver \n"
			"error number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
			BASS_ASIO_ErrorGetCode());
	}
	
	obs_property_list_clear(list);
	if (channelInfo->format == BASS_ASIO_FORMAT_16BIT) {
		obs_property_list_add_int(list, "16 bit (native)", AUDIO_FORMAT_16BIT);
		obs_property_list_add_int(list, "32 bit", AUDIO_FORMAT_32BIT);
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT);
	}
	else if (channelInfo->format == BASS_ASIO_FORMAT_32BIT) {
		obs_property_list_add_int(list, "16 bit", AUDIO_FORMAT_16BIT);
		obs_property_list_add_int(list, "32 bit (native)", AUDIO_FORMAT_32BIT);
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT);
	}
	else if (channelInfo->format == BASS_ASIO_FORMAT_FLOAT) {
		obs_property_list_add_int(list, "16 bit", AUDIO_FORMAT_16BIT);
		obs_property_list_add_int(list, "32 bit", AUDIO_FORMAT_32BIT);
		obs_property_list_add_int(list, "32 bit float (native)", AUDIO_FORMAT_FLOAT);
	}
	else {
		blog(LOG_ERROR, "Your device uses unsupported bit depth.\n"
			"Only 16 bit, 32 bit signed int and 32 bit float are supported.\n"
			"Change accordingly your device settings.\n"
			"Forcing bit depth to 32 bit float");
		obs_property_list_add_int(list, "32 bit float", AUDIO_FORMAT_FLOAT);
		return false;
	}
	return true;
}

static bool asio_device_changed(obs_properties_t *props,
	obs_property_t *list, obs_data_t *settings)
{
	const char *curDeviceId = obs_data_get_string(settings, "device_id");
	obs_property_t *sample_rate = obs_properties_get(props, "sample rate");
	obs_property_t *bit_depth = obs_properties_get(props, "bit depth");
	// be sure to set device as current one

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
	else {
		DWORD device_index = get_device_index(curDeviceId);
		bool ret = BASS_ASIO_SetDevice(device_index);
		if (!ret) {
			blog(LOG_ERROR, "Unable to set device %i\n", device_index);
			if (BASS_ASIO_ErrorGetCode() == BASS_ERROR_INIT) {
				BASS_ASIO_Init(device_index, BASS_ASIO_JOINORDER);
				BASS_ASIO_SetDevice(device_index);
			}
			else if (BASS_ASIO_ErrorGetCode() == BASS_ERROR_DEVICE) {
				blog(LOG_ERROR, "Device index is invalid\n");
			}
		}		
	}
	// get channel number from output speaker layout set by obs
	DWORD recorded_channels = get_obs_output_channels();

	obs_property_t *route[MAX_AUDIO_CHANNELS];
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;
	const char* route_name_format = "route %i";
	char* route_name = new char[strlen(route_name_format) + pad_digits];
	for (unsigned int i = 0; i < recorded_channels; i++) {
		std::string name = "route " + std::to_string(i);
		route[i] = obs_properties_get(props, name.c_str());
		obs_property_list_clear(route[i]);
		sprintf(route_name, route_name_format, i);
		obs_data_set_default_int(settings, route_name, -1); // default is muted channels
		obs_property_set_modified_callback(route[i], fill_out_channels_modified);
	}
	obs_property_list_clear(sample_rate);
	obs_property_list_clear(bit_depth);

	obs_property_set_modified_callback(sample_rate, fill_out_sample_rates);
	obs_property_set_modified_callback(bit_depth, fill_out_bit_depths);

	return true;
}

int mix(uint8_t *inputBuffer, obs_source_audio *out, size_t bytes_per_ch, int route[], unsigned int recorded_device_chs = UINT_MAX) {
	DWORD recorded_channels = get_obs_output_channels();
	short j = 0;
	for (size_t i = 0; i < recorded_channels; i++) {
		if (route[i] > -1 && route[i] < (int)recorded_device_chs) {
			out->data[j++] = inputBuffer + route[i] * bytes_per_ch;
		}
		else if (route[i] == -1) {
			uint8_t * silent_buffer;
			silent_buffer = (uint8_t *)calloc(bytes_per_ch, 1);
			out->data[j++] = silent_buffer;
		}
	}
	return true;
}

DWORD CALLBACK create_asio_buffer(BOOL input, DWORD channel, void *buffer, DWORD length, void *asiodata) {
	asio_data *data = (asio_data *)userData;

	int route[MAX_AUDIO_CHANNELS];
	int recorded_channels = data->recorded_channels;
	for (short i = 0; i < MAX_AUDIO_CHANNELS; i++) {
		if (i < recorded_channels) {
			route[i] = data->route[i];
		}
		else {
			route[i] = -1; // not necessary, just avoids it being unset
		}	
	}

	// retrieve device info (for debug)
	RtAudio::DeviceInfo info = get_device_info(data->device);
	data->info = info;
	//uint8_t *buffer;
	uint8_t *inputBuf = (uint8_t *)inputBuffer;

	if (status) {
		blog(LOG_WARNING, "Stream overflow detected!");
		return 0;
	}

	// won't ever reach that part of code unless we 've made some severe coding error
	if (recorded_channels > MAX_AUDIO_CHANNELS) {
		blog(LOG_ERROR, "OBS does not support more than %i channels",
				MAX_AUDIO_CHANNELS);
		return 2;
	}

	/* buffer in Bytes =
	* number of frames in buffer x number of channels x bitdepth / 8
	* buffer per channel in Bytes = number of frames in buffer x bitdepth / 8
	*/
	int BitDepthBytes = bytedepth_format(data->BitDepth);
	size_t bufSizePerChannelBytes = nBufferFrames * BitDepthBytes;
	size_t bufSizeBytes = bufSizePerChannelBytes * recorded_channels;
	if (recorded_channels == 7) {
		bufSizeBytes = bufSizePerChannelBytes * 8;
	}

	if (status) {
		blog(LOG_WARNING, "Stream overflow detected!");
		return 0;
	}

	struct obs_source_audio out;
	mix(inputBuf, &out, bufSizePerChannelBytes, route, data->channels);

	out.format = data->BitDepth;
	out.speakers = asio_channels_to_obs_speakers(recorded_channels);
	if (recorded_channels == 7) {
		out.speakers = SPEAKERS_7POINT1;
	}
	out.samples_per_sec = data->SampleRate;
	out.frames = nBufferFrames;// beware, may differ from data->BufferSize;
	out.timestamp = os_gettime_ns() - ((nBufferFrames * NSEC_PER_SEC) / data->SampleRate);

	if (!data->first_ts) {
		data->first_ts = out.timestamp;
	}

	if (out.timestamp > data->first_ts && recorded_channels != 0) {
		obs_source_output_audio(data->source, &out);
	}

	return 0;
}

void asio_init(struct asio_data *data)
{
	// get info, useful for debug
	BASS_ASIO_INFO info; 
	bool ret = BASS_ASIO_GetInfo(&info);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \n"
			"error number is : %i \n; check BASS_ASIO_ErrorGetCode\n",
			BASS_ASIO_ErrorGetCode());
	}
	data->info = &info;

	uint8_t deviceNumber = getDeviceCount();
	if (deviceNumber < 1) {
		blog(LOG_INFO, "\nNo audio devices found!\n");
	}

	// get channel number from output speaker layout set by obs
	DWORD recorded_channels = get_obs_output_channels();

	// initialize channels
	// first find the first non muted channel
	for (unsigned int i = 0; i < recorded_channels; i++) {}

	for (unsigned int i = 0; i < recorded_channels; i++) {
		// convert all samples to float irrespective of what the user sets in the settings ==> disable this setting later
		ret = BASS_ASIO_ChannelSetFormat(true, data->route[i], BASS_ASIO_FORMAT_FLOAT);
		if (!ret)
		{
			blog(LOG_ERROR, "Could not set channel bitdepth; error code : %i", BASS_ASIO_ErrorGetCode());
		}
		//ret = BASS_ASIO_ChannelSetRate(true, data->route[i], data->SampleRate);
		//if (!ret)
		//{
		//	blog(LOG_ERROR, "Could not set sample rate; error code : %i", BASS_ASIO_ErrorGetCode());
		//}
		ret = BASS_ASIO_ChannelEnable(true, data->route[i], &create_asio_buffer, data);		
		if (!ret)
		{
			blog(LOG_ERROR, "Could not enable channel; error code : %i", BASS_ASIO_ErrorGetCode());
		}
	}

	//start asio device
	ret = BASS_ASIO_Start(data->BufferSize, recorded_channels);
	switch (BASS_ASIO_ErrorGetCode()) {
	case BASS_ERROR_INIT:
	case BASS_ERROR_ALREADY:
	case BASS_ERROR_NOCHAN:
		blog(LOG_ERROR, "Unknown error when trying to start the device\n");
	case BASS_ERROR_UNKNOWN:
	default:
			blog(LOG_ERROR, "Unknown error when trying to start the device\n");
		}
	}
	RtAudio::StreamParameters parameters;
	parameters.deviceId = data->device_index;
	parameters.nChannels = data->channels;
	parameters.firstChannel = 0;
	unsigned int sampleRate = data->SampleRate;
	unsigned int bufferFrames = data->BufferSize;
	RtAudioFormat audioFormat = obs_to_rtasio_audio_format(data->BitDepth);
	//force planar formats
	RtAudio::StreamOptions options;
	options.flags = RTAUDIO_NONINTERLEAVED;

	if (!adc.isStreamOpen() && data->recorded_channels != 0) {
		try {
			adc.openStream(NULL, &parameters, audioFormat, sampleRate,
				&bufferFrames, &create_asio_buffer, data, &options);
		}
		catch (RtAudioError& e) {
			e.printMessage();
			blog(LOG_INFO, "error caught in openStream\n");
			blog(LOG_INFO, "error type number is %i\n", e.getType());
			blog(LOG_INFO, "error: %s\n", e.getMessage().c_str());
			goto cleanup;
		}
	}
	if (!adc.isStreamRunning()) {
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
	data->info = NULL;

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
	struct asio_data *data = (asio_data *)vptr;
	const char *device;
	unsigned int rate;
	audio_format BitDepth;
	uint16_t BufferSize;
	unsigned int channels;
	BASS_ASIO_INFO info;
	bool reset = false;
	bool initialized = true;
	DWORD route[MAX_AUDIO_CHANNELS];

	// get channel number from output speaker layout set by obs
	DWORD recorded_channels = get_obs_output_channels();
	data->recorded_channels = recorded_channels;

	// get device from settings
	device = obs_data_get_string(settings, "device_id");

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
	DWORD device_index = get_device_index(device);
	bool ret = BASS_ASIO_Free(); 
	if (!ret) {
		blog(LOG_ERROR, "Driver was already freed \n"
			"no need to free it\n");
	}
	ret = BASS_ASIO_Init(device_index, BASS_ASIO_JOINORDER);
	if (!ret) {
		blog(LOG_ERROR, "Unable to initialize the current driver \n"
			"error number is : %i \n;"
			"code 3: driver could not be initialized\n"
			"code 14: device already initialized\n"
			"code 23: invalid device index\n",
			BASS_ASIO_ErrorGetCode());
		initialized = false;
	}

	ret = BASS_ASIO_GetInfo(&info);
	if (!ret) {
		blog(LOG_ERROR, "Unable to retrieve info on the current driver \n"
			"error number is : %i \n; driver is not initialized\n",
			BASS_ASIO_ErrorGetCode());
	}

	// DEBUG: check that the current device in bass thread is the correct one
	// once code is fine the check can be removed
	const char* device = obs_data_get_string(settings, "device_id");
	if (!strcmp(device, info.name)) {
		blog(LOG_ERROR, "Device loaded is not the one in settings\n");
	}

	for (unsigned int i = 0; i < recorded_channels; i++) {
		std::string route_str = "route " + std::to_string(i);
		route[i] = (int)obs_data_get_int(settings, route_str.c_str());
		if (data->route[i] != route[i]) {
			data->route[i] = route[i];
		}
	}

	rate = (double)obs_data_get_int(settings, "sample rate");
	if (data->SampleRate != rate) {
		data->SampleRate = rate;
		bool ret = BASS_ASIO_SetRate(rate);
		switch (BASS_ASIO_ErrorGetCode()) {
		case BASS_ERROR_NOTAVAIL:
			blog(LOG_ERROR, "Selected sample rate not supported by device\n");
		case BASS_ERROR_INIT:
			blog(LOG_ERROR, "Device not initialized; sample rate can not be set\n");
		case BASS_ERROR_UNKNOWN:
			blog(LOG_ERROR, "Unknown error when trying to set the sample rate\n");
		} 
		reset = true;
	}

	BitDepth = (audio_format)obs_data_get_int(settings, "bit depth");
	if (data->BitDepth != BitDepth) {
		data->BitDepth = get_planar_format(BitDepth);
		reset = true;
	}

	BufferSize = (uint16_t)obs_data_get_int(settings, "buffer");
	if (data->BufferSize != BufferSize) {
		data->BufferSize = BufferSize;
		reset = true;
	}

	data->info = &info;
	data->channels = info.inputs;
	channels = data->channels;
	data->output_channels = info.outputs;
	data->device_index = get_device_index(device);

	if (reset) {
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
	obs_data_set_default_int(settings, "sample rate", 48000);
	obs_data_set_default_int(settings, "bit depth", AUDIO_FORMAT_FLOAT_PLANAR);
}

obs_properties_t * asio_get_properties(void *unused)
{
	obs_properties_t *props;
	obs_property_t *devices;
	obs_property_t *rate;
	obs_property_t *bit_depth;
	obs_property_t *buffer_size;
	obs_property_t *route[MAX_AUDIO_CHANNELS];
	int pad_digits = (int)floor(log10(abs(MAX_AUDIO_CHANNELS))) + 1;

	UNUSED_PARAMETER(unused);

	props = obs_properties_create();
	devices = obs_properties_add_list(props, "device_id",
			obs_module_text("Device"), OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_STRING);
	obs_property_set_modified_callback(devices, asio_device_changed);
	fill_out_devices(devices);
	std::string dev_descr = "ASIO devices.\n"
			"OBS-Studio supports for now a single ASIO source.\n"
			"But duplication of an ASIO source in different scenes is still possible";
	obs_property_set_long_description(devices, dev_descr.c_str());
	// get channel number from output speaker layout set by obs
	DWORD recorded_channels = get_obs_output_channels();

	std::string route_descr = "For each OBS output channel, pick one\n of the input channels of your ASIO device.\n";
	const char* route_name_format = "route %i";
	char* route_name = new char[strlen(route_name_format) + pad_digits];

	const char* route_obs_format = "Route.%i";
	char* route_obs = new char[strlen(route_obs_format) + pad_digits];
	for (size_t i = 0; i < recorded_channels; i++) {
		sprintf(route_name, route_name_format, i);
		sprintf(route_obs, route_obs_format, i);
		route[i] = obs_properties_add_list(props, route_name, obs_module_text(route_obs),
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		obs_property_set_long_description(route[i], route_descr.c_str());
	}

	free(route_name);
	free(route_obs);

	rate = obs_properties_add_list(props, "sample rate",
			obs_module_text("SampleRate"), OBS_COMBO_TYPE_LIST,
			OBS_COMBO_FORMAT_INT);
	std::string rate_descr = "Sample rate : number of samples per channel in one second.\n";
	obs_property_set_long_description(rate, rate_descr.c_str());
	
	bit_depth = obs_properties_add_list(props, "bit depth",
			TEXT_BITDEPTH, OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	std::string bit_descr = "Bit depth : size of a sample in bits and format.\n"
			"Float should be preferred.";
	obs_property_set_long_description(bit_depth, bit_descr.c_str());

	buffer_size = obs_properties_add_list(props, "buffer", TEXT_BUFFER_SIZE,
			OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(buffer_size, "64", 64);
	obs_property_list_add_int(buffer_size, "128", 128);
	obs_property_list_add_int(buffer_size, "256", 256);
	obs_property_list_add_int(buffer_size, "512", 512);
	obs_property_list_add_int(buffer_size, "1024", 1024);
	std::string buffer_descr = "Buffer : number of samples in a single frame.\n"
			"A lower value implies lower latency.\n"
			"256 should be OK for most cards.\n"
			"Warning: the real buffer returned by the device may differ";
	obs_property_set_long_description(buffer_size, buffer_descr.c_str());

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