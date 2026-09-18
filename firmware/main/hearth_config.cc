#include "hearth_config.h"

#include <cstdio>
#include <cstring>

#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_log.h"
#include "esp_system.h"
#include "hearth_stats.h"
#include "hearth_util.h"
#include "nvs.h"
#include "sdkconfig.h"

namespace {

constexpr const char* kTag = "hearth.cfg";
constexpr const char* kNamespace = "hearth";

HearthConfig* g_config = nullptr;

void GetStr(nvs_handle_t handle, const char* key, char* out, size_t cap) {
    size_t length = cap;
    if (nvs_get_str(handle, key, out, &length) != ESP_OK) {
        out[0] = '\0';
    }
}

}  // namespace

esp_err_t HearthConfigLoad(HearthConfig* out) {
    if (out == nullptr) {
        return ESP_ERR_INVALID_ARG;
    }
    HearthCopy(out->ssid, sizeof(out->ssid), CONFIG_HEARTH_WIFI_SSID);
    HearthCopy(out->password, sizeof(out->password), CONFIG_HEARTH_WIFI_PASSWORD);
    HearthCopy(out->hub, sizeof(out->hub), CONFIG_HEARTH_HUB_URL);

    nvs_handle_t handle;
    if (nvs_open(kNamespace, NVS_READONLY, &handle) != ESP_OK) {
        return ESP_OK;
    }
    char scratch[96];
    GetStr(handle, "ssid", scratch, sizeof(scratch));
    if (scratch[0]) {
        HearthCopy(out->ssid, sizeof(out->ssid), scratch);
    }
    GetStr(handle, "pass", scratch, sizeof(scratch));
    if (scratch[0]) {
        HearthCopy(out->password, sizeof(out->password), scratch);
    }
    GetStr(handle, "hub", scratch, sizeof(scratch));
    if (scratch[0]) {
        HearthCopy(out->hub, sizeof(out->hub), scratch);
    }
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t HearthConfigSave(const HearthConfig& config) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(kNamespace, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        return err;
    }
    nvs_set_str(handle, "ssid", config.ssid);
    nvs_set_str(handle, "pass", config.password);
    nvs_set_str(handle, "hub", config.hub);
    err = nvs_commit(handle);
    nvs_close(handle);
    return err;
}

bool HearthConfigComplete(const HearthConfig& config) {
    return config.ssid[0] != '\0' && config.hub[0] != '\0';
}

namespace {

struct {
    struct arg_str* key;
    struct arg_str* value;
    struct arg_end* end;
} g_set_args;

int CmdShow(int, char**) {
    const HearthConfig& c = *g_config;
    printf("ssid  %s\n", c.ssid);
    printf("pass  %s\n", c.password[0] ? "(set)" : "(empty)");
    printf("hub   %s\n", c.hub);
    return 0;
}

int CmdStats(int, char**) {
    const HearthStats& s = g_hearth_stats;
    printf("up      %us (%u wakes, %u refreshes, %u paints skipped)\n",
           static_cast<unsigned>(s.uptime_s), static_cast<unsigned>(s.wakes),
           static_cast<unsigned>(s.refreshes),
           static_cast<unsigned>(s.paints_skipped));
    printf("poster  %u fetches, %u changed and repainted\n",
           static_cast<unsigned>(s.poster_fetches),
           static_cast<unsigned>(s.poster_repaints));
    printf("poll    next fetch in %u ms\n", static_cast<unsigned>(s.poll_ms));
    if (s.battery_valid) {
        printf("battery %u mV %u%%%s\n", static_cast<unsigned>(s.battery_mv),
               s.battery_percent, s.charging ? " charging" : "");
    } else {
        printf("battery (no ADC reading)\n");
    }
    printf("screen  %s\n", s.screen);
    return 0;
}

int CmdSet(int argc, char** argv) {
    const int errors = arg_parse(argc, argv, (void**)&g_set_args);
    if (errors != 0) {
        arg_print_errors(stderr, g_set_args.end, argv[0]);
        return 1;
    }
    const char* key = g_set_args.key->sval[0];
    const char* value = g_set_args.value->count ? g_set_args.value->sval[0] : "";
    HearthConfig& c = *g_config;
    if (strcmp(key, "ssid") == 0) {
        HearthCopy(c.ssid, sizeof(c.ssid), value);
    } else if (strcmp(key, "pass") == 0) {
        HearthCopy(c.password, sizeof(c.password), value);
    } else if (strcmp(key, "hub") == 0) {
        HearthCopy(c.hub, sizeof(c.hub), value);
    } else {
        printf("unknown key '%s' (ssid pass hub)\n", key);
        return 1;
    }
    printf("set %s; run 'hearth-save' then 'hearth-reboot'\n", key);
    return 0;
}

int CmdSave(int, char**) {
    const esp_err_t err = HearthConfigSave(*g_config);
    printf("%s\n", err == ESP_OK ? "saved" : esp_err_to_name(err));
    return err == ESP_OK ? 0 : 1;
}

int CmdReboot(int, char**) {
    printf("rebooting\n");
    fflush(stdout);
    esp_restart();
    return 0;
}

}  // namespace

void HearthConfigRegisterConsole(HearthConfig* config) {
    g_config = config;
    esp_console_repl_t* repl = nullptr;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "hearth>";
    repl_config.max_cmdline_length = 160;
    if (esp_console_new_repl_stdio(&repl_config, &repl) != ESP_OK) {
        ESP_LOGW(kTag, "console unavailable; using stored config only");
        return;
    }
    g_set_args.key = arg_str1(nullptr, nullptr, "<key>", "ssid|pass|hub");
    g_set_args.value = arg_str0(nullptr, nullptr, "<value>", "new value");
    g_set_args.end = arg_end(2);

    const esp_console_cmd_t commands[] = {
        {"hearth-show", "Print Wi-Fi and hub config", nullptr, CmdShow,
         nullptr, nullptr, nullptr},
        {"hearth-stats", "Print wake-ups, refreshes, and battery", nullptr,
         CmdStats, nullptr, nullptr, nullptr},
        {"hearth-set", "Set ssid, pass, or hub", nullptr, CmdSet, &g_set_args,
         nullptr, nullptr},
        {"hearth-save", "Write config to NVS", nullptr, CmdSave, nullptr,
         nullptr, nullptr},
        {"hearth-reboot", "Restart", nullptr, CmdReboot, nullptr, nullptr,
         nullptr},
    };
    for (const auto& command : commands) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&command));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(kTag, "console ready: hearth-show / hearth-stats / hearth-set");
}
