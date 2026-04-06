#ifndef NET_MINECRAFT_WORLD_LEVEL_CHUNK__DataLayer_H__
#define NET_MINECRAFT_WORLD_LEVEL_CHUNK__DataLayer_H__

#include <cstring>

// Глобальный пустой массив (весит всего 16 КБ, но спасает мегабайты!)
static unsigned char SHARED_EMPTY_ARRAY[16384] = {0};

class DataLayer
{
public:
	DataLayer()
	:	data(NULL),
		length(0),
        slotMax(0),
        isShared(true)
	{}

    DataLayer(int length) {
		this->length = length >> 1;
        // ВСЕ чанки со старта смотрят в общий пустой массив. Выделения памяти НЕТ.
		this->data = SHARED_EMPTY_ARRAY;
		this->slotMax = this->length;
        this->isShared = true;
    }

    DataLayer(unsigned char* rawData, int length) {
		this->length = length >> 1;
        this->data = rawData;
        this->slotMax = this->length;
        this->isShared = false;
    }

	~DataLayer() {
        // Удаляем память ТОЛЬКО если это не наш глобальный пустой массив
        if (!isShared && data != NULL) {
		    delete[] data;
        }
	}

    // Принудительное выделение, если нам нужно записать сырые данные (например, из сохранения)
    __inline void forceAllocate() {
        if (isShared) {
            data = new unsigned char[length];
            memset(data, 0, length);
            isShared = false;
        }
    }

    int get(int x, int y, int z) {
        return get(x << 11 | z << 7 | y);
    }

    void set(int x, int y, int z, int val) {
        set(x << 11 | z << 7 | y, val);
    }

	__inline int get(int pos) {
		int slot = pos >> 1;
        // Чтение работает молниеносно, без if/else
        int shift = (pos & 1) << 2;
        return (data[slot] >> shift) & 0xF;
	}

	__inline void set(int pos, int val) {
        // Ленивое выделение: если пытаемся писать в общий пустой массив
        if (isShared) {
            if (val == 0) return; // Пишем ноль в нули? Игнорим, экономим память.
            forceAllocate(); // Пишем свет? Вот теперь выделяем память.
        }

        int slot = pos >> 1;
        int shift = (pos & 1) << 2;
        data[slot] = (data[slot] & ~(0xF << shift)) | ((val & 0xF) << shift);
	}

    bool isValid() {
        return data != NULL;
    }

    void setAll(int br) {
        if (br == 0) {
            // Если просят залить нулями - просто удаляем массив и возвращаемся к пустышке
            if (!isShared && data != NULL) {
                delete[] data;
            }
            data = SHARED_EMPTY_ARRAY;
            isShared = true;
        } else {
            forceAllocate();
            unsigned char val = (br & 0xF) | ((br & 0xF) << 4);
		    memset(data, val, length);
        }
    }

	unsigned char* data;
	int length;
	int slotMax;
    bool isShared; // Флаг: смотрим ли мы на общий пустой массив
};

#endif /*NET_MINECRAFT_WORLD_LEVEL_CHUNK__DataLayer_H__*/

#ifndef NET_MINECRAFT_WORLD_LEVEL_CHUNK__DataLayer_H__
#define NET_MINECRAFT_WORLD_LEVEL_CHUNK__DataLayer_H__

//package net.minecraft.world.level.chunk;
#include <cstring>

//This file is using most of memory

class DataLayer
{
public:
	DataLayer()
	:	data(NULL),
		length(0)
	{}

    DataLayer(int length) {
		this->length = length >> 1;
		data = new unsigned char[this->length];
		setAll(0);
		slotMax = this->length;
    }

    DataLayer(unsigned char* data, int length) {
		this->length = length >> 1;
        this->data = data;
    }

	~DataLayer() {
		delete[] data;
	}

    int get(int x, int y, int z) {
        return get(x << 11 | z << 7 | y);
    }

    void set(int x, int y, int z, int val) {
        set(x << 11 | z << 7 | y, val);
    }

	__inline int get(int pos) {
		int slot = pos >> 1;
		int part = pos & 1;

		if (part == 0) {
			return data[slot] & 0xf;
		} else {
			return (data[slot] >> 4) & 0xf;
		}
	}
	__inline void set(int pos, int val) {
        int slot = pos >> 1;
	    int part = pos & 1;

	    if (part == 0) {
		    data[slot] = ((data[slot] & 0xf0) | (val & 0xf));
	    } else {
            data[slot] = ((data[slot] & 0x0f) | ((val & 0xf) << 4));
	    }
	}

    bool isValid() {
        return data != NULL;
    }

    void setAll(int br) {
        unsigned char val = (br & (br << 4));
		memset(data, val, length);
    }

	unsigned char* data;
	int length;
	int slotMax;
};

#endif /*NET_MINECRAFT_WORLD_LEVEL_CHUNK__DataLayer_H__*/
