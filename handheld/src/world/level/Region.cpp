#include "Region.h"
#include "chunk/LevelChunk.h"
#include "material/Material.h"
#include "tile/Tile.h"
#include "Level.h"

Region::Region(Level* level, int x1, int y1, int z1, int x2, int y2, int z2) {
    this->level = level;

    xc1 = x1 >> 4;
    zc1 = z1 >> 4;
    int xc2 = x2 >> 4;
    int zc2 = z2 >> 4;

	size_x = xc2 - xc1 + 1;
	size_z = zc2 - zc1 + 1;

    // Region is used per chunk rebuild; cap to the inline grid. For the
    // current caller (chunks + 1-block halo) size_x/size_z <= 3.
    if (size_x > MAX_SIZE) size_x = MAX_SIZE;
    if (size_z > MAX_SIZE) size_z = MAX_SIZE;

    for (int xc = 0; xc < size_x; xc++) {
        for (int zc = 0; zc < size_z; zc++) {
            chunks[xc * MAX_SIZE + zc] = level->getChunk(xc1 + xc, zc1 + zc);
        }
    }
}

Region::~Region() {
}

int Region::getTile(int x, int y, int z) {
    if (y < 0) return 0;
    if (y >= Level::DEPTH) return 0;

    int xc = (x >> 4) - xc1;
    int zc = (z >> 4) - zc1;

	if (xc < 0 || xc >= size_x || zc < 0 || zc >= size_z) {
        return 0;
    }

    LevelChunk* lc = chunks[xc * MAX_SIZE + zc];
    if (lc == NULL) return 0;

    return lc->getTile(x & 15, y, z & 15);
}

bool Region::isEmptyTile( int x, int y, int z )
{
	//return getTile(x, y, z) == 0; //@todo?
	return Tile::tiles[getTile(x, y, z)] == NULL;
}

//TileEntity getTileEntity(int x, int y, int z) {
//    int xc = (x >> 4) - xc1;
//    int zc = (z >> 4) - zc1;

//    return chunks[xc][zc].getTileEntity(x & 15, y, z & 15);
//}

float Region::getBrightness(int x, int y, int z) {
    return level->dimension->brightnessRamp[getRawBrightness(x, y, z)];
}

int Region::getRawBrightness(int x, int y, int z) {
    return getRawBrightness(x, y, z, true);
}

int Region::getRawBrightness(int x, int y, int z, bool propagate) {
    if (x < -Level::MAX_LEVEL_SIZE || z < -Level::MAX_LEVEL_SIZE || x >= Level::MAX_LEVEL_SIZE || z > Level::MAX_LEVEL_SIZE) {
        return Level::MAX_BRIGHTNESS;
    }

    if (propagate) {
        int id = getTile(x, y, z);
        if (id == Tile::stoneSlabHalf->id || id == Tile::farmland->id) {
            int br = getRawBrightness(x, y + 1, z, false);
            int br1 = getRawBrightness(x + 1, y, z, false);
            int br2 = getRawBrightness(x - 1, y, z, false);
            int br3 = getRawBrightness(x, y, z + 1, false);
            int br4 = getRawBrightness(x, y, z - 1, false);
            if (br1 > br) br = br1;
            if (br2 > br) br = br2;
            if (br3 > br) br = br3;
            if (br4 > br) br = br4;
            return br;
        }
    }

    if (y < 0) return 0;
    if (y >= Level::DEPTH) {
        int br = Level::MAX_BRIGHTNESS - level->skyDarken;
        if (br < 0) br = 0;
        return br;
    }

    int xc = (x >> 4) - xc1;
    int zc = (z >> 4) - zc1;

    if (xc < 0 || xc >= size_x || zc < 0 || zc >= size_z) {
        return Level::MAX_BRIGHTNESS;
    }
    LevelChunk* lc = chunks[xc * MAX_SIZE + zc];
    if (lc == NULL) return Level::MAX_BRIGHTNESS;
    return lc->getRawBrightness(x & 15, y, z & 15, level->skyDarken);
}

int Region::getData(int x, int y, int z) {
    if (y < 0) return 0;
    if (y >= Level::DEPTH) return 0;
    int xc = (x >> 4) - xc1;
    int zc = (z >> 4) - zc1;

    if (xc < 0 || xc >= size_x || zc < 0 || zc >= size_z) {
        return 0;
    }
    LevelChunk* lc = chunks[xc * MAX_SIZE + zc];
    if (lc == NULL) return 0;
    return lc->getData(x & 15, y, z & 15);
}

const Material* Region::getMaterial(int x, int y, int z) {
    int t = getTile(x, y, z);
    if (t == 0) return Material::air;
    return Tile::tiles[t]->material;
}

bool Region::isSolidBlockingTile(int x, int y, int z)
{
	Tile* tile = Tile::tiles[getTile(x, y, z)];
	if (tile == NULL) return false;
	return tile->material->isSolidBlocking() && tile->isCubeShaped();
}

bool Region::isSolidRenderTile(int x, int y, int z) {
    Tile* tile = Tile::tiles[getTile(x, y, z)];
    if (tile == NULL) return false;
    return tile->isSolidRender();
}

Biome* Region::getBiome( int x, int z ) {
	return level->getBiome(x, z);
}

//BiomeSource getBiomeSource() {
//    return level.getBiomeSource();
//}
