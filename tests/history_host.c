// Native persistence tests: real files + deterministic clock/NVS/RTOS adapters.
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
static int64_t wall, monotonic;
static unsigned char nvram[64];
static size_t nvram_size;
static bool nvs_fail;
static time_t fake_time(time_t *out) { if (out) *out=wall; return wall; }
#define time fake_time
#define HISTORY_BASE_PATH "."
#include "../main/history.c"
#undef time
const char *esp_err_to_name(esp_err_t e) { return e ? "IO_ERROR" : "ESP_OK"; }
int64_t esp_timer_get_time(void) { return monotonic; }
SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
int xSemaphoreTake(SemaphoreHandle_t s, unsigned t) { return 1; }
int xSemaphoreGive(SemaphoreHandle_t s) { return 1; }
uint32_t esp_crc32_le(uint32_t c, const uint8_t *p, size_t n) {
  c=~c; while(n--) { c^=*p++; for(int i=0;i<8;i++) c=(c>>1) ^ (0xedb88320U & -(c&1)); } return ~c;
}
esp_err_t nvs_open(const char *ns, int mode, nvs_handle_t *h) { *h=1; return ESP_OK; }
esp_err_t nvs_set_blob(nvs_handle_t h,const char *k,const void *p,size_t n) {
  if(nvs_fail) return ESP_FAIL; assert(n<=sizeof(nvram)); memcpy(nvram,p,n); nvram_size=n; return ESP_OK;
}
esp_err_t nvs_get_blob(nvs_handle_t h,const char *k,void *p,size_t *n) {
  if(!nvram_size) return ESP_ERR_NVS_NOT_FOUND; assert(*n>=nvram_size); memcpy(p,nvram,nvram_size); *n=nvram_size; return ESP_OK;
}
esp_err_t nvs_commit(nvs_handle_t h) { return nvs_fail ? ESP_FAIL : ESP_OK; }
void nvs_close(nvs_handle_t h) {}
static char output[32768]; static size_t used;
static esp_err_t collect(void *ctx,const char *p,size_t n) { assert(used+n<sizeof(output));memcpy(output+used,p,n); used+=n;output[used]=0;return ESP_OK; }
static void reset_runtime(void) {
  active=(record_t){0}; lock=NULL; ready=synced=false; last_error=0;last_wall=last_mono=saved_mono=0;pruned_day=-1;monotonic=1000000;
}
static void advance_seconds(int n) { wall+=n;monotonic+=(int64_t)n*1000000; }
static int64_t midnight=1793548800; // 2026-11-02 Beijing midnight
static void setup(void) {
  char path[]="/tmp/light-history-test-XXXXXX"; assert(mkdtemp(path));assert(chdir(path)==0);
  nvram_size=0;nvs_fail=false;reset_runtime();wall=midnight;assert(history_init(true)==ESP_OK);
}
static void dump(void) { used=0;assert(history_write_json(midnight,midnight+2*DAY,collect,NULL)==ESP_OK); }
int main(void) {
  setup();
  // Controls before NTP are ignored, then start at sync time, not retrospectively.
  history_change(true,HISTORY_CALIBRATION); assert(!active.magic);
  advance_seconds(100); history_clock_synced(); history_tick(true,true); assert(active.start==wall);
  advance_seconds(60);history_tick(true,true); int64_t persisted=active.end;
  advance_seconds(25);history_tick(true,true); assert(active.end==wall);
  reset_runtime();wall+=3600;assert(history_init(true)==ESP_OK);assert(!active.magic);
  history_clock_synced();history_tick(false,false);dump();
  char needle[64];snprintf(needle,sizeof(needle),",%lld,1,3]",(long long)persisted);assert(strstr(output,needle));
  history_change(false,HISTORY_CALIBRATION);assert(active.start==wall && !active.on);
  advance_seconds(120);history_change(true,HISTORY_WEB);assert(active.on);dump();
  assert(strstr(output,",0,2]"));

  setup();history_clock_synced();wall=midnight+DAY-60;history_change(true,HISTORY_CALIBRATION);
  advance_seconds(120);history_tick(true,true);assert(active.start==midnight+DAY);assert(active.cause==HISTORY_CONTINUATION);dump();
  snprintf(needle,sizeof(needle),",%lld,1,2]",(long long)(midnight+DAY));assert(strstr(output,needle));
  // A clock jump doesn't turn an hour correction into an hour of usage.
  int64_t before=active.end;wall+=3600;monotonic+=1000000;history_tick(true,true);assert(active.start==wall);dump();
  snprintf(needle,sizeof(needle),",%lld,1,0]",(long long)before);assert(strstr(output,needle));

  setup();history_clock_synced();history_change(true,HISTORY_CALIBRATION);advance_seconds(120);history_tick(true,true);
  record_t recovery;memcpy(&recovery,nvram,sizeof(recovery));assert(append(&recovery)==ESP_OK);assert(append(&recovery)==ESP_OK);
  char path[64];snprintf(path,sizeof(path),"h%lld.bin",(long long)day_of(wall));struct stat st;assert(stat(path,&st)==0 && st.st_size==32);
  FILE *f=fopen(path,"ab");fwrite("torn",1,4,f);fclose(f);
  advance_seconds(60);history_change(false,HISTORY_CALIBRATION);assert(ready);assert(stat(path,&st)==0 && st.st_size==64);
  // Storage failure stops reporting live coverage but does not affect controls.
  nvs_fail=true;advance_seconds(60);history_tick(true,false);assert(!ready);assert(last_error==ESP_FAIL);

  setup();history_clock_synced();
  assert(oldest(midnight)==1762012800); // 2025-11-02 Beijing midnight
  int64_t leap=1835366400; // 2028-02-29 Beijing midnight
  assert(oldest(leap)==1803744000); // 2027-02-28 Beijing midnight
  FILE *old=fopen("h19000.bin","wb");fclose(old);FILE *rf=fopen("power_sample_1.rf","wb");fclose(rf);
  history_tick(false,false);assert(access("h19000.bin",F_OK)!=0);assert(access("power_sample_1.rf",F_OK)==0);
  assert(history_write_json(midnight+1,midnight+DAY,collect,NULL)==ESP_ERR_INVALID_ARG);
  assert(history_write_json(midnight,midnight+32*DAY,collect,NULL)==ESP_ERR_INVALID_ARG);
  puts("history host tests passed");
}
