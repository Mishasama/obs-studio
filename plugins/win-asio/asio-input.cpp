/*
Copyright (C) 2019 by andersama <anderson.john.alexander@gmail.com>
and pkv <pkv.stream@gmail.com>.
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

/* For full GPL v2 compatibility it is required to build libs with
 * our open source sdk instead of steinberg sdk , see our fork:
 * https://github.com/pkviet/portaudio , branch : openasio
 * If you build with original asio sdk, you are free to do so to the
 * extent that you do not distribute your binaries.
 */

#include <util/bmem.h>
#include <util/platform.h>
#include <obs-module.h>
#include <obs-frontend-api.h>
#include <vector>
#include <JuceHeader.h>
#include <QWidget>
#include <QMainWindow>
#include <QWindow>
#include <QMessageBox>
#include <QString>
#include <QLabel>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("win-asio", "en-US")

#define blog(level, msg, ...) blog(level, "asio-input: " msg, ##__VA_ARGS__)

static void fill_out_devices(obs_property_t *prop, bool isAsio);

static juce::AudioIODeviceType *deviceTypeAsio = AudioIODeviceType::createAudioIODeviceType_ASIO();
static juce::AudioIODeviceType *deviceTypeWasapi =
		AudioIODeviceType::createAudioIODeviceType_WASAPI(WASAPIDeviceMode::sharedLowLatency);

class AudioCB;
TimeSliceThread *global_thread;

static bool asio_device_changed(void *vptr, obs_properties_t *props, obs_property_t *list, obs_data_t *settings);
static bool asio_layout_changed(void *vptr, obs_properties_t *props, obs_property_t *list, obs_data_t *settings);
static bool fill_out_channels_modified(void *priv, obs_properties_t *props, obs_property_t *list, obs_data_t *settings);

static std::vector<AudioCB *> callbacksAsio;
static std::vector<AudioCB *> callbacksWasapi;
enum audio_format             string_to_obs_audio_format(std::string format)
{
	if (format == "32 Bit Int") {
		return AUDIO_FORMAT_32BIT;
	} else if (format == "32 Bit Float") {
		return AUDIO_FORMAT_FLOAT;
	} else if (format == "16 Bit Int") {
		return AUDIO_FORMAT_16BIT;
	}

	return AUDIO_FORMAT_UNKNOWN;
}

// returns corresponding planar format on entering some interleaved one
enum audio_format get_planar_format(audio_format format)
{
	if (is_audio_planar(format))
		return format;

	switch (format) {
	case AUDIO_FORMAT_U8BIT:
		return AUDIO_FORMAT_U8BIT_PLANAR;
	case AUDIO_FORMAT_16BIT:
		return AUDIO_FORMAT_16BIT_PLANAR;
	case AUDIO_FORMAT_32BIT:
		return AUDIO_FORMAT_32BIT_PLANAR;
	case AUDIO_FORMAT_FLOAT:
		return AUDIO_FORMAT_FLOAT_PLANAR;
		// should NEVER get here
	default:
		return AUDIO_FORMAT_UNKNOWN;
	}
}

// returns the size in bytes of a sample from an obs audio_format
int bytedepth_format(audio_format format)
{
	return (int)get_audio_bytes_per_channel(format);
}

// get number of output channels (this is set in obs general audio settings
int get_obs_output_channels()
{
	// get channel number from output speaker layout set by obs
	struct obs_audio_info aoi;
	obs_get_audio_info(&aoi);
	return (int)get_audio_channels(aoi.speakers);
}

int get_max_obs_channels()
{
	static int channels = 0;
	if (channels > 0) {
		return channels;
	} else {
		for (int i = 0; i < 1024; i++) {
			int c = get_audio_channels((speaker_layout)i);
			if (c > channels)
				channels = c;
		}
		return channels;
	}
}

static std::vector<speaker_layout> known_layouts = {
		SPEAKERS_MONO,    /**< Channels: MONO */
		SPEAKERS_STEREO,  /**< Channels: FL, FR */
		SPEAKERS_2POINT1, /**< Channels: FL, FR, LFE */
		SPEAKERS_3POINT0,
		SPEAKERS_4POINT0, /**< Channels: FL, FR, FC, RC */
		SPEAKERS_QUAD,
		SPEAKERS_3POINT1,
		SPEAKERS_5POINT0,
		SPEAKERS_4POINT1, /**< Channels: FL, FR, FC, LFE, RC */
		SPEAKERS_5POINT1, /**< Channels: FL, FR, FC, LFE, RL, RR */
		SPEAKERS_6POINT0,
		SPEAKERS_6POINT1,
		SPEAKERS_7POINT0,
		SPEAKERS_7POINT1, /**< Channels: FL, FR, FC, LFE, RL, RR, SL, SR */
		SPEAKERS_OCTAGONAL,
		SPEAKERS_9POINT0,
		SPEAKERS_10POINT0,
		SPEAKERS_11POINT0,
		SPEAKERS_12POINT0,
		SPEAKERS_13POINT0,
		SPEAKERS_14POINT0,
		SPEAKERS_15POINT0,
		SPEAKERS_HEXADECAGONAL,
		SPEAKERS_NHK,
};

static std::vector<std::string> known_layouts_str = {"Mono", "Stereo", "2.1", "3.0", "4.0", "quad", "3.1", "5.0", "4.1",
		"5.1", "6.0", "6.1", "7.0", "7.1", "octagonal", "9.0", "10.0", "11.0", "12.0", "13.0", "14.0", "15.0",
		"hexadecagonal", "22.2"};

class AudioCB : public juce::AudioIODeviceCallback {
private:
	AudioIODevice *  _device      = nullptr;
	char *           _name        = nullptr;
	int              _write_index = 0;
	double           sample_rate;
	TimeSliceThread *_thread = nullptr;

public:
	struct AudioBufferInfo {
		AudioBuffer<float> buffer;
		obs_source_audio   out;
	};

	int write_index()
	{
		return _write_index;
	}

private:
	std::vector<AudioBufferInfo> buffers;

public:
	class AudioListener : public TimeSliceClient {
	private:
		std::vector<short> _route;
		obs_source_audio   in;
		obs_source_t *     source;

		bool     active;
		int      read_index = 0;
		int      wait_time  = 4;
		AudioCB *callback;
		AudioCB *current_callback;

		size_t   silent_buffer_size = 0;
		uint8_t *silent_buffer      = nullptr;

		bool set_data(AudioBufferInfo *info, obs_source_audio &out, const std::vector<short> &route,
				int *sample_rate)
		{
			out.speakers        = in.speakers;
			out.samples_per_sec = info->out.samples_per_sec;
			out.format          = AUDIO_FORMAT_FLOAT_PLANAR;
			out.timestamp       = info->out.timestamp;
			out.frames          = info->buffer.getNumSamples();

			*sample_rate = out.samples_per_sec;
			// cache a silent buffer
			size_t buffer_size = (out.frames * sizeof(bytedepth_format(out.format)));
			if (silent_buffer_size < buffer_size) {
				if (silent_buffer)
					bfree(silent_buffer);
				silent_buffer      = (uint8_t *)bzalloc(buffer_size);
				silent_buffer_size = buffer_size;
			}

			int       ichs = info->buffer.getNumChannels();
			int       ochs = get_audio_channels(out.speakers);
			uint8_t **data = (uint8_t **)info->buffer.getArrayOfWritePointers();

			bool muted = true;
			for (int i = 0; i < ochs; i++) {
				if (route[i] >= 0 && route[i] < ichs) {
					out.data[i] = data[route[i]];
					muted       = false;
				} else {
					out.data[i] = silent_buffer;
				}
			}
			return !muted;
		}

	public:
		AudioListener(obs_source_t *source, AudioCB *cb) : source(source), callback(cb)
		{
			active = true;
		}

		~AudioListener()
		{
			if (silent_buffer)
				bfree(silent_buffer);
			disconnect();
		}

		void disconnect()
		{
			active = false;
		}

		void reconnect()
		{
			active = true;
		}

		void setOutput(obs_source_audio o)
		{
			in.format          = o.format;
			in.samples_per_sec = o.samples_per_sec;
			in.speakers        = o.speakers;
		}

		void setCurrentCallback(AudioCB *cb)
		{
			current_callback = cb;
		}

		void setCallback(AudioCB *cb)
		{
			callback = cb;
		}

		void setReadIndex(int idx)
		{
			read_index = idx;
		}

		void setRoute(std::vector<short> route)
		{
			_route = route;
		}

		AudioCB *getCallback()
		{
			return callback;
		}

		int useTimeSlice()
		{
			if (!active || callback != current_callback)
				return -1;
			int write_index = callback->write_index();
			if (read_index == write_index)
				return wait_time;

			std::vector<short> route           = _route;
			int                sample_rate     = 0;
			int                max_sample_rate = 1;
			int                m               = (int)callback->buffers.size();

			while (read_index != write_index) {
				obs_source_audio out;
				bool unmuted = set_data(&callback->buffers[read_index], out, route, &sample_rate);
				if (unmuted && out.speakers)
					obs_source_output_audio(source, &out);
				if (sample_rate > max_sample_rate)
					max_sample_rate = sample_rate;
				read_index = (read_index + 1) % m;
			}
			wait_time = ((1000 / 2) * AUDIO_OUTPUT_FRAMES) / max_sample_rate;
			return wait_time;
		}
	};

	AudioIODevice *getDevice()
	{
		return _device;
	}

	const char *getName()
	{
		return _name;
	}

	void setDevice(AudioIODevice *device, const char *name)
	{
		_device = device;
		if (_name)
			bfree(_name);
		_name = bstrdup(name);
	}

	AudioCB(AudioIODevice *device, const char *name)
	{
		_device = device;
		_name   = bstrdup(name);
	}

	~AudioCB()
	{
		bfree(_name);
	}

	void audioDeviceIOCallback(const float **inputChannelData, int numInputChannels, float **outputChannelData,
			int numOutputChannels, int numSamples)
	{
		uint64_t ts = os_gettime_ns();

		for (int i = 0; i < numInputChannels; i++)
			buffers[_write_index].buffer.copyFrom(i, 0, inputChannelData[i], numSamples);
		buffers[_write_index].out.timestamp       = ts;
		buffers[_write_index].out.frames          = numSamples;
		buffers[_write_index].out.samples_per_sec = (uint32_t)sample_rate;
		_write_index                              = (_write_index + 1) % buffers.size();

		UNUSED_PARAMETER(numOutputChannels);
		UNUSED_PARAMETER(outputChannelData);
	}

	void add_client(AudioListener *client)
	{
		if (!_thread)
			_thread = global_thread;

		client->setCurrentCallback(this);
		client->setReadIndex(_write_index);
		_thread->addTimeSliceClient(client);
	}

	void remove_client(AudioListener *client)
	{
		if (_thread)
			_thread->removeTimeSliceClient(client);
	}

	void audioDeviceAboutToStart(juce::AudioIODevice *device)
	{
		blog(LOG_INFO, "Starting (%s)", device->getName().toStdString().c_str());
		juce::String name = device->getName();
		sample_rate       = device->getCurrentSampleRate();
		int buf_size      = device->getCurrentBufferSizeSamples();
		int target_size   = AUDIO_OUTPUT_FRAMES * 2;
		int count         = std::max(8, target_size / buf_size);
		int ch_count      = device->getActiveInputChannels().countNumberOfSetBits();

		if (buffers.size() < count)
			buffers.reserve(count);
		int i = 0;
		for (; i < buffers.size(); i++) {
			buffers[i].buffer              = AudioBuffer<float>(ch_count, buf_size);
			buffers[i].out.format          = AUDIO_FORMAT_FLOAT_PLANAR;
			buffers[i].out.samples_per_sec = sample_rate;
		}
		for (; i < count; i++) {
			AudioBufferInfo inf;
			inf.buffer              = AudioBuffer<float>(ch_count, buf_size);
			inf.out.format          = AUDIO_FORMAT_FLOAT_PLANAR;
			inf.out.samples_per_sec = sample_rate;
			buffers.push_back(inf);
		}

		if (!_thread) {
			_thread = global_thread;
		} else {
			for (int i = 0; i < _thread->getNumClients(); i++) {
				AudioListener *l = static_cast<AudioListener *>(_thread->getClient(i));
				l->setCurrentCallback(this);
			}
		}
		if (!_thread->isThreadRunning())
			_thread->startThread(10);
	}

	void audioDeviceStopped()
	{
		blog(LOG_INFO, "Stopped (%s)", _device->getName().toStdString().c_str());
	}

	void audioDeviceError(const juce::String &errorMessage)
	{
		if (_thread)
			_thread->stopThread(200);
		std::string error = errorMessage.toStdString();
		blog(LOG_ERROR, "Device Error!\n%s", error.c_str());
	}
};

static bool show_panel(obs_properties_t *props, obs_property_t *property, void *data);

class ASIOPlugin {
private:
	AudioIODevice *         _device   = nullptr;
	AudioCB::AudioListener *_listener = nullptr;
	std::vector<uint16_t>   _route;
	speaker_layout          _speakers;
	bool                    _isAsio;

public:
	AudioIODevice *getDevice()
	{
		return _device;
	}

	bool isAsio()
	{
		return _isAsio;
	}

	void setIsAsio(bool isAsio)
	{
		_isAsio = isAsio;
	}

	ASIOPlugin::ASIOPlugin(obs_data_t *settings, obs_source_t *source)
	{
		UNUSED_PARAMETER(settings);
		_listener = new AudioCB::AudioListener(source, nullptr);
	}

	ASIOPlugin::~ASIOPlugin()
	{
		if (_listener) {
			AudioCB *cb = _listener->getCallback();
			_listener->disconnect();
			if (cb)
				cb->remove_client(_listener);
			delete _listener;
			_listener = nullptr;
		}
	}

	static void *Create(obs_data_t *settings, obs_source_t *source, bool isAsio)
	{
		ASIOPlugin *plugin = new ASIOPlugin(settings, source);
		plugin->_isAsio    = isAsio;
		plugin->update(settings, isAsio);
		return plugin;
	}
	static void *CreateAsio(obs_data_t *settings, obs_source_t *source)
	{
		return Create(settings, source, true);
	}
	static void *CreateWasapi(obs_data_t *settings, obs_source_t *source)
	{
		return Create(settings, source, false);
	}
	static void Destroy(void *vptr)
	{
		ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
		delete plugin;
		plugin = nullptr;
	}

	// version of plugin
	static bool credits(obs_properties_t *props, obs_property_t *property, void *data)
	{
		UNUSED_PARAMETER(props);
		UNUSED_PARAMETER(property);
		UNUSED_PARAMETER(data);
		QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();
		QMessageBox  mybox(main_window);
		QString      text = "v.2.0.3\r\n © 2020, license GPL v3\r\n"
			       "Based on Juce library\r\n\r\n"
			       "Authors:\r\n"
			       "Andersama (main author) & pkv\r\n";
		mybox.setText(text);
		mybox.setIconPixmap(QPixmap(":/res/images/asiologo.png"));
		mybox.setWindowTitle(QString("Credits: obs-asio"));
		mybox.exec();
		return true;
	}
	static bool creditsWasapi(obs_properties_t *props, obs_property_t *property, void *data)
	{
		UNUSED_PARAMETER(props);
		UNUSED_PARAMETER(property);
		UNUSED_PARAMETER(data);
		QMainWindow *main_window = (QMainWindow *)obs_frontend_get_main_window();
		QMessageBox  mybox(main_window);
		QString      text = "v.1.0.0\r\n © 2020, license GPL v3\r\n"
			       "Based on Juce library\r\n\r\n"
			       "Author:\r\n"
			       "pkv\r\n"
			       "based on juce asio plugin by Andersama";
		mybox.setText(text);
		mybox.setIconPixmap(QPixmap(":/res/images/juce.png"));
		mybox.setWindowTitle(QString("Credits:"));
		mybox.exec();
		return true;
	}
	static void destroy_isAsio(void *param)
	{
		delete param;
	}

	static obs_properties_t *Properties(void *vptr, bool isAsio)
	{
		obs_properties_t *            props;
		obs_property_t *              devices;
		obs_property_t *              format;
		obs_property_t *              panel;
		obs_property_t *              button;
		int                           max_channels = get_max_obs_channels();
		std::vector<obs_property_t *> route(max_channels, nullptr);

		props              = obs_properties_create();
		ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
		plugin->setIsAsio(isAsio);

		devices = obs_properties_add_list(props, "device_id", obs_module_text("Device"), OBS_COMBO_TYPE_LIST,
				OBS_COMBO_FORMAT_STRING);
		obs_property_set_modified_callback2(devices, asio_device_changed, vptr);
		fill_out_devices(devices, isAsio);
		obs_property_set_long_description(devices, obs_module_text("ASIO Devices"));

		format = obs_properties_add_list(props, "speaker_layout", obs_module_text("Format"),
				OBS_COMBO_TYPE_LIST, OBS_COMBO_FORMAT_INT);
		for (size_t i = 0; i < known_layouts.size(); i++)
			obs_property_list_add_int(format, known_layouts_str[i].c_str(), known_layouts[i]);
		obs_property_set_modified_callback2(format, asio_layout_changed, vptr);

		for (size_t i = 0; i < max_channels; i++) {
			route[i] = obs_properties_add_list(props, ("route " + std::to_string(i)).c_str(),
					obs_module_text(("Route." + std::to_string(i)).c_str()), OBS_COMBO_TYPE_LIST,
					OBS_COMBO_FORMAT_INT);
			obs_property_set_long_description(
					route[i], obs_module_text(("Route.Desc." + std::to_string(i)).c_str()));
		}

		if (isAsio) {
			panel = obs_properties_add_button2(
					props, "ctrl", obs_module_text("Control Panel"), show_panel, vptr);
			ASIOPlugin *   plugin = static_cast<ASIOPlugin *>(vptr);
			AudioIODevice *device = nullptr;
			if (plugin)
				device = plugin->getDevice();

			obs_property_set_visible(panel, device && device->hasControlPanel());
		}

		button = obs_properties_add_button(props, "credits", "CREDITS", isAsio ? credits : creditsWasapi);
		return props;
	}

	static obs_properties_t *PropertiesAsio(void *vptr)
	{
		return Properties(vptr, true);
	}
	static obs_properties_t *PropertiesWasapi(void *vptr)
	{
		return Properties(vptr, false);
	}

	void update(obs_data_t *settings, bool isAsio)
	{
		std::string    name     = obs_data_get_string(settings, "device_id");
		speaker_layout layout   = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
		AudioCB *      callback = nullptr;

		juce::AudioIODeviceType *mydeviceType    = isAsio ? deviceTypeAsio : deviceTypeWasapi;
		std::vector<AudioCB *>   myCallbacks     = isAsio ? callbacksAsio : callbacksWasapi;
		AudioIODevice *          selected_device = nullptr;
		for (int i = 0; i < myCallbacks.size(); i++) {
			AudioCB *      cb     = myCallbacks[i];
			AudioIODevice *device = cb->getDevice();
			std::string    n      = cb->getName();
			if (n == name) {
				if (!device) {
					String deviceName = name.c_str();
					device            = mydeviceType->createDevice(deviceName, deviceName);
					cb->setDevice(device, name.c_str());
				}
				selected_device = device;
				_device         = device;
				callback        = cb;
				break;
			}
		}

		if (selected_device == nullptr) {
			AudioCB *cb = _listener->getCallback();

			_listener->setCurrentCallback(callback);
			_listener->disconnect();

			if (cb)
				cb->remove_client(_listener);
			return;
		}

		StringArray in_chs  = _device->getInputChannelNames();
		StringArray out_chs = _device->getOutputChannelNames();
		BigInteger  in      = 0;
		BigInteger  out     = 0;
		in.setRange(0, in_chs.size(), true);
		out.setRange(0, out_chs.size(), true);
		juce::String err;

		/* Open Up Particular Device */
		if (!_device->isOpen()) {
			err = _device->open(in, out, _device->getCurrentSampleRate(),
					_device->getCurrentBufferSizeSamples());
			if (!err.toStdString().empty()) {
				blog(LOG_WARNING, "%s", err.toStdString().c_str());
				AudioCB *cb = _listener->getCallback();

				_listener->setCurrentCallback(callback);
				_listener->disconnect();

				if (cb)
					cb->remove_client(_listener);
				return;
			}
		}

		AudioCB *cb = _listener->getCallback();
		_listener->setCurrentCallback(callback);

		if (_device->isOpen() && !_device->isPlaying() && callback)
			_device->start(callback);

		if (callback) {
			if (cb != callback) {
				_listener->disconnect();
				if (cb)
					cb->remove_client(_listener);
			}

			int                recorded_channels = get_audio_channels(layout);
			int                max_channels      = get_max_obs_channels();
			std::vector<short> r;
			r.reserve(max_channels);

			for (int i = 0; i < recorded_channels; i++) {
				std::string route_str = "route " + std::to_string(i);
				r.push_back(obs_data_get_int(settings, route_str.c_str()));
			}
			for (int i = recorded_channels; i < max_channels; i++) {
				r.push_back(-1);
			}

			_listener->setRoute(r);

			obs_source_audio out;
			out.speakers = layout;
			_listener->setOutput(out);

			_listener->setCallback(callback);
			if (cb != callback) {
				_listener->reconnect();
				callback->add_client(_listener);
			}
		} else {
			_listener->disconnect();
			if (cb)
				cb->remove_client(_listener);
		}
	}

	static void Update(void *vptr, obs_data_t *settings, bool isAsio)
	{
		ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
		if (plugin)
			plugin->update(settings, isAsio);
	}
	static void UpdateAsio(void *vptr, obs_data_t *settings)
	{
		Update(vptr, settings, true);
	}
	static void UpdateWasapi(void *vptr, obs_data_t *settings)
	{
		Update(vptr, settings, false);
	}
	static void Defaults(obs_data_t *settings)
	{
		struct obs_audio_info aoi;
		obs_get_audio_info(&aoi);
		int recorded_channels = get_audio_channels(aoi.speakers);
		int max_channels      = get_max_obs_channels();
		// default is muted channels
		for (int i = 0; i < recorded_channels; i++) {
			std::string name = "route " + std::to_string(i);
			obs_data_set_default_int(settings, name.c_str(), -1);
		}
		for (int i = recorded_channels; i < max_channels; i++) {
			std::string name = "route " + std::to_string(i);
			obs_data_set_default_int(settings, name.c_str(), -1);
		}

		obs_data_set_default_int(settings, "speaker_layout", aoi.speakers);
	}

	static const char *NameAsio(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("ASIO Input Capture");
	}

	static const char *NameWasapi(void *unused)
	{
		UNUSED_PARAMETER(unused);
		return obs_module_text("Wasapi Input Capture (low latency)");
	}
};

static bool show_panel(obs_properties_t *props, obs_property_t *property, void *data)
{
	UNUSED_PARAMETER(props);
	UNUSED_PARAMETER(property);

	if (!data)
		return false;
	ASIOPlugin *   plugin = static_cast<ASIOPlugin *>(data);
	AudioIODevice *device = plugin->getDevice();
	if (device && device->hasControlPanel())
		device->showControlPanel();
	return false;
}

static bool fill_out_channels_modified(void *priv, obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
{
	UNUSED_PARAMETER(props);
	ASIOPlugin *             plugin = static_cast<ASIOPlugin *>(priv);
	juce::AudioIODeviceType *mydeviceType;
	mydeviceType                       = plugin->isAsio() ? deviceTypeAsio : deviceTypeWasapi;
	std::vector<AudioCB *> myCallbacks = plugin->isAsio() ? callbacksAsio : callbacksWasapi;
	std::string            name        = obs_data_get_string(settings, "device_id");
	AudioCB *              _callback   = nullptr;
	AudioIODevice *        _device     = nullptr;

	for (int i = 0; i < myCallbacks.size(); i++) {
		AudioCB *      cb     = myCallbacks[i];
		AudioIODevice *device = cb->getDevice();
		std::string    n      = cb->getName();
		if (n == name) {
			if (!device) {
				String deviceName = name.c_str();
				device            = mydeviceType->createDevice(deviceName, deviceName);
				cb->setDevice(device, name.c_str());
			}
			_device   = device;
			_callback = cb;
			break;
		}
	}

	obs_property_list_clear(list);
	obs_property_list_add_int(list, obs_module_text("Mute"), -1);

	if (!_callback || !_device)
		return true;

	juce::StringArray in_names       = _device->getInputChannelNames();
	int               input_channels = in_names.size();

	int i = 0;

	for (; i < input_channels; i++)
		obs_property_list_add_int(list, in_names[i].toStdString().c_str(), i);

	return true;
}

static bool asio_device_changed(void *vptr, obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
{
	size_t                        i;
	const char *                  curDeviceId  = obs_data_get_string(settings, "device_id");
	int                           max_channels = get_max_obs_channels();
	std::vector<obs_property_t *> route(max_channels, nullptr);
	speaker_layout                layout = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
	obs_property_t *              panel  = obs_properties_get(props, "ctrl");

	int recorded_channels = get_audio_channels(layout);

	size_t itemCount = obs_property_list_item_count(list);
	bool   itemFound = false;

	for (i = 0; i < itemCount; i++) {
		const char *DeviceId = obs_property_list_item_string(list, i);
		if (strcmp(DeviceId, curDeviceId) == 0) {
			itemFound = true;
			break;
		}
	}

	if (!itemFound) {
		obs_property_list_insert_string(list, 0, " ", curDeviceId);
		obs_property_list_item_disable(list, 0, true);
	} else {
		for (i = 0; i < max_channels; i++) {
			std::string name = "route " + std::to_string(i);
			route[i]         = obs_properties_get(props, name.c_str());
			obs_property_list_clear(route[i]);
			obs_property_set_modified_callback2(route[i], fill_out_channels_modified, vptr);
			obs_property_set_visible(route[i], i < recorded_channels);
		}
	}

	ASIOPlugin *plugin = static_cast<ASIOPlugin *>(vptr);
	if (plugin) {
		juce::AudioIODevice *device = plugin->getDevice();
		obs_property_set_visible(panel, device && device->hasControlPanel());
	}

	return true;
}

static bool asio_layout_changed(void *vptr, obs_properties_t *props, obs_property_t *list, obs_data_t *settings)
{
	UNUSED_PARAMETER(list);
	int                           max_channels = get_max_obs_channels();
	std::vector<obs_property_t *> route(max_channels, nullptr);
	speaker_layout                layout            = (speaker_layout)obs_data_get_int(settings, "speaker_layout");
	int                           recorded_channels = get_audio_channels(layout);
	int                           i                 = 0;
	for (i = 0; i < max_channels; i++) {
		std::string name = "route " + std::to_string(i);
		route[i]         = obs_properties_get(props, name.c_str());
		obs_property_list_clear(route[i]);
		obs_property_set_modified_callback2(route[i], fill_out_channels_modified, vptr);
		obs_property_set_visible(route[i], i < recorded_channels);
	}
	return true;
}

static void fill_out_devices(obs_property_t *prop, bool isAsio)
{
	juce::AudioIODeviceType *mydeviceType;
	mydeviceType                       = isAsio ? deviceTypeAsio : deviceTypeWasapi;
	std::vector<AudioCB *> myCallbacks = isAsio ? callbacksAsio : callbacksWasapi;
	StringArray            deviceNames(mydeviceType->getDeviceNames(!isAsio));
	for (int j = 0; j < deviceNames.size(); j++) {
		bool found = false;
		for (int i = 0; i < myCallbacks.size(); i++) {
			AudioCB *   cb = myCallbacks[i];
			std::string n  = cb->getName();
			if (deviceNames[j].toStdString() == n) {
				found = true;
				break;
			}
		}
		if (!found) {
			char *   name = bstrdup(deviceNames[j].toStdString().c_str());
			AudioCB *cb   = new AudioCB(nullptr, name);
			bfree(name);
			myCallbacks.push_back(cb);
		}
	}

	obs_property_list_clear(prop);

	for (int i = 0; i < myCallbacks.size(); i++) {
		AudioCB *   cb = myCallbacks[i];
		const char *n  = cb->getName();
		obs_property_list_add_string(prop, n, n);
	}
}

bool obs_module_load(void)
{
	obs_audio_info aoi;
	obs_get_audio_info(&aoi);

	MessageManager::getInstance();
	global_thread = new TimeSliceThread("global");
	deviceTypeAsio->scanForDevices();
	StringArray deviceNames(deviceTypeAsio->getDeviceNames(false));
	for (int j = 0; j < deviceNames.size(); j++) {
		char *name = bstrdup(deviceNames[j].toStdString().c_str());

		AudioCB *cb = new AudioCB(nullptr, name);
		bfree(name);
		callbacksAsio.push_back(cb);
	}
	deviceTypeWasapi->scanForDevices();
	StringArray deviceNames2(deviceTypeWasapi->getDeviceNames(true));
	for (int j = 0; j < deviceNames2.size(); j++) {
		char *name = bstrdup(deviceNames2[j].toStdString().c_str());

		AudioCB *cb = new AudioCB(nullptr, name);
		bfree(name);
		callbacksWasapi.push_back(cb);
	}

	struct obs_source_info asio_input_capture = {};
	asio_input_capture.id                     = "asio_input_capture";
	asio_input_capture.type                   = OBS_SOURCE_TYPE_INPUT;
	asio_input_capture.output_flags           = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	asio_input_capture.create                 = ASIOPlugin::CreateAsio;
	asio_input_capture.destroy                = ASIOPlugin::Destroy;
	asio_input_capture.update                 = ASIOPlugin::UpdateAsio;
	asio_input_capture.get_defaults           = ASIOPlugin::Defaults;
	asio_input_capture.get_name               = ASIOPlugin::NameAsio;
	asio_input_capture.get_properties         = ASIOPlugin::PropertiesAsio;
	asio_input_capture.icon_type              = OBS_ICON_TYPE_AUDIO_INPUT;

	obs_register_source(&asio_input_capture);

	struct obs_source_info wasapi_input_capture = {};
	wasapi_input_capture.id                     = "wasapi_input_capture2";
	wasapi_input_capture.type                   = OBS_SOURCE_TYPE_INPUT;
	wasapi_input_capture.output_flags           = OBS_SOURCE_AUDIO | OBS_SOURCE_DO_NOT_DUPLICATE;
	wasapi_input_capture.create                 = ASIOPlugin::CreateWasapi;
	wasapi_input_capture.destroy                = ASIOPlugin::Destroy;
	wasapi_input_capture.update                 = ASIOPlugin::UpdateWasapi;
	wasapi_input_capture.get_defaults           = ASIOPlugin::Defaults;
	wasapi_input_capture.get_name               = ASIOPlugin::NameWasapi;
	wasapi_input_capture.get_properties         = ASIOPlugin::PropertiesWasapi;
	wasapi_input_capture.icon_type              = OBS_ICON_TYPE_AUDIO_INPUT;

	obs_register_source(&wasapi_input_capture);
	return true;
}

void obs_module_unload(void)
{
	for (int i = 0; i < callbacksAsio.size(); i++) {
		AudioCB *      cb     = callbacksAsio[i];
		AudioIODevice *device = cb->getDevice();
		if (device) {
			if (device->isPlaying())
				device->stop();
			if (device->isOpen())
				device->close();
			delete device;
		}
		device = nullptr;
		delete cb;
		callbacksAsio[i] = nullptr;
	}
	callbacksAsio.clear();
	for (int i = 0; i < callbacksWasapi.size(); i++) {
		AudioCB *      cb     = callbacksWasapi[i];
		AudioIODevice *device = cb->getDevice();
		if (device) {
			if (device->isPlaying())
				device->stop();
			if (device->isOpen())
				device->close();
			delete device;
		}
		device = nullptr;
		delete cb;
		callbacksWasapi[i] = nullptr;
	}
	callbacksWasapi.clear();

	delete deviceTypeAsio;
	delete deviceTypeWasapi;
	DeletedAtShutdown::deleteAll();
	MessageManager::deleteInstance();
	if (!global_thread->stopThread(200))
		blog(LOG_ERROR, "win-asio: Thread had to be force-stopped");
	delete global_thread;
}
