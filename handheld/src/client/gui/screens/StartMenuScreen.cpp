#include "StartMenuScreen.h"
#include "SelectWorldScreen.h"
#include "ProgressScreen.h"
#include "JoinGameScreen.h"
#include "OptionsScreen.h"
#include "PauseScreen.h"
#include "InvalidLicenseScreen.h"
#include "PrerenderTilesScreen.h" // test button
//#include "BuyGameScreen.h"

#include "../../../util/Mth.h"

#include "../Font.h"
#include "../components/SmallButton.h"
#include "../components/ScrolledSelectionList.h"

#include "../../Minecraft.h"
#include "../../renderer/Tesselator.h"
#include "../../../AppPlatform.h"
#include "../../../LicenseCodes.h"
#include "SimpleChooseLevelScreen.h"
#include "../../renderer/Textures.h"
#include "../../../SharedConstants.h"

// Some kind of default settings, might be overridden in ::init
StartMenuScreen::StartMenuScreen()
#ifdef __3DS__
:	bHost(    2, 0, 0, 80, 12, "Start Game"),
	bJoin(    3, 0, 0, 80, 12, "Join Game"),
	bOptions( 4, 0, 0,  39, 11, "Options"),
	bBuy(     5, 0, 0, 39, 11, "Buy"),
	bTest(    999, 0, 0, 39, 11, "Create")
#else
:	bHost(    2, 0, 0, 160, 24, "Start Game"),
	bJoin(    3, 0, 0, 160, 24, "Join Game"),
	bOptions( 4, 0, 0,  78, 22, "Options"),
	bBuy(     5, 0, 0, 78, 22, "Buy"),
	bTest(    999, 0, 0, 78, 22, "Create")
#endif
{
}

StartMenuScreen::~StartMenuScreen()
{
}

void StartMenuScreen::init()
{
#ifdef __3DS__
	// Принудительно уменьшаем размеры кнопок в 2 раза для 3DS
	bHost.width = 40;
	bHost.height = 6;
	bJoin.width = 40;
	bJoin.height = 6;
	bOptions.width = 39;
	bOptions.height = 6; // или 11/2 = 5.5 -> 5
	bBuy.width = 19;
	bBuy.height = 6;
	bTest.width = 19;
	bTest.height = 6;
#endif

	buttons.push_back(&bHost);
#ifndef __3DS__
	// LAN-play отключён для 3DS — без сети по локалке игра не нужна,
	// а кнопка только мозолит глаза.
	buttons.push_back(&bJoin);
#endif

#ifndef RPI
	buttons.push_back(&bOptions);
#endif

#ifdef DEMO_MODE
	buttons.push_back(&bBuy);
#endif

	copyright = "\xffMojang AB";//. Do not distribute!";

	#ifdef PRE_ANDROID23
		std::string versionString = Common::getGameVersionString("j");
	#else
		std::string versionString = Common::getGameVersionString();
	#endif

	#ifdef DEMO_MODE
	#ifdef __APPLE__
		version = versionString + " (Lite)";
	#else
		version = versionString + " (Demo)";
	#endif
	#else
		#ifdef RPI
			version = "v0.1.1 alpha";//(MCPE " + versionString + " compatible)";
		#else
			version = versionString;
		#endif
	#endif

	bJoin.active = bHost.active = bOptions.active = false;
}

void StartMenuScreen::setupPositions() {
#ifdef __3DS__
	// На 3DS у нас две раздельные плоскости: верхний экран — лого/инфо,
	// нижний — кнопки. Раскладываем кнопки плотно по центру нижней зоны.
	int yBase = height / 2 + 4;
	bHost.y = yBase - 18;
	bOptions.y = yBase + 2;
	bTest.y = bBuy.y = bOptions.y;
	// bJoin не используется (не добавлен в buttons), но всё равно ставим в стек
	bJoin.y = yBase;

	bHost.x = (width - bHost.width) / 2;
	bJoin.x = (width - bJoin.width) / 2;
	bOptions.x = (width - bOptions.width) / 2;
	bTest.x = bBuy.x = bOptions.x + bOptions.width + 4;
#else
	int yBase = height / 2 + 25;

	//#ifdef ANDROID
	bHost.y =	 yBase - 28;
#ifdef RPI
	bJoin.y =	 yBase + 4;
#else
	bJoin.y =	 yBase;
#endif

	bOptions.y = yBase + 28 + 2;
	bTest.y = bBuy.y = bOptions.y;
	//#endif

	// Center buttons
	bHost.x = (width - bHost.width) / 2;
	bJoin.x = (width - bJoin.width) / 2;
	bOptions.x = (width - bJoin.width) / 2;
	bTest.x = bBuy.x = bOptions.x + bOptions.width + 4;
#endif

	copyrightPosX = width - minecraft->font->width(copyright) - 1;
	versionPosX = (width - minecraft->font->width(version)) / 2;// - minecraft->font->width(version) - 2;
}

void StartMenuScreen::tick() {
	_updateLicense();
}

void StartMenuScreen::buttonClicked(Button* button) {

	if (button->id == bHost.id)
	{
        #if defined(DEMO_MODE) || defined(APPLE_DEMO_PROMOTION)
			minecraft->setScreen( new SimpleChooseLevelScreen("_DemoLevel") );
		#else
			minecraft->screenChooser.setScreen(SCREEN_SELECTWORLD);
		#endif
	}
	if (button->id == bJoin.id)
	{
		minecraft->locateMultiplayer();
		minecraft->screenChooser.setScreen(SCREEN_JOINGAME);
	}
	if (button->id == bOptions.id)
	{
		minecraft->setScreen(new OptionsScreen());
	}
	if (button->id == bTest.id)
	{
		//minecraft->setScreen(new PauseScreen());
		//minecraft->setScreen(new PrerenderTilesScreen());
	}
	if (button->id == bBuy.id)
	{
		minecraft->platform()->buyGame();
		//minecraft->setScreen(new BuyGameScreen());
	}
}

bool StartMenuScreen::isInGameScreen() { return false; }

#ifdef __3DS__
bool StartMenuScreen::renderOnTopScreen3ds() { return true; }
#endif

void StartMenuScreen::render( int xm, int ym, float a )
{
#if defined(__3DS__)
	const bool isTop = Screen::s_isRenderingTopScreen3ds;

	renderBackground();

	if (isTop) {
		// ===== Верхний экран: лого крупно, версия, копирайт, подзаголовок =====
		TextureId id = minecraft->textures->loadTexture("gui/title.png");
		const TextureData* data = minecraft->textures->getTemporaryTextureData(id);

		if (data) {
			minecraft->textures->bind(id);
			const float cx = (float)width / 2.0f;
			const float cy = (float)height * 0.42f;
			// Лого занимает ~80% ширины экрана, но не больше его натурального размера
			const float wh = Mth::Min((float)width * 0.42f, (float)data->w);
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

		// Подзаголовок под лого
		const char* tagline = "Pocket Edition  -  3DS Port by efimandreev0";
		int tw = minecraft->font->width(tagline);
		drawString(font, tagline, (width - tw) / 2, (int)((float)height * 0.42f) + 36, 0xffffdd55);

		// Версия и копирайт
		drawString(font, version, versionPosX, height - 32, 0xffcccccc);
		drawString(font, copyright, copyrightPosX, height - 12, 0xffffffff);
	} else {
		// ===== Нижний экран: только кнопки =====
		// Масштабируем кнопки 0.5x относительно центра — как было раньше
		glPushMatrix();
		glTranslatef(width / 2.0f, height / 2.0f, 0.0f);
		glScalef(0.5f, 0.5f, 1.0f);
		glTranslatef(-width / 2.0f, -height / 2.0f, 0.0f);

		int sxm = (int)((xm - width / 2.0f) * 2.0f + width / 2.0f);
		int sym = (int)((ym - height / 2.0f) * 2.0f + height / 2.0f);

		Screen::render(sxm, sym, a);

		glPopMatrix();
	}
#else
	renderBackground();

	int sxm = xm;
	int sym = ym;

	// Title (logo)
	#if defined(RPI)
	TextureId id = minecraft->textures->loadTexture("gui/pi_title.png");
	#else
	TextureId id = minecraft->textures->loadTexture("gui/title.png");
	#endif
	const TextureData* data = minecraft->textures->getTemporaryTextureData(id);

	if (data) {
		minecraft->textures->bind(id);
		const float x = (float)width / 2;
		const float y = 4;
		const float wh = Mth::Min((float)width/2.0f, (float)data->w / 2);
		const float scale = 2.0f * wh / (float)data->w;
		const float h = scale * (float)data->h;

		Tesselator& t = Tesselator::instance;
		glColor4f2(1, 1, 1, 1);
		t.begin();
		t.vertexUV(x-wh, y+h, blitOffset, 0, 1);
		t.vertexUV(x+wh, y+h, blitOffset, 1, 1);
		t.vertexUV(x+wh, y+0, blitOffset, 1, 0);
		t.vertexUV(x-wh, y+0, blitOffset, 0, 0);
		t.draw();
	}

	#if defined(RPI)
	if (Textures::isTextureIdValid(minecraft->textures->loadAndBindTexture("gui/logo/raknet_high_72.png")))
		blit(0, height - 12, 0, 0, 43, 12, 256, 72+72);
	#endif

	drawString(font, version, versionPosX, 62, 0xffcccccc);
	drawString(font, copyright, copyrightPosX, height - 10, 0xffffff);

	Screen::render(sxm, sym, a);
#endif
}

// Обработка кликов с коррекцией координат
void StartMenuScreen::mouseClicked(Minecraft* minecraft, int x, int y, int buttonNum) {
	#if defined(__3DS__)
	float cx = width / 2.0f;
	float cy = height / 2.0f;
	int sx = (int)((x - cx) * 2.0f + cx);
	int sy = (int)((y - cy) * 2.0f + cy);
	Screen::mouseClicked(sx, sy, buttonNum);
	#else
	Screen::mouseClicked(minecraft, x, y, buttonNum);
	#endif
}

void StartMenuScreen::mouseReleased(Minecraft* minecraft, int x, int y, int buttonNum) {
	#if defined(__3DS__)
	float cx = width / 2.0f;
	float cy = height / 2.0f;
	int sx = (int)((x - cx) * 2.0f + cx);
	int sy = (int)((y - cy) * 2.0f + cy);
	Screen::mouseReleased(sx, sy, buttonNum);
	#else
	Screen::mouseReleased(minecraft, x, y, buttonNum);
	#endif
}
void StartMenuScreen::_updateLicense()
{
	int id = minecraft->getLicenseId();
	if (LicenseCodes::isReady(id))
	{
		if (LicenseCodes::isOk(id))
			bJoin.active = bHost.active = bOptions.active = true;
		else
		{
			bool hasBuyButton = minecraft->platform()->hasBuyButtonWhenInvalidLicense();
			minecraft->setScreen(new InvalidLicenseScreen(id, hasBuyButton));
		}
	} else {
		bJoin.active = bHost.active = bOptions.active = false;
	}
}

bool StartMenuScreen::handleBackEvent( bool isDown ) {
	minecraft->quit();
	return true;
}
