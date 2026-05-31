#include <obs-module.h>

OBS_DECLARE_MODULE()
OBS_MODULE_USE_DEFAULT_LOCALE("stream-saver", "en-US")

extern obs_source_info stream_saver_filter_info;

bool obs_module_load(void)
{
	obs_register_source(&stream_saver_filter_info);
	blog(LOG_INFO, "[stream-saver] plugin loaded");
	return true;
}

void obs_module_unload(void)
{
	blog(LOG_INFO, "[stream-saver] plugin unloaded");
}
