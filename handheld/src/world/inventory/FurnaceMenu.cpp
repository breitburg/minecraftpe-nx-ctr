#include "FurnaceMenu.h"
#include "../entity/player/Player.h"
#include "../item/ItemInstance.h"
#include "../level/tile/entity/FurnaceTileEntity.h"

FurnaceMenu::FurnaceMenu( FurnaceTileEntity* furnace )
:   super(ContainerType::FURNACE),
	furnace(furnace),
	lastTickCount(0),
	lastLitTime(0),
	lastLitDuration(0)
{
	furnaceTileEntityId = furnace->runningId;
}

void FurnaceMenu::setSlot( int slot, ItemInstance* item )
{
	furnace->setItem(slot, item);
}

void FurnaceMenu::setData( int id, int value )
{
	if (id == 0) furnace->tickCount = value;
	if (id == 1) furnace->litTime = value;
	if (id == 2) furnace->litDuration = value;
}

std::vector<ItemInstance> FurnaceMenu::getItems()
{
	std::vector<ItemInstance> out;
	for (int i = 0; i < furnace->getContainerSize(); ++i)
		out.push_back(*furnace->getItem(i));
	return out;
}

void FurnaceMenu::broadcastChanges()
{
	super::broadcastChanges();

	if (!listener)
		return;

	//LOGI("broadcast: Derived: data: %d, %d : %d, %d\n", furnace->tickCount, lastTickCount, furnace->litTime, lastLitTime);

	if (furnace->litDuration != lastLitDuration) {
		listener->setContainerData(this, 2, furnace->litDuration);
		lastLitDuration = furnace->litDuration;
	}

	const int burnStep = 24;
	const int litStep = 14;
	const int litDuration = lastLitDuration != 0 ? lastLitDuration : FurnaceTileEntity::BURN_INTERVAL;
	if (furnace->tickCount != lastTickCount
	 && (furnace->tickCount == 0
	  || furnace->tickCount * burnStep / FurnaceTileEntity::BURN_INTERVAL != lastTickCount * burnStep / FurnaceTileEntity::BURN_INTERVAL)) {
		listener->setContainerData(this, 0, furnace->tickCount);
		lastTickCount = furnace->tickCount;
	}
	if (furnace->litTime != lastLitTime
	 && (furnace->litTime == 0
	  || lastLitTime == 0
	  || furnace->litTime * litStep / litDuration != lastLitTime * litStep / litDuration)) {
		listener->setContainerData(this, 1, furnace->litTime);
		lastLitTime = furnace->litTime;
	}
}

void FurnaceMenu::setListener( IContainerListener* listener )
{
	super::setListener(listener);

	if (listener) {
		listener->setContainerData(this, 0, furnace->tickCount);
		listener->setContainerData(this, 1, furnace->litTime);
		listener->setContainerData(this, 2, furnace->litDuration);
		lastTickCount = furnace->tickCount;
		lastLitTime = furnace->litTime;
		lastLitDuration = furnace->litDuration;
	}
}

bool FurnaceMenu::tileEntityDestroyedIsInvalid( int tileEntityId )
{
	return (tileEntityId == furnaceTileEntityId);
}
