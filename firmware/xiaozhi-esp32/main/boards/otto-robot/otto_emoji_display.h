#pragma once

#include "display/lcd_display.h"
#include "sdkconfig.h"

/**
 * @brief Otto机器人GIF表情显示类
 * 继承SpiLcdDisplay，通过EmojiCollection添加GIF表情支持
 */
class OttoEmojiDisplay : public SpiLcdDisplay {
public:
    /**
     * @brief 构造函数，参数与SpiLcdDisplay相同
     */
    OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel, int width,
                     int height, int offset_x, int offset_y, bool mirror_x, bool mirror_y,
                     bool swap_xy);

    virtual ~OttoEmojiDisplay();
    virtual void SetStatus(const char* status) override;
    virtual void SetPreviewImage(std::unique_ptr<LvglImage> image) override;
    virtual void SetupUI() override;
#if CONFIG_OTTO_DUCK_BODY
    virtual void SetEmotion(const char* emotion) override;
    virtual void SetTheme(Theme* theme) override;
    virtual void SetChatMessage(const char* role, const char* content) override;
    virtual void RemoveChatMessage(const char* role, const char* content) override;
    virtual void ClearChatMessages() override;
    virtual void ShowNotification(const char* notification, int duration_ms = 3000) override;
    virtual void ShowNotification(const std::string& notification, int duration_ms = 3000) override;
#endif
    // 空闲帮助卡片已常驻唤醒词说明，不再弹启动提示
    virtual bool WantsStartupWakeWordHint() const override { return false; }

private:
    void InitializeOttoEmojis();
    void SetupPreviewImage();
#if CONFIG_OTTO_DUCK_BODY
    enum class DuckEyeState { Idle, Listening, Speaking, Connecting, Connected };
    void SetupDuckWindow();
    void RefreshDuckWindow();
    lv_obj_t* duck_window_ = nullptr;
    lv_obj_t* duck_eye_ = nullptr;
    lv_obj_t* duck_iris_ = nullptr;
    lv_obj_t* duck_pupil_ = nullptr;
    lv_obj_t* duck_upper_lid_ = nullptr;
    lv_obj_t* duck_lower_lid_ = nullptr;
    lv_obj_t* duck_text_ = nullptr;
    lv_obj_t* duck_preview_ = nullptr;
    lv_timer_t* duck_timer_ = nullptr;
    DuckEyeState duck_eye_state_ = DuckEyeState::Idle;
    std::string duck_status_;
    std::string duck_system_message_;
    std::string duck_notification_;
    std::string duck_rendered_text_;
    int64_t duck_notification_until_us_ = 0;
    int64_t duck_connected_until_us_ = 0;
#endif
};
