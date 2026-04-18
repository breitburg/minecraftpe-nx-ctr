#include "RakThread.h"
#include "RakAssert.h"
#include "RakNetDefines.h"
#include "RakSleep.h"
#include "RakMemoryOverride.h"

using namespace RakNet;




#if   defined(_WIN32)
#include "WindowsIncludes.h"
#include <stdio.h>
	#if !defined(_WIN32_WCE)
	#include <process.h>
	#endif
#elif defined(__VITA__)
#elif defined(__3DS__)
#include <3ds.h>
#include <pthread.h>
#else
#include <pthread.h>
#endif

#if defined(__VITA__)
static int vita_thread_entry(size_t argc, void* argv) {
	void** args = (void**)argv;
	((void(*)(void*))args[0])(args[1]);
	return 0;
}
#endif

#if defined(_WIN32_WCE)
int RakThread::Create( LPTHREAD_START_ROUTINE start_address, void *arglist, int priority)
#elif defined(_WIN32)
int RakThread::Create( unsigned __stdcall start_address( void* ), void *arglist, int priority)
#elif defined(__VITA__)
int RakThread::Create( unsigned start_address( void* ), void *arglist, int priority)

#else
int RakThread::Create( void* start_address( void* ), void *arglist, int priority)
#endif
{
#ifdef _WIN32
	HANDLE threadHandle;
	unsigned threadID = 0;


#if   defined (_WIN32_WCE)
	threadHandle = CreateThread(NULL,MAX_ALLOCA_STACK_ALLOCATION*2,start_address,arglist,0,(DWORD*)&threadID);
	SetThreadPriority(threadHandle, priority);
#else
	threadHandle = (HANDLE) _beginthreadex( NULL, MAX_ALLOCA_STACK_ALLOCATION*2, start_address, arglist, 0, &threadID );
#endif
	SetThreadPriority(threadHandle, priority);

	if (threadHandle==0)
	{
		return 1;
	}
	else
	{
		CloseHandle( threadHandle );
		return 0;
	}























#elif defined(__VITA__)
	SceUID thid = sceKernelCreateThread(
		"RakThread",
		vita_thread_entry,
		0x10000100,
		MAX_ALLOCA_STACK_ALLOCATION*2,
		0,
		SCE_KERNEL_CPU_MASK_USER_ALL,
		NULL
	);
	void* args[2] = {
		(void*)start_address,
		arglist
	};
	int res = sceKernelStartThread(thid, 8, args);
	return res;
#elif defined(__3DS__)
	(void)priority;
	// 3DS: остаёмся на pthread (нужен TLS/_reent для pthread_cond_timedwait
	// внутри RakSleep и SignaledEvent). Но RakNet передаёт priority=1000,
	// а ядро принимает только 0x18..0x3F — и стек 2 МБ для libctru тоже перебор.
	// Поэтому переопределяем оба параметра до pthread_create.
	pthread_t threadHandle;
	pthread_attr_t attr;
	pthread_attr_init(&attr);

	s32 mainPrio = 0x30;
	svcGetThreadPriority(&mainPrio, CUR_THREAD_HANDLE);
	int prio = mainPrio - 1;
	if (prio < 0x18) prio = 0x18;
	if (prio > 0x3F) prio = 0x3F;

	sched_param param;
	param.sched_priority = prio;
	pthread_attr_setschedparam(&attr, &param);

	pthread_attr_setstacksize(&attr, 64 * 1024);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

	int res = pthread_create(&threadHandle, &attr, start_address, arglist);
	pthread_attr_destroy(&attr);
	RakAssert(res == 0 && "pthread_create in RakThread.cpp failed (3DS).")
	return res;
#else
	pthread_t threadHandle;
	// Create thread linux
	pthread_attr_t attr;
	sched_param param;
	param.sched_priority=priority;
	pthread_attr_init( &attr );
	pthread_attr_setschedparam(&attr, &param);





	pthread_attr_setstacksize(&attr, MAX_ALLOCA_STACK_ALLOCATION*2);

	pthread_attr_setdetachstate( &attr, PTHREAD_CREATE_DETACHED );
	int res = pthread_create( &threadHandle, &attr, start_address, arglist );
	RakAssert(res==0 && "pthread_create in RakThread.cpp failed.")
	return res;
#endif
}












































