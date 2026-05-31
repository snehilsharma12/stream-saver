#include "stream_saver_filter.h"

#include "image_encoder.h"

#include <obs-frontend-api.h>
#include <graphics/graphics.h>

#include <algorithm>
#include <array>
#include <cstdio>
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
constexpr const char *SETTING_OCR_MAX_WIDTH = "ocr_max_width";
constexpr const char *SETTING_DEBUG_OVERLAY = "debug_overlay";
constexpr const char *SETTING_WORKER_PATH = "worker_path";
constexpr const char *SETTING_WORKER_PORT = "worker_port";
constexpr uint64_t OCR_STARTUP_COOLDOWN_FRAMES = 300;
constexpr uint64_t OCR_ERROR_COOLDOWN_FRAMES = 900;

const char *filter_name(void *)
{
	return obs_module_text("StreamSaver");
}

void set_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, SETTING_OCR_MODE, static_cast<long long>(OcrMode::Interval));
	obs_data_set_default_int(settings, SETTING_FRAME_INTERVAL, 60);
	obs_data_set_default_double(settings, SETTING_CONFIDENCE, 0.75);
	obs_data_set_default_double(settings, SETTING_BLUR_STRENGTH, 8.0);
	obs_data_set_default_int(settings, SETTING_BOX_PADDING, 8);
	obs_data_set_default_int(settings, SETTING_OCR_MAX_WIDTH, 960);
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
	obs_properties_add_int_slider(props, SETTING_OCR_MAX_WIDTH,
				      obs_module_text("StreamSaver.OcrMaxWidth"), 320, 1920, 16);
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
	if (!filter->matcher.has_phrases()) {
		std::lock_guard<std::mutex> lock(filter->regions_mutex);
		filter->regions.clear();
	}
	filter->ocr_mode = static_cast<OcrMode>(obs_data_get_int(settings, SETTING_OCR_MODE));
	filter->frame_interval =
		static_cast<uint32_t>(std::max<int64_t>(1, obs_data_get_int(settings, SETTING_FRAME_INTERVAL)));
	filter->confidence_threshold = static_cast<float>(obs_data_get_double(settings, SETTING_CONFIDENCE));
	filter->blur_strength = static_cast<float>(obs_data_get_double(settings, SETTING_BLUR_STRENGTH));
	filter->box_padding = static_cast<int>(obs_data_get_int(settings, SETTING_BOX_PADDING));
	filter->ocr_max_width = static_cast<uint32_t>(
		std::min<int64_t>(960, std::max<int64_t>(320, obs_data_get_int(settings, SETTING_OCR_MAX_WIDTH))));
	filter->debug_overlay = obs_data_get_bool(settings, SETTING_DEBUG_OVERLAY);
	filter->worker_path = obs_data_get_string(settings, SETTING_WORKER_PATH);
	filter->worker_port = static_cast<uint16_t>(obs_data_get_int(settings, SETTING_WORKER_PORT));
	filter->ocr_client.configure("127.0.0.1", filter->worker_port);

	if (!filter->matcher.has_phrases()) {
		filter->worker_process.stop();
		return;
	}
}

bool output_active()
{
	return obs_frontend_recording_active() || obs_frontend_streaming_active();
}

std::string resolve_worker_path(StreamSaverFilter *filter)
{
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

	return worker_path;
}

bool ensure_worker_started(StreamSaverFilter *filter)
{
	if (filter->worker_process.running())
		return true;

	const std::string worker_path = resolve_worker_path(filter);
	if (worker_path.empty())
		return false;

	const bool started = filter->worker_process.start(worker_path, filter->worker_port);
	if (started)
		filter->next_ocr_frame.store(filter->frame_index.load() + OCR_STARTUP_COOLDOWN_FRAMES);
	return started;
}

void restart_worker_if_needed(StreamSaverFilter *filter)
{
	if (!filter->matcher.has_phrases() || !output_active())
		filter->worker_process.stop();
}

void *create_filter(obs_data_t *settings, obs_source_t *source)
{
	auto *filter = new StreamSaverFilter();
	filter->source = source;

	char *effect_path = obs_module_file("effects/redact_blur.effect");
	if (effect_path) {
		obs_enter_graphics();
		char *effect_error = nullptr;
		filter->effect = gs_effect_create_from_file(effect_path, &effect_error);
	filter->capture_texrender = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	obs_leave_graphics();

		if (filter->effect)
			blog(LOG_INFO, "[stream-saver] loaded effect: %s", effect_path);
		else if (effect_error)
			blog(LOG_WARNING, "[stream-saver] failed to load effect %s: %s", effect_path,
			     effect_error);
		if (effect_error)
			bfree(effect_error);
		bfree(effect_path);
	}

	if (!filter->effect)
		blog(LOG_WARNING, "[stream-saver] failed to load redact_blur.effect");

	update_settings(filter, settings);
	restart_worker_if_needed(filter);
	return filter;
}

void destroy_filter(void *data)
{
	auto *filter = static_cast<StreamSaverFilter *>(data);
	if (!filter)
		return;

	filter->worker_process.stop();

	obs_enter_graphics();
	if (filter->effect)
		gs_effect_destroy(filter->effect);
	if (filter->capture_stage)
		gs_stagesurface_destroy(filter->capture_stage);
	if (filter->capture_texrender)
		gs_texrender_destroy(filter->capture_texrender);
	obs_leave_graphics();

	delete filter;
}

void update_filter(void *data, obs_data_t *settings)
{
	auto *filter = static_cast<StreamSaverFilter *>(data);
	update_settings(filter, settings);
	restart_worker_if_needed(filter);
}

bool should_submit_frame(StreamSaverFilter *filter, uint64_t frame_index)
{
	if (!filter->matcher.has_phrases())
		return false;

	if (!output_active())
		return false;

	if (frame_index < filter->next_ocr_frame.load())
		return false;

	if (filter->ocr_client.busy())
		return false;

	if (filter->ocr_mode == OcrMode::EveryFrame)
		return true;

	return frame_index % filter->frame_interval == 0;
}

bool render_capture_texture(StreamSaverFilter *filter, uint32_t width, uint32_t height)
{
	obs_source_t *target = obs_filter_get_target(filter->source);
	obs_source_t *parent = obs_filter_get_parent(filter->source);
	if (!target || !parent || !filter->capture_texrender)
		return false;

	const uint32_t parent_flags = obs_source_get_output_flags(parent);
	const bool custom_draw = (parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
	const bool async = (parent_flags & OBS_SOURCE_ASYNC) != 0;

	if (!gs_texrender_begin(filter->capture_texrender, width, height))
		return false;

	gs_blend_state_push();
	gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);

	struct vec4 clear_color;
	vec4_zero(&clear_color);
	gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
	gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);

	if (target == parent && !custom_draw && !async)
		obs_source_default_render(target);
	else
		obs_source_video_render(target);

	gs_blend_state_pop();
	gs_texrender_end(filter->capture_texrender);
	gs_texrender_reset(filter->capture_texrender);
	return gs_texrender_get_texture(filter->capture_texrender) != nullptr;
}

bool capture_png_base64(StreamSaverFilter *filter, uint32_t width, uint32_t height, std::string &png_base64)
{
	if (!render_capture_texture(filter, width, height))
		return false;

	gs_texture_t *texture = gs_texrender_get_texture(filter->capture_texrender);
	if (!texture)
		return false;

	if (!filter->capture_stage || filter->capture_width != width || filter->capture_height != height) {
		if (filter->capture_stage)
			gs_stagesurface_destroy(filter->capture_stage);
		filter->capture_stage = gs_stagesurface_create(width, height, GS_RGBA);
		filter->capture_width = width;
		filter->capture_height = height;
	}

	if (!filter->capture_stage)
		return false;

	gs_stage_texture(filter->capture_stage, texture);

	uint8_t *data = nullptr;
	uint32_t linesize = 0;
	if (!gs_stagesurface_map(filter->capture_stage, &data, &linesize))
		return false;

	png_base64 = rgba_to_png_base64(data, width, height, linesize);
	gs_stagesurface_unmap(filter->capture_stage);

	return !png_base64.empty();
}

void submit_frame_for_ocr(StreamSaverFilter *filter, uint64_t frame_index)
{
	if (!filter->matcher.has_phrases())
		return;

	if (!output_active()) {
		filter->worker_process.stop();
		return;
	}

	if (!ensure_worker_started(filter)) {
		if (filter->debug_overlay)
			blog(LOG_INFO, "[stream-saver] OCR frame %llu skipped: OCR worker not available",
			     static_cast<unsigned long long>(frame_index));
		filter->next_ocr_frame.store(frame_index + OCR_ERROR_COOLDOWN_FRAMES);
		return;
	}

	const uint32_t width = obs_source_get_base_width(obs_filter_get_target(filter->source));
	const uint32_t height = obs_source_get_base_height(obs_filter_get_target(filter->source));
	if (width == 0 || height == 0) {
		if (filter->debug_overlay)
			blog(LOG_INFO, "[stream-saver] OCR frame %llu skipped: source has zero size",
			     static_cast<unsigned long long>(frame_index));
		return;
	}

	uint32_t ocr_width = width;
	uint32_t ocr_height = height;
	if (ocr_width > filter->ocr_max_width) {
		ocr_width = filter->ocr_max_width;
		ocr_height = std::max<uint32_t>(1, static_cast<uint32_t>(
						       (static_cast<uint64_t>(height) * ocr_width) / width));
	}

	std::string png_base64;
	if (!capture_png_base64(filter, ocr_width, ocr_height, png_base64)) {
		if (filter->debug_overlay)
			blog(LOG_INFO,
			     "[stream-saver] OCR frame %llu skipped: failed to capture %ux%u frame",
			     static_cast<unsigned long long>(frame_index), ocr_width, ocr_height);
		return;
	}

	OcrFrame frame;
	frame.frame_id = frame_index;
	frame.width = static_cast<int>(ocr_width);
	frame.height = static_cast<int>(ocr_height);
	frame.png_base64 = std::move(png_base64);

	if (filter->debug_overlay)
		blog(LOG_INFO, "[stream-saver] OCR frame %llu submitted: %ux%u, %zu base64 bytes",
		     static_cast<unsigned long long>(frame_index), ocr_width, ocr_height,
		     frame.png_base64.size());

	const bool submitted = filter->ocr_client.submit(frame, [filter, ocr_width, ocr_height](OcrResult result) {
		if (!result.error.empty()) {
			blog(filter->debug_overlay ? LOG_INFO : LOG_DEBUG,
			     "[stream-saver] OCR frame %llu error: %s",
			     static_cast<unsigned long long>(result.frame_id), result.error.c_str());
			filter->worker_process.stop();
			filter->next_ocr_frame.store(result.frame_id + OCR_ERROR_COOLDOWN_FRAMES);
			return;
		}

		auto regions = filter->matcher.match(result.detections, filter->confidence_threshold,
						     filter->box_padding, static_cast<int>(ocr_width),
						     static_cast<int>(ocr_height));
		if (filter->debug_overlay) {
			blog(LOG_INFO, "[stream-saver] OCR frame %llu: %zu detections, %zu redaction regions",
			     static_cast<unsigned long long>(result.frame_id), result.detections.size(),
			     regions.size());
		}
		std::lock_guard<std::mutex> lock(filter->regions_mutex);
		filter->regions = std::move(regions);
	});

	if (!submitted && filter->debug_overlay)
		blog(LOG_INFO, "[stream-saver] OCR frame %llu skipped: OCR client busy",
		     static_cast<unsigned long long>(frame_index));
}

void set_effect_params(StreamSaverFilter *filter)
{
	if (!filter->effect)
		return;

	std::array<vec4, 1> rects = {};
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

	if (filter->debug_overlay && rect_count == 0) {
		vec4_set(&rects[0], 0.05f, 0.05f, 0.35f, 0.20f);
		rect_count = 1;
	}

	gs_effect_set_vec4(gs_effect_get_param_by_name(filter->effect, "redact_rect0"), &rects[0]);
	gs_effect_set_float(gs_effect_get_param_by_name(filter->effect, "redact_rect_count"),
			    rect_count > 0 ? 1.0f : 0.0f);
	gs_effect_set_float(gs_effect_get_param_by_name(filter->effect, "blur_strength"),
			    filter->blur_strength);
	gs_effect_set_float(gs_effect_get_param_by_name(filter->effect, "debug_overlay"),
			    filter->debug_overlay ? 1.0f : 0.0f);
}

void video_render(void *data, gs_effect_t *)
{
	auto *filter = static_cast<StreamSaverFilter *>(data);
	if (!filter)
		return;

	const uint64_t frame_index = filter->frame_index.fetch_add(1);

	if (!filter->effect) {
		obs_source_skip_video_filter(filter->source);
		return;
	}

	if (!obs_source_process_filter_begin(filter->source, GS_RGBA, OBS_NO_DIRECT_RENDERING))
		return;

	set_effect_params(filter);
	obs_source_process_filter_end(filter->source, filter->effect, 0, 0);

	if (!output_active()) {
		if (filter->worker_process.running())
			filter->worker_process.stop();
		return;
	}

	if (filter->matcher.has_phrases() && frame_index >= filter->next_ocr_frame.load())
		ensure_worker_started(filter);

	if (should_submit_frame(filter, frame_index))
		submit_frame_for_ocr(filter, frame_index);
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
