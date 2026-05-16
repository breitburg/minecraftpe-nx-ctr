#ifndef NET_MINECRAFT_CLIENT_PLAYER__N3dsInput_H__
#define NET_MINECRAFT_CLIENT_PLAYER__N3dsInput_H__

#include "KeyboardInput.h"
#include "ITurnInput.h"
#include "touchscreen/TouchInputHolder.h"
#include "MouseBuildInput.h"
#include "../../../platform/input/Controller.h"

// На 3DS: 1 - левый стик (Circle Pad), 2 - правый стик (C-Stick на New 3DS или CPP)
static const int moveStick = 1;
static const int lookStick = 2;

class N3dsTurnBuild : public UnifiedTurnBuild {
public:
	N3dsTurnBuild(int turnMode, int width, int height, float maxMovementDelta, float sensitivity, IInputHolder* holder, Minecraft* minecraft) :
		UnifiedTurnBuild(turnMode, width, height, maxMovementDelta, sensitivity, holder, minecraft) {}

	TurnDelta getTurnDelta() override {
		// Если игрок водит стилусом/пальцем по нижнему экрану (камера или UI)
		if(Multitouch::getFirstActivePointerIdEx() >= 0) {
			return UnifiedTurnBuild::getTurnDelta();
		}

		TurnDelta td = UnifiedTurnBuild::getTurnDelta();

		// Считываем C-Stick. 
		// Мертвая зона (0.2f) важна, так как C-Stick часто "дрифтит"
		float stickX = Controller::getTransformedX(lookStick, 0.2f, 1.25f, true);
		float stickY = Controller::getTransformedY(lookStick, 0.2f, 1.25f, true);

		float dx = 0, dy = 0;
		float dt = getDeltaTime();
		
		// Чувствительность для C-Stick. Поднял т.к. пимпочка тугая
		// и крутить камеру было невозможно.
		const float MaxTurnX = 450.0f;
		const float MaxTurnY = 360.0f;
		
		dx = linearTransform( stickX, 0.2f, MaxTurnX ) * dt;
		dy = linearTransform( stickY, 0.2f, MaxTurnY ) * dt;
		
		td.x += dx;
		td.y += dy;
		return td;
	}

	bool tickBuild(Player* p, BuildActionIntention* bai) override {
		// Тапаем по экрану
		if(Multitouch::getFirstActivePointerIdEx() >= 0) {
			return UnifiedTurnBuild::tickBuild(p, bai);
		}

		// Левая кнопка (обычно замаплена на ZR или R в Controller.cpp) - ломать блоки/атака
		if (Mouse::getButtonState(MouseAction::ACTION_LEFT) != 0) {
			if(totalMineTicks++ <= 0) {
				*bai = BuildActionIntention(BuildActionIntention::BAI_FIRSTREMOVE | BuildActionIntention::BAI_ATTACK);
				return true;
			}
			else {
				*bai = BuildActionIntention(BuildActionIntention::BAI_REMOVE | BuildActionIntention::BAI_ATTACK);
				return true;
			}
		} else {
			totalMineTicks = 0;
		}

		// Правая кнопка (обычно ZL или L) - ставить блоки
		if (Mouse::getButtonState(MouseAction::ACTION_RIGHT) != 0) {
				if ((buildHoldTicks++ % buildDelayTicks) == 0) {
					*bai = BuildActionIntention(BuildActionIntention::BAI_BUILD | BuildActionIntention::BAI_INTERACT);
					return true;
				}
 		} else {
			buildHoldTicks = 0;
		}

		return false;
	}

	void onConfigChanged(const Config& c) override {
		UnifiedTurnBuild::onConfigChanged(c);
	}
private:
	int totalMineTicks = 0;
	int buildHoldTicks = 0;
	int buildDelayTicks = 5;
};

class N3dsMoveInput : public KeyboardInput {
	typedef KeyboardInput super;
public:
	N3dsMoveInput(Options* options)
	:	super(options)
	{
		sprintTapTime = 0;
		sprintForwardHeld = false;
		stickSprinting = false;
	}

	void tick(Player* player) override {
		super::tick(player);
		// Левый Circle Pad
		float stickX = Controller::getTransformedX(moveStick, 0.2f, 1.25f, true);
		float stickY = Controller::getTransformedY(moveStick, 0.2f, 1.25f, true);
		xa += -stickX;
		ya += -stickY;
		updateStickSprint(-stickY);
		sprinting = stickSprinting;
	}

	void releaseAllKeys() override {
		super::releaseAllKeys();
		sprintTapTime = 0;
		sprintForwardHeld = false;
		stickSprinting = false;
	}

private:
	void updateStickSprint(float forward) {
		if (sprintTapTime > 0) sprintTapTime--;

		const bool forwardNow = forward > 0.82f;
		const bool released = forward < 0.35f;

		if (!forwardNow) {
			if (released) {
				sprintForwardHeld = false;
				stickSprinting = false;
			}
			return;
		}

		if (!sprintForwardHeld) {
			if (sprintTapTime > 0) stickSprinting = true;
			else sprintTapTime = 7;
			sprintForwardHeld = true;
		}
	}

	int sprintTapTime;
	bool sprintForwardHeld;
	bool stickSprinting;
};

class N3dsInputHolder : public IInputHolder {
	static const int MovementLimit = 200; // per update

public:
	N3dsInputHolder(Minecraft* mc, Options* options) :
		_mc(mc),
		_move(options),
		_turnBuild(UnifiedTurnBuild::MODE_DELTA, mc->width, mc->height, (float)MovementLimit, 1, this, mc)
	{
		onConfigChanged(createConfig(mc));
	}
	~N3dsInputHolder() = default;

	void onConfigChanged(const Config& c) override {
		_move.onConfigChanged(c);
		_turnBuild.moveArea = RectangleArea(0,0,0,0);
		_turnBuild.inventoryArea = _mc->gui.getRectangleArea( _mc->options.isLeftHanded ? 1 : -1 );
		// Чувствительность тачскрина (стилус по нижнему экрану)
		_turnBuild.setSensitivity(c.options->isJoyTouchArea ? 2.8f : 1.8f);
		((ITurnInput*)&_turnBuild)->onConfigChanged(c);
	}

	bool allowPicking() override {
		// Проверка тачскрина
		int pointer = Multitouch::getFirstActivePointerIdEx();

		if(pointer >= 0) { 
			const float x = Multitouch::getX(pointer);
			const float y = Multitouch::getY(pointer);

			if (_turnBuild.isInsideArea(x, y)) {
				mousex = x;
				mousey = y;
				return true;
			}
			else {
				return false;
			}
		}
		else {
			// Если тача нет, прицел в центре верхнего экрана
			mousex = _mc->width / 2;
			mousey = _mc->height / 2;
			return true;
		}
	}

	void render(float alpha) override {
		_turnBuild.render(alpha);
	}

	IMoveInput*		getMoveInput()  override { return &_move; }
	ITurnInput*		getTurnInput()  override { return &_turnBuild; }
	IBuildInput*	getBuildInput() override { return &_turnBuild; }

private:
	N3dsMoveInput _move;
	N3dsTurnBuild _turnBuild;
	Minecraft* _mc;
};

#endif /*NET_MINECRAFT_CLIENT_PLAYER__N3dsInput_H__*/
