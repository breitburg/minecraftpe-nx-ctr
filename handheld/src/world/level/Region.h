#ifndef NET_MINECRAFT_WORLD_LEVEL__Region_H__
#define NET_MINECRAFT_WORLD_LEVEL__Region_H__

//package net.minecraft.world.level;

#include "LevelSource.h"

class Level;
class Material;
class LevelChunk;

class Region: public LevelSource
{
public:
    // A chunk-rebuild region is CHUNK_SIZE + 2 wide on X/Z, so it touches at
    // most 3 world chunks in each horizontal direction. Keep the pointer grid
    // inline to avoid three heap allocations per Chunk::rebuild() (hot path).
    static const int MAX_SIZE = 4;

    Region(Level* level, int x1, int y1, int z1, int x2, int y2, int z2);
	~Region();

	bool isSolidRenderTile(int x, int y, int z);
    bool isSolidBlockingTile(int x, int y, int z);
	int  getTile(int x, int y, int z);
	bool isEmptyTile(int x, int y, int z);

	float getBrightness(int x, int y, int z);
    int   getRawBrightness(int x, int y, int z);
	int   getRawBrightness(int x, int y, int z, bool propagate);

	int getData(int x, int y, int z);
	const Material* getMaterial(int x, int y, int z);
	Biome* getBiome(int x, int z);
private:
    int xc1, zc1;
    LevelChunk* chunks[MAX_SIZE * MAX_SIZE];
    Level* level;

	int size_x;
	int size_z;
};

#endif /*NET_MINECRAFT_WORLD_LEVEL__Region_H__*/
