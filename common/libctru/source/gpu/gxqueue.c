#include <stdlib.h>
#include <string.h>
#include <3ds/types.h>
#include <3ds/svc.h>
#include <3ds/synchronization.h>
#include <3ds/gpu/gx.h>
#include <3ds/services/gspgpu.h>

#define MAX_PARALLEL_CMDS 3

static gxCmdQueue_s* curQueue;
static gxCmdQueue_s* callbackQueue;
static bool isActive, isRunning, shouldStop, callbackRunning;
static LightLock queueLock = 1;

static void gxCmdQueueDoCommands(void)
{
	if (shouldStop || !curQueue)
		return;
	int batchSize = curQueue->lastEntry+MAX_PARALLEL_CMDS-curQueue->curEntry;
	while (curQueue->curEntry < curQueue->numEntries && batchSize--)
	{
		gxCmdEntry_s* entry = &curQueue->entries[curQueue->curEntry++];
		gspSubmitGxCommand(entry->data);
	}
}

void gxCmdQueueInterrupt(GSPGPU_Event irq)
{
	if (irq==GSPGPU_EVENT_PSC1 || irq==GSPGPU_EVENT_VBlank0 || irq==GSPGPU_EVENT_VBlank1)
		return;
	gxCmdQueue_s* runCb = NULL;
	void (*runCallback)(gxCmdQueue_s*) = NULL;
	LightLock_Lock(&queueLock);
	if (!isRunning || !curQueue)
	{
		LightLock_Unlock(&queueLock);
		return;
	}
	curQueue->lastEntry++;
	if (shouldStop)
	{
		curQueue = NULL;
		isActive = false;
		isRunning = false;
		shouldStop = false;
	}
	else if (curQueue->lastEntry < curQueue->numEntries)
		gxCmdQueueDoCommands();
	else
	{
		runCb = curQueue;
		runCallback = runCb->callback;
		isRunning = false;
		callbackQueue = runCallback ? runCb : NULL;
		callbackRunning = runCallback != NULL;
	}
	LightLock_Unlock(&queueLock);
	if (runCallback)
	{
		runCallback(runCb);
		LightLock_Lock(&queueLock);
		if (callbackQueue == runCb)
		{
			callbackQueue = NULL;
			callbackRunning = false;
		}
		LightLock_Unlock(&queueLock);
	}
}

void gxCmdQueueClear(gxCmdQueue_s* queue)
{
	if (queue==curQueue && isRunning)
		svcBreak(USERBREAK_PANIC); // Shouldn't happen.
	queue->numEntries = 0;
	queue->curEntry = 0;
	queue->lastEntry = 0;
}

void gxCmdQueueAdd(gxCmdQueue_s* queue, const gxCmdEntry_s* entry)
{
	if (queue->numEntries == queue->maxEntries)
		svcBreak(USERBREAK_PANIC); // Shouldn't happen.
	memcpy(&queue->entries[queue->numEntries], entry, sizeof(gxCmdEntry_s));
	LightLock_Lock(&queueLock);
	queue->numEntries++;
	if (queue==curQueue && isActive && !isRunning)
	{
		isRunning = true;
		gxCmdQueueDoCommands();
	}
	LightLock_Unlock(&queueLock);
}

void gxCmdQueueRun(gxCmdQueue_s* queue)
{
	LightLock_Lock(&queueLock);
	if (isRunning || callbackRunning)
	{
		LightLock_Unlock(&queueLock);
		return;
	}
	curQueue = queue;
	isActive = true;
	shouldStop = false;
	if (queue->lastEntry < queue->numEntries)
	{
		isRunning = true;
		gxCmdQueueDoCommands();
	} else
		isRunning = false;
	LightLock_Unlock(&queueLock);
}

void gxCmdQueueStop(gxCmdQueue_s* queue)
{
	LightLock_Lock(&queueLock);
	if (queue != curQueue)
	{
		LightLock_Unlock(&queueLock);
		return;
	}
	if (!isRunning)
	{
		curQueue = NULL;
		isActive = false;
		shouldStop = false;
	} else
		shouldStop = true;
	LightLock_Unlock(&queueLock);
}

bool gxCmdQueueWait(gxCmdQueue_s* queue, s64 timeout)
{
	u64 deadline = U64_MAX;
	if (timeout >= 0)
		deadline = svcGetSystemTick() + timeout;
	while (true)
	{
		LightLock_Lock(&queueLock);
		bool busy = (queue == curQueue && isRunning) ||
		            (queue == callbackQueue && callbackRunning);
		LightLock_Unlock(&queueLock);
		if (!busy)
			return true;
		if (timeout >= 0 && (s64)(u64)(svcGetSystemTick()-deadline) >= 0)
			return false;
		gspWaitForAnyEvent();
	}
}
