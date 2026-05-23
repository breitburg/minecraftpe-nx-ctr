//
// Created by efimandreev0 on 10.03.2026.
//

#ifndef MINECRAFTCPP_MAIN_CTR_H
#define MINECRAFTCPP_MAIN_CTR_H

#include <cassert>
#include <vector>
#include <cmath>
#include <malloc.h>
#include <algorithm>
#include <unistd.h>
#include <sys/stat.h>

#if defined(__3DS__)
#include <3ds.h>
#include <citro3d.h>
#include <NovaGL.h>
#endif

#include "App.h"
#include "AppPlatform_ctr.h"
#include "client/renderer/GameRenderer.h"
#include "platform/log.h"
#include "platform/input/Mouse.h"
#include "platform/input/Multitouch.h"
#include "platform/input/Keyboard.h"
#include "platform/input/Controller.h"
#include "platform/ctr_caps.h"
#include "util/FrameProf.h"

static bool _app_inited = false;

u32 __ctru_linear_heap_size = 40 * 1024 * 1024;
u32 __stacksize__ = 256 * 1024;

// Диагностика выхода: каждая строка сразу пишется в файл на SD и закрывается,
// чтобы при зависании последняя успешная строка точно осталась на карте.
#define EXIT_LOG_PATH "sdmc:/3ds/minecraftpe/exit_log.txt"
static void exitLog(const char* msg) {
    FILE* f = fopen(EXIT_LOG_PATH, "a");
    if (f) {
        fputs(msg, f);
        fputc('\n', f);
        fclose(f);
    }
    printf("%s\n", msg);
}

static void initGraphics(App* app, AppContext* state) {
    osSetSpeedupEnable(true);
    ctr_caps_init();
    printf("[CAPS] Console: %s\n", isOld3ds() ? "Old 3DS (aggressive cuts ON)" : "New 3DS");

    gfxInitDefault();
    nova_init();
    gfxSet3D(true);
    // Disable NovaGL's built-in per-eye projection shift (it defaults to 0.05f
    // and leaks to non-eye-targeted draws like the bottom screen and menus).
    // All parallax is handled manually by setupCamera / renderItemInHand.
    novaSet3DDepth(0.0f);
    g_stereoNativeActive = true;

    if (!_app_inited) {
        _app_inited = true;
        app->init(*state);
    } else {
        app->onGraphicsReset(*state);
    }

    app->setSize(NOVA_SCREEN_W, NOVA_SCREEN_H);
}

static void deinitGraphics() {
    exitLog("[EXIT] nova_fini: begin");
    nova_fini(); // Очистка транслятора
    //exitLog("[EXIT] nova_fini: done");
    exitLog("[EXIT] gfxExit: begin");
    gfxExit();
    exitLog("[EXIT] gfxExit: done");
}

// Указатель на приложение — нужен обработчикам ввода, чтобы читать опции
// (схему управления) и спрашивать у Gui попадание тача по кнопкам.
static MAIN_CLASS* s_app = nullptr;

static bool ctrXybaInGame() {
    return s_app && s_app->options.xybaCamera
        && s_app->level != nullptr && s_app->screen == nullptr;
}

void handleTouch() {
    static bool wasTouching = false;
    static int16_t lastX = 0;
    static int16_t lastY = 0;
    static bool jumpHeld = false;
    static bool invHeld  = false;

    touchPosition touch;
    hidTouchRead(&touch);

    bool isTouching = (hidKeysHeld() & KEY_TOUCH) != 0;
    bool xyba = ctrXybaInGame();

    if (isTouching) {
        int16_t x = (touch.px * NOVA_SCREEN_W) / NOVA_SCREEN_BOTTOM_H;
        int16_t y = (touch.py * NOVA_SCREEN_H) / NOVA_SCREEN_BOTTOM_W;

        if (!wasTouching) {
            Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_DOWN, x, y);
            // В схеме XYBA стилус не крутит камеру — Multitouch не кормим.
            if (!xyba) Multitouch::feed(1, MouseAction::DATA_DOWN, x, y, 0);
            wasTouching = true;
        } else if (x != lastX || y != lastY) {
            Mouse::feed(MouseAction::ACTION_MOVE, MouseAction::DATA_DOWN, x, y);
            if (!xyba) Multitouch::feed(1, MouseAction::DATA_DOWN, x, y, 0);
        }

        lastX = x;
        lastY = y;
    }
    else if (wasTouching) {
        Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_UP, lastX, lastY);
        if (!xyba) Multitouch::feed(1, MouseAction::DATA_UP, lastX, lastY, 0);
        wasTouching = false;
    }

    int btn = (xyba && isTouching && s_app)
        ? s_app->gui.controlButtonAt(lastX, lastY)
        : 0;

    bool wantJump = (btn == 1);
    if (wantJump != jumpHeld) {
        Keyboard::feed(Keyboard::KEY_SPACE, wantJump ? 1 : 0);
        jumpHeld = wantJump;
    }
    bool wantInv = (btn == 2);
    if (wantInv != invHeld) {
        Keyboard::feed(Keyboard::KEY_E, wantInv ? 1 : 0);
        invHeld = wantInv;
    }
}
void printMemoryStats() {
    struct mallinfo mi = mallinfo();

    u32 linearFree = linearSpaceFree();

    float heapUsedMB = (float)mi.uordblks / 1024.0f / 1024.0f;
    float linearFreeMB = (float)linearFree / 1024.0f / 1024.0f;

    printf("[MEMORY] Heap Used: %.2f MB | LINEAR RAM FREE: %.2f MB\n",
           heapUsedMB, linearFreeMB);
}
static void trackpadFeed(int stick, float x, float y) {
    float limitX = (std::abs(x) > 0.15f) ? x : 0.0f;
    float limitY = (std::abs(y) > 0.15f) ? y : 0.0f;

    limitX = std::max(-1.0f, std::min(1.0f, limitX));
    limitY = std::max(-1.0f, std::min(1.0f, limitY));

    Controller::feed(stick, Controller::STATE_TOUCH, limitX, limitY);
}

void handleController() {
    u32 kDown = hidKeysDown();
    u32 kUp = hidKeysUp();
    u32 kHeld = hidKeysHeld();
    u32 changed = kDown | kUp;

    circlePosition cp;
    hidCircleRead(&cp);
    trackpadFeed(1, (float)cp.dx / 156.0f, -(float)cp.dy / 156.0f);

    circlePosition cs;
    irrstCstickRead(&cs);
    trackpadFeed(2, (float)cs.dx / 156.0f, -(float)cs.dy / 156.0f);

    if(changed & KEY_DUP) Keyboard::feed(Keyboard::KEY_F5, (kHeld & KEY_DUP) ? 1 : 0);
    if(changed & (KEY_DRIGHT | KEY_ZR)) Keyboard::feed(Keyboard::KEY_RIGHT, (kHeld & (KEY_DRIGHT | KEY_ZR)) ? 1 : 0);
    if(changed & (KEY_DLEFT | KEY_ZL)) Keyboard::feed(Keyboard::KEY_LEFT, (kHeld & (KEY_DLEFT | KEY_ZL)) ? 1 : 0);
    if(changed & KEY_DDOWN) Keyboard::feed(Keyboard::KEY_LSHIFT, (kHeld & KEY_DDOWN) ? 1 : 0);

    if (ctrXybaInGame()) {
        float lx = 0.0f, ly = 0.0f;
        if (kHeld & KEY_A) lx += 1.0f;
        if (kHeld & KEY_Y) lx -= 1.0f;
        if (kHeld & KEY_B) ly += 1.0f;
        if (kHeld & KEY_X) ly -= 1.0f;
        const float kLookSpeed = 0.75f;
        Controller::feed(2, Controller::STATE_TOUCH, lx * kLookSpeed, ly * kLookSpeed);
    } else {
        if(changed & KEY_A) Keyboard::feed(Keyboard::KEY_SPACE, (kHeld & KEY_A) ? 1 : 0);
        if(changed & KEY_X) Keyboard::feed(Keyboard::KEY_C, (kHeld & KEY_X) ? 1 : 0);
        if(changed & KEY_B) Keyboard::feed(Keyboard::KEY_ESCAPE, (kHeld & KEY_B) ? 1 : 0);
        if(changed & KEY_Y) Keyboard::feed(Keyboard::KEY_E, (kHeld & KEY_Y) ? 1 : 0);
    }
    if(changed & KEY_START) Keyboard::feed(Keyboard::KEY_P, (kHeld & KEY_START) ? 1 : 0);
    if(changed & KEY_R) Mouse::feed(MouseAction::ACTION_LEFT, (kHeld & KEY_R) ? 1 : 0, 0, 0);
    if(changed & KEY_L) Mouse::feed(MouseAction::ACTION_RIGHT, (kHeld & KEY_L) ? 1 : 0, 0, 0);
}
//extern "C" {
//    u32 __ctru_heap_size = 1024 * 1024 * 45;
//
//    u32 __ctru_linear_heap_size = 1024 * 1024 * 24;
//}
int main(int argc, char** argv) {
    romfsInit();

    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/minecraftpe", 0777);
    mkdir("sdmc:/3ds/minecraftpe/cache", 0777);

    // Очищаем лог выхода в начале сессии (дальше exitLog только дописывает).
    { FILE* f = fopen(EXIT_LOG_PATH, "w"); if (f) fclose(f); }

    //FILE* log_file = fopen("sdmc:/3ds/minecraftpe/debug_log.txt", "w");
    //if (log_file) {
    //    static char logBuffer[16 * 1024];
    //    setvbuf(log_file, logBuffer, _IOFBF, sizeof(logBuffer));
    //    dup2(fileno(log_file), STDOUT_FILENO);
    //    dup2(fileno(log_file), STDERR_FILENO);
    //}

    irrstInit();

    MAIN_CLASS* app = new MAIN_CLASS();
    s_app = app;

    app->externalStoragePath = "sdmc:/3ds/minecraftpe";
    app->externalCacheStoragePath = "sdmc:/3ds/minecraftpe/cache";

    int commandPort = 0;
    if (argc > 1) commandPort = atoi(argv[1]);
    if (commandPort != 0) app->commandPort = commandPort;

    AppContext context;
    AppPlatform_3ds platform;
    context.doRender = true;
    context.platform = &platform;

    initGraphics(app, &context);

    int frameCounter = 0;

    const u64 kTargetFrameNs = 1000000000ULL / 30ULL; // 33,333,333 ns
    u64 nextFrameTick = svcGetSystemTick();
    const u64 kSysTicksPerSec = SYSCLOCK_ARM11; // 268,123,480 на 3DS

    while (aptMainLoop()) {
        //FrameProf::beginFrame();

        {
            FrameProf::Scoped _s_input("00.input");
            hidScanInput();

            if ((hidKeysHeld() & KEY_START) && (hidKeysHeld() & KEY_SELECT)) {
                exitLog("[EXIT] loop break: START+SELECT");
                break;
            }

            handleTouch();
            handleController();
        }

        {
            float slider = osGet3DSliderState();
            g_stereoSlider = slider;
            g_stereoEyeCount = (slider > 0.001f) ? novaGetEyeCount() : 1;
        }

        {
            FrameProf::Scoped _s_update("01.app_update");
            app->update();
        }

        {
            FrameProf::Scoped _s_swap("02.swap");
            novaSwapBuffers();
        }

        if (frameCounter % 60 == 0) {
            printMemoryStats();
        }
        frameCounter++;

        // ticks = ns * (TicksPerSec / 1e9).
        const u64 kTargetFrameTicks = (kTargetFrameNs * kSysTicksPerSec) / 1000000000ULL;
        nextFrameTick += kTargetFrameTicks;
        u64 now = svcGetSystemTick();
        if (now < nextFrameTick) {
            FrameProf::Scoped _s_sleep("03.frame_sleep");
            u64 remainingTicks = nextFrameTick - now;
            s64 remainingNs = (s64)((remainingTicks * 1000000000ULL) / kSysTicksPerSec);
            if (remainingNs > 0) svcSleepThread(remainingNs);
        } else {
            nextFrameTick = now;
        }

        //FrameProf::endFrame();
    }

    exitLog("[EXIT] main loop ended (aptMainLoop returned false or break)");

    exitLog("[EXIT] delete app: begin");
    //delete app;
    //s_app = nullptr;
    exitLog("[EXIT] delete app: done");

    //deinitGraphics();

    exitLog("[EXIT] irrstExit: begin");
    //irrstExit();
    exitLog("[EXIT] irrstExit: done");

    exitLog("[EXIT] romfsExit: begin");
    //romfsExit();
    exitLog("[EXIT] romfsExit: done");

    exitLog("[EXIT] returning from main");

    return 0;
}

#endif //MINECRAFTCPP_MAIN_CTR_H
