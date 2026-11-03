#include "otto_emoji_display.h"

#include <esp_log.h>

#include <algorithm>
#include <cstring>
#include <vector>

#include "assets.h"
#include "assets/lang_config.h"
#include "display/lvgl_display/emoji_collection.h"
#include "display/lvgl_display/lvgl_image.h"
#include "display/lvgl_display/lvgl_theme.h"

#define TAG "OttoEmojiDisplay"
OttoEmojiDisplay::OttoEmojiDisplay(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_handle_t panel,
                                   int width, int height, int offset_x, int offset_y, bool mirror_x,
                                   bool mirror_y, bool swap_xy)
    : SpiLcdDisplay(panel_io, panel, width, height, offset_x, offset_y, mirror_x, mirror_y,
                    swap_xy) {}

OttoEmojiDisplay::~OttoEmojiDisplay() {
#if CONFIG_OTTO_DUCK_BODY
    esp_timer_stop(preview_timer_);
    DisplayLockGuard lock(this);
    if (duck_timer_ != nullptr) {
        lv_timer_delete(duck_timer_);
        duck_timer_ = nullptr;
    }
    if (duck_window_ != nullptr) {
        lv_obj_delete(duck_window_);
        duck_window_ = nullptr;
        duck_preview_ = nullptr;
    }
#endif
}

void OttoEmojiDisplay::SetupUI() {
    // Prevent duplicate calls - parent SetupUI() will also check, but check here for early return
    if (setup_ui_called_) {
        ESP_LOGW(TAG, "SetupUI() called multiple times, skipping duplicate call");
        return;
    }

    // Call parent SetupUI() first to create all lvgl objects
    SpiLcdDisplay::SetupUI();

    // UI 对象创建完成后切换主题
    auto* dark_theme = LvglThemeManager::GetInstance().GetTheme("dark");
    if (dark_theme != nullptr) {
        SetTheme(dark_theme);
    }

    // Setup preview image after UI is initialized - release lock before calling SetEmotion
    // to avoid deadlock (SetEmotion also acquires DisplayLockGuard internally)
    {
        DisplayLockGuard lock(this);
        lv_obj_set_size(preview_image_, width_, height_);
    }

#if CONFIG_OTTO_DUCK_BODY
    SetupDuckWindow();
#else
    // Set default emotion after UI is initialized
    SetEmotion("staticstate");
#endif
}

void OttoEmojiDisplay::SetupPreviewImage() {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGW(TAG,
                 "SetupPreviewImage called but preview_image_ is nullptr (UI not initialized yet)");
        return;
    }
    lv_obj_set_size(preview_image_, width_, height_);
}

void OttoEmojiDisplay::InitializeOttoEmojis() {
    ESP_LOGI(TAG, "Otto表情初始化将由Assets系统处理");
    // 表情初始化已移至assets系统,通过DEFAULT_EMOJI_COLLECTION=otto-gif配置
    // assets.cc会从assets分区加载GIF表情并设置到theme
    // Note: Default emotion is now set in SetupUI() after LVGL objects are created
}

LV_FONT_DECLARE(OTTO_ICON_FONT);
void OttoEmojiDisplay::SetStatus(const char* status) {
#if CONFIG_OTTO_DUCK_BODY
    if (status == nullptr) {
        return;
    }
    LvglDisplay::SetStatus(status);
    DisplayLockGuard lock(this);
    duck_status_ = status;
    duck_eye_state_ = DuckEyeState::Idle;
    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        duck_eye_state_ = DuckEyeState::Listening;
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        duck_eye_state_ = DuckEyeState::Speaking;
    } else if (strcmp(status, Lang::Strings::CONNECTING) == 0 ||
               strcmp(status, Lang::Strings::SCANNING_WIFI) == 0 ||
               strcmp(status, Lang::Strings::REGISTERING_NETWORK) == 0) {
        duck_eye_state_ = DuckEyeState::Connecting;
    }
    RefreshDuckWindow();
#else
    auto lvgl_theme = static_cast<LvglTheme*>(current_theme_);
    auto text_font = lvgl_theme->text_font()->font();
    DisplayLockGuard lock(this);
    if (!status) {
        ESP_LOGE(TAG, "SetStatus: status is nullptr");
        return;
    }

    if (strcmp(status, Lang::Strings::LISTENING) == 0) {
        lv_obj_set_style_text_font(status_label_, &OTTO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x84\xB0");  // U+F130 麦克风图标
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    } else if (strcmp(status, Lang::Strings::SPEAKING) == 0) {
        lv_obj_set_style_text_font(status_label_, &OTTO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x80\xA8");  // U+F028 说话图标
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    } else if (strcmp(status, Lang::Strings::CONNECTING) == 0) {
        lv_obj_set_style_text_font(status_label_, &OTTO_ICON_FONT, 0);
        lv_label_set_text(status_label_, "\xEF\x83\x81");  // U+F0c1 连接图标
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    } else if (strcmp(status, Lang::Strings::STANDBY) == 0) {
        lv_obj_set_style_text_font(status_label_, text_font, 0);
        lv_label_set_text(status_label_, "");
        lv_obj_clear_flag(status_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_set_style_text_font(status_label_, text_font, 0);
    lv_label_set_text(status_label_, status);
    // 从图标模式（LISTENING/SPEAKING/CONNECTING）切回文本时恢复状态栏图标
    lv_obj_clear_flag(network_label_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(battery_label_, LV_OBJ_FLAG_HIDDEN);
#endif
}

void OttoEmojiDisplay::SetPreviewImage(std::unique_ptr<LvglImage> image) {
    DisplayLockGuard lock(this);
    if (preview_image_ == nullptr) {
        ESP_LOGE(TAG, "Preview image is not initialized");
        return;
    }

    if (image == nullptr) {
#if CONFIG_OTTO_DUCK_BODY
        if (duck_preview_ != nullptr) {
            lv_obj_add_flag(duck_preview_, LV_OBJ_FLAG_HIDDEN);
            lv_image_set_src(duck_preview_, nullptr);
            lv_obj_remove_flag(duck_eye_, LV_OBJ_FLAG_HIDDEN);
        }
#endif
        esp_timer_stop(preview_timer_);
        lv_obj_remove_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
        preview_image_cached_.reset();
        if (gif_controller_) {
            gif_controller_->Start();
        }
        return;
    }

    preview_image_cached_ = std::move(image);
    auto img_dsc = preview_image_cached_->image_dsc();
#if CONFIG_OTTO_DUCK_BODY
    if (duck_preview_ != nullptr && img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        lv_image_set_src(duck_preview_, img_dsc);
        lv_image_set_rotation(duck_preview_, 900);
        const int available = CONFIG_OTTO_DUCK_EYE_SIZE * 2 / 3;
        const int scale = std::min(256 * CONFIG_OTTO_DUCK_EYE_SIZE / img_dsc->header.h,
                                   256 * available / img_dsc->header.w);
        lv_image_set_scale(duck_preview_, scale);
        lv_obj_remove_flag(duck_preview_, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(duck_eye_, LV_OBJ_FLAG_HIDDEN);
    }
#endif
    // 设置图片源并显示预览图片
    lv_image_set_src(preview_image_, img_dsc);
    lv_image_set_rotation(preview_image_, 900);
    if (img_dsc->header.w > 0 && img_dsc->header.h > 0) {
        // zoom factor 1.0
        lv_image_set_scale(preview_image_, 256 * width_ / img_dsc->header.w);
    }

    // Hide emoji_box_
    if (gif_controller_) {
        gif_controller_->Stop();
    }
    lv_obj_add_flag(emoji_box_, LV_OBJ_FLAG_HIDDEN);
    lv_obj_remove_flag(preview_image_, LV_OBJ_FLAG_HIDDEN);
    esp_timer_stop(preview_timer_);
    ESP_ERROR_CHECK(esp_timer_start_once(preview_timer_, PREVIEW_IMAGE_DURATION_MS * 1000));
}

#if CONFIG_OTTO_DUCK_BODY
void OttoEmojiDisplay::SetupDuckWindow() {
    static_assert(CONFIG_OTTO_DUCK_EYE_SIZE >= 64, "Duck window must fit a status line");
    static_assert(CONFIG_OTTO_DUCK_EYE_X >= 0 &&
                      CONFIG_OTTO_DUCK_EYE_X + CONFIG_OTTO_DUCK_EYE_SIZE <= 240 &&
                      CONFIG_OTTO_DUCK_EYE_Y >= 0 &&
                      CONFIG_OTTO_DUCK_EYE_Y + CONFIG_OTTO_DUCK_EYE_SIZE <= 240,
                  "Duck window must fit the 240x240 display");
    DisplayLockGuard lock(this);
    duck_window_ = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(duck_window_);
    lv_obj_set_pos(duck_window_, CONFIG_OTTO_DUCK_EYE_X, CONFIG_OTTO_DUCK_EYE_Y);
    lv_obj_set_size(duck_window_, CONFIG_OTTO_DUCK_EYE_SIZE, CONFIG_OTTO_DUCK_EYE_SIZE);
    lv_obj_set_style_bg_color(duck_window_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(duck_window_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(duck_window_, LV_OBJ_FLAG_SCROLLABLE);

    duck_eye_ = lv_obj_create(duck_window_);
    lv_obj_remove_style_all(duck_eye_);
    lv_obj_set_size(duck_eye_, CONFIG_OTTO_DUCK_EYE_SIZE, CONFIG_OTTO_DUCK_EYE_SIZE);
    lv_obj_set_style_radius(duck_eye_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(duck_eye_, lv_color_hex(0xFFF8E9), 0);
    lv_obj_set_style_bg_opa(duck_eye_, LV_OPA_COVER, 0);
    lv_obj_set_style_clip_corner(duck_eye_, true, 0);
    lv_obj_center(duck_eye_);
    lv_obj_remove_flag(duck_eye_, LV_OBJ_FLAG_SCROLLABLE);

    duck_iris_ = lv_obj_create(duck_eye_);
    lv_obj_remove_style_all(duck_iris_);
    lv_obj_set_style_radius(duck_iris_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(duck_iris_, LV_OPA_COVER, 0);
    lv_obj_remove_flag(duck_iris_, LV_OBJ_FLAG_SCROLLABLE);

    duck_pupil_ = lv_obj_create(duck_iris_);
    lv_obj_remove_style_all(duck_pupil_);
    lv_obj_set_size(duck_pupil_, CONFIG_OTTO_DUCK_EYE_SIZE / 4, CONFIG_OTTO_DUCK_EYE_SIZE / 4);
    lv_obj_set_style_radius(duck_pupil_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(duck_pupil_, lv_color_hex(0x161616), 0);
    lv_obj_set_style_bg_opa(duck_pupil_, LV_OPA_COVER, 0);
    lv_obj_center(duck_pupil_);

    auto* glint = lv_obj_create(duck_pupil_);
    lv_obj_remove_style_all(glint);
    lv_obj_set_size(glint, CONFIG_OTTO_DUCK_EYE_SIZE / 12, CONFIG_OTTO_DUCK_EYE_SIZE / 12);
    lv_obj_set_style_radius(glint, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(glint, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(glint, LV_OPA_COVER, 0);
    lv_obj_align(glint, LV_ALIGN_TOP_RIGHT, -2, 2);

    duck_upper_lid_ = lv_obj_create(duck_eye_);
    duck_lower_lid_ = lv_obj_create(duck_eye_);
    for (auto* lid : {duck_upper_lid_, duck_lower_lid_}) {
        lv_obj_remove_style_all(lid);
        lv_obj_set_width(lid, CONFIG_OTTO_DUCK_EYE_SIZE);
        lv_obj_set_style_bg_color(lid, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(lid, LV_OPA_COVER, 0);
        lv_obj_remove_flag(lid, LV_OBJ_FLAG_SCROLLABLE);
    }

    duck_preview_ = lv_image_create(duck_window_);
    lv_obj_align(duck_preview_, LV_ALIGN_CENTER, 0, -CONFIG_OTTO_DUCK_EYE_SIZE / 6);
    lv_obj_add_flag(duck_preview_, LV_OBJ_FLAG_HIDDEN);
    duck_text_ = lv_label_create(duck_window_);
    lv_obj_set_width(duck_text_, CONFIG_OTTO_DUCK_EYE_SIZE - 4);
    lv_obj_align(duck_text_, LV_ALIGN_BOTTOM_MID, 0, -2);
    lv_obj_set_style_text_color(duck_text_, lv_color_white(), 0);
    lv_obj_set_style_bg_color(duck_text_, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(duck_text_, LV_OPA_80, 0);
    lv_obj_set_style_text_font(duck_text_,
                               static_cast<LvglTheme*>(current_theme_)->text_font()->font(), 0);
    lv_label_set_long_mode(duck_text_, LV_LABEL_LONG_SCROLL_CIRCULAR);
    lv_label_set_text(duck_text_, "");
    duck_timer_ = lv_timer_create(
        [](lv_timer_t* timer) {
            auto* self = static_cast<OttoEmojiDisplay*>(lv_timer_get_user_data(timer));
            self->RefreshDuckWindow();
        },
        50, this);
    RefreshDuckWindow();
}

void OttoEmojiDisplay::RefreshDuckWindow() {
    if (duck_window_ == nullptr) {
        return;
    }
    const int64_t now = esp_timer_get_time();
    std::string text = duck_system_message_.empty() ? duck_status_ : duck_system_message_;
    // Only translate the exact system help generated by Application::IdleHelpText.
    // Keep the source message untouched so RemoveChatMessage can still match it.
    static constexpr struct {
        const char* help;
        const char* server_test;
        const char* server_pro;
        const char* actions;
        const char* duck_actions;
    } help_variants[] = {
        {"你好小智唤醒 · 单击=对话\n长按说话 双击模式 三击服务器\n试试说: 走两步 / 挥挥手",
         "\n服务器: 灵矽测试", "\n服务器: 灵矽正式", "试试说: 走两步 / 挥挥手",
         "试试说: 走两步 / 转头 / 张嘴"},
        {"Hi,xiaozhi wake · Click=Chat\nHold=Talk Dbl=Mode 3x=Server\nTry: walk / wave",
         "\nServer: LingXi Test", "\nServer: LingXi Pro", "Try: walk / wave",
         "Try: walk / turn head / quack"},
    };
    for (const auto& variant : help_variants) {
        const auto help_start = duck_system_message_.rfind(variant.help);
        if (help_start == std::string::npos ||
            (help_start != 0 && duck_system_message_[help_start - 1] != '\n')) {
            continue;
        }
        const auto suffix = duck_system_message_.substr(help_start + strlen(variant.help));
        if (suffix != variant.server_test && suffix != variant.server_pro) {
            continue;
        }
        if (duck_eye_state_ == DuckEyeState::Idle) {
            const auto action_start = help_start + strlen(variant.help) - strlen(variant.actions);
            text.replace(action_start, strlen(variant.actions), variant.duck_actions);
#if CONFIG_SR_WN_WN9_XIAOYAXIAOYA_TTS2
            // The duck build may select a different wake model without changing
            // Application's stored help text (which is used for removal).
            const auto wake = text.find("你好小智唤醒", help_start);
            if (wake != std::string::npos) {
                text.replace(wake, strlen("你好小智唤醒"), "小鸭小鸭唤醒");
            }
#endif
        } else {
            text = duck_status_;
        }
        break;
    }
    if (now < duck_notification_until_us_) {
        text = duck_notification_;
    }
    if (low_battery_popup_ != nullptr && !lv_obj_has_flag(low_battery_popup_, LV_OBJ_FLAG_HIDDEN)) {
        text = std::string(Lang::Strings::BATTERY_NEED_CHARGE) + " " + text;
    }
    // Keep activation codes and multiline Wi-Fi instructions readable through the aperture.
    std::replace(text.begin(), text.end(), '\n', ' ');
    if (text != duck_rendered_text_) {
        lv_label_set_text(duck_text_, text.c_str());
        duck_rendered_text_ = std::move(text);
    }

    if (duck_eye_state_ == DuckEyeState::Connected && now >= duck_connected_until_us_) {
        duck_eye_state_ = DuckEyeState::Idle;
    }

    // A single circular eye fits behind the shell aperture. The ordinary status
    // line and important setup/notification text remain visible above it.
    const int size = CONFIG_OTTO_DUCK_EYE_SIZE;
    const int tick = static_cast<int>((now / 50000) % 120);
    const int wave = tick < 60 ? tick : 120 - tick;
    int gaze_x = (wave - 30) * size / 360;
    int iris_percent = 52;
    int upper_percent = 17;
    int lower_percent = 8;
    uint32_t iris_color = 0xD99137;
    switch (duck_eye_state_) {
        case DuckEyeState::Connecting:
            iris_color = 0x48BFE0;
            iris_percent = 45;
            upper_percent = 4;
            lower_percent = 2;
            break;
        case DuckEyeState::Connected:
            iris_color = 0x62CB7A;
            gaze_x = 0;
            upper_percent = 35;
            lower_percent = 18;
            break;
        case DuckEyeState::Listening:
            iris_color = 0x9577E8;
            iris_percent = 60;
            gaze_x /= 4;
            upper_percent = 0;
            lower_percent = 0;
            break;
        case DuckEyeState::Speaking:
            iris_color = 0xE87942;
            iris_percent = 56;
            upper_percent = 8;
            lower_percent = 4;
            break;
        case DuckEyeState::Idle:
            break;
    }
    const int iris_size = size * iris_percent / 100;
    lv_obj_set_size(duck_iris_, iris_size, iris_size);
    lv_obj_align(duck_iris_, LV_ALIGN_CENTER, gaze_x, 0);
    lv_obj_align(duck_pupil_, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(duck_iris_, lv_color_hex(iris_color), 0);

    const int blink_frame = static_cast<int>((now / 50000) % 110);
    const int blink = blink_frame == 0 || blink_frame == 4   ? 35
                      : blink_frame == 1 || blink_frame == 3 ? 75
                      : blink_frame == 2                     ? 100
                                                             : 0;
    upper_percent += (60 - upper_percent) * blink / 100;
    lower_percent += (40 - lower_percent) * blink / 100;
    const int upper_height = size * upper_percent / 100;
    const int lower_height = size * lower_percent / 100;
    lv_obj_set_height(duck_upper_lid_, upper_height);
    lv_obj_set_height(duck_lower_lid_, lower_height);
    lv_obj_set_pos(duck_lower_lid_, 0, size - lower_height);
}

void OttoEmojiDisplay::SetEmotion(const char* emotion) {
    // The eye represents device state. Do not allocate hidden GIFs behind the shell.
    (void)emotion;
}

void OttoEmojiDisplay::SetTheme(Theme* theme) {
    LcdDisplay::SetTheme(theme);
    DisplayLockGuard lock(this);
    if (duck_text_ != nullptr) {
        lv_obj_set_style_text_font(duck_text_,
                                   static_cast<LvglTheme*>(current_theme_)->text_font()->font(), 0);
    }
}

void OttoEmojiDisplay::SetChatMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr) {
        return;
    }
    LcdDisplay::SetChatMessage(role, content);
    if (strcmp(role, "system") == 0) {
        DisplayLockGuard lock(this);
        duck_system_message_ = content;
        RefreshDuckWindow();
    }
}

void OttoEmojiDisplay::RemoveChatMessage(const char* role, const char* content) {
    if (role == nullptr || content == nullptr) {
        return;
    }
    LcdDisplay::RemoveChatMessage(role, content);
    DisplayLockGuard lock(this);
    if (strcmp(role, "system") == 0 && duck_system_message_ == content) {
        duck_system_message_.clear();
        RefreshDuckWindow();
    }
}

void OttoEmojiDisplay::ClearChatMessages() {
    LcdDisplay::ClearChatMessages();
    DisplayLockGuard lock(this);
    duck_system_message_.clear();
    RefreshDuckWindow();
}

void OttoEmojiDisplay::ShowNotification(const char* notification, int duration_ms) {
    if (notification == nullptr) {
        return;
    }
    LvglDisplay::ShowNotification(notification, std::max(duration_ms, 1));
    DisplayLockGuard lock(this);
    duck_notification_ = notification;
    if (strncmp(notification, Lang::Strings::CONNECTED_TO, strlen(Lang::Strings::CONNECTED_TO)) ==
        0) {
        duck_eye_state_ = DuckEyeState::Connected;
        duck_connected_until_us_ = esp_timer_get_time() + 2000000;
    } else if (strcmp(notification, Lang::Strings::SCANNING_WIFI) == 0 ||
               strncmp(notification, Lang::Strings::CONNECT_TO,
                       strlen(Lang::Strings::CONNECT_TO)) == 0) {
        duck_eye_state_ = DuckEyeState::Connecting;
    }
    duck_notification_until_us_ =
        esp_timer_get_time() + static_cast<int64_t>(std::max(duration_ms, 1)) * 1000;
    RefreshDuckWindow();
}

void OttoEmojiDisplay::ShowNotification(const std::string& notification, int duration_ms) {
    ShowNotification(notification.c_str(), duration_ms);
}
#endif
