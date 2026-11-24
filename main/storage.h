#pragma once
#include "waveform.h"
esp_err_t storage_init(void);
esp_err_t storage_save(const char *name, const waveform_t *wave);
esp_err_t storage_load(const char *name, waveform_t *wave);
void storage_list(void);
