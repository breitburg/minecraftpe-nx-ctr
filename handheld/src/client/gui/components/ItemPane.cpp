#include "ItemPane.h"
#include "../Gui.h"
#include "../../renderer/gles.h"
#include "../../renderer/Tesselator.h"
#include "NinePatch.h"
#include "../../renderer/entity/ItemRenderer.h"

const int rgbActive = 0xfff0f0f0;
const int rgbInactive = 0xc0635558;
const int rgbInactiveShadow = 0xc0aaaaaa;

namespace {

const int rgbShadow3ds = 0xaa151315;
const int rgbInactive3ds = 0xff9a9093;

void drawScaledText(Font* font, const char* text, float x, float y, float scale, int color, bool shadow) {
	glPushMatrix2();
	glTranslatef2(x, y, 0.0f);
	glScalef2(scale, scale, 1.0f);
#ifdef __3DS__
	if (shadow) {
		font->draw(text, 1.0f, 1.0f, rgbShadow3ds);
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

void drawText3ds(Font* font, const std::string& text, float x, float y, int color, bool shadow) {
	if (shadow)
		font->draw(text, x + 1.0f, y + 1.0f, rgbShadow3ds);
	font->draw(text, x, y, color);
}

std::string fitText(Font* font, const std::string& text, int maxWidth) {
	if (font->width(text) <= maxWidth)
		return text;

	std::string out = text;
	while (out.length() > 3 && font->width(out + "...") > maxWidth)
		out.erase(out.length() - 1);

	if (out.length() > 3)
		out += "...";
	return out;
}

}

ItemPane::ItemPane( IItemPaneCallback* screen,
					Textures* textures,
					const IntRectangle& rect,
					int numItems,
					int guiHeight,
					int physicalScreenHeight,
					bool isVertical /*= true*/)
:	super(
	(isVertical?SF_LockX:SF_LockY)/*|SF_Scissor*/|SF_ShowScrollbar,
	rect,							// Pane rect
	isVertical?IntRectangle(0, 0, rect.w, 22)  // Item rect if vertical
			  :IntRectangle(0, 0, 32, rect.h), // Item rect if horizontal
	isVertical?1:numItems, numItems, Gui::GuiScale),
	screen(screen),
	textures(textures),
	physicalScreenHeight(physicalScreenHeight),
	guiSlotItem(NULL),
	guiSlotItemSelected(NULL),
	isVertical(isVertical)
{
	// Expand the area to make it easier to scroll
	area._x0 -= 4;
	area._x1 += 4;
	area._y0 = 0;
	area._y1 = (float)guiHeight;

	// GUI
	NinePatchFactory builder(textures, "gui/spritesheet.png");
	guiSlotItem = builder.createSymmetrical(IntRectangle(20, 32, 8, 8), 2, 2);
	guiSlotItemSelected = builder.createSymmetrical(IntRectangle(28, 32, 8, 8), 2, 2);
	guiSlotItem->setSize((float)rect.w + 4, 22);
	guiSlotItemSelected->setSize((float)rect.w + 4, 22);
}

ItemPane::~ItemPane() {
	delete guiSlotItem;
	delete guiSlotItemSelected;
}

void ItemPane::renderBatch( std::vector<GridItem>& items, float alpha )
{
	//fill(bbox.x, bbox.y, bbox.x + bbox.w, bbox.y + bbox.h, 0xff666666);
	const std::vector<CItem*>& cat = screen->getItems(this);
	if (cat.empty()) return;

	glEnable2(GL_SCISSOR_TEST);
	GLuint x = (GLuint)(Gui::ScissorScaleX * bbox.x);
	GLuint y = physicalScreenHeight - (GLuint)(Gui::ScissorScaleY * (bbox.y + bbox.h));
	GLuint w = (GLuint)(Gui::ScissorScaleX * bbox.w);
	GLuint h = (GLuint)(Gui::ScissorScaleY * bbox.h);
	glScissor(x, y, w, h);

	Tesselator& t = Tesselator::instance;

#ifdef __3DS__
	glDisable2(GL_SCISSOR_TEST);

	const float rowH = (float)itemBbox.h;
	const float iconX = (float)bbox.x + 3.0f;
	const float textX = (float)bbox.x + 23.0f;
	const float maxTextW = (float)bbox.w - 48.0f;

	for (unsigned int i = 0; i < items.size(); ++i) {
		GridItem& item = items[i];
		CItem* citem = cat[item.id];

		const bool craftable = citem->canCraft();
		const int bg = item.selected ? 0xff5f6763 : (craftable ? 0xff373234 : 0xff2a2729);
		const int textColor = craftable ? rgbActive : rgbInactive3ds;

		const float y0 = Gui::floorAlignToScreenPixel(item.yf);
		const float y1 = Gui::floorAlignToScreenPixel(item.yf + rowH - 1.0f);
		fill((float)bbox.x, y0, (float)(bbox.x + bbox.w - 1), y1, bg);
		if (item.selected)
			fill((float)bbox.x, y0, (float)(bbox.x + bbox.w - 1), y0 + 1.0f, 0xffa5d38c);

		ItemRenderer::renderGuiItem(NULL, textures, &citem->item,
			Gui::floorAlignToScreenPixel(iconX),
			Gui::floorAlignToScreenPixel(item.yf + 3.0f), 16, 16, false);

		const std::string label = fitText(f, citem->text, (int)maxTextW);
		drawText3ds(f, label,
			Gui::floorAlignToScreenPixel(textX),
			Gui::floorAlignToScreenPixel(item.yf + 7.0f), textColor, true);

		char buf[64] = {0};
		int c = Gui::itemCountItoa(buf, citem->inventoryCount);
		if (c > 0) {
			drawScaledText(f, buf,
				Gui::floorAlignToScreenPixel((float)bbox.x + (float)bbox.w - 5.0f - c * 4.0f),
				Gui::floorAlignToScreenPixel(item.yf + 13.0f),
				0.6667f, textColor, craftable);
		}
	}

	fillGradient((float)bbox.x, (float)bbox.y, (float)(bbox.x + bbox.w), (float)(bbox.y + 12), 0xaa000000, 0x00000000);
	fillGradient((float)bbox.x, (float)(bbox.y + bbox.h - 12), (float)(bbox.x + bbox.w), (float)(bbox.y + bbox.h), 0x00000000, 0xaa000000);

	drawScrollBar(vScroll);
	return;
#endif

	t.beginOverride();
	for (unsigned int i = 0; i < items.size(); ++i) {
		GridItem& item = items[i];
		(item.selected? guiSlotItemSelected : guiSlotItem)->draw(t, Gui::floorAlignToScreenPixel(item.xf-1), Gui::floorAlignToScreenPixel(item.yf));
	}
	t.endOverrideAndDraw();

	t.beginOverride();
	for (unsigned int i = 0; i < items.size(); ++i) {
		GridItem& item = items[i];
		CItem* citem = cat[item.id];

		ItemRenderer::renderGuiItem(NULL, textures, &citem->item,
			Gui::floorAlignToScreenPixel(item.xf + itemBbox.w - 16),
			Gui::floorAlignToScreenPixel(2 + item.yf), 16, 16, false);
	}
	t.endOverrideAndDraw();

	for (unsigned int i = 0; i < items.size(); ++i) {
		GridItem& item = items[i];
		CItem* citem = cat[item.id];

		char buf[64] = {0};
		int c = Gui::itemCountItoa(buf, citem->inventoryCount);

		float xf = item.xf - 1;
		const float countScale = 0.6667f;
		const float countX = Gui::floorAlignToScreenPixel(xf + itemBbox.w - c * 4);
		const float countY = Gui::floorAlignToScreenPixel(item.yf + itemBbox.h - 8);
		if (citem->canCraft()) {
			f->drawShadow(citem->text,
				Gui::floorAlignToScreenPixel(xf + 2),
				Gui::floorAlignToScreenPixel(item.yf + 6), rgbActive);
			drawScaledText(f, buf, countX, countY, countScale, rgbActive, true);
		} else {
			f->draw(citem->text,
				Gui::floorAlignToScreenPixel(xf + 3),
				Gui::floorAlignToScreenPixel(item.yf + 7), rgbInactiveShadow);
			f->draw(citem->text,
				Gui::floorAlignToScreenPixel(xf + 2),
				Gui::floorAlignToScreenPixel(item.yf + 6), rgbInactive);
			drawScaledText(f, buf, countX, countY, countScale, rgbInactive, false);
		}
	}

	//fillGradient(bbox.x, bbox.y, bbox.x + bbox.w, 20, 0x00000000, 0x80ff0000)
	if (isVertical) {
		fillGradient(bbox.x, bbox.y, bbox.x + bbox.w, bbox.y + 28, 0xbb000000, 0x00000000);
		fillGradient(bbox.x, bbox.y + bbox.h - 28, bbox.x + bbox.w, bbox.y + bbox.h, 0x00000000, 0xbb000000);//0xbb2A272B);
	} else {
		fillHorizontalGradient(bbox.x, bbox.y, bbox.x + 28, bbox.y + bbox.h, 0xbb000000, 0x00000000);
		fillHorizontalGradient(bbox.x + bbox.w - 28, bbox.y, bbox.x + bbox.w, bbox.y + bbox.h, 0x00000000, 0xbb000000);//0xbb2A272B);
	}

	//LOGI("scroll: %f - %f, %f :: %f, %f\n", hScroll.alpha, hScroll.x, hScroll.y, hScroll.w, hScroll.h);
	glDisable2(GL_SCISSOR_TEST);

	drawScrollBar(hScroll);
	drawScrollBar(vScroll);
}

bool ItemPane::onSelect( int gridId, bool selected )
{
	if (selected)
		screen->onItemSelected(this, gridId);

	return selected;
}

void ItemPane::drawScrollBar( ScrollBar& sb ) {
	if (sb.alpha <= 0)
		return;

	int color = ((int)(255.0f * sb.alpha) << 24) | 0xffffff;
	fill(2 + sb.x, sb.y, 2 + sb.x + sb.w, sb.y + sb.h, color);
}
