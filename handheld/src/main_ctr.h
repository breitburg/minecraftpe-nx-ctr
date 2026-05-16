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
#include "platform/log.h"
#include "platform/input/Mouse.h"
#include "platform/input/Multitouch.h"
#include "platform/input/Keyboard.h"
#include "platform/input/Controller.h"
#include "util/FrameProf.h"

static bool _app_inited = false;
static u32* soc_sharedmem = NULL;

u32 __ctru_linear_heap_size = 40 * 1024 * 1024;
u32 __stacksize__ = 256 * 1024;

void networkInit() {
    if (soc_sharedmem != NULL) return;

    // Обязательно: без этого новые потоки на 3DS живут только на core 0
    // вместе с main loop и могут залипнуть в RakSleep навсегда. На N3DS
    // это же открывает доступ ко второму ядру системы.
    Result aptRc = APT_SetAppCpuTimeLimit(30);
    if (R_FAILED(aptRc)) {
        printf("APT_SetAppCpuTimeLimit failed: 0x%08lX\n", aptRc);
    }

    soc_sharedmem = (u32*)memalign(0x1000, 0x100000);
    if(soc_sharedmem == NULL) {
        printf("Failed to allocate SOC memory!\n");
        return;
    }

    Result ret = socInit(soc_sharedmem, 0x100000);
    if(R_FAILED(ret)) {
        printf("socInit failed: 0x%08lX\n", ret);
    } else {
        printf("Network initialized perfectly!\n");
    }
}

void networkExit() {
    if (soc_sharedmem) {
        socExit();
        free(soc_sharedmem);
        soc_sharedmem = NULL;
    }
}

static void initGraphics(App* app, AppContext* state) {
    //osSetSpeedupEnable(true);

    gfxInitDefault();
    nova_init();

    if (!_app_inited) {
        _app_inited = true;
        app->init(*state);
    } else {
        app->onGraphicsReset(*state);
    }

    app->setSize(NOVA_SCREEN_W, NOVA_SCREEN_H);
}

static void deinitGraphics() {
    nova_fini(); // Очистка транслятора
    gfxExit();
}

void handleTouch() {
    static bool wasTouching = false;
    static int16_t lastX = 0;
    static int16_t lastY = 0;

    touchPosition touch;
    hidTouchRead(&touch);

    bool isTouching = (hidKeysHeld() & KEY_TOUCH) != 0;

    if (isTouching) {
        int16_t x = (touch.px * NOVA_SCREEN_W) / NOVA_SCREEN_BOTTOM_H;
        int16_t y = (touch.py * NOVA_SCREEN_H) / NOVA_SCREEN_BOTTOM_W;

        if (!wasTouching) {
            Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_DOWN, x, y);
            Multitouch::feed(1, MouseAction::DATA_DOWN, x, y, 0);
            wasTouching = true;
        } else if (x != lastX || y != lastY) {
            Mouse::feed(MouseAction::ACTION_MOVE, MouseAction::DATA_DOWN, x, y);
            Multitouch::feed(1, MouseAction::DATA_DOWN, x, y, 0);
        }

        lastX = x;
        lastY = y;
    }
    else if (wasTouching) {
        Mouse::feed(MouseAction::ACTION_LEFT, MouseAction::DATA_UP, lastX, lastY);
        Multitouch::feed(1, MouseAction::DATA_UP, lastX, lastY, 0);
        wasTouching = false;
    }
}
void printMemoryStats() {
    struct mallinfo mi = mallinfo();

    // Свободная линейная память (ИМЕННО ОНА НУЖНА ДЛЯ ЧАНКОВ НА 3DS)
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
    if(changed & KEY_DRIGHT) Keyboard::feed(Keyboard::KEY_RIGHT, (kHeld & KEY_DRIGHT) ? 1 : 0);
    if(changed & KEY_DLEFT) Keyboard::feed(Keyboard::KEY_LEFT, (kHeld & KEY_DLEFT) ? 1 : 0);
    if(changed & KEY_DDOWN) Keyboard::feed(Keyboard::KEY_LSHIFT, (kHeld & KEY_DDOWN) ? 1 : 0);
    if(changed & KEY_A) Keyboard::feed(Keyboard::KEY_SPACE, (kHeld & KEY_A) ? 1 : 0);
    if(changed & KEY_X) Keyboard::feed(Keyboard::KEY_C, (kHeld & KEY_X) ? 1 : 0);
    if(changed & KEY_B) Keyboard::feed(Keyboard::KEY_ESCAPE, (kHeld & KEY_B) ? 1 : 0);
    if(changed & KEY_Y) Keyboard::feed(Keyboard::KEY_E, (kHeld & KEY_Y) ? 1 : 0);
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

    //FILE* log_file = fopen("sdmc:/3ds/minecraftpe/debug_log.txt", "w");
    //if (log_file) {
    //    static char logBuffer[16 * 1024];
    //    setvbuf(log_file, logBuffer, _IOFBF, sizeof(logBuffer));
    //    dup2(fileno(log_file), STDOUT_FILENO);
    //    dup2(fileno(log_file), STDERR_FILENO);
    //}

    networkInit();
    irrstInit();

    MAIN_CLASS* app = new MAIN_CLASS();

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

            if ((hidKeysHeld() & KEY_START) && (hidKeysHeld() & KEY_SELECT)) break;

            handleTouch();
            handleController();
        }

        {
            FrameProf::Scoped _s_update("01.app_update");
            app->update();
        }

        {
            FrameProf::Scoped _s_swap("02.swap");
            novaSwapBuffers();
        }

        if (frameCounter % 600 == 0) {
            printMemoryStats();
        }
        frameCounter++;

        // Считаем дедлайн следующего кадра от ПРОШЛОГО дедлайна, а не от
        // "сейчас" — так не накапливается дрейф из-за пары быстрых кадров
        // подряд, и средний FPS точно держится 30. Дедлайн в "system ticks":
        // ticks = ns * (TicksPerSec / 1e9).
        const u64 kTargetFrameTicks = (kTargetFrameNs * kSysTicksPerSec) / 1000000000ULL;
        nextFrameTick += kTargetFrameTicks;
        u64 now = svcGetSystemTick();
        if (now < nextFrameTick) {
            FrameProf::Scoped _s_sleep("03.frame_sleep");
            // Осталось до дедлайна — спим. Конвертируем тики обратно в ns.
            u64 remainingTicks = nextFrameTick - now;
            s64 remainingNs = (s64)((remainingTicks * 1000000000ULL) / kSysTicksPerSec);
            if (remainingNs > 0) svcSleepThread(remainingNs);
        } else {
            // Опоздали — сбрасываем дедлайн на текущий момент, чтобы не
            // пытаться "догонять" пачкой быстрых кадров (это даёт рывки).
            nextFrameTick = now;
        }

        //FrameProf::endFrame();
    }

    deinitGraphics();

    irrstExit();
    networkExit();
    romfsExit();

    //if (log_file) fclose(log_file);

    return 0;
}

#endif //MINECRAFTCPP_MAIN_CTR_H
