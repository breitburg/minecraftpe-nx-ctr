#include "TouchCreateWorldScreen.h"
#include "../ProgressScreen.h"
#include "../../../Minecraft.h"
#include "../../Font.h"
#include "../../../renderer/Tesselator.h"
#include "../../../renderer/Textures.h"
#include "../../../renderer/TextureData.h"
#include "../../../../util/Mth.h"

namespace Touch {



static char ILLEGAL_FILE_CHARACTERS[] = {
    '/', '\n', '\r', '\t', '\0', '\f', '`', '?', '*', '\\', '<', '>', '|', '\"', ':'
};

CreateWorldScreen::CreateWorldScreen()
:   bHeader(0, "World name"),
    bBack(1, "Back"),
    bStart(2, "Start!"),
    bLevelName(3, "Unnamed world"),
    bGameMode(4, "Gamemode"),
    bSeed(5, "")
{
}

CreateWorldScreen::~CreateWorldScreen()
{
}

void CreateWorldScreen::init() {
    buttons.push_back(&bHeader);
    buttons.push_back(&bBack);
    buttons.push_back(&bStart);
    buttons.push_back(&bLevelName);
    buttons.push_back(&bGameMode);
    buttons.push_back(&bSeed);
}

void CreateWorldScreen::setupPositions() {

    int padding = 30;

    // Кнопки этого экрана компактнее дефолтного TButton (78x30) — иначе
    // верхняя панель и выбор режима занимают слишком много места.
    bBack.width  = bStart.width  = 64;
    bBack.height = bStart.height = 24;
    bGameMode.width  = 60;
    bGameMode.height = 22;

    bBack.x = 0;
    bBack.y = 0;

    bStart.y = 0;
    bStart.x = width - bStart.width;

    bHeader.x = bBack.width;
    bHeader.width = width - (bBack.width + bStart.width);
    bHeader.height = bStart.height;

    bLevelName.height = bHeader.height;
    bLevelName.width = width - (padding*2);
    bLevelName.x = padding;
    bLevelName.y = (bHeader.height + (height/3 - 50));

    bGameMode.x = (width - bGameMode.width) / 2;
    bGameMode.y = (2*height/3 - 50);

    bSeed.width = width - (padding*2);
    bSeed.height = bHeader.height;
    bSeed.x = padding;
    bSeed.y = (height - 50);
}

void CreateWorldScreen::tick() {
    bGameMode.msg = (this->gameType == GameType::Creative) ? "Creative" : "Survival";
}

#ifdef __3DS__
bool CreateWorldScreen::renderOnTopScreen3ds() { return true; }
#endif

void CreateWorldScreen::render( int xm, int ym, float a )
{
    renderDirtBackground(0);
    glEnable2(GL_BLEND);

#ifdef __3DS__
    // Верхний экран: лого + заголовок, чтобы экран не оставался пустым.
    if (Screen::s_isRenderingTopScreen3ds) {
        TextureId id = minecraft->textures->loadTexture("gui/title.png");
        const TextureData* data = minecraft->textures->getTemporaryTextureData(id);
        if (data) {
            minecraft->textures->bind(id);
            const float cx = (float)width / 2.0f;
            const float cy = (float)height * 0.38f;
            const float wh = Mth::Min((float)width * 0.45f, (float)data->w);
            const float scale = 2.0f * wh / (float)data->w;
            const float h = scale * (float)data->h;
            const float top = cy - h / 2.0f;

            Tesselator& t = Tesselator::instance;
            glColor4f2(1, 1, 1, 1);
            t.begin();
                t.vertexUV(cx-wh, top+h, blitOffset, 0, 1);
                t.vertexUV(cx+wh, top+h, blitOffset, 1, 1);
                t.vertexUV(cx+wh, top+0, blitOffset, 1, 0);
                t.vertexUV(cx-wh, top+0, blitOffset, 0, 0);
            t.draw();
        }
        const char* caption = "Create New World";
        int tw = minecraft->font->width(caption);
        drawString(font, caption, (width - tw) / 2,
            (int)((float)height * 0.38f) + 40, 0xffffdd55);
        glDisable2(GL_BLEND);
        return;
    }
#endif

    drawCenteredString(minecraft->font, (this->gameType == GameType::Creative) ? "Unlimited resources and flying" : "Mobs, health and gather resources", width/2, bGameMode.y + bGameMode.height + 3, 0xffcccccc);
    drawCenteredString(minecraft->font, "World Generator seed, Leave blank for random.", width/2, bSeed.y - 10, 0xffcccccc);
    drawCenteredString(minecraft->font, "Leave blank for random seed", width/2, bSeed.y + bSeed.height + 3, 0xffcccccc);

    Screen::render(xm, ym, a);
    glDisable2(GL_BLEND);
}

void CreateWorldScreen::buttonClicked(Button* button) {
    if(button == &bBack) {
        minecraft->screenChooser.setScreen(SCREEN_SELECTWORLD);
        return;
    }

    if(button == &bGameMode) {
        // swap game types
        if(this->gameType == GameType::Creative) {
            this->gameType = GameType::Survival;
        } else {
            this->gameType = GameType::Creative;
        }
        return;
    }

    if(button == &bStart) {

        // Read the level name.
        // 1) Trim name 2) Remove all bad chars 3) Append '-' chars 'til the name is unique
        std::string levelName = bLevelName.text;
        std::string levelId = levelName;

        for (int i = 0; i < sizeof(ILLEGAL_FILE_CHARACTERS) / sizeof(char); ++i)
            levelId = Util::stringReplace(levelId, std::string(1, ILLEGAL_FILE_CHARACTERS[i]), "");
                if ((int)levelId.length() == 0) {
                    levelId = "no_name";
            }
            levelId = getUniqueLevelName(levelId);

            // Read the seed
            int seed = getEpochTimeS();
            if (!bSeed.text.empty()) {
                std::string seedString = Util::stringTrim(bSeed.text);
                if (seedString.length() > 0) {
                    int tmpSeed;
                    // Try to read it as an integer
                    if (sscanf(seedString.c_str(), "%d", &tmpSeed) > 0) {
                        seed = tmpSeed;
                    } // Hash the "seed"
                    else {
                        seed = Util::hashCode(seedString);
                    }
                }
            }
            // Read the game mode
            bool isCreative = this->gameType == GameType::Creative;

            // Start a new level with the given name and seed
            LOGI("Creating a level with id '%s', name '%s' and seed '%d'\n", levelId.c_str(), levelName.c_str(), seed);
            LevelSettings settings(seed, isCreative? GameType::Creative : GameType::Survival);
            minecraft->selectLevel(levelId, levelName, settings);
            minecraft->hostMultiplayer();
            minecraft->setScreen(new ProgressScreen());

        return;
    }
}

bool CreateWorldScreen::handleBackEvent(bool isDown)
{
    minecraft->screenChooser.setScreen(SCREEN_SELECTWORLD);
    return true;
}

};
