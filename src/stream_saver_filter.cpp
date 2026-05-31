#include "stream_saver_filter.h"

#include <graphics/graphics.h>

#include <algorithm>
#include <array>
#include <memory>
#include <utility>

using namespace stream_saver;

namespace {

constexpr const char *SETTING_PHRASES = "phrases";
constexpr const char *SETTING_OCR_MODE = "ocr_mode";
constexpr const char *SETTING_FRAME_INTERVAL = "frame_interval";
constexpr const char *SETTING_CONFIDENCE = "confidence_threshold";
constexpr const char *SETTING_BLUR_STRENGTH = "blur_strength";
constexpr const char *SETTING_BOX_PADDING = "box_padding";
constexpr const char *SETTING_DEBUG_OVERLAY = "debug_overlay";
constexpr const char *SETTING_WORKER_PATH = "worker_path";
constexpr const char *SETTING_WORKER_PORT = "worker_port";

const char *filter_name(void *)
{
	return obs_module_text("StreamSaver");
}

void set_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_OCR_MODE, static_cast<long long>(OcrMode::Interval));
	obs_data_set_default_int(settings, SETTING_FRAME_INTERVAL, 5);
	obs_data_set_default_double(settings, SETTING_CONFIDENCE, 0.75);
	obs_data_set_default_double(settings, SETTING_BLUR_STRENGTH, 8.0);
	obs_data_set_default_int(settings, SETTING_BOX_PADDING, 8);
	obs_data_set_default_bool(settings, SETTING_DEBUG_OVERLAY, false);
	obs_data_set_default_int(settings, SETTING_WORKER_PORT, 48741);
}

obs_properties_t *get_properties(void *)
{
	obs_properties_t *props = obs_properties_create();

	obs_properties_add_text(props, SETTING_PHRASES, obs_module_text("StreamSaver.Phrases"),
				OBS_TEXT_MULTILINE);

	obs_property_t *mode = obs_properties_add_list(props, SETTING_OCR_MODE,
						       obs_module_text("StreamSaver.OcrMode"),
						       OBS_COMBO_TYPE_LIST,
						       OBS_COMBO_FORMAT_INT);
	obs_property_list_add_int(mode, obs_module_text("StreamSaver.OcrMode.EveryFrame"),
				  static_cast<long long>(OcrMode::EveryFrame));
	obs_property_list_add_int(mode, obs_module_text("StreamSaver.OcrMode.Interval"),
				  static_cast<long long>(OcrMode::Interval));

	obs_properties_add_int_slider(props, SETTING_FRAME_INTERVAL,
				      obs_module_text("StreamSaver.FrameInterval"), 1, 120, 1);
	obs_properties_add_float_slider(props, SETTING_CONFIDENCE,
					obs_module_text("StreamSaver.Confidence"), 0.0, 1.0, 0.01);
	obs_properties_add_float_slider(props, SETTING_BLUR_STRENGTH,
					obs_module_text("StreamSaver.BlurStrength"), 1.0, 40.0, 1.0);
	obs_properties_add_int_slider(props, SETTING_BOX_PADDING,
				      obs_module_text("StreamSaver.BoxPadding"), 0, 80, 1);
	obs_properties_add_bool(props, SETTING_DEBUG_OVERLAY,
				obs_module_text("StreamSaver.DebugOverlay"));
	obs_properties_add_path(props, SETTING_WORKER_PATH,
				obs_module_text("StreamSaver.WorkerPath"), OBS_PATH_FILE, nullptr, nullptr);
	obs_properties_add_int(props, SETTING_WORKER_PORT,
			       obs_module_text("StreamSaver.WorkerPort"), 1024, 65535, 1);

	return props;
}

void update_settings(StreamSaverFilter *filter, obs_data_t *settings)
{
	filter->phrases = obs_data_get_string(settings, SETTING_PHRASES);
	filter->matcher.set_phrases(filter->phrases);
	filter->ocr_mode = static_cast<OcrMode>(obs_data_get_int(settings, SETTING_OCR_MODE));
	filter->frame_interval =
		static_cast<uint32_t>(std::max<int64_t>(1, obs_data_get_int(settings, SETTING_FRAME_INTERVAL)));
	filter->confidence_threshold = static_cast<float>(obs_data_get_double(settings, SETTING_CONFIDENCE));
	filter->blur_strength = static_cast<float>(obs_data_get_double(settings, SETTING_BLUR_STRENGTH));
	filter->box_padding = static_cast<int>(obs_data_get_int(settings, SETTING_BOX_PADDING));
	filter->debug_overlay = obs_data_get_bool(settings, SETTING_DEBUG_OVERLAY);
	filter->worker_path = obs_data_get_string(settings, SETTING_WORKER_PATH);
	filter->worker_port = static_cast<uint16_t>(obs_data_get_int(settings, SETTING_WORKER_PORT));
	filter->ocr_client.configure("127.0.0.1", filter->worker_port);

	if (!filter->worker_process.running()) {
		std::string worker_path = filter->worker_path;
		if (worker_path.empty()) {
#ifdef _WIN32
			char *module_worker_path = obs_module_file("worker/stream-saver-ocr.exe");
			if (!module_worker_path)
				module_worker_path = obs_module_file("worker/stream_saver_ocr.py");
#else
			char *module_worker_path = obs_module_file("worker/stream-saver-ocr");
#endif
			if (module_worker_path) {
				worker_path = module_worker_path;
				bfree(module_worker_path);
			}
		}

		if (!worker_path.empty())
			filter->worker_process.start(worker_path, filter->worker_port);
	}
}

void *create_filter(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new StreamSaverFilter();
	filter->source = source;

	char *effect_path = obs_module_file("effects/redact_blur.effect");
	if (effect_path) {
		filter->effect = gs_effect_create_from_file(effect_path, nullptr);
		bfree(effect_path);
	}

	if (!filter->effect)
		blog(LOG_WARNING, "[stream-saver] failed to load redact_blur.effect");

	update_settings(filter, settings);
	return filter;
}

void destroy_filter(void *data)
{
	auto *filter = static_cast<StreamSaverFilter *>(data);
	if (!filter)
		return;

	if (filter->effect)
		gs_effect_destroy(filter->effect);

	delete filter;
}

void update_filter(void *data, obs_data_t *settings)
{
	update_settings(static_cast<StreamSaverFilter *>(data), settings);
}

bool should_submit_frame(StreamSaverFilter *filter, uint64_t frame_index)
{
	if (filter->ocr_client.busy())
		return false;

	if (filter->ocr_mode == OcrMode::EveryFrame)
		return true;

	return frame_index % filter->frame_interval == 0;
}

void submit_placeholder_frame(StreamSaverFilter *filter, uint64_t frame_index)
{
	const uint32_t width = obs_source_get_base_width(obs_filter_get_target(filter->source));
	const uint32_t height = obs_source_get_base_height(obs_filter_get_target(filter->source));
	if (width == 0 || height == 0)
		return;

	/*
	 * OBS GPU readback and PNG encoding are intentionally isolated behind this
	 * submission point. The current scaffold wires scheduling, IPC, matching,
	 * and rendering; production capture should fill png_base64 with a staged
	 * source frame without blocking video_render.
	 */
	OcrFrame frame;
	frame.frame_id = frame_index;
	frame.width = static_cast<int>(width);
	frame.height = static_cast<int>(height);

	filter->ocr_client.submit(frame, [filter, width, height](OcrResult result) {
		if (!result.error.empty()) {
			blog(LOG_DEBUG, "[stream-saver] OCR error: %s", result.error.c_str());
			return;
		}

		auto regions = filter->matcher.match(result.detections, filter->confidence_threshold,
						     filter->box_padding, static_cast<int>(width),
						     static_cast<int>(height));
		std::lock_guard<std::mutex> lock(filter->regions_mutex);
		filter->regions = std::move(regions);
	});
}

void set_effect_params(StreamSaverFilter *filter)
{
	if (!filter->effect)
		return;

	std::array<vec4, 64> rects = {};
	int rect_count = 0;
	{
		std::lock_guard<std::mutex> lock(filter->regions_mutex);
		rect_count = static_cast<int>(std::min<size_t>(filter->regions.size(), rects.size()));
		for (int i = 0; i < rect_count; ++i) {
			const auto &region = filter->regions[static_cast<size_t>(i)];
			vec4_set(&rects[static_cast<size_t>(i)], region.left, region.top,
				 region.right, region.bottom);
		}
	}

	gs_effect_set_val(gs_effect_get_param_by_name(filter->effect, "redact_rects"),
			  rects.data(), sizeof(vec4) * static_cast<size_t>(rect_count));
	gs_effect_set_int(gs_effect_get_param_by_name(filter->effect, "redact_rect_count"), rect_count);
	gs_effect_set_float(gs_effect_get_param_by_name(filter->effect, "blur_strength"),
			    filter->blur_strength);
	gs_effect_set_bool(gs_effect_get_param_by_name(filter->effect, "debug_overlay"),
			   filter->debug_overlay);
}

void video_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<StreamSaverFilter *>(data);
	if (!filter)
		return;

	const uint64_t frame_index = filter->frame_index.fetch_add(1);
	if (should_submit_frame(filter, frame_index))
		submit_placeholder_frame(filter, frame_index);

	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING))
		return;

	set_effect_params(filter);
	obs_source_process_filter_end(filter->source, filter->effect, 0, 0);
}

} // namespace

obs_source_info stream_saver_filter_info = [] {
	obs_source_info info = {};
	info.id = "stream_saver_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = filter_name;
	info.create = create_filter;
	info.destroy = destroy_filter;
	info.update = update_filter;
	info.get_defaults = set_defaults;
	info.get_properties = get_properties;
	info.video_render = video_render;
	return info;
}();
