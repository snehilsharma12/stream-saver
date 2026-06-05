#pragma once

#include "matcher.h"
#include "ocr_client.h"
#include "worker_process.h"

#include <obs-module.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

namespace stream_saver {

enum class OcrMode {
	EveryFrame = 0,
	Interval = 1,
};

enum class InferenceBackend {
	OnnxCpu = 0,
	DirectMl = 1,
	Cuda = 2,
	OpenVino = 3,
	Custom = 4,
};

struct StreamSaverFilter {
	obs_source_t *source = nullptr;
	gs_effect_t *effect = nullptr;
	gs_texrender_t *capture_texrender = nullptr;
	gs_stagesurf_t *capture_stage = nullptr;
	uint32_t capture_width = 0;
	uint32_t capture_height = 0;

	PhraseMatcher matcher;
	OcrClient ocr_client;
	WorkerProcess worker_process;

	std::string phrases;
	std::string worker_path;
	std::string inference_backend = "onnxruntime";
	std::string inference_device = "cpu";
	std::string custom_inference_backend;
	std::string yolo_model_path = "yolo11n-text.onnx";
	OcrMode ocr_mode = OcrMode::Interval;
	InferenceBackend inference_backend_mode = InferenceBackend::OnnxCpu;
	int directml_device_id = 0;
	uint32_t frame_interval = 5;
	float confidence_threshold = 0.75f;
	float blur_strength = 8.0f;
	int box_padding = 8;
	uint32_t ocr_max_width = 1280;
	uint32_t yolo_image_size = 640;
	bool debug_overlay = false;
	uint16_t worker_port = 48741;

	std::atomic_uint64_t frame_index{0};
	std::atomic_uint64_t next_ocr_frame{0};
	std::atomic_uint64_t latest_ocr_submission{0};
	std::atomic_bool warmup_submitted{false};
	std::atomic_bool output_was_active{false};
	std::atomic_uint64_t output_generation{0};
	std::mutex regions_mutex;
	std::vector<RedactionRegion> regions;
};

} // namespace stream_saver
