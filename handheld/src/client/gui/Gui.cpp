#include "Gui.h"
#include "Font.h"
#include "screens/IngameBlockSelectionScreen.h"
#include "../Minecraft.h"
#include "../player/LocalPlayer.h"
#include "../renderer/Tesselator.h"
#include "../renderer/TileRenderer.h"
#include "../renderer/LevelRenderer.h"
#include "../renderer/GameRenderer.h"
#include "../renderer/entity/ItemRenderer.h"
#include "../player/input/IInputHolder.h"
#include "../gamemode/GameMode.h"
#include "../gamemode/CreativeMode.h"
#include "../renderer/Textures.h"
#include "../../AppConstants.h"
#include "../../world/entity/player/Inventory.h"
#include "../../world/level/material/Material.h"
#include "../../world/item/Item.h"
#include "../../world/item/ItemInstance.h"
#include "../../platform/input/Mouse.h"
#include "../../world/level/Level.h"
#include "../../world/level/chunk/LevelChunk.h"
#include "../../world/level/tile/Tile.h"
#include "../../util/Mth.h"
#include "../../world/PosTranslator.h"
#include <climits>
#include <cstdio>

float Gui::InvGuiScale = 1.0f / 3.0f;
float Gui::GuiScale = 1.0f / Gui::InvGuiScale;
float Gui::ScissorScaleX = Gui::GuiScale;
float Gui::ScissorScaleY = Gui::GuiScale;
const float Gui::DropTicks = 40.0f;

//#include <android/log.h>

Gui::Gui(Minecraft* minecraft)
:	minecraft(minecraft),
	tickCount(0),
	progress(0),
	overlayMessageTime(0),
	animateOverlayMessageColor(false),
	tbr(1),
	_inventoryNeedsUpdate(true),
	_flashSlotId(-1),
	_flashSlotStartTime(-1),
	_slotFont(NULL),
	_numSlots(4),
	_currentDropTicks(-1),
	_currentDropSlot(-1),
	MAX_MESSAGE_WIDTH(240),
	itemNameOverlayTime(2)
#ifdef __3DS__
	, _minimapTexture(0),
	_minimapChunkX(INT_MAX),
	_minimapChunkZ(INT_MAX),
	_minimapReady(false)
#endif
{
	glGenBuffers2(1, &_inventoryRc.vboId);
	glGenBuffers2(1, &rcFeedbackInner.vboId);
	glGenBuffers2(1, &rcFeedbackOuter.vboId);
	//Gui::InvGuiScale = 1.0f / (int) (3 * Minecraft::width / 854);
}

Gui::~Gui()
{
#ifdef __3DS__
	if (_minimapTexture) {
		GLuint tex = (GLuint)_minimapTexture;
		glDeleteTextures(1, &tex);
		_minimapTexture = 0;
	}
#endif
	if (_slotFont)
		delete _slotFont;
	glDeleteBuffers(1, &_inventoryRc.vboId);
}

void Gui::render(float a, bool mouseFree, int xMouse, int yMouse) {
	(void)mouseFree;
	(void)xMouse;
	(void)yMouse;

	renderInGameHud(a, true, true);
}

#ifdef __3DS__
void Gui::renderTopHud(float a) {
	renderInGameHud(a, true, false);
}

void Gui::renderBottomHotbar(float a) {
	renderInGameHud(a, false, true);
}

// Земляной фон под хотбар на нижнем экране, как в меню.
// Тайлится gui/background.png; цвет приглушённый, чтобы предметы хотбара
// читались сверху.
void Gui::renderBottomDirt(float a) {
	(void)a;

	const int screenWidth  = (int)(minecraft->width  * InvGuiScale);
	const int screenHeight = (int)(minecraft->height * InvGuiScale);

	glDisable2(GL_FOG);
	glDisable2(GL_ALPHA_TEST);
	glDisable2(GL_BLEND);
	glColor4f2(1, 1, 1, 1);
	minecraft->textures->loadAndBindTexture("gui/background.png");

	Tesselator& t = Tesselator::instance;
	const float s = 32.0f;
	// Z=-200: глубже хотбара (blitOffset=-90) и тач-инпута, чтобы они
	// отрисовывались поверх. Без этого землянка перекрывает слоты.
	const float bgZ = -200.0f;
	t.begin();
	t.color(0x606060);
	t.vertexUV(0.0f,                 (float)screenHeight, bgZ, 0.0f,                 (float)screenHeight / s);
	t.vertexUV((float)screenWidth,   (float)screenHeight, bgZ, (float)screenWidth/s, (float)screenHeight / s);
	t.vertexUV((float)screenWidth,   0.0f,                bgZ, (float)screenWidth/s, 0.0f);
	t.vertexUV(0.0f,                 0.0f,                bgZ, 0.0f,                 0.0f);
	t.draw();

	glEnable2(GL_ALPHA_TEST);
	glEnable2(GL_BLEND);
}

// Хотбар на верхнем экране — вызывается когда нижний экран занят
// инвентарём/крафтом/креатив-меню.
//
// Внутри renderToolBar() прибиты гвоздями getSlotPos()/getHotbarYSlot(),
// которые на 3DS всегда дают y=3 (верх нижнего экрана). Если вызвать
// renderToolBar(ySlot=58, ...) — слот-фреймы окажутся на y=3 (старая
// позиция), а предметы — на y=58 (новая). Получается раздвоение, что и
// видно в Креатив-меню: пустые рамки сверху, иконки снизу.
//
// Решение — не править renderToolBar, а просто сдвинуть всю
// model-view матрицу на нужный delta. Тогда фреймы, селект, предметы,
// счётчики едут вниз одним блоком.
void Gui::renderHotbarOnTop(float a) {
	if (!minecraft->level || !minecraft->player)
		return;

	const int screenWidth  = (int)(minecraft->width  * InvGuiScale);
	const int screenHeight = (int)(minecraft->height * InvGuiScale);

	const int defaultYSlot = getHotbarYSlot(screenHeight);   // 6 на 3DS
	const int wantYSlot    = screenHeight - 22;              // y нижней кромки
	const float dy         = (float)(wantYSlot - defaultYSlot);

	glPushMatrix();
	glTranslatef2(0.0f, dy, 0.0f);
	renderToolBar(a, defaultYSlot, screenWidth);
	glPopMatrix();
}

// Простой палеточный mapping tile-id → RGB-цвет для мини-карты. Если тайла
// нет в таблице, спрашиваем у Tile::getColor (medium-cost). Воздух = 0.
// Возвращает ARGB (alpha=ff в верхних битах для непрозрачности).
static unsigned int minimap_color_for_tile(int tileId, Level* level, int x, int y, int z) {
	if (tileId <= 0) return 0;
	switch (tileId) {
		case 1:  return 0xff7f7f7f; // stone
		case 2:  return 0xff5db050; // grass
		case 3:  return 0xff8b6038; // dirt
		case 4:  return 0xff606060; // cobblestone
		case 5:  return 0xffb88f4f; // planks
		case 7:  return 0xff202020; // bedrock
		case 8: case 9:   return 0xff3060c0; // water
		case 10: case 11: return 0xffe05010; // lava
		case 12: return 0xffded3a3; // sand
		case 13: return 0xff8a8378; // gravel
		case 14: return 0xff9d8060; // gold ore
		case 15: return 0xff806d5f; // iron ore
		case 16: return 0xff333333; // coal ore
		case 17: return 0xff6b4f2e; // log
		case 18: return 0xff406030; // leaves
		case 20: return 0xffc0d8e0; // glass
		case 24: return 0xffd9d294; // sandstone
		case 31: return 0xff63a14a; // tallgrass
		case 35: return 0xffe0e0e0; // wool
		case 38: case 37: return 0xffe04050; // flower red / yellow
		case 41: return 0xffe9d24c; // gold block
		case 42: return 0xffdddddd; // iron block
		case 49: return 0xff1a0d30; // obsidian
		case 56: return 0xff7ad6dc; // diamond ore
		case 78: return 0xffffffff; // snow layer (top snow)
		case 79: return 0xffa0c0e0; // ice
		case 80: return 0xffeeeeff; // snow
		case 82: return 0xff9099a9; // clay
		default: {
			Tile* tile = Tile::tiles[tileId];
			if (!tile) return 0;
			int c = tile->getColor(level, x, y, z);
			return (unsigned int)(c | 0xff000000);
		}
	}
}

static int minimap_floor_chunk(int block) {
	return block >= 0 ? block / 16 : (block - 15) / 16;
}

static unsigned int minimap_argb_to_abgr(unsigned int argb) {
	return (argb & 0xff00ff00)
		| ((argb & 0x00ff0000) >> 16)
		| ((argb & 0x000000ff) << 16);
}

void Gui::buildWorldMinimap() {
	if (!minecraft->level || !minecraft->player) {
		_minimapReady = false;
		return;
	}

	Level* level = minecraft->level;
	const int playerBlockX = Mth::floor(minecraft->player->x);
	const int playerBlockZ = Mth::floor(minecraft->player->z);
	const int playerChunkX = minimap_floor_chunk(playerBlockX);
	const int playerChunkZ = minimap_floor_chunk(playerBlockZ);
	const int startBlockX = (playerChunkX - 1) * 16;
	const int startBlockZ = (playerChunkZ - 1) * 16;

	int cachedCx = INT_MAX;
	int cachedCz = INT_MAX;
	LevelChunk* cachedChunk = NULL;

	for (int i = 0; i < kMinimapTextureSize * kMinimapTextureSize; i++)
		_minimapPixels[i] = 0xff181820;

	for (int my = 0; my < kMinimapInnerSize; my++) {
		for (int mx = 0; mx < kMinimapInnerSize; mx++) {
			const int wx = startBlockX + mx;
			const int wz = startBlockZ + my;
			const int cx = minimap_floor_chunk(wx);
			const int cz = minimap_floor_chunk(wz);

			if (cx != cachedCx || cz != cachedCz) {
				cachedCx = cx;
				cachedCz = cz;
				cachedChunk = level->hasChunk(cx, cz) ? level->getChunk(cx, cz) : NULL;
			}

			unsigned int abgr = 0xff181820;
			if (cachedChunk) {
				const int lx = wx - cx * 16;
				const int lz = wz - cz * 16;
				const int h = cachedChunk->getHeightmap(lx, lz);
				if (h > 0) {
					int tileId = cachedChunk->getTile(lx, h - 1, lz);
					// Тонкий снежный слой (topSnow) часто не поднимает heightmap —
					// поверхностью числится трава под ним. Подглядываем на блок
					// выше: если там снег, рисуем точку белой.
					if (h < 128 && cachedChunk->getTile(lx, h, lz) == 78)
						tileId = 78;
					const unsigned int argb = minimap_color_for_tile(tileId, level, wx, h - 1, wz);
					if ((argb & 0xff000000) != 0)
						abgr = minimap_argb_to_abgr(argb);
				}
			}

			_minimapPixels[mx + my * kMinimapTextureSize] = abgr;
		}
	}

	if (!_minimapTexture) {
		GLuint tex = 0;
		glGenTextures(1, &tex);
		_minimapTexture = tex;
		glBindTexture2(GL_TEXTURE_2D, tex);
		glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri2(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTexImage2D2(GL_TEXTURE_2D, 0, GL_RGBA, kMinimapTextureSize, kMinimapTextureSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, _minimapPixels);
	} else {
		glBindTexture2(GL_TEXTURE_2D, (GLuint)_minimapTexture);
		glTexSubImage2D2(GL_TEXTURE_2D, 0, 0, 0, kMinimapTextureSize, kMinimapTextureSize, GL_RGBA, GL_UNSIGNED_BYTE, _minimapPixels);
	}

	_minimapChunkX = playerChunkX;
	_minimapChunkZ = playerChunkZ;
	_minimapReady = true;
}

void Gui::renderWorldMinimap(float a) {
	(void)a;
	if (!minecraft->level || !minecraft->player) return;

	const int screenWidth  = (int)(minecraft->width  * InvGuiScale);
	const int screenHeight = (int)(minecraft->height * InvGuiScale);

	const int mapInner = kMinimapInnerSize;
	const int mapOuter = kMinimapSize;
	const int x0 = screenWidth - mapOuter - 4;
	const int y0 = screenHeight - mapOuter - 4;
	const int ix0 = x0 + 1;
	const int iy0 = y0 + 1;
	const int panelY0 = getHotbarYSlot(screenHeight) + 25;
	const int coordY0 = panelY0;
	const int coordY1 = y0 - 2;
	const bool hasCoordPanel = (coordY1 - coordY0) >= 24;

	glDisable2(GL_TEXTURE_2D);
	glDisable2(GL_ALPHA_TEST);
	glEnable2(GL_BLEND);
	glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	Tesselator& t = Tesselator::instance;

	const int playerBlockX = Mth::floor(minecraft->player->x);
	const int playerBlockY = Mth::floor(minecraft->player->y);
	const int playerBlockZ = Mth::floor(minecraft->player->z);
	const int playerChunkX = minimap_floor_chunk(playerBlockX);
	const int playerChunkZ = minimap_floor_chunk(playerBlockZ);
	const int startBlockX = (playerChunkX - 1) * 16;
	const int startBlockZ = (playerChunkZ - 1) * 16;

	if (!_minimapReady || _minimapChunkX != playerChunkX || _minimapChunkZ != playerChunkZ)
		buildWorldMinimap();

	const float pxF = Mth::clamp(minecraft->player->x - (float)startBlockX, 0.0f, (float)(mapInner - 1));
	const float pzF = Mth::clamp(minecraft->player->z - (float)startBlockZ, 0.0f, (float)(mapInner - 1));
	const float pix = (float)ix0 + pxF;
	const float piy = (float)iy0 + pzF;
	const bool haveTex = (_minimapReady && _minimapTexture != 0);

	int infoY0 = hasCoordPanel ? coordY0 : y0 + 2;
	int infoY1 = hasCoordPanel ? coordY1 : y0 + 29;
	if (infoY1 > y0 + mapOuter - 2)
		infoY1 = y0 + mapOuter - 2;
	const bool hasInfoPanel = (infoY1 - infoY0 >= 20);

	// Каждый t.begin()/t.draw() — это отдельный glBufferData+glDrawArrays, на
	// 3DS дорогой. Поэтому всю неотекстуренную геометрию мини-карты сливаем в
	// два батча: «под картой» и «поверх карты». Цвет в Tesselator — атрибут
	// вершины, так что разные цвета в одном батче не мешают.

	// --- Батч 1: фон-плашки под картой ---
	t.begin();
	if (panelY0 < y0 - 2) {
		t.colorABGR(0x90202028);
		t.vertex((float)x0,              (float)(y0 - 2), 0);
		t.vertex((float)(x0 + mapOuter), (float)(y0 - 2), 0);
		t.vertex((float)(x0 + mapOuter), (float)panelY0,  0);
		t.vertex((float)x0,              (float)panelY0,  0);
		t.colorABGR(0xff606078);
		t.vertex((float)x0,              (float)(panelY0 + 1), 0);
		t.vertex((float)(x0 + mapOuter), (float)(panelY0 + 1), 0);
		t.vertex((float)(x0 + mapOuter), (float)panelY0,       0);
		t.vertex((float)x0,              (float)panelY0,       0);
	}
	t.colorABGR(0xff000000);
	t.vertex((float)x0,              (float)(y0 + mapOuter), 0);
	t.vertex((float)(x0 + mapOuter), (float)(y0 + mapOuter), 0);
	t.vertex((float)(x0 + mapOuter), (float)y0,             0);
	t.vertex((float)x0,              (float)y0,             0);
	if (!haveTex) {
		t.colorABGR(0xff181820);
		t.vertex((float)ix0,              (float)(iy0 + mapInner), 0);
		t.vertex((float)(ix0 + mapInner), (float)(iy0 + mapInner), 0);
		t.vertex((float)(ix0 + mapInner), (float)iy0,              0);
		t.vertex((float)ix0,              (float)iy0,              0);
	}
	t.draw();

	// --- Карта (текстура) — отдельный батч, нужен GL_TEXTURE_2D ---
	if (haveTex) {
		const float uv = (float)kMinimapInnerSize / (float)kMinimapTextureSize;
		glEnable2(GL_TEXTURE_2D);
		glColor4f2(1, 1, 1, 1);
		glBindTexture2(GL_TEXTURE_2D, (GLuint)_minimapTexture);
		t.begin();
		t.vertexUV((float)ix0,              (float)(iy0 + mapInner), 0, 0.0f, uv);
		t.vertexUV((float)(ix0 + mapInner), (float)(iy0 + mapInner), 0, uv,   uv);
		t.vertexUV((float)(ix0 + mapInner), (float)iy0,              0, uv,   0.0f);
		t.vertexUV((float)ix0,              (float)iy0,              0, 0.0f, 0.0f);
		t.draw();
		glDisable2(GL_TEXTURE_2D);
	}

	// --- Батч 2: всё поверх карты — сетка, маркер игрока, плашка координат ---
	t.begin();
	t.colorABGR(0x90000000);
	for (int grid = 16; grid <= 32; grid += 16) {
		const float gx = (float)(ix0 + grid);
		const float gy = (float)(iy0 + grid);
		t.vertex(gx,        (float)(iy0 + mapInner), 0);
		t.vertex(gx + 1.0f, (float)(iy0 + mapInner), 0);
		t.vertex(gx + 1.0f, (float)iy0,              0);
		t.vertex(gx,        (float)iy0,              0);
		t.vertex((float)ix0,              gy + 1.0f, 0);
		t.vertex((float)(ix0 + mapInner), gy + 1.0f, 0);
		t.vertex((float)(ix0 + mapInner), gy,        0);
		t.vertex((float)ix0,              gy,        0);
	}
	// Маркер игрока — зелёная точка с тёмной обводкой для контраста.
	t.colorABGR(0xff000000);
	t.vertex(pix - 3.0f, piy + 4.0f, 0);
	t.vertex(pix + 4.0f, piy + 4.0f, 0);
	t.vertex(pix + 4.0f, piy - 3.0f, 0);
	t.vertex(pix - 3.0f, piy - 3.0f, 0);
	t.colorABGR(0xff30d030);
	t.vertex(pix - 2.0f, piy + 3.0f, 0);
	t.vertex(pix + 3.0f, piy + 3.0f, 0);
	t.vertex(pix + 3.0f, piy - 2.0f, 0);
	t.vertex(pix - 2.0f, piy - 2.0f, 0);
	if (hasInfoPanel) {
		t.colorABGR(0xd0202028);
		t.vertex((float)x0,              (float)infoY1, 0);
		t.vertex((float)(x0 + mapOuter), (float)infoY1, 0);
		t.vertex((float)(x0 + mapOuter), (float)infoY0, 0);
		t.vertex((float)x0,              (float)infoY0, 0);
		t.colorABGR(0xff606078);
		t.vertex((float)x0,              (float)(infoY0 + 1), 0);
		t.vertex((float)(x0 + mapOuter), (float)(infoY0 + 1), 0);
		t.vertex((float)(x0 + mapOuter), (float)infoY0,       0);
		t.vertex((float)x0,              (float)infoY0,       0);
	}
	t.draw();

	// Right-side rail: readable coordinates above the chunk minimap.
	glEnable2(GL_TEXTURE_2D);
	glEnable2(GL_ALPHA_TEST);

	// Мини-карта биндила свою GL-текстуру напрямую, в обход Textures —
	// кэш менеджера протух. Сбрасываем его, иначе Font::draw() решит, что
	// текстура шрифта уже привязана, и текст отрендерится кусками карты.
	minecraft->textures->resetBoundTexture();

	Font* f = minecraft->font;
	if (f && infoY1 - infoY0 >= 20) {
		char buf[32];
		const int textX = x0 + 3;
		int textY = infoY0 + 3;
		const int lineH = 8;

		snprintf(buf, sizeof(buf), "X:%d", playerBlockX);
		f->drawShadow(std::string(buf), textX, textY, 0xffffffff);
		textY += lineH;
		snprintf(buf, sizeof(buf), "Y:%d", playerBlockY);
		f->drawShadow(std::string(buf), textX, textY, 0xffffffff);
		textY += lineH;
		snprintf(buf, sizeof(buf), "Z:%d", playerBlockZ);
		f->drawShadow(std::string(buf), textX, textY, 0xffffffff);
		textY += lineH;
		if (textY + lineH <= infoY1) {
			snprintf(buf, sizeof(buf), "C:%d,%d", playerChunkX, playerChunkZ);
			f->drawShadow(std::string(buf), textX, textY, 0xffaaccff);
		}
	}
}

// Hint-плашка "Cam Zone" в левой-нижней части bottom-screen — показывает
// игроку, что свободная область используется для управления камерой стилусом.
// Шейп: octagon-like (прямоугольник + горизонтальная полоса = крест без углов)
// → выглядит как rect со скруглёнными углами 2px.
void Gui::getControlButtonRect(int which, int& x0, int& y0, int& x1, int& y1) {
	const int screenWidth  = (int)(minecraft->width  * InvGuiScale);
	const int screenHeight = (int)(minecraft->height * InvGuiScale);
	const int mapX0 = screenWidth - kMinimapSize - 4;
	const int gap = 4;
	const int px0 = 4;
	const int px1 = mapX0 - gap;
	const int py0 = getHotbarYSlot(screenHeight) + 25;
	const int py1 = screenHeight - 4;
	const int mid = (px0 + px1) / 2;
	if (which == 1) { x0 = px0;     y0 = py0; x1 = mid - 2; y1 = py1; } // Jump
	else            { x0 = mid + 2; y0 = py0; x1 = px1;     y1 = py1; } // Inventory
}

int Gui::controlButtonAt(int mouseX, int mouseY) {
	if (!minecraft->options.xybaCamera) return 0;
	if (!minecraft->level || !minecraft->player) return 0;
	// Кнопки рисуются только когда нижний экран не занят меню/инвентарём.
	if (minecraft->screen != NULL) return 0;
	// mouseX/Y приходят в сыром Mouse-пространстве — переводим в GUI-координаты
	// тем же множителем, что и getSlotIdAt().
	const int gx = (int)(mouseX * InvGuiScale);
	const int gy = (int)(mouseY * InvGuiScale);
	for (int which = 1; which <= 2; which++) {
		int bx0, by0, bx1, by1;
		getControlButtonRect(which, bx0, by0, bx1, by1);
		if (gx >= bx0 && gx < bx1 && gy >= by0 && gy < by1)
			return which;
	}
	return 0;
}

void Gui::renderCamZoneHint(float a) {
	(void)a;
	if (!minecraft->level || !minecraft->player) return;

	const int screenWidth  = (int)(minecraft->width  * InvGuiScale);
	const int screenHeight = (int)(minecraft->height * InvGuiScale);

	const int mapX0 = screenWidth - kMinimapSize - 4;
	const int gap = 4;
	const int x0 = 4;
	const int x1 = mapX0 - gap;
	const int y0 = getHotbarYSlot(screenHeight) + 25;
	const int y1 = screenHeight - 4;

	if (x1 - x0 < 24 || y1 - y0 < 12) return; // слишком мало места

	const int r = 2; // chamfer-radius

	glDisable2(GL_TEXTURE_2D);
	glDisable2(GL_ALPHA_TEST);
	glEnable2(GL_BLEND);
	glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	Tesselator& t = Tesselator::instance;

	// Схема "XYBA = камера": нижний экран занят не подсказкой Cam Zone, а
	// двумя тач-кнопками — Jump (слева, удержание) и Inventory (справа, тап).
	if (minecraft->options.xybaCamera) {
		int b[2][4];
		getControlButtonRect(1, b[0][0], b[0][1], b[0][2], b[0][3]);
		getControlButtonRect(2, b[1][0], b[1][1], b[1][2], b[1][3]);

		t.begin();
		for (int i = 0; i < 2; i++) {
			const float bx0 = (float)b[i][0], by0 = (float)b[i][1];
			const float bx1 = (float)b[i][2], by1 = (float)b[i][3];
			t.colorABGR(0xb0303040); // заливка
			t.vertex(bx0, by1, 0); t.vertex(bx1, by1, 0);
			t.vertex(bx1, by0, 0); t.vertex(bx0, by0, 0);
			t.colorABGR(0xff707088); // рамка 1px
			t.vertex(bx0, by0 + 1, 0); t.vertex(bx1, by0 + 1, 0);
			t.vertex(bx1, by0,     0); t.vertex(bx0, by0,     0);
			t.vertex(bx0, by1,     0); t.vertex(bx1, by1,     0);
			t.vertex(bx1, by1 - 1, 0); t.vertex(bx0, by1 - 1, 0);
			t.vertex(bx0,     by1, 0); t.vertex(bx0 + 1, by1, 0);
			t.vertex(bx0 + 1, by0, 0); t.vertex(bx0,     by0, 0);
			t.vertex(bx1 - 1, by1, 0); t.vertex(bx1,     by1, 0);
			t.vertex(bx1,     by0, 0); t.vertex(bx1 - 1, by0, 0);
		}
		t.draw();

		glEnable2(GL_TEXTURE_2D);
		glEnable2(GL_ALPHA_TEST);

		Font* f = minecraft->font;
		if (f) {
			const char* labels[2] = { "Jump", "Inventory" };
			for (int i = 0; i < 2; i++) {
				int tw = f->width(labels[i]);
				int tx = (b[i][0] + b[i][2]) / 2 - tw / 2;
				int ty = (b[i][1] + b[i][3]) / 2 - 4;
				f->drawShadow(labels[i], tx, ty, 0xffe8e8f0);
			}
		}
		return;
	}

	// "Скруглённый" rect = два overlap'ящих rect-а (вертикальный + горизонтальный),
	// углы по r×r остаются "обкусанными" — выглядит как rounded.
	// Заливка + рамка одним батчем — на 3DS каждый draw-call дорогой.
	t.begin();
	t.colorABGR(0x90202028); // полупрозрачный тёмно-синий
	// Узкий вертикальный (центральный)
	t.vertex(x0 + r, y1,     0);
	t.vertex(x1 - r, y1,     0);
	t.vertex(x1 - r, y0,     0);
	t.vertex(x0 + r, y0,     0);
	// Широкий горизонтальный (центральный по высоте)
	t.vertex(x0,     y1 - r, 0);
	t.vertex(x1,     y1 - r, 0);
	t.vertex(x1,     y0 + r, 0);
	t.vertex(x0,     y0 + r, 0);

	// 1-пиксельная рамка чуть посветлее — обводим тот же шейп.
	t.colorABGR(0xff606078);
	// Верх (между chamfer'ами)
	t.vertex(x0 + r, y0 + 1, 0); t.vertex(x1 - r, y0 + 1, 0);
	t.vertex(x1 - r, y0,     0); t.vertex(x0 + r, y0,     0);
	// Низ
	t.vertex(x0 + r, y1,     0); t.vertex(x1 - r, y1,     0);
	t.vertex(x1 - r, y1 - 1, 0); t.vertex(x0 + r, y1 - 1, 0);
	// Левый бок
	t.vertex(x0,     y1 - r, 0); t.vertex(x0 + 1, y1 - r, 0);
	t.vertex(x0 + 1, y0 + r, 0); t.vertex(x0,     y0 + r, 0);
	// Правый бок
	t.vertex(x1 - 1, y1 - r, 0); t.vertex(x1,     y1 - r, 0);
	t.vertex(x1,     y0 + r, 0); t.vertex(x1 - 1, y0 + r, 0);
	t.draw();

	glEnable2(GL_TEXTURE_2D);
	glEnable2(GL_ALPHA_TEST);

	// Заполняем панель полезной инфой: координаты + направление взгляда.
	// Шрифт ~8px tall, нативный размер — читаемо. Зона тача камеры при этом
	// остаётся (текст не перехватывает stylus), просто перестаёт быть пустой.
	Font* f = minecraft->font;
	if (f) {
		const char* msg = "Cam Zone";
		int tw = f->width(msg);
		int tx = (x0 + x1) / 2 - tw / 2;
		int ty = (y0 + y1) / 2 - 4;
		f->drawShadow(msg, tx, ty, 0xffd0d0e0);
	}
	return;
#if 0
	if (!f) return;

	const int padL = 5;       // отступ слева
	const int padT = 3;       // отступ сверху
	const int lineH = 9;      // высота строки (шрифт 8 + 1px зазор)
	int textX = x0 + padL;
	int textY = y0 + padT;

	// Заголовок-маркер "Cam Zone" мелким акцентным цветом — пользователь
	// помнит, что это всё ещё touch-area для камеры.
	const char* hdr = "Cam Zone";
	int hdrW = f->width(hdr);
	f->drawShadow(hdr, x1 - hdrW - padL, textY, 0xff909098);

	// Игрок: координаты блока (целочисленные).
	const int px = Mth::floor(minecraft->player->x);
	const int py = Mth::floor(minecraft->player->y);
	const int pz = Mth::floor(minecraft->player->z);

	char buf[32];
	int ry = textY;

	// Подпись + значения по столбцу — крупно и читаемо.
	f->drawShadow("Coords:", textX, ry, 0xffc8c8e0);
	ry += lineH;
	snprintf(buf, sizeof(buf), "X %d", px);
	f->drawShadow(buf, textX, ry, 0xffffffff);
	ry += lineH;
	snprintf(buf, sizeof(buf), "Y %d", py);
	f->drawShadow(buf, textX, ry, 0xffffffff);
	ry += lineH;
	snprintf(buf, sizeof(buf), "Z %d", pz);
	f->drawShadow(buf, textX, ry, 0xffffffff);
	ry += lineH + 2; // небольшой разделитель

	// Направление взгляда. yRot в MC: 0=юг, 90=запад, 180=север, 270=восток.
	float yaw = minecraft->player->yRot;
	yaw = yaw - 360.0f * Mth::floor(yaw / 360.0f); // нормализация в [0, 360)
	const char* dir;
	if      (yaw < 22.5f  || yaw >= 337.5f) dir = "S";
	else if (yaw < 67.5f )                  dir = "SW";
	else if (yaw < 112.5f)                  dir = "W";
	else if (yaw < 157.5f)                  dir = "NW";
	else if (yaw < 202.5f)                  dir = "N";
	else if (yaw < 247.5f)                  dir = "NE";
	else if (yaw < 292.5f)                  dir = "E";
	else                                    dir = "SE";

	if (ry + lineH <= y1) {
		snprintf(buf, sizeof(buf), "Facing  %s", dir);
		f->drawShadow(buf, textX, ry, 0xffaaccff);
		ry += lineH;
	}

	// Время дня (тики мира: 24000 = полный цикл, 0=рассвет).
	if (ry + lineH <= y1 && minecraft->level) {
		long t_ticks = minecraft->level->getTime();
		int hour = (int)(((t_ticks + 6000) / 1000) % 24);
		int minute = (int)((((t_ticks + 6000) % 1000) * 60) / 1000);
		snprintf(buf, sizeof(buf), "Time  %02d:%02d", hour, minute);
		f->drawShadow(buf, textX, ry, 0xffe0d088);
	}
#endif
}
#endif

void Gui::renderInGameHud(float a, bool renderStatus, bool renderHotbar) {

	if (!minecraft->level || !minecraft->player)
		return;

	//minecraft->gameRenderer->setupGuiScreen();
	Font* font = minecraft->font;

	const bool isTouchInterface = minecraft->useTouchscreen();
	const int screenWidth = (int)(minecraft->width * InvGuiScale);
	const int screenHeight = (int)(minecraft->height * InvGuiScale);
	blitOffset = -90;

	// H: 4
	// T: 7
    // L: 6
    // F: 3
	int ySlot = getHotbarYSlot(screenHeight);

	// Прицел в центре экрана — туда, куда смотрит игрок. Рисуем только на
	// основном экране (renderStatus); на нижнем хотбаре прицел не нужен.
	// Инвертирующий блендинг (ONE_MINUS_DST_COLOR) делает крест видимым на
	// любом фоне — и на небе, и на блоках.
	if (renderStatus) {
		Tesselator& tc = Tesselator::instance;
		const float cx = screenWidth  * 0.5f;
		const float cy = screenHeight * 0.5f;
		glDisable2(GL_TEXTURE_2D);
		glDisable2(GL_ALPHA_TEST);
		glEnable2(GL_BLEND);
		glBlendFunc2(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR);
		tc.begin();
		tc.colorABGR(0xffffffff);
		tc.vertex(cx - 5.0f, cy + 1.0f, 0);
		tc.vertex(cx + 5.0f, cy + 1.0f, 0);
		tc.vertex(cx + 5.0f, cy,        0);
		tc.vertex(cx - 5.0f, cy,        0);
		tc.vertex(cx,        cy + 5.0f, 0);
		tc.vertex(cx + 1.0f, cy + 5.0f, 0);
		tc.vertex(cx + 1.0f, cy - 5.0f, 0);
		tc.vertex(cx,        cy - 5.0f, 0);
		tc.draw();
		glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		glEnable2(GL_TEXTURE_2D);
		glEnable2(GL_ALPHA_TEST);
	}

	if (renderStatus) {
		renderProgressIndicator(isTouchInterface, screenWidth, screenHeight, a);

		glColor4f2(1, 1, 1, 1);

		if (minecraft->gameMode->canHurtPlayer()) {
			minecraft->textures->loadAndBindTexture("gui/icons.png");
			Tesselator& t = Tesselator::instance;
			t.beginOverride();
			t.colorABGR(0xffffffff);
			renderHearts();
			renderBubbles();
			t.endOverrideAndDraw();
		}

		if(minecraft->player->getSleepTimer() > 0) {
			glDisable(GL_DEPTH_TEST);
			glDisable(GL_ALPHA_TEST);

			renderSleepAnimation(screenWidth, screenHeight);

			glEnable(GL_ALPHA_TEST);
			glEnable(GL_DEPTH_TEST);
		}
	}

	if (renderHotbar)
		renderToolBar(a, ySlot, screenWidth);

	//font->drawShadow(APP_NAME, 2, 2, 0xffffffff);
	//font->drawShadow("This is a demo, not the finished product", 2, 10 + 2, 0xffffffff);
	#ifdef APPLE_DEMO_PROMOTION
		if (renderStatus)
			font->drawShadow("Demo version", 2, 0 + 2, 0xffffffff);
	#endif /*APPLE_DEMO_PROMOTION*/
	if (renderStatus) {
		glEnable(GL_BLEND);
		unsigned int max = 10;
		bool isChatting = false;
		renderChatMessages(screenHeight, max, isChatting, font);
	}
#if !defined(RPI)
	if (renderHotbar)
		renderOnSelectItemNameText(screenWidth, font, ySlot);
#endif
#if defined(RPI)
	if (renderStatus)
		renderDebugInfo();
#endif

//        glPopMatrix2();
//
//        glEnable(GL_ALPHA_TEST);
	glDisable(GL_BLEND);
	glEnable2(GL_ALPHA_TEST);
}

int Gui::getHotbarYSlot(int screenHeight) const {
#ifdef __3DS__
	(void)screenHeight;
	return 6;
#else
	int ySlot = screenHeight - 16 - 3;
	return ySlot;
#endif
}

int Gui::getSlotIdAt(int x, int y) {
	int screenWidth = (int)(minecraft->width * InvGuiScale);
	int screenHeight = (int)(minecraft->height * InvGuiScale);
	x = (int)(x * InvGuiScale);
	y = (int)(y * InvGuiScale);

	const int yBase = getHotbarYSlot(screenHeight) - 3;
	if (y < yBase || y > yBase + 25)
		return -1;

	int xBase = 2 + screenWidth / 2 - getNumSlots() * 10;
	int xRel  = (x - xBase);
	if (xRel < 0)
		return -1;

	int slot = xRel / 20;
	return (slot >= 0 && slot < getNumSlots())? slot : -1;
}

bool Gui::isInside(int x, int y) {
	return getSlotIdAt(x, y) != -1;
}

int Gui::getNumSlots() {
	return _numSlots;
}

void Gui::flashSlot(int slotId) {
	_flashSlotId = slotId;
	_flashSlotStartTime = getTimeS();
}

void Gui::getSlotPos(int slot, int& posX, int& posY) {
	int screenWidth = (int)(minecraft->width * InvGuiScale);
	int screenHeight = (int)(minecraft->height * InvGuiScale);
	posX = screenWidth / 2 - getNumSlots() * 10 + slot * 20, 
	posY = getHotbarYSlot(screenHeight) - 3;
}

RectangleArea Gui::getRectangleArea(int extendSide) {
	const int Spacing = 3;
	const float pCenterX   = 2.0f + (float)(minecraft->width / 2);
	const float pHalfWidth = (1.0f + (getNumSlots() * 10 + Spacing)) * Gui::GuiScale;
	const float pHeight    = (22 + Spacing) * Gui::GuiScale;
	const float pTop = getHotbarYSlot((int)(minecraft->height * InvGuiScale)) * Gui::GuiScale;
	const float pBottom = pTop + pHeight;

	if (extendSide < 0)
		return RectangleArea(0, pTop, pCenterX+pHalfWidth+2, pBottom);
	if (extendSide > 0)
		return RectangleArea(pCenterX-pHalfWidth, pTop, (float)minecraft->width, pBottom);
	
	return RectangleArea(pCenterX-pHalfWidth, pTop, pCenterX+pHalfWidth+2, pBottom);
}

void Gui::handleClick(int button, int x, int y) {
	if (button != MouseAction::ACTION_LEFT)	return;

	int slot = getSlotIdAt(x, y);
	if (slot != -1)
	{
		if (slot == (getNumSlots()-1))
		{
			minecraft->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
		}
		else
		{
			minecraft->player->inventory->selectSlot(slot);
			itemNameOverlayTime = 0;
		}
	}
}

void Gui::handleKeyPressed(int key)
{
	if (key == 99)
	{
		if (minecraft->player->inventory->selected > 0)
		{
			minecraft->player->inventory->selected--;
		}
	}
	else if (key == 4)
	{
		if (minecraft->player->inventory->selected < (getNumSlots() - 2))
		{
			minecraft->player->inventory->selected++;
		}
	}
	else if (key == 100)
	{
		minecraft->screenChooser.setScreen(SCREEN_BLOCKSELECTION);
	}
}

void Gui::tick() {
	if (overlayMessageTime > 0) overlayMessageTime--;
	tickCount++;
	if(itemNameOverlayTime < 2)
		itemNameOverlayTime += 1.0f / SharedConstants::TicksPerSecond;
	for (unsigned int i = 0; i < guiMessages.size(); i++) {
	    guiMessages.at(i).ticks++;
	}

    if (!minecraft->isCreativeMode())
        tickItemDrop();
}

void Gui::addMessage(const std::string& _string) {
	if (!minecraft->font)
		return;

	std::string string = _string;
	while (minecraft->font->width(string) > MAX_MESSAGE_WIDTH) {
		unsigned int i = 1;
		while (i < string.length() && minecraft->font->width(string.substr(0, i + 1)) <= MAX_MESSAGE_WIDTH) {
			i++;
		}
		addMessage(string.substr(0, i));
		string = string.substr(i);
	}
	GuiMessage message;
	message.message = string;
	message.ticks = 0;
	guiMessages.insert(guiMessages.begin(), message);
	while (guiMessages.size() > 30) {
		guiMessages.pop_back();
	}
}

void Gui::setNowPlaying(const std::string& string) {
	overlayMessageString = "Now playing: " + string;
	overlayMessageTime = 20 * 3;
	animateOverlayMessageColor = true;
}

void Gui::displayClientMessage(const std::string& messageId) {
	//Language language = Language.getInstance();
	//std::string languageString = language.getElement(messageId);
	addMessage(std::string("Client message: ") + messageId);
}

void Gui::renderVignette(float br, int w, int h) {
	br = 1 - br;
	if (br < 0) br = 0;
	if (br > 1) br = 1;
	tbr += (br - tbr) * 0.01f;

	glDisable(GL_DEPTH_TEST);
	glDepthMask(false);
	glBlendFunc2(GL_ZERO, GL_ONE_MINUS_SRC_COLOR);
	glColor4f2(tbr, tbr, tbr, 1);
	minecraft->textures->loadAndBindTexture("misc/vignette.png");

	Tesselator& t = Tesselator::instance;
	t.begin();
	t.vertexUV(0, (float)h, -90, 0, 1);
	t.vertexUV((float)w, (float)h, -90, 1, 1);
	t.vertexUV((float)w, 0, -90, 1, 0);
	t.vertexUV(0, 0, -90, 0, 0);
	t.draw();
	glDepthMask(true);
	glEnable(GL_DEPTH_TEST);
	glColor4f2(1, 1, 1, 1);
	glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void Gui::renderSlot(int slot, int x, int y, float a) {
	ItemInstance* item = minecraft->player->inventory->getItem(slot);
	if (!item) {
		//LOGW("Warning: item @ Gui::renderSlot is NULL\n");
		return;
	}

	const bool fancy = true;
	ItemRenderer::renderGuiItem(minecraft->font, minecraft->textures, item, (float)x, (float)y, fancy);
}

void Gui::renderSlotText( const ItemInstance* item, float x, float y, bool hasFinite, bool shadow )
{
	//if (!item || item->getItem()->getMaxStackSize() <= 1) {
	if (item->count <= 1) {
		return;
	}

	int c = item->count;

	char buffer[4] = {0,0,0,0};
	if (hasFinite)
		itemCountItoa(buffer, c);
	else
		buffer[0] = (char)157;

	//LOGI("slot: %d - %s\n", slot, buffer);
	if (shadow)
		minecraft->font->drawShadow(buffer, x, y, item->count>0?0xffcccccc:0x60cccccc);
	else
		minecraft->font->draw(buffer, x, y, item->count>0?0xffcccccc:0x60cccccc);
}

void Gui::inventoryUpdated() {
	_inventoryNeedsUpdate = true;
}

void Gui::onGraphicsReset() {
    inventoryUpdated();
#ifdef __3DS__
	_minimapTexture = 0;
	_minimapReady = false;
	_minimapChunkX = INT_MAX;
	_minimapChunkZ = INT_MAX;
#endif
}

void Gui::texturesLoaded( Textures* textures ) {
	//_slotFont = new Font(&minecraft->options, "gui/gui_blocks.png", textures, 0, 504, 10, 1, '0');
}

void Gui::onConfigChanged( const Config& c ) {
	Tesselator& t = Tesselator::instance;
	t.begin();

	//
	// Create outer feedback circle
	//
#ifdef ANDROID
	const float mm = 12;
#else
	const float mm = 12;
#endif
	const float maxRadius = minecraft->pixelCalcUi.millimetersToPixels(mm);
	const float radius = Mth::Min(80.0f/2, maxRadius);
	//LOGI("radius, maxradius: %f, %f\n", radius, maxRadius);
	const float radiusInner = radius * 0.95f;

	const int steps = 24;
	const float fstep = Mth::TWO_PI / steps;
	for (int i = 0; i < steps; ++i) {
		float a = i * fstep;;
		float b = a + fstep;

		float aCos = Mth::cos(a);
		float bCos = Mth::cos(b);
		float aSin = Mth::sin(a);
		float bSin = Mth::sin(b);
		float x00 = radius * aCos;
		float x01 = radiusInner * aCos;
		float x10 = radius * bCos;
		float x11 = radiusInner * bCos;
		float y00 = radius * aSin;
		float y01 = radiusInner * aSin;
		float y10 = radius * bSin;
		float y11 = radiusInner * bSin;

		t.vertexUV(x01, y01, 0, 0, 1);
		t.vertexUV(x11, y11, 0, 1, 1);
		t.vertexUV(x10, y10, 0, 1, 0);
		t.vertexUV(x00, y00, 0, 0, 0);
	}
	rcFeedbackOuter = t.end(true, rcFeedbackOuter.vboId);

	//
	// Create the inner feedback ring
	//
	t.begin(GL_TRIANGLE_FAN);
	t.vertex(0, 0, 0);
	for (int i = 0; i < steps + 1; ++i) {
		float a = -i * fstep;
		float xx = radiusInner * Mth::cos(a);
		float yy = radiusInner * Mth::sin(a);
		t.vertex(xx, yy, 0);
		//LOGI("x,y: %f, %f\n", xx, yy);
	}
	rcFeedbackInner = t.end(true, rcFeedbackInner.vboId);

	if (c.minecraft->useTouchscreen()) {
		// I'll bump this up to 6.
		int num = 6; // without "..." dots
		if (!c.minecraft->options.isJoyTouchArea && c.width > 480) {
			while (num < Inventory::MAX_SELECTION_SIZE - 1) {
				int x0, x1, y;
				getSlotPos(0, x0, y);
				getSlotPos(num, x1, y);
				int width = x1 - x0;
				float leftoverPixels = c.width - c.guiScale*width;
				if (c.pixelCalc.pixelsToMillimeters(leftoverPixels) < 80)
					break;
				num++;
			}
		}
		_numSlots = num;
#if defined(__APPLE__)
		_numSlots = Mth::Min(7, _numSlots);
#endif
	} else {
		_numSlots = Inventory::MAX_SELECTION_SIZE; // Xperia Play
	}
#if defined(__VITA__) || defined(__SWITCH__) || defined(__3DS__)
	_numSlots = Inventory::MAX_SELECTION_SIZE;
#endif
	MAX_MESSAGE_WIDTH = c.guiWidth;
}

float Gui::floorAlignToScreenPixel(float v) {
	return (int)(v * Gui::GuiScale) * Gui::InvGuiScale;
}

int Gui::itemCountItoa( char* buffer, int count )
{
	if (count < 0)
		return 0;

	if (count < 10) { // 1 digit
		buffer[0] = '0' + count;
		buffer[1] = 0;
		return 1;
	} else if (count < 100) { // 2 digits
		int digit = count/10;
		buffer[0] = '0' + digit;
		buffer[1] = '0' + count - digit*10;
		buffer[2] = 0;
	} else { // 3 digits -> "99+"
		buffer[0] = buffer[1] = '9';
		buffer[2] = '+';
		buffer[3] = 0;
		return 3;
	}
	return 2;
}

void Gui::tickItemDrop()
{
	// Handle item drop
	static bool isCurrentlyActive = false;
	isCurrentlyActive = false;
	if (Mouse::isButtonDown(MouseAction::ACTION_LEFT)) {
		int slot = getSlotIdAt(Mouse::getX(), Mouse::getY());
		if (slot >= 0 && slot < getNumSlots()-1) {
			if (slot != _currentDropSlot) {
				_currentDropTicks = 0;
				_currentDropSlot = slot;
			}
			isCurrentlyActive = true;
			if ((_currentDropTicks += 1.0f) >= DropTicks) {
				minecraft->player->inventory->dropSlot(slot, false);
				minecraft->level->playSound(minecraft->player, "random.pop", 0.3f, 1);
				isCurrentlyActive = false;
			}
		}
	}
	if (!isCurrentlyActive) {
		_currentDropSlot = -1;
		_currentDropTicks = -1;
	}
}

void Gui::postError( int errCode )
{
	static std::set<int> posted;
	if (posted.find(errCode) != posted.end())
		return;

	posted.insert(errCode);

	std::stringstream s;
	s << "Something went wrong! (errcode " << errCode << ")\n";
	addMessage(s.str());
}

void Gui::setScissorRect( const IntRectangle& bbox )
{
	GLuint x = (GLuint)(ScissorScaleX * bbox.x);
	GLuint y = minecraft->height - (GLuint)(ScissorScaleY * (bbox.y + bbox.h));
	GLuint w = (GLuint)(ScissorScaleX * bbox.w);
	GLuint h = (GLuint)(ScissorScaleY * bbox.h);
	glScissor(x, y, w, h);
}

float Gui::cubeSmoothStep(float percentage, float min, float max) {
	//percentage = percentage * percentage;
	//return (min * percentage) + (max * (1 - percentage));
	return (percentage) * (percentage) * (3 - 2 * (percentage));
}

void Gui::renderProgressIndicator( const bool isTouchInterface, const int screenWidth, const int screenHeight, float a ) {
	ItemInstance* currentItem = minecraft->player->inventory->getSelected();
	bool bowEquipped = currentItem != NULL ? currentItem->getItem() == Item::bow : false;
	bool itemInUse = currentItem != NULL ? currentItem->getItem() == minecraft->player->getUseItem()->getItem() : false;
	if (!isTouchInterface || minecraft->options.isJoyTouchArea || (bowEquipped && itemInUse)) {
		minecraft->textures->loadAndBindTexture("gui/icons.png");
		glEnable(GL_BLEND);
		glBlendFunc2(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR);
		blit(screenWidth/2 - 8, screenHeight/2 - 8, 0, 0, 16, 16);
		glDisable(GL_BLEND);
	} else if(!bowEquipped) {
		const float tprogress = minecraft->gameMode->destroyProgress;
		const float alpha = Mth::clamp(minecraft->inputHolder->alpha, 0.0f, 1.0f);
		//LOGI("alpha: %f\n", alpha);

		if (tprogress <= 0 && minecraft->inputHolder->alpha >= 0) {
			glDisable2(GL_TEXTURE_2D);
			glEnable2(GL_BLEND);
			glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
			if (minecraft->hitResult.isHit())
				glColor4f2(1, 1, 1, 0.8f * alpha);
			else
				glColor4f2(1, 1, 1, Mth::Min(0.4f, alpha*0.4f));

			//LOGI("alpha2: %f\n", alpha);
			const float x = InvGuiScale * minecraft->inputHolder->mousex;
			const float y = InvGuiScale * minecraft->inputHolder->mousey;
			glTranslatef2(x, y, 0);
			drawArrayVT(rcFeedbackOuter.vboId, rcFeedbackOuter.vertexCount, 24);
			glTranslatef2(-x, -y, 0);

			glEnable2(GL_TEXTURE_2D);
			glDisable(GL_BLEND);
		} else if (tprogress > 0) {
			const float oProgress = minecraft->gameMode->oDestroyProgress;
			const float progress = 0.5f * (oProgress + (tprogress - oProgress) * a);

			//static Stopwatch w;
			//w.start();

			glDisable2(GL_TEXTURE_2D);
			glColor4f2(1, 1, 1, 0.8f * alpha);
			glEnable(GL_BLEND);
			glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

			const float x = InvGuiScale * minecraft->inputHolder->mousex;
			const float y = InvGuiScale * minecraft->inputHolder->mousey;
			glPushMatrix2();
			glTranslatef2(x, y, 0);
			drawArrayVT(rcFeedbackOuter.vboId, rcFeedbackOuter.vertexCount, 24);
			glScalef2(0.5f + progress, 0.5f + progress, 1);
			//glDisable2(GL_CULL_FACE);
			glColor4f2(1, 1, 1, 1);
			glBlendFunc2(GL_ONE_MINUS_DST_COLOR, GL_ONE_MINUS_SRC_COLOR);
			drawArrayVT(rcFeedbackInner.vboId, rcFeedbackInner.vertexCount, 24, GL_TRIANGLE_FAN);
			glPopMatrix2();

			glDisable(GL_BLEND);
			glEnable2(GL_TEXTURE_2D);

			//w.stop();
			//w.printEvery(100, "feedback-r ");
		}
	}
}

void Gui::renderHearts() {
	bool blink = (minecraft->player->invulnerableTime / 3) % 2 == 1;
	if (minecraft->player->invulnerableTime < 10) blink = false;
	int h = minecraft->player->health;
	int oh = minecraft->player->lastHealth;
	random.setSeed(tickCount * 312871);

	int xx = 2;//screenWidth / 2 - getNumSlots() * 10;

	int armor = minecraft->player->getArmorValue();
	for (int i = 0; i < Player::MAX_HEALTH / 2; i++) {
		int yo = 2;
		int ip2 = i + i + 1;

		if (armor > 0) {
		    int xo = xx + 80 + i * 8 + 4;
		    if (ip2 < armor) blit(xo, yo, 16 + 2 * 9, 9 * 1, 9, 9);
		    else if (ip2 == armor) blit(xo, yo, 16 + 4 * 9, 9 * 1, 9, 9);
		    else if (ip2 > armor) blit(xo, yo, 16 + 0 * 9, 9 * 1, 9, 9);
		}

		int bg = 0;
		if (blink) bg = 1;
		int xo = xx + i * 8;
		if (h <= 4) {
			yo = yo + random.nextInt(2) - 1;
		}
		blit(xo, yo, 16 + bg * 9, 9 * 0, 9, 9);
		if (blink) {
			if (ip2 < oh) blit(xo, yo, 16 + 6 * 9, 9 * 0, 9, 9);
			else if (ip2 == oh) blit(xo, yo, 16 + 7 * 9, 9 * 0, 9, 9);
		}
		if (ip2 < h) blit(xo, yo, 16 + 4 * 9, 9 * 0, 9, 9);
		else if (ip2 == h) blit(xo, yo, 16 + 5 * 9, 9 * 0, 9, 9);
	}
}

void Gui::renderBubbles() {
	if (minecraft->player->isUnderLiquid(Material::water)) {
		int yo = 12;
		int count = (int) std::ceil((minecraft->player->airSupply - 2) * 10.0f / Player::TOTAL_AIR_SUPPLY);
		int extra = (int) std::ceil((minecraft->player->airSupply) * 10.0f / Player::TOTAL_AIR_SUPPLY) - count;
		for (int i = 0; i < count + extra; i++) {
			int xo =  i * 8 + 2;
			if (i < count) blit(xo, yo, 16, 9 * 2, 9, 9);
			else blit(xo, yo, 16 + 9, 9 * 2, 9, 9);
		}
	}
}

static OffsetPosTranslator posTranslator;
void Gui::onLevelGenerated() {
	if (Level* level = minecraft->level) {
		Pos p = level->getSharedSpawnPos();
		posTranslator = OffsetPosTranslator((float)-p.x, (float)-p.y, (float)-p.z);
	}
#ifdef __3DS__
	_minimapReady = false;
	_minimapChunkX = INT_MAX;
	_minimapChunkZ = INT_MAX;
#endif
}

void Gui::renderDebugInfo() {
	static char buf[256];
	float xx = minecraft->player->x;
	float yy = minecraft->player->y - minecraft->player->heightOffset;
	float zz = minecraft->player->z;
	posTranslator.to(xx, yy, zz);
	sprintf(buf, "pos: %3.1f, %3.1f, %3.1f\n", xx, yy, zz);
	Tesselator& t = Tesselator::instance;
	t.beginOverride();
	t.scale2d(InvGuiScale, InvGuiScale);
	minecraft->font->draw(buf, 2, 2, 0xffffff);
	t.resetScale();
	t.endOverrideAndDraw();
}

void Gui::renderSleepAnimation( const int screenWidth, const int screenHeight ) {
	int timer = minecraft->player->getSleepTimer();
	float amount = (float) timer / (float) Player::SLEEP_DURATION;
	if (amount > 1) {
		// waking up
		amount = 1.0f - ((float) (timer - Player::SLEEP_DURATION) / (float) Player::WAKE_UP_DURATION);
	}

	int color = (int) (220.0f * amount) << 24 | (0x101020);
	fill(0, 0, screenWidth, screenHeight, color);
}

void Gui::renderOnSelectItemNameText( const int screenWidth, Font* font, int ySlot ) {
	if(itemNameOverlayTime < 1.0f) {
		ItemInstance* item = minecraft->player->inventory->getSelected();
		if(item != NULL) {
			float x = float(screenWidth / 2 - font->width(item->getName()) / 2);
			float y = float(ySlot - 22);
			int alpha = 255;
			if(itemNameOverlayTime > 0.75) {
				float time = 0.25f - (itemNameOverlayTime - 0.75f);
				float percentage = cubeSmoothStep(time *  4, 0.0f, 1.0f);
				alpha = int(percentage * 255);
			}
			if(alpha != 0)
				font->drawShadow(item->getName(), x, y, 0x00ffffff + (alpha << 24));
		}
	}
}

void Gui::renderChatMessages( const int screenHeight, unsigned int max, bool isChatting, Font* font ) {
	//        if (minecraft.screen instanceof ChatScreen) {
	//            max = 20;
	//            isChatting = true;
	//        }
	//
	//        glEnable(GL_BLEND);
	//        glBlendFunc2(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	//        glDisable(GL_ALPHA_TEST);
	//
	//        glPushMatrix2();
	//        glTranslatef2(0, screenHeight - 48, 0);
	//        // glScalef2(1.0f / ssc.scale, 1.0f / ssc.scale, 1);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	int baseY = screenHeight - 48;
	for (unsigned int i = 0; i < guiMessages.size() && i < max; i++) {
		if (guiMessages.at(i).ticks < 20 * 10 || isChatting) {
			float t = guiMessages.at(i).ticks / (20 * 10.0f);
			t = 1 - t;
			t = t * 10;
			if (t < 0) t = 0;
			if (t > 1) t = 1;
			t = t * t;
			int alpha = (int) (255 * t);
			if (isChatting) alpha = 255;

			if (alpha > 0) {
				const float x = 2;
				const float y = (float)(baseY - i * 9);
				std::string msg = guiMessages.at(i).message;
				this->fill(x, y - 1, x + MAX_MESSAGE_WIDTH, y + 8, (alpha / 2) << 24);
				glEnable(GL_BLEND);

				font->drawShadow(msg, x, y, 0xffffff + (alpha << 24));
			}
		}
	}
}

void Gui::renderToolBar( float a, int ySlot, const int screenWidth ) {
	glColor4f2(1, 1, 1, .5);
	minecraft->textures->loadAndBindTexture("gui/gui.png");

	Inventory* inventory = minecraft->player->inventory;

	int xBase, yBase;
	getSlotPos(0, xBase, yBase);
	const float baseItemX = (float)xBase + 3;
	const int slotsWidth = 20 * getNumSlots();
	// Left + right side of the selection bar
	blit(xBase, yBase, 0, 0, slotsWidth, 22);
	blit(xBase + slotsWidth, yBase, 180, 0, 2, 22);

	if (_currentDropSlot >= 0 && inventory->getItem(_currentDropSlot)) {
		int x = xBase + 3 +  _currentDropSlot * 20;
		int color = 0x8000ff00;
		int yy = (int)(17.0f * (_currentDropTicks + a) / DropTicks);

		if (_currentDropTicks >= 3) {
			glColor4f2(0, 1, 0, 0.5f);
		}
		fill(x, ySlot+16-yy, x+16, ySlot+16, color);
	}
	blit(xBase-1 + 20*inventory->selected, yBase - 1, 0, 22, 24, 22);
	glColor4f2(1, 1, 1, 1);

	// Flash a slot background
	if (_flashSlotId >= 0) {
		const float since = getTimeS() - _flashSlotStartTime;
		if (since > 0.2f) _flashSlotId = -1;
		else {
			int x = screenWidth / 2 - getNumSlots() * 10 + _flashSlotId * 20 + 2;
			int color = 0xffffff + (((int)(/*0x80 * since +*/ 0x51 - 0x50 * Mth::cos(10 * 6.28f * since))) << 24);
			//LOGI("Color: %.8x\n", color);
			fill(x, ySlot, x+16, ySlot+16, color);
		}
	}
	glColor4f2(1, 1, 1, 1);

	//static Stopwatch w;
	//w.start();

	Tesselator& t = Tesselator::instance;
	t.beginOverride();

	float x = baseItemX;
	for (int i = 0; i < getNumSlots()-1; i++) {
		renderSlot(i, (int)x, ySlot, a);
		x += 20;
	}
	_inventoryNeedsUpdate = false;
	//_inventoryRc = t.end(_inventoryRc.vboId);

	//drawArrayVTC(_inventoryRc.vboId, _inventoryRc.vertexCount);

	//renderSlotWatch.stop();
	//renderSlotWatch.printEvery(100, "Render slots:");

	//int x = screenWidth / 2 + getNumSlots() * 10 + (getNumSlots()-1) * 20 + 2;
	blit(screenWidth / 2 + 10 * getNumSlots() - 20 + 4, ySlot + 6, 242, 252, 14, 4, 14, 4);

	minecraft->textures->loadAndBindTexture("gui/gui_blocks.png");
	t.endOverrideAndDraw();

	// Render damaged items (@todo: investigate if it's faster by drawing in same batch)
	glDisable2(GL_DEPTH_TEST);
	glDisable2(GL_TEXTURE_2D);
	t.beginOverride();
	x = baseItemX;
	for (int i = 0; i < getNumSlots()-1; i++) {
		ItemRenderer::renderGuiItemDecorations(minecraft->player->inventory->getItem(i), x, (float)ySlot);
		x += 20;
	}
	t.endOverrideAndDraw();
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_TEXTURE_2D);

	//w.stop();
	//w.printEvery(100, "gui-slots");

	// Draw count
	//Tesselator& t = Tesselator::instance;
	glPushMatrix2();
	glScalef2(InvGuiScale + InvGuiScale, InvGuiScale + InvGuiScale, 1);
	const float k = 0.5f * GuiScale;

	if (minecraft->gameMode->isSurvivalType()) {
		x = baseItemX;
		for (int i = 0; i < getNumSlots()-1; i++) {
			ItemInstance* item = minecraft->player->inventory->getItem(i);
			if (item && item->count >= 0)
				renderSlotText(item, k*x, k*ySlot + 1, true, true);
			x += 20;
		}
	}

	glPopMatrix2();
}
