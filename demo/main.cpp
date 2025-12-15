#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <mutex>
#include <vector>
#include <android/native_window.h>

extern "C" {
#include "renderer.h"
#include "microui.h"
}

#include "ANativeWindowCreator.h"
#include "ATouchEvent.h"

static std::atomic<bool> g_running{true};

static std::mutex g_input_mutex;
static mu_Context *g_ctx = nullptr;

static char logbuf[64000];
static int logbuf_updated = 0;

static void write_log(const char *text) {
    if (logbuf[0]) strcat(logbuf, "\n");
    strcat(logbuf, text);
    logbuf_updated = 1;
}

static void test_window(mu_Context *ctx) {
    if (mu_begin_window(ctx, "Demo Window", mu_rect(40, 40, 300, 450))) {
        mu_Container *win = mu_get_current_container(ctx);
        win->rect.w = mu_max(win->rect.w, 240);
        win->rect.h = mu_max(win->rect.h, 300);

        if (mu_header(ctx, "Window Info")) {
            char buf[64];
            mu_layout_row(ctx, 2, (int[]) { 54, -1 }, 0);
            mu_label(ctx, "Position:");
            sprintf(buf, "%d, %d", win->rect.x, win->rect.y);
            mu_label(ctx, buf);
            mu_label(ctx, "Size:");
            sprintf(buf, "%d, %d", win->rect.w, win->rect.h);
            mu_label(ctx, buf);
        }

        if (mu_header_ex(ctx, "Test Buttons", MU_OPT_EXPANDED)) {
            mu_layout_row(ctx, 3, (int[]) { 86, -110, -1 }, 0);
            mu_label(ctx, "Test buttons 1:");
            if (mu_button(ctx, "Button 1")) write_log("Pressed button 1");
            if (mu_button(ctx, "Button 2")) write_log("Pressed button 2");
            mu_label(ctx, "Test buttons 2:");
            if (mu_button(ctx, "Button 3")) write_log("Pressed button 3");
            if (mu_button(ctx, "Popup")) mu_open_popup(ctx, "Test Popup");
            if (mu_begin_popup(ctx, "Test Popup")) {
                mu_button(ctx, "Hello");
                mu_button(ctx, "World");
                mu_end_popup(ctx);
            }
        }
        mu_end_window(ctx);
    }
}

static void log_window(mu_Context *ctx) {
    if (mu_begin_window(ctx, "Log Window", mu_rect(350, 40, 300, 200))) {
        mu_layout_row(ctx, 1, (int[]) { -1 }, -25);
        mu_begin_panel(ctx, "Log Output");
        mu_Container *panel = mu_get_current_container(ctx);
        mu_layout_row(ctx, 1, (int[]) { -1 }, -1);
        mu_text(ctx, logbuf);
        mu_end_panel(ctx);
        if (logbuf_updated) {
            panel->scroll.y = panel->content_size.y;
            logbuf_updated = 0;
        }
        static char buf[128];
        int submitted = 0;
        mu_layout_row(ctx, 2, (int[]) { -70, -1 }, 0);
        if (mu_textbox(ctx, buf, sizeof(buf)) & MU_RES_SUBMIT) {
            mu_set_focus(ctx, ctx->last_id);
            submitted = 1;
        }
        if (mu_button(ctx, "Submit")) submitted = 1;
        if (submitted) {
            write_log(buf);
            buf[0] = '\0';
        }
        mu_end_window(ctx);
    }
}

static int uint8_slider(mu_Context *ctx, unsigned char *value, int low, int high) {
    static float tmp;
    mu_push_id(ctx, &value, sizeof(value));
    tmp = *value;
    int res = mu_slider_ex(ctx, &tmp, low, high, 0, "%.0f", MU_OPT_ALIGNCENTER);
    *value = tmp;
    mu_pop_id(ctx);
    return res;
}

static void style_window(mu_Context *ctx) {
    static struct { const char *label; int idx; } colors[] = {
        { "text:",         MU_COLOR_TEXT        },
        { "border:",       MU_COLOR_BORDER      },
        { "windowbg:",     MU_COLOR_WINDOWBG    },
        { "titlebg:",      MU_COLOR_TITLEBG     },
        { "titletext:",    MU_COLOR_TITLETEXT   },
        { "panelbg:",      MU_COLOR_PANELBG     },
        { "button:",       MU_COLOR_BUTTON      },
        { "buttonhover:",  MU_COLOR_BUTTONHOVER },
        { "buttonfocus:",  MU_COLOR_BUTTONFOCUS },
        { "base:",         MU_COLOR_BASE        },
        { "basehover:",    MU_COLOR_BASEHOVER   },
        { "basefocus:",    MU_COLOR_BASEFOCUS   },
        { "scrollbase:",   MU_COLOR_SCROLLBASE  },
        { "scrollthumb:",  MU_COLOR_SCROLLTHUMB },
        { NULL }
    };

    if (mu_begin_window(ctx, "Style Editor", mu_rect(350, 250, 300, 240))) {
        int sw = mu_get_current_container(ctx)->body.w * 0.14;
        mu_layout_row(ctx, 6, (int[]) { 80, sw, sw, sw, sw, -1 }, 0);
        for (int i = 0; colors[i].label; i++) {
            mu_label(ctx, colors[i].label);
            uint8_slider(ctx, &ctx->style->colors[i].r, 0, 255);
            uint8_slider(ctx, &ctx->style->colors[i].g, 0, 255);
            uint8_slider(ctx, &ctx->style->colors[i].b, 0, 255);
            uint8_slider(ctx, &ctx->style->colors[i].a, 0, 255);
            mu_draw_rect(ctx, mu_layout_next(ctx), ctx->style->colors[i]);
        }
        mu_end_window(ctx);
    }
}

static void process_frame(mu_Context *ctx) {
    mu_begin(ctx);
    style_window(ctx);
    log_window(ctx);
    test_window(ctx);
    mu_end(ctx);
}

static int text_width(mu_Font font, const char *text, int len) {
    (void)font;
    if (len == -1) len = strlen(text);
    return r_get_text_width(text, len);
}

static int text_height(mu_Font font) {
    (void)font;
    return r_get_text_height();
}

static void touch_thread(int screen_w, int screen_h, int theta, float ui_scale) {
    android::ATouchEvent touch;
    android::ATouchEvent::TouchEvent ev{};

    while (g_running) {
        if (!touch.GetTouchEvent(&ev)) {
            usleep(1000);
            continue;
        }

        ev.TransformToScreen(screen_w, screen_h, theta);
        int x = ev.x / ui_scale;
        int y = ev.y / ui_scale;

        std::lock_guard<std::mutex> lock(g_input_mutex);
        if (!g_ctx) continue;

        switch (ev.type) {
            case android::ATouchEvent::EventType::Move:
                mu_input_mousemove(g_ctx, x, y);
                break;
            case android::ATouchEvent::EventType::TouchDown:
                mu_input_mousemove(g_ctx, x, y);
                mu_input_mousedown(g_ctx, x, y, MU_MOUSE_LEFT);
                break;
            case android::ATouchEvent::EventType::TouchUp:
                mu_input_mousemove(g_ctx, x, y);
                mu_input_mouseup(g_ctx, x, y, MU_MOUSE_LEFT);
                break;
            default:
                break;
        }
    }
}

int main(int argc, char **argv) {
    (void)argc; (void)argv;

    auto displayInfo = android::ANativeWindowCreator::GetDisplayInfo();
    int screen_w = displayInfo.width;
    int screen_h = displayInfo.height;

    ANativeWindow *native_window = android::ANativeWindowCreator::Create("MicroUI", screen_w, screen_h);
    if (!native_window) return -1;

    if (r_init(EGL_DEFAULT_DISPLAY, native_window) != 0) {
        ANativeWindow_release(native_window);
        return -1;
    }
    r_set_size(screen_w, screen_h);

    mu_Context *ctx = (mu_Context*)malloc(sizeof(mu_Context));
    mu_init(ctx);
    ctx->text_width = text_width;
    ctx->text_height = text_height;

    {
        std::lock_guard<std::mutex> lock(g_input_mutex);
        g_ctx = ctx;
    }

    int theta = displayInfo.theta;
    float ui_scale = 2.0f;

    std::thread input_thread(touch_thread, screen_w, screen_h, theta, ui_scale);

    while (g_running) {
        android::ANativeWindowCreator::ProcessMirrorDisplay();
        process_frame(ctx);

        r_clear(mu_color(0, 0, 0, 0));
        mu_Command *cmd = NULL;
        while (mu_next_command(ctx, &cmd)) {
            switch (cmd->type) {
                case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
                case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
                case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
                case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
            }
        }
        r_present();

        usleep(16000);
    }

    g_running = false;
    input_thread.join();

    free(ctx);
    r_destroy();
    android::ANativeWindowCreator::Destroy(native_window);

    return 0;
}
