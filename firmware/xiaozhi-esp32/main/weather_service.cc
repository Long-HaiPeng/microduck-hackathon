#include "weather_service.h"

#include "board.h"
#include "http.h"
#include "settings.h"

#include <cJSON.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <cstdio>

#define TAG "WeatherService"

namespace {

// 刷新间隔：成功 30 分钟一次；失败（如刚开机 Wi-Fi 未连上）15 秒重试
constexpr uint32_t kRefreshOkMs = 30 * 60 * 1000;
constexpr uint32_t kRetryFailMs = 15 * 1000;
constexpr int kHttpTimeoutMs = 8000;
// 默认坐标（上海），可用 NVS weather/location 覆盖
constexpr const char* kDefaultLocation = "31.23,121.47";

// WMO 天气代码 -> 描述
const char* DescribeWeatherCode(int code) {
#if CONFIG_LANGUAGE_ZH_CN
    switch (code) {
        case 0: return "晴";
        case 1: return "少云";
        case 2: return "多云";
        case 3: return "阴";
        case 45:
        case 48: return "雾";
        case 51: case 53: case 55: case 56: case 57: return "毛毛雨";
        case 61: case 63: case 65: case 66: case 67: return "雨";
        case 71: case 73: case 75: case 77: return "雪";
        case 80: case 81: case 82: return "阵雨";
        case 85:
        case 86: return "阵雪";
        case 95: case 96: case 99: return "雷阵雨";
        default: return "";
    }
#else
    switch (code) {
        case 0: return "Sunny";
        case 1: return "Mostly Clear";
        case 2: return "Cloudy";
        case 3: return "Overcast";
        case 45:
        case 48: return "Fog";
        case 51: case 53: case 55: case 56: case 57: return "Drizzle";
        case 61: case 63: case 65: case 66: case 67: return "Rain";
        case 71: case 73: case 75: case 77: return "Snow";
        case 80: case 81: case 82: return "Showers";
        case 85:
        case 86: return "Snow Showers";
        case 95: case 96: case 99: return "Thunderstorm";
        default: return "";
    }
#endif
}

}  // namespace

WeatherService& WeatherService::GetInstance() {
    static WeatherService instance;
    return instance;
}

WeatherService::WeatherService() {
    // 先加载 NVS 缓存，开机联网前也能显示最近一次天气
    Settings settings("weather", false);
    summary_ = settings.GetString("summary");
}

void WeatherService::Start() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (started_) {
        return;
    }
    started_ = true;
    xTaskCreate(
        [](void* arg) { static_cast<WeatherService*>(arg)->TaskLoop(); }, "weather_task", 6144,
        this, 2, nullptr);
}

std::string WeatherService::GetSummary() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return summary_;
}

void WeatherService::SetOnUpdated(std::function<void()> callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_updated_ = std::move(callback);
}

void WeatherService::TaskLoop() {
    while (true) {
        bool ok = FetchOnce();
        vTaskDelay(pdMS_TO_TICKS(ok ? kRefreshOkMs : kRetryFailMs));
    }
}

bool WeatherService::FetchOnce() {
    // 坐标拆成纬度/经度两段进 URL；越界或格式不对时回退默认坐标
    Settings settings("weather", false);
    std::string location = settings.GetString("location", kDefaultLocation);
    size_t comma = location.find(',');
    std::string lat = (comma == std::string::npos) ? "" : location.substr(0, comma);
    std::string lon = (comma == std::string::npos) ? "" : location.substr(comma + 1);
    if (lat.empty() || lon.empty() || lat.size() > 16 || lon.size() > 16) {
        lat = "31.23";
        lon = "121.47";
    }

    char url[256];
    snprintf(url, sizeof(url),
             "https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s"
             "&current=temperature_2m,weather_code",
             lat.c_str(), lon.c_str());

    auto network = Board::GetInstance().GetNetwork();
    if (network == nullptr) {
        return false;
    }
    auto http = network->CreateHttp(0);
    if (http == nullptr) {
        return false;
    }
    http->SetTimeout(kHttpTimeoutMs);
    if (!http->Open("GET", url)) {
        ESP_LOGW(TAG, "Weather request failed: %s", url);
        return false;
    }
    auto status = http->GetStatusCode();
    if (!status || *status != 200) {
        ESP_LOGW(TAG, "Weather request status: %d", status ? *status : -1);
        http->Close();
        return false;
    }
    std::string data = http->ReadAll();
    http->Close();

    cJSON* root = cJSON_Parse(data.c_str());
    if (root == nullptr) {
        ESP_LOGW(TAG, "Failed to parse weather response");
        return false;
    }
    std::string new_summary;
    cJSON* current = cJSON_GetObjectItem(root, "current");
    cJSON* temp = current ? cJSON_GetObjectItem(current, "temperature_2m") : nullptr;
    if (cJSON_IsNumber(temp)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%.0f°C", temp->valuedouble);
        new_summary = buf;
        cJSON* code = cJSON_GetObjectItem(current, "weather_code");
        if (cJSON_IsNumber(code)) {
            const char* desc = DescribeWeatherCode((int)code->valuedouble);
            if (desc != nullptr && desc[0] != '\0') {
                new_summary += " ";
                new_summary += desc;
            }
        }
    }
    cJSON_Delete(root);
    if (new_summary.empty()) {
        ESP_LOGW(TAG, "Weather response missing current conditions");
        return false;
    }

    bool changed = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (new_summary != summary_) {
            summary_ = new_summary;
            changed = true;
        }
    }
    if (changed) {
        ESP_LOGI(TAG, "Weather updated: %s", new_summary.c_str());
        Settings cache("weather", true);
        cache.SetString("summary", new_summary);
        std::function<void()> on_updated;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            on_updated = on_updated_;
        }
        if (on_updated) {
            on_updated();
        }
    }
    return true;
}
