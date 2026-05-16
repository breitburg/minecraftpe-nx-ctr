#include "Chunk.h"
#include "Tesselator.h"
#include "TileRenderer.h"
#include "culling/Culler.h"
#include "../../world/entity/Entity.h"
#include "../../world/level/tile/Tile.h"
#include "../../world/level/Region.h"
#include "../../world/level/chunk/LevelChunk.h"
#include "../../util/Mth.h"
#include "../../util/FrameProf.h"
//#include "../../platform/time.h"
#ifdef __3DS__
#include <stdint.h>
#endif

/*static*/ int Chunk::updates = 0;
//static Stopwatch swRebuild;
//int* _layerChunks[3] = {0, 0, 0}; //Chunk::NumLayers];
//int _layerChunkCount[3] = {0, 0, 0};

Chunk::Chunk( Level* level_, int x, int y, int z, int size, int lists_, GLuint* ptrBuf/*= NULL*/)
:	level(level_),
	visible(false),
	compiled(false),
    _empty(true),
	xs(size), ys(size), zs(size),
	dirty(false),
	occlusion_visible(true),
	occlusion_querying(false),
	lists(lists_),
	vboBuffers(ptrBuf),
	bb(0,0,0,1,1,1),
	t(Tesselator::instance)
{
	for (int l = 0; l < NumLayers; l++) {
		empty[l] = false;
	}

	radius = Mth::sqrt((float)(xs * xs + ys * ys + zs * zs)) * 0.5f;

	this->x = -999;
	setPos(x, y, z);
}

void Chunk::setPos( int x, int y, int z )
{
	if (x == this->x && y == this->y && z == this->z) return;

	reset();
	this->x = x;
	this->y = y;
	this->z = z;
	xm = x + xs / 2;
	ym = y + ys / 2;
	zm = z + zs / 2;

	const float xzg = 1.0f;
	const float yp = 2.0f;
	const float yn = 0.0f;
	bb.set(x-xzg, y-yn, z-xzg, x + xs+xzg, y + ys+yp, z + zs+xzg);

	//glNewList(lists + 2, GL_COMPILE);
	//ItemRenderer.renderFlat(AABB.newTemp(xRenderOffs - g, yRenderOffs - g, zRenderOffs - g, xRenderOffs + xs + g, yRenderOffs + ys + g, zRenderOffs + zs + g));
	//glEndList();
	setDirty();
}

void Chunk::translateToPos()
{
	glTranslatef2((float)x, (float)y, (float)z);
}

void Chunk::rebuild()
{
	if (!dirty) return;
	FP_SCOPE("40.chunk.rebuild");
	//if (!visible) return;
	updates++;

	int x0 = x;
	int y0 = y;
	int z0 = z;
	int x1 = x + xs;
	int y1 = y + ys;
	int z1 = z + zs;
	for (int l = 0; l < NumLayers; l++) {
		empty[l] = true;
	}
    _empty = true;

	LevelChunk::touchedSky = false;

	int r = 1;
	Region region(level, x0 - r, y0 - r, z0 - r, x1 + r, y1 + r, z1 + r);
	TileRenderer tileRenderer(&region);

#ifdef __3DS__
	// SINGLE-PASS rebuild для 3DS.
	//
	// Старая схема делала до 3 проходов по всему чанку (16³=4096 ячеек,
	// region.getTile + Tile::tiles[] lookup на каждой) — по проходу на
	// каждый renderLayer (terrain/alpha/water). На чанках с водой+листьями
	// это ~12K getTile-вызовов на ребилд, и каждый ребилд на Old 3DS
	// ловил пики до 25ms (см. LevelRenderer.h).
	//
	// Новая схема: один проход. Бакетизируем непустые тайлы по renderLayer
	// в три SOA-массива (positions + tile ptrs). Затем для каждого
	// присутствующего слоя один проход по его собственному списку — без
	// перепроверок region.getTile() и без обхода пустых ячеек.
	//
	// Память: фикс-массивы static (BSS), 16³ × 3 слоя — но в худшем случае
	// сумма всех слоёв = 16³, поэтому делим один пул на 4096 элементов.
	// Хранение: uint16_t packedPos = (lx<<8) | (ly<<4) | lz (xs,ys,zs<=16),
	// плюс Tile* (4 байта) = 6 байт на тайл × 4096 = 24 КБ static.
	//
	// Гарантия: layers строятся в порядке 0..2, как и в старой схеме —
	// важно для тесселяции (внутри tesselateInWorld есть взаимодействия с
	// уже выставленным GL-стейтом для текущего слоя).
	static uint16_t s_packed[NumLayers][4096];
	static Tile*    s_tilePtr[NumLayers][4096];
	int             layerCount[NumLayers] = {0, 0, 0};

	for (int yy = y0; yy < y1; yy++) {
		for (int zz = z0; zz < z1; zz++) {
			for (int xx = x0; xx < x1; xx++) {
				int tileId = region.getTile(xx, yy, zz);
				if (tileId <= 0) continue;
				Tile* tile = Tile::tiles[tileId];
				if (tile == NULL) continue;
				int rl = tile->getRenderLayer();
				if ((unsigned)rl >= (unsigned)NumLayers) continue;
				int n = layerCount[rl];
				if (n >= 4096) continue; // safety; не должно достигаться
				int lx = xx - x0;
				int ly = yy - y0;
				int lz = zz - z0;
				s_packed[rl][n]  = (uint16_t)((lx << 8) | (ly << 4) | lz);
				s_tilePtr[rl][n] = tile;
				layerCount[rl] = n + 1;
			}
		}
	}

	for (int l = 0; l < NumLayers; l++) {
		int n = layerCount[l];
		if (n <= 0) continue;
		bool rendered = false;
		bool started = false;

		for (int i = 0; i < n; i++) {
			if (!started) {
				started = true;
#ifndef USE_VBO
				glNewList(lists + l, GL_COMPILE);
				glPushMatrix2();
				translateToPos();
				float ss = 1.000001f;
				glTranslatef2(-zs / 2.0f, -ys / 2.0f, -zs / 2.0f);
				glScalef2(ss, ss, ss);
				glTranslatef2(zs / 2.0f, ys / 2.0f, zs / 2.0f);
#endif
				t.begin();
				t.offset((float)(-this->x), (float)(-this->y), (float)(-this->z));
			}
			uint16_t p = s_packed[l][i];
			int lx = (p >> 8) & 0xF;
			int ly = (p >> 4) & 0xF;
			int lz = p & 0xF;
			Tile* tile = s_tilePtr[l][i];
			rendered |= tileRenderer.tesselateInWorld(tile, x0 + lx, y0 + ly, z0 + lz);
		}

		if (started) {
#ifdef USE_VBO
			renderChunk[l] = t.end(true, vboBuffers[l]);
			renderChunk[l].pos.x = (float)this->x;
			renderChunk[l].pos.y = (float)this->y;
			renderChunk[l].pos.z = (float)this->z;
#else
			t.end(false, -1);
			glPopMatrix2();
			glEndList();
#endif
			t.offset(0, 0, 0);
		}
		if (rendered) {
			empty[l] = false;
			_empty = false;
		}
	}
#else
    bool doRenderLayer[NumLayers] = {true, false, false};
	for (int l = 0; l < NumLayers; l++) {
        if (!doRenderLayer[l]) continue;
		bool renderNextLayer = false;
		bool rendered = false;

		bool started = false;
        int cindex = -1;

		for (int y = y0; y < y1; y++) {
			for (int z = z0; z < z1; z++) {
				for (int x = x0; x < x1; x++) {
                    ++cindex;
					int tileId = region.getTile(x, y, z);
					if (tileId > 0) {
						if (!started) {
							started = true;

#ifndef USE_VBO
							glNewList(lists + l, GL_COMPILE);
							glPushMatrix2();
							translateToPos();
							float ss = 1.000001f;
							glTranslatef2(-zs / 2.0f, -ys / 2.0f, -zs / 2.0f);
							glScalef2(ss, ss, ss);
							glTranslatef2(zs / 2.0f, ys / 2.0f, zs / 2.0f);
#endif
							t.begin();
							t.offset((float)(-this->x), (float)(-this->y), (float)(-this->z));
						}

						Tile* tile = Tile::tiles[tileId];
						int renderLayer = tile->getRenderLayer();

						if (renderLayer > l) {
							renderNextLayer = true;
                            doRenderLayer[renderLayer] = true;
						} else if (renderLayer == l) {
							rendered |= tileRenderer.tesselateInWorld(tile, x, y, z);
						}
					}
				}
			}
		}

		if (started) {

#ifdef USE_VBO
			renderChunk[l] = t.end(true, vboBuffers[l]);
			renderChunk[l].pos.x = (float)this->x;
			renderChunk[l].pos.y = (float)this->y;
			renderChunk[l].pos.z = (float)this->z;
#else
			t.end(false, -1);
			glPopMatrix2();
			glEndList();
#endif
			t.offset(0, 0, 0);
		} else {
			rendered = false;
		}
		if (rendered) {
            empty[l] = false;
            _empty = false;
        }
		if (!renderNextLayer) break;
	}
#endif

	skyLit = LevelChunk::touchedSky;
	compiled = true;
	return;
}

float Chunk::distanceToSqr( const Entity* player ) const
{
	float xd = (float) (player->x - xm);
	float yd = (float) (player->y - ym);
	float zd = (float) (player->z - zm);
	return xd * xd + yd * yd + zd * zd;
}

float Chunk::squishedDistanceToSqr( const Entity* player ) const
{
	float xd = (float) (player->x - xm);
	float yd = (float) (player->y - ym) * 2;
	float zd = (float) (player->z - zm);
	return xd * xd + yd * yd + zd * zd;
}

void Chunk::reset()
{
	for (int i = 0; i < NumLayers; i++) {
		empty[i] = true;
	}
	visible = false;
	compiled = false;
    _empty = true;
}

int Chunk::getList( int layer )
{
	if (!visible) return -1;
	if (!empty[layer]) return lists + layer;
	return -1;
}

RenderChunk& Chunk::getRenderChunk( int layer )
{
	return renderChunk[layer];
}

int Chunk::getAllLists( int displayLists[], int p, int layer )
{
	if (!visible) return p;
	if (!empty[layer]) displayLists[p++] = (lists + layer);
	return p;
}

void Chunk::cull( Culler* culler )
{
	visible = culler->isVisible(bb);
}

void Chunk::renderBB()
{
	//glCallList(lists + 2);
}

bool Chunk::isEmpty()
{
	return compiled && _empty;//empty[0] && empty[1] && empty[2];
//	if (!compiled) return false;
//	return empty[0] && empty[1];
}

void Chunk::setDirty()
{
	dirty = true;
}

void Chunk::setClean()
{
	dirty = false;
}

bool Chunk::isDirty()
{
	return dirty;
}

void Chunk::resetUpdates()
{
	updates = 0;
	//swRebuild.reset();
}
