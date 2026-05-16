#include "PaneCraftingScreen.h"
#include "../touch/TouchStartMenuScreen.h"
#include "../../Screen.h"
#include "../../Font.h"
#include "../../components/NinePatch.h"
#include "../../../Minecraft.h"
#include "../../../player/LocalPlayer.h"
#include "../../../renderer/Tesselator.h"
#include "../../../renderer/Textures.h"
#include "../../../renderer/entity/ItemRenderer.h"
#include "../../../../world/item/Item.h"
#include "../../../../world/item/crafting/Recipes.h"
#include "../../../../world/item/ItemCategory.h"
#include "../../../../world/entity/player/Inventory.h"
#include "../../../../util/StringUtils.h"
#include "../../../../locale/I18n.h"
#include "../../../../world/entity/item/ItemEntity.h"
#include "../../../../world/level/Level.h"
#include "../../../../world/item/DyePowderItem.h"
#include "../../../../world/item/crafting/Recipe.h"

static NinePatchLayer* guiPaneFrame = NULL;

const float BorderPixels = 6.0f;
const int   descFrameWidth = 100;

const int rgbActive = 0xfff0f0f0;
const int rgbInactive = 0xc0635558;
const int rgbInactiveShadow = 0xc0aaaaaa;

namespace {
int getCategoryButtonSize(int height, int numCategories) {
	int buttonHeight = (height - 16) / Mth::Max(numCategories, 4);
#ifdef __3DS__
	buttonHeight = Mth::Min(buttonHeight, 24);
#endif
	return Mth::Max(buttonHeight, 18);
}

int getDescriptionPaneWidth(int width) {
#ifdef __3DS__
	return Mth::Max(80, Mth::Min(88, width / 3));
#else
	(void)width;
	return descFrameWidth;
#endif
}

void drawScaledFont(Font* font, const char* text, float x, float y, float scale, int color, bool shadow) {
	glPushMatrix2();
	glTranslatef2(x, y, 0.0f);
	glScalef2(scale, scale, 1.0f);
#ifdef __3DS__
	if (shadow) {
		font->draw(text, 1.0f, 1.0f, 0xaa151315);
		font->draw(text, 0.0f, 0.0f, color);
	} else {
		font->draw(text, 0.0f, 0.0f, color);
	}
#else
	if (shadow)
		font->drawShadow(text, 0.0f, 0.0f, color);
	else
		font->draw(text, 0.0f, 0.0f, color);
#endif
	glPopMatrix2();
}
}

class CategoryButton: public ImageButton {
	typedef ImageButton super;
public:
	CategoryButton(int id, const ImageButton* const* selectedPtr, NinePatchLayer* stateNormal, NinePatchLayer* statePressed)
	:	super(id, ""),
		selectedPtr(selectedPtr),
		stateNormal(stateNormal),
		statePressed(statePressed)
	{}

	void renderBg(Minecraft* minecraft, int xm, int ym) {
		//fill(x+1, y+1, x+w-1, y+h-1, 0xff999999);
		
		bool hovered = active && (minecraft->useTouchscreen()?
			(_currentlyDown && xm >= x && ym >= y && xm < x + width && ym < y + height) : false);

		if (hovered || *selectedPtr == this)
			statePressed->draw(Tesselator::instance, (float)x, (float)y);
		else
			stateNormal->draw(Tesselator::instance, (float)x, (float)y);
	}
	bool isSecondImage(bool hovered) { return false; }

private:
	const ImageButton* const* selectedPtr;
	NinePatchLayer* stateNormal;
	NinePatchLayer* statePressed;
};

PaneCraftingScreen::PaneCraftingScreen(int craftingSize)
:	craftingSize(craftingSize),
	currentCategory(-1),
	currentItem(NULL),
	pane(NULL),
	btnCraft(1),
	btnClose(2, ""),
	selectedCategoryButton(NULL),
	guiBackground(NULL),
	guiSlotCategory(NULL),
	guiSlotCategorySelected(NULL),
	numCategories(4)
{
	for (int i = 0; i < numCategories; ++i) {
		categoryBitmasks.push_back(1 << i);
		categoryIcons.push_back(i);
	}
}

PaneCraftingScreen::~PaneCraftingScreen() {
	for (unsigned int i = 0; i < _items.size(); ++i)
		delete _items[i];

	for (unsigned int i = 0; i < _categoryButtons.size(); ++i)
		delete _categoryButtons[i];

	clearCategoryItems();

	delete pane;
	delete guiBackground;

	// statics
	delete guiSlotCategory;
	delete guiSlotCategorySelected;
	delete guiPaneFrame;
}

void PaneCraftingScreen::init() {
	ImageDef def;
	def.name = "gui/spritesheet.png";
	def.x = 0;
	def.y = 1;
	def.width = def.height = 18;
	def.setSrc(IntRectangle(60, 0, 18, 18));
	btnClose.setImageDef(def, true);
	btnClose.scaleWhenPressed = false;

	btnCraft.init(minecraft->textures);

	buttons.push_back(&btnCraft);
	buttons.push_back(&btnClose);

	// GUI patches
	NinePatchFactory builder(minecraft->textures, "gui/spritesheet.png");

	guiBackground   = builder.createSymmetrical(IntRectangle(0, 0, 16, 16), 4, 4);
	guiPaneFrame    = builder.createSymmetrical(IntRectangle(0, 20, 8, 8), 1, 2)->setExcluded(1 << 4);
	guiSlotCategory = builder.createSymmetrical(IntRectangle(8, 32, 8, 8), 2, 2);
	guiSlotCategorySelected = builder.createSymmetrical(IntRectangle(0, 32, 8, 8), 2, 2);

	initCategories();
}

void PaneCraftingScreen::initCategories() {
	_categories.resize(numCategories);

	// Category buttons
	for (int i = 0; i < numCategories; ++i) {
		ImageButton* button = new CategoryButton(100 + i, &selectedCategoryButton, guiSlotCategory, guiSlotCategorySelected);
		_categoryButtons.push_back( button );
		buttons.push_back( button );
	}

	const RecipeList& all = Recipes::getInstance()->getRecipes();
	RecipeList filtered;
	filtered.reserve(all.size());

	// Apply size filter
	for (unsigned int i = 0; i < all.size(); ++i) {
		if (craftingSize >= all[i]->getCraftingSize())
			filtered.push_back(all[i]);
	}
	// Filter by subclass impl
	filterRecipes(filtered);

	// Add items from filtered recipes
	for (unsigned int i = 0; i < filtered.size(); ++i)
		addItem(filtered[i]);

	recheckRecipes();
}

void PaneCraftingScreen::setupPositions() {
#ifdef __3DS__
	setupPositions3ds();
	return;
#endif

	// Left  - Categories
	const int buttonHeight = getCategoryButtonSize(height, numCategories);
	for (unsigned c = 0; c < _categoryButtons.size(); ++c) {
		ImageButton* button = _categoryButtons[c];
		button->x = (int)BorderPixels;
		button->y = (int)BorderPixels + c * (1 + buttonHeight);
		button->width = (int)buttonHeight;
		button->height = (int)buttonHeight;

		int icon = categoryIcons[c];
		ImageDef def;
		def.x = 0;
		def.width = def.height = (float)buttonHeight;
		def.name = "gui/spritesheet.png";
		def.setSrc(IntRectangle(32 * (icon/2), 64 + (icon&1) * 32, 32, 32));
		button->setImageDef(def, false);
	}
	const int detailsWidth = getDescriptionPaneWidth(width);
	const int paneGap = 4;

	// Middle - Scrolling pane
	paneRect.x = buttonHeight + 2 * (int)BorderPixels;
	paneRect.y = (int)BorderPixels + 2;
	paneRect.w = Mth::Max(48, width - paneRect.x - detailsWidth - paneGap);
	paneRect.h = height - 2 * (int)BorderPixels - 4;

	// Right  - Description
	const int craftW = Mth::Max(detailsWidth - 2 * (int)BorderPixels - 4, 56);
	const int detailsX = paneRect.x + paneRect.w + paneGap;
	btnCraft.x = detailsX + (detailsWidth - craftW) / 2;
	btnCraft.y = 20;
	btnCraft.setSize((float)craftW, 62);

	btnClose.width = btnClose.height = 19;
	btnClose.x = width - btnClose.width;
	btnClose.y = 0;

	guiPaneFrame->setSize((float)paneRect.w + 2, (float)paneRect.h + 4);
	guiBackground->setSize((float)width, (float)height);
	guiSlotCategory->setSize((float)buttonHeight, (float)buttonHeight);
	guiSlotCategorySelected->setSize((float)buttonHeight, (float)buttonHeight);

	int oldCategory = currentCategory;
	currentCategory = -1;
	const int nextCategory = (pane && oldCategory >= 0) ? oldCategory : 0;
	buttonClicked(_categoryButtons[nextCategory]);
}

void PaneCraftingScreen::tick() {
	if (pane) pane->tick();
}

#ifdef __3DS__
void PaneCraftingScreen::setupPositions3ds() {
	const int margin = 4;
	const int buttonHeight = 23;
	const int gap = 4;
	const int reservedHotbarTop = 31;

	for (unsigned c = 0; c < _categoryButtons.size(); ++c) {
		ImageButton* button = _categoryButtons[c];
		button->x = margin;
		button->y = reservedHotbarTop + c * (buttonHeight + 3);
		button->width = buttonHeight;
		button->height = buttonHeight;
	}

	const int detailsWidth = Mth::Max(76, Mth::Min(84, width / 3));
	paneRect.x = margin + buttonHeight + gap;
	paneRect.y = reservedHotbarTop;
	paneRect.w = Mth::Max(72, width - paneRect.x - detailsWidth - gap - margin);
	paneRect.h = height - paneRect.y - 4;

	const int detailsX = paneRect.x + paneRect.w + gap;
	btnCraft.x = detailsX + 5;
	btnCraft.y = paneRect.y + 8;
	btnCraft.setSize((float)Mth::Max(56, detailsWidth - 10), 48.0f);

	btnClose.width = btnClose.height = 18;
	btnClose.x = width - btnClose.width - 2;
	btnClose.y = paneRect.y - 1;

	int oldCategory = currentCategory;
	currentCategory = -1;
	const int nextCategory = (pane && oldCategory >= 0) ? oldCategory : 0;
	buttonClicked(_categoryButtons[nextCategory]);
}

void PaneCraftingScreen::drawPanel3ds(int x, int y, int w, int h, int fillColor, int borderColor) {
	fill(x, y, x + w, y + h, fillColor);
	fill(x, y, x + w, y + 1, borderColor);
	fill(x, y + h - 1, x + w, y + h, 0xff151315);
	fill(x, y, x + 1, y + h, borderColor);
	fill(x + w - 1, y, x + w, y + h, 0xff151315);
}

void PaneCraftingScreen::drawSprite3ds(int x, int y, int w, int h, int sx, int sy, int sw, int sh) {
	TextureId texId = minecraft->textures->loadAndBindTexture("gui/spritesheet.png");
	const TextureData* data = Textures::isTextureIdValid(texId) ? minecraft->textures->getTemporaryTextureData(texId) : NULL;
	const float us = data ? (1.0f / (float)data->w) : (1.0f / 128.0f);
	const float vs = data ? (1.0f / (float)data->h) : (1.0f / 128.0f);

	Tesselator& t = Tesselator::instance;
	t.begin();
	t.colorABGR(0xffffffff);
	t.vertexUV((float)x,     (float)(y + h), blitOffset, (float)sx * us,        (float)(sy + sh) * vs);
	t.vertexUV((float)(x+w), (float)(y + h), blitOffset, (float)(sx + sw) * us, (float)(sy + sh) * vs);
	t.vertexUV((float)(x+w), (float)y,       blitOffset, (float)(sx + sw) * us, (float)sy * vs);
	t.vertexUV((float)x,     (float)y,       blitOffset, (float)sx * us,        (float)sy * vs);
	t.draw();
}

void PaneCraftingScreen::renderCategoryBar3ds(int xm, int ym) {
	for (unsigned c = 0; c < _categoryButtons.size(); ++c) {
		ImageButton* button = _categoryButtons[c];
		const bool selected = selectedCategoryButton == button;
		const bool hovered = button->active && button->isInside(xm, ym);
		const int fillColor = selected ? 0xff59645d : (hovered ? 0xff4a4447 : 0xff2d292c);
		const int borderColor = selected ? 0xff93c47d : 0xff72676b;

		drawPanel3ds(button->x, button->y, button->width, button->height, fillColor, borderColor);

		const int icon = categoryIcons[c];
		const int iconPad = 3;
		drawSprite3ds(button->x + iconPad, button->y + iconPad,
			button->width - 2 * iconPad, button->height - 2 * iconPad,
			32 * (icon / 2), 64 + (icon & 1) * 32, 32, 32);
	}
}

void PaneCraftingScreen::renderDetails3ds(float a) {
	(void)a;

	const int detailsX = paneRect.x + paneRect.w + 4;
	const int detailsW = width - detailsX - 4;
	drawPanel3ds(detailsX, paneRect.y, detailsW, paneRect.h, 0xff2a2729, 0xff72676b);

	if (!currentItem)
		return;

	const bool craftable = currentItem->canCraft();
	drawPanel3ds(btnCraft.x, btnCraft.y, btnCraft.width, btnCraft.height,
		craftable ? 0xff40543b : 0xff353033,
		craftable ? 0xff93c47d : 0xff75676c);

	const float slotWidth = (float)btnCraft.width / 2.0f;
	const float slotHeight = (float)btnCraft.height / 2.0f;
	const float slotBx = (float)btnCraft.x + slotWidth / 2.0f - 8.0f;
	const float slotBy = (float)btnCraft.y + slotHeight / 2.0f - 9.0f;

	ItemInstance reqItem;
	for (unsigned int i = 0; i < currentItem->neededItems.size(); ++i) {
		const float xx = slotBx + slotWidth * (float)(i % 2);
		const float yy = slotBy + slotHeight * (float)(i / 2);
		CItem::ReqItem& req = currentItem->neededItems[i];
		reqItem = req.item;
		if (reqItem.getAuxValue() == -1) reqItem.setAuxValue(0);

		fill(xx - 2.0f, yy - 2.0f, xx + 18.0f, yy + 18.0f, 0xff1f1c1e);
		ItemRenderer::renderGuiItem(NULL, minecraft->textures, &reqItem, xx, yy, 16, 16, true);
	}

	char buf[16];
	const float scale = 2.0f / 3.0f;
	const int inactive3ds = 0xff9a9093;
	for (unsigned int i = 0; i < currentItem->neededItems.size(); ++i) {
		const float xx = Gui::floorAlignToScreenPixel(slotBx + slotWidth * (float)(i % 2) + 3.0f);
		const float yy = Gui::floorAlignToScreenPixel(slotBy + slotHeight * (float)(i / 2) + 15.0f);
		CItem::ReqItem& req = currentItem->neededItems[i];

		int bufIndex = 0;
		bufIndex += Gui::itemCountItoa(&buf[bufIndex], req.has);
		strcpy(&buf[bufIndex], "/"); bufIndex += 1;
		bufIndex += Gui::itemCountItoa(&buf[bufIndex], req.item.count);
		buf[bufIndex] = 0;

		if (req.enough())
			drawScaledFont(minecraft->font, buf, xx, yy, scale, rgbActive, true);
		else {
			drawScaledFont(minecraft->font, buf, xx, yy, scale, inactive3ds, true);
		}
	}

	// Клипуем по нижней грани правой панели — без этого длинное описание
	// (например, у Snow «A compact way to store snowballs») вылезает
	// за рамку панели вниз.
	const float descTop    = (float)(btnCraft.y + btnCraft.height + 8);
	const float descBottom = (float)(paneRect.y + paneRect.h - 2);
	minecraft->font->drawWordWrapClipped(currentItemDesc,
		(float)detailsX + 4.0f,
		descTop,
		(float)detailsW - 8.0f,
		descBottom,
		rgbActive);
}

void PaneCraftingScreen::render3ds(int xm, int ym, float a) {
	(void)a;

	fill(0, 0, width, height, 0xff171416);
	fill(0, 0, width, 6, 0xff2d292c);

	renderCategoryBar3ds(xm, ym);

	drawPanel3ds(paneRect.x - 1, paneRect.y - 1, paneRect.w + 2, paneRect.h + 2, 0xff201d1f, 0xff72676b);
	if (pane)
		pane->render(xm, ym, a);

	renderDetails3ds(a);

	const bool closeHover = btnClose.isInside(xm, ym);
	drawPanel3ds(btnClose.x, btnClose.y, btnClose.width, btnClose.height,
		closeHover ? 0xff72504f : 0xff3d3638, closeHover ? 0xffd08a83 : 0xff8c7e82);
	drawSprite3ds(btnClose.x + 1, btnClose.y + 1, 16, 16, 60, 0, 18, 18);
}
#endif

void PaneCraftingScreen::render(int xm, int ym, float a) {
#ifdef __3DS__
	render3ds(xm, ym, a);
	return;
#endif

	const int N = 5;
	static StopwatchNLast r(N);
	//renderBackground();
	Tesselator& t = Tesselator::instance;
	guiBackground->draw(t, 0, 0);
	glEnable2(GL_ALPHA_TEST);

	// Buttons (Left side + crafting)
	super::render(xm, ym, a);

	// Mid
	r.start();
	// Blit frame
	guiPaneFrame->draw(t, (float)paneRect.x - 1, (float)paneRect.y - 2);
	if (pane) pane->render(xm, ym, a);
	r.stop();
	//r.printEvery(N, "test");

	const float slotWidth = (float)btnCraft.width / 2.0f;
	const float slotHeight = (float)btnCraft.height / 2.0f;
	const float slotBx = (float)btnCraft.x + slotWidth/2 - 8;
	const float slotBy = (float)btnCraft.y + slotHeight/2 - 9;

	ItemInstance reqItem;
	// Right side
	if (currentItem) {
		t.beginOverride();
		for (unsigned int i = 0; i < currentItem->neededItems.size(); ++i) {
			const float xx = slotBx + slotWidth  * (float)(i % 2);
			const float yy = slotBy + slotHeight * (float)(i / 2);
			CItem::ReqItem& req = currentItem->neededItems[i];
			reqItem = req.item;
			if (reqItem.getAuxValue() == -1) reqItem.setAuxValue(0);
			ItemRenderer::renderGuiItem(NULL, minecraft->textures, &reqItem, xx, yy, 16, 16, true);
		}
		t.endOverrideAndDraw();

		char buf[16];
		const float scale = 2.0f / 3.0f;
		for (unsigned int i = 0; i < currentItem->neededItems.size(); ++i) {
			const float xx = Gui::floorAlignToScreenPixel(slotBx + slotWidth  * (float)(i % 2) + 3.0f);
			const float yy = Gui::floorAlignToScreenPixel(slotBy + slotHeight * (float)(i / 2) + 15.0f);
			CItem::ReqItem& req = currentItem->neededItems[i];

			int bufIndex = 0;
			bufIndex += Gui::itemCountItoa(&buf[bufIndex], req.has);
			strcpy(&buf[bufIndex], "/"); bufIndex += 1;
			bufIndex += Gui::itemCountItoa(&buf[bufIndex], req.item.count);

			buf[bufIndex] = 0;
			if (req.enough())
				drawScaledFont(minecraft->font, buf, xx, yy, scale, rgbActive, true);
			else {
				drawScaledFont(minecraft->font, buf, xx + 1.0f, yy + 1.0f, scale, rgbInactiveShadow, false);
				drawScaledFont(minecraft->font, buf, xx, yy, scale, rgbInactive, false);
			}
		}

		//minecraft->font->drawWordWrap(currentItemDesc, rightBx + 2, (float)btnCraft.y + btnCraft.h + 6, descFrameWidth-4, rgbActive);
		minecraft->font->drawWordWrap(currentItemDesc, (float)btnCraft.x, (float)(btnCraft.y + btnCraft.height + 6), (float)btnCraft.width, rgbActive);
	}
	//glDisable2(GL_ALPHA_TEST);
}

void PaneCraftingScreen::buttonClicked(Button* button) {
	if (button == &btnCraft)
		craftSelectedItem();

	if (button == &btnClose)
		minecraft->setScreen(NULL);

	// Did we click a category?
	if (button->id >= 100 && button->id < 200) {
		int categoryId = button->id - 100;
		ItemList& cat = _categories[categoryId];
		if (!cat.empty()) {
			onItemSelected(categoryId, cat[0]);
			pane->setSelected(0, true);
		}
		currentCategory = categoryId;
		selectedCategoryButton = (CategoryButton*)button;
	}
}

static void randomlyFillItemPack(ItemPack* ip, int numItems) {
	int added = 0;
	ItemInstance item(0, 1, 0);
	while (added < numItems) {
		int t = Mth::random(512);
		if (!Item::items[t]) continue;

		item.id = t;
		int id = ItemPack::getIdForItemInstance(&item);
		int count = Mth::random(10);
		for (int i = 0; i < count; ++i)
			ip->add(id);
		++added;
	}
}

static bool sortCanCraftPredicate(const CItem* a, const CItem* b) {
	//if (a->maxBuildCount == 0 && b->maxBuildCount > 0) return false;
	//if (b->maxBuildCount == 0 && a->maxBuildCount > 0) return true;
	return a->sortText < b->sortText;
}

void PaneCraftingScreen::recheckRecipes() {
	ItemPack ip;

	if (minecraft->player && minecraft->player->inventory) {
		Inventory* inv = (minecraft->player)->inventory;

		for (int i = Inventory::MAX_SELECTION_SIZE; i < inv->getContainerSize(); ++i) {
			if (ItemInstance* item = inv->getItem(i))
				ip.add(ItemPack::getIdForItemInstance(item), item->count);
		}
	} else {
		randomlyFillItemPack(&ip, 50);
	}

	ip.print();

	Stopwatch w;
	w.start();

	for (unsigned int i = 0; i < _items.size(); ++i) {
		CItem* item = _items[i];
		item->neededItems.clear();
		item->setCanCraft(true);

		Recipe* recipe = item->recipe;
		item->inventoryCount = ip.getCount(ItemPack::getIdForItemInstance(&item->item));
		//item->maxBuildCount = recipe->getMaxCraftCount(ip);
		// Override the canCraft thing, since I'm too lazy
		// to fix the above (commented out) function
		std::vector<ItemInstance> items = recipe->getItemPack().getItemInstances();
		for (unsigned int j = 0; j < items.size(); ++j) {
			ItemInstance& jtem = items[j];
			int has = 0;
			if (!Recipe::isAnyAuxValue(&jtem) && (jtem.getAuxValue() == Recipe::ANY_AUX_VALUE)) {
				// If the aux value on the item matters, but the recipe says it doesn't,
				// use this override (by fetching all items with aux-ids 0-15)
				ItemInstance aux(jtem);
				for (int i = 0; i < 16; ++i) {
					aux.setAuxValue(i);
					has += ip.getCount(ItemPack::getIdForItemInstance(&aux));
				}
			} else {
				// Else just use the normal aux-value rules
				has = ip.getCount(ItemPack::getIdForItemInstance(&jtem));
			}
			CItem::ReqItem req(jtem, has);
			item->neededItems.push_back(req);
			item->setCanCraft(item->canCraft() && req.enough());
		}
	}
	w.stop();
	w.printEvery(1, "> craft ");

	for (unsigned int c = 0; c < _categories.size(); ++c)
		std::stable_sort(_categories[c].begin(), _categories[c].end(), sortCanCraftPredicate);
}

void PaneCraftingScreen::addItem( Recipe* recipe )
{
	ItemInstance instance = recipe->getResultItem();
	Item* item = instance.getItem();
	CItem* ci = new CItem(instance, recipe, instance.getName());//item->getDescriptionId());
	if (item->id == Tile::cloth->id)
		ci->sortText = "Wool " + ci->text;
	if (item->id == Item::dye_powder->id)
		ci->sortText = "ZDye " + ci->text;
	_items.push_back(ci);

	if (item->category < 0)
		return;

	for (int i = 0; i < (int)categoryBitmasks.size(); ++i) {
		int bitmask = categoryBitmasks[i];
		if ((bitmask & item->category) != 0)
			_categories[i].push_back( ci );
	}
}

void PaneCraftingScreen::onItemSelected(const ItemPane* forPane, int itemIndexInCurrentCategory) {
	if (currentCategory >= (int)_categories.size()) return;
	if (itemIndexInCurrentCategory >= (int)_categories[currentCategory].size()) return;
	onItemSelected(currentCategory, _categories[currentCategory][itemIndexInCurrentCategory]);
}

void PaneCraftingScreen::onItemSelected(int buttonIndex, CItem* item) {
	currentItem = item;
	currentItemDesc = I18n::getDescriptionString(currentItem->item);

	if (buttonIndex != currentCategory) {
		// Clear item buttons for this category
		clearCategoryItems();

		// Setup new buttons for the items in this category
		const int NumCategoryItems = _categories[buttonIndex].size();

		if (pane) delete pane;
		pane = new ItemPane(this, minecraft->textures, paneRect, NumCategoryItems, height, minecraft->height);
		pane->f = minecraft->font;

		currentCategory = buttonIndex;
	}
}

void PaneCraftingScreen::clearCategoryItems()
{
	for (unsigned int i = 0; i < currentCategoryButtons.size(); ++i) {
		delete currentCategoryButtons[i];
	}
	currentCategoryButtons.clear();
}

void PaneCraftingScreen::keyPressed( int eventKey )
{
	if (eventKey == Keyboard::KEY_ESCAPE) {
		minecraft->setScreen(NULL);
		//minecraft->grabMouse();
	} else {
		super::keyPressed(eventKey);
	}
}

void PaneCraftingScreen::craftSelectedItem()
{
	if (!currentItem)
		return;
    if (!currentItem->canCraft())
        return;

	ItemInstance resultItem = currentItem->item;

	if (minecraft->player) {
		// Remove all items required for the recipe and ...
		for (unsigned int i = 0; i < currentItem->neededItems.size(); ++i) {
			CItem::ReqItem& req = currentItem->neededItems[i];

            // If the recipe allows any aux-value as ingredients, first deplete
            // aux == 0 from inventory. Since I'm not sure if this always is
            // correct, let's only do it for ingredient sandstone for now.
            ItemInstance toRemove = req.item;

            if (Tile::sandStone->id == req.item.id
             && Recipe::ANY_AUX_VALUE == req.item.getAuxValue()) {
                 toRemove.setAuxValue(0);
                 toRemove.count = minecraft->player->inventory->removeResource(toRemove, true);
                 toRemove.setAuxValue(Recipe::ANY_AUX_VALUE);
            }

            if (toRemove.count > 0) {
                minecraft->player->inventory->removeResource(toRemove);
            }
		}
		// ... add the new one! (in this order, to fill empty slots better)
		// if it doesn't fit, throw it on the ground!
		if (!minecraft->player->inventory->add(&resultItem)) {
			minecraft->player->drop(new ItemInstance(resultItem), false);
		}

		recheckRecipes();
	}
}

bool PaneCraftingScreen::renderGameBehind()
{
	return false;
}

bool PaneCraftingScreen::closeOnPlayerHurt() {
    return true;
}

void PaneCraftingScreen::filterRecipes(RecipeList& recipes) {
	for (int i = recipes.size() - 1; i >= 0; --i) {
		if (!filterRecipe(*recipes[i]))
			recipes.erase(recipes.begin() + i);
	}
}

const std::vector<CItem*>& PaneCraftingScreen::getItems(const ItemPane* forPane)
{
	return _categories[currentCategory];
}

void PaneCraftingScreen::setSingleCategoryAndIcon(int categoryBitmask, int categoryIcon) {
	assert(!minecraft && "setSingleCategoryAndIcon needs to be called from subclass constructor!\n");

	numCategories = 1;

	categoryIcons.clear();
	categoryIcons.push_back(categoryIcon);

	categoryBitmasks.clear();
	categoryBitmasks.push_back(categoryBitmask);
}

//
// Craft button
//
CraftButton::CraftButton( int id)
:	super(id, ""),
	bg(NULL),
	bgSelected(NULL),
	numItems(0)
{
}

CraftButton::~CraftButton()
{
	delete bg;
	delete bgSelected;
}

void CraftButton::setSize(float w, float h ) {
	this->width = (int)w;
	this->height = (int)h;

	if (bg && bgSelected) {
		bg->setSize(w, h);
		bgSelected->setSize(w, h);
	}
}

void CraftButton::init( Textures* textures)
{
	NinePatchFactory builder(textures, "gui/spritesheet.png");
	bg = builder.createSymmetrical(IntRectangle(112, 0, 8, 67), 2, 2);
	bgSelected = builder.createSymmetrical(IntRectangle(120, 0, 8, 67), 2, 2);
}

IntRectangle CraftButton::getItemPos( int i )
{
	return IntRectangle();
}

void CraftButton::renderBg(Minecraft* minecraft, int xm, int ym) {
	if (!bg || !bgSelected)
		return;
	//fill(x+1, y+1, x+w-1, y+h-1, 0xff999999);

	bool hovered = active && (minecraft->useTouchscreen()?
		(_currentlyDown && xm >= x && ym >= y && xm < x + width && ym < y + height) : false);

	if (hovered || selected)
		bgSelected->draw(Tesselator::instance, (float)x, (float)y);
	else
		bg->draw(Tesselator::instance, (float)x, (float)y);
}
