#include "storage.h"
#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "esp_crc.h"
#include "esp_spiffs.h"

// Deliberately incompatible with the earlier Arduino experiment's slot files.
#define MAGIC 0x49444631U
typedef struct {
    uint32_t magic, version, count;
    rf_settings_t rf;
    uint32_t crc;
} file_header_t;
static bool mounted;

static bool path_for(const char *name, char path[64])
{
    size_t len = strlen(name);
    if (!len || len > 24) return false;
    for (size_t i = 0; i < len; ++i)
        if (!isalnum((unsigned char)name[i]) && name[i] != '_' && name[i] != '-') return false;
    snprintf(path, 64, "/data/%s.rf", name);
    return true;
}

esp_err_t storage_init(void)
{
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/data", .partition_label = "storage", .max_files = 4,
        .format_if_mount_failed = false,
    };
    esp_err_t err = esp_vfs_spiffs_register(&conf);
    mounted = err == ESP_OK;
    return err;
}

static uint32_t checksum(const file_header_t *header, const int32_t *pulses)
{
    uint32_t crc = esp_crc32_le(0, (const uint8_t *)header, offsetof(file_header_t, crc));
    return esp_crc32_le(crc, (const uint8_t *)pulses, header->count * sizeof(*pulses));
}

esp_err_t storage_save(const char *name, const waveform_t *wave)
{
    char path[64];
    if (!mounted) return ESP_ERR_INVALID_STATE;
    if (!path_for(name, path) || !waveform_valid(wave)) return ESP_ERR_INVALID_ARG;
    file_header_t header = {.magic = MAGIC, .version = 1, .count = wave->count, .rf = wave->rf};
    header.crc = checksum(&header, wave->pulses);
    FILE *file = fopen(path, "wb");
    if (!file) return ESP_FAIL;
    bool ok = fwrite(&header, sizeof(header), 1, file) == 1 &&
              fwrite(wave->pulses, sizeof(int32_t), wave->count, file) == wave->count;
    if (fclose(file)) ok = false;
    if (!ok) remove(path);
    return ok ? ESP_OK : ESP_FAIL;
}

esp_err_t storage_load(const char *name, waveform_t *wave)
{
    char path[64];
    wave->count = 0;
    if (!mounted) return ESP_ERR_INVALID_STATE;
    if (!path_for(name, path)) return ESP_ERR_INVALID_ARG;
    FILE *file = fopen(path, "rb");
    if (!file) return ESP_ERR_NOT_FOUND;
    // Failed loads invalidate RAM; no stale waveform can be replayed by mistake.
    file_header_t header = {0};
    struct stat info;
    bool ok = !stat(path, &info) && fread(&header, sizeof(header), 1, file) == 1 &&
              header.magic == MAGIC && header.version == 1 && header.count >= 4 &&
              header.count <= PULSE_CAPACITY && cc1101_settings_valid(&header.rf) &&
              info.st_size == sizeof(header) + header.count * sizeof(int32_t);
    if (ok) ok = fread(wave->pulses, sizeof(int32_t), header.count, file) == header.count &&
                 checksum(&header, wave->pulses) == header.crc;
    fclose(file);
    if (!ok) return ESP_ERR_INVALID_CRC;
    wave->rf = header.rf;
    wave->count = header.count;
    if (!waveform_valid(wave)) { wave->count = 0; return ESP_ERR_INVALID_SIZE; }
    return ESP_OK;
}

void storage_list(void)
{
    if (!mounted) { puts("ERR: filesystem unavailable"); return; }
    DIR *dir = opendir("/data");
    if (!dir) { puts("ERR: cannot open storage"); return; }
    struct dirent *entry;
    while ((entry = readdir(dir))) printf("%s\n", entry->d_name);
    closedir(dir);
}
