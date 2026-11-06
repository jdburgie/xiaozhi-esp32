// round-box-clock: see clock_sd.h.
#include "clock_sd.h"

#include <dirent.h>
#include <driver/gpio.h>
#include <esp_heap_caps.h>
#include <driver/sdmmc_host.h>
#include <esp_log.h>
#include <esp_vfs_fat.h>
#include <sdmmc_cmd.h>
#include <sys/stat.h>

#include <cJSON.h>

#define TAG "ClockSD"

namespace clock_sd {
namespace {

constexpr gpio_num_t kClk = GPIO_NUM_17;
constexpr gpio_num_t kCmd = GPIO_NUM_18;
constexpr gpio_num_t kD0 = GPIO_NUM_21;
constexpr gpio_num_t kD3 = GPIO_NUM_13;  // must read high at init, or the card enters SPI mode

sdmmc_card_t* card_ = nullptr;
std::string error_;
std::string summary_json_;
std::string probe_json_ = "{}";  // raw card + filesystem identification, even if mounting fails

std::string Oem(const uint8_t* s) {  // 8-char boot-sector OEM / FS label, trimmed
    std::string r(reinterpret_cast<const char*>(s), 8);
    while (!r.empty() && (r.back() == ' ' || r.back() == '\0')) r.pop_back();
    for (char& c : r) if (c < 32 || c > 126) c = '?';
    return r;
}

// Identify the card and its filesystem without mounting: init the card, read the MBR
// and the first partition's boot sector. Read-only. Deinits the host afterwards.
void Probe(const sdmmc_host_t& host, const sdmmc_slot_config_t& slot) {
    cJSON* p = cJSON_CreateObject();
    sdmmc_card_t card = {};
    uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(512, MALLOC_CAP_DMA));
    esp_err_t err = host.init();
    if (err == ESP_OK) err = sdmmc_host_init_slot(host.slot, &slot);
    if (err == ESP_OK) err = sdmmc_card_init(&host, &card);
    if (err != ESP_OK) {
        cJSON_AddStringToObject(p, "card_init", esp_err_to_name(err));
    } else {
        cJSON_AddStringToObject(p, "card_init", "ok");
        cJSON_AddStringToObject(p, "name", card.cid.name);
        cJSON_AddNumberToObject(p, "size_mb", static_cast<double>(card.csd.capacity) * card.csd.sector_size / 1048576.0);
        if (buf && sdmmc_read_sectors(&card, buf, 0, 1) == ESP_OK && buf[510] == 0x55 && buf[511] == 0xAA) {
            const std::string oem0 = Oem(buf + 3);
            if (oem0 == "EXFAT" || oem0 == "NTFS" || buf[0] == 0xEB || buf[0] == 0xE9) {
                if (oem0 == "EXFAT" || oem0 == "NTFS") {
                    cJSON_AddStringToObject(p, "layout", "no partition table");
                    cJSON_AddStringToObject(p, "fs", oem0.c_str());
                }
            }
            if (!cJSON_GetObjectItem(p, "fs")) {
                const uint8_t type = buf[0x1C2];
                const uint32_t lba = buf[0x1C6] | (buf[0x1C7] << 8) | (buf[0x1C8] << 16) | (static_cast<uint32_t>(buf[0x1C9]) << 24);
                char t[8];
                snprintf(t, sizeof(t), "0x%02X", type);
                cJSON_AddStringToObject(p, "layout", "MBR");
                cJSON_AddStringToObject(p, "part1_type", t);  // 0x0B/0x0C FAT32, 0x07 exFAT/NTFS, 0xEE GPT
                cJSON_AddNumberToObject(p, "part1_lba", lba);
                if (type != 0 && type != 0xEE && sdmmc_read_sectors(&card, buf, lba, 1) == ESP_OK) {
                    const std::string oem = Oem(buf + 3);
                    if (oem == "EXFAT" || oem == "NTFS") {
                        cJSON_AddStringToObject(p, "fs", oem.c_str());
                    } else if (Oem(buf + 82) == "FAT32") {
                        cJSON_AddStringToObject(p, "fs", "FAT32");
                    } else if (Oem(buf + 54).rfind("FAT", 0) == 0) {
                        cJSON_AddStringToObject(p, "fs", Oem(buf + 54).c_str());
                    } else {
                        cJSON_AddStringToObject(p, "fs", ("unknown, OEM '" + oem + "'").c_str());
                    }
                } else if (type == 0xEE) {
                    cJSON_AddStringToObject(p, "fs", "GPT disk (not supported; needs MBR)");
                }
            }
        } else {
            cJSON_AddStringToObject(p, "layout", "sector 0 unreadable or no 55AA signature");
        }
    }
    heap_caps_free(buf);
    host.deinit();
    char* s = cJSON_PrintUnformatted(p);
    probe_json_ = s ? s : "{}";
    cJSON_free(s);
    cJSON_Delete(p);
    ESP_LOGI(TAG, "SD probe: %s", probe_json_.c_str());
}

void BuildSummary() {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "mounted", card_ != nullptr);
    if (cJSON* probe = cJSON_Parse(probe_json_.c_str())) {
        cJSON_AddItemToObject(root, "probe", probe);
    }
    if (!card_) {
        cJSON_AddStringToObject(root, "error", error_.c_str());
    } else {
        cJSON_AddStringToObject(root, "name", card_->cid.name);
        const double mb = static_cast<double>(card_->csd.capacity) * card_->csd.sector_size / (1024.0 * 1024.0);
        cJSON_AddNumberToObject(root, "size_mb", static_cast<int>(mb + 0.5));
        cJSON_AddStringToObject(root, "type", (card_->ocr & (1u << 30)) ? "SDHC/SDXC" : "SDSC");
        cJSON_AddNumberToObject(root, "freq_khz", card_->real_freq_khz);

        uint64_t total = 0, free_bytes = 0;
        if (esp_vfs_fat_info(kMountPoint, &total, &free_bytes) == ESP_OK) {
            cJSON_AddNumberToObject(root, "fs_total_mb", static_cast<double>(total / (1024 * 1024)));
            cJSON_AddNumberToObject(root, "fs_free_mb", static_cast<double>(free_bytes / (1024 * 1024)));
        }

        cJSON* entries = cJSON_AddArrayToObject(root, "root");
        if (DIR* dir = opendir(kMountPoint)) {
            int n = 0;
            while (dirent* e = readdir(dir)) {
                if (++n > 25) break;
                cJSON* item = cJSON_CreateObject();
                cJSON_AddStringToObject(item, "name", e->d_name);
                cJSON_AddBoolToObject(item, "dir", e->d_type == DT_DIR);
                if (e->d_type != DT_DIR) {
                    std::string path = std::string(kMountPoint) + "/" + e->d_name;
                    struct stat st;
                    if (stat(path.c_str(), &st) == 0) {
                        cJSON_AddNumberToObject(item, "bytes", static_cast<double>(st.st_size));
                    }
                }
                cJSON_AddItemToArray(entries, item);
            }
            closedir(dir);
        }
    }
    char* s = cJSON_PrintUnformatted(root);
    summary_json_ = s ? s : "{}";
    cJSON_free(s);
    cJSON_Delete(root);
}

}  // namespace

bool Mount() {
    if (card_) return true;

    // Hold D3 high through card init (1-bit mode doesn't drive it).
    gpio_config_t d3 = {};
    d3.pin_bit_mask = 1ULL << kD3;
    d3.mode = GPIO_MODE_OUTPUT;
    gpio_config(&d3);
    gpio_set_level(kD3, 1);

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.max_freq_khz = SDMMC_FREQ_DEFAULT;  // 20 MHz; long internal traces, be conservative

    sdmmc_slot_config_t slot = SDMMC_SLOT_CONFIG_DEFAULT();
    slot.width = 1;
    slot.clk = kClk;
    slot.cmd = kCmd;
    slot.d0 = kD0;
    slot.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    Probe(host, slot);

    esp_vfs_fat_sdmmc_mount_config_t cfg = {};
    cfg.format_if_mount_failed = false;  // never touch the user's card
    cfg.max_files = 4;
    cfg.allocation_unit_size = 16 * 1024;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(kMountPoint, &host, &slot, &cfg, &card_);
    if (err != ESP_OK) {
        card_ = nullptr;
        error_ = esp_err_to_name(err);
        ESP_LOGW(TAG, "SD mount failed: %s", error_.c_str());
        BuildSummary();
        return false;
    }
    sdmmc_card_print_info(stdout, card_);
    BuildSummary();
    ESP_LOGI(TAG, "SD mounted at %s: %s", kMountPoint, summary_json_.c_str());
    return true;
}

bool IsMounted() { return card_ != nullptr; }

std::string StatusJson() {
    if (summary_json_.empty()) {
        return "{\"mounted\":false,\"error\":\"not attempted\"}";
    }
    return summary_json_;
}

}  // namespace clock_sd
