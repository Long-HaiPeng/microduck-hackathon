#ifndef WEATHER_SERVICE_H
#define WEATHER_SERVICE_H

#include <functional>
#include <mutex>
#include <string>

// 待机天气表盘：固件本地定时拉取 Open-Meteo（免 key 免注册）当前天气，
// 显示在待机帮助卡片首行。结果缓存到 NVS，断网或重启后仍显示最近一次。
// 坐标可用 NVS weather/location（"纬度,经度"）覆盖，默认上海。
class WeatherService {
public:
    static WeatherService& GetInstance();

    // 联网成功后调用一次，启动后台刷新任务；重复调用无效果
    void Start();

    // 天气摘要（如 "26°C 多云"），无数据时返回空串
    std::string GetSummary() const;

    // 数据更新回调。在天气任务线程触发，调用方需自行切回主线程
    void SetOnUpdated(std::function<void()> callback);

private:
    WeatherService();
    void TaskLoop();
    bool FetchOnce();

    mutable std::mutex mutex_;
    std::string summary_;
    std::function<void()> on_updated_;
    bool started_ = false;
};

#endif  // WEATHER_SERVICE_H
