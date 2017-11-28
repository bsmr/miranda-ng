/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (с) 2012-17 Miranda NG project (https://miranda-ng.org),
Copyright (c) 2000-12 Miranda IM project,
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

*/

#include "stdafx.h"

#include <m_netlib.h>

/////////////////////////////////////////////////////////////////////////////////////////
// APC and mutex functions

static void __stdcall DummyAPCFunc(ULONG_PTR)
{
	/* called in the context of thread that cleared it's APC queue */
	return;
}

static int MirandaWaitForMutex(HANDLE hEvent)
{
	for (;;) {
		// will get WAIT_IO_COMPLETE for QueueUserAPC() which isnt a result
		DWORD rc = MsgWaitForMultipleObjectsEx(1, &hEvent, INFINITE, QS_ALLINPUT, MWMO_ALERTABLE);
		if (rc == WAIT_OBJECT_0 + 1) {
			MSG msg;
			while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE)) {
				if (msg.hwnd != nullptr && IsDialogMessage(msg.hwnd, &msg)) /* Wine fix. */
					continue;
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}
		else if (rc == WAIT_OBJECT_0) { // got object
			return 1;
		}
		else if (rc == WAIT_ABANDONED_0 || rc == WAIT_FAILED)
			return 0;
	}
}

/////////////////////////////////////////////////////////////////////////////////////////
// exception handling

static DWORD __cdecl sttDefaultFilter(DWORD, EXCEPTION_POINTERS*)
{
	return EXCEPTION_EXECUTE_HANDLER;
}

pfnExceptionFilter pMirandaExceptFilter = sttDefaultFilter;

MIR_CORE_DLL(pfnExceptionFilter) GetExceptionFilter()
{
	return pMirandaExceptFilter;
}

MIR_CORE_DLL(pfnExceptionFilter) SetExceptionFilter(pfnExceptionFilter _mirandaExceptFilter)
{
	pfnExceptionFilter oldOne = pMirandaExceptFilter;
	if (_mirandaExceptFilter != nullptr)
		pMirandaExceptFilter = _mirandaExceptFilter;
	return oldOne;
}

/////////////////////////////////////////////////////////////////////////////////////////
// thread support functions

struct THREAD_WAIT_ENTRY
{
	DWORD dwThreadId;	// valid if hThread isn't signalled
	HANDLE hThread;
	HINSTANCE hOwner;
	void *pObject, *pEntryPoint;
};

static LIST<THREAD_WAIT_ENTRY> threads(10, NumericKeySortT);

struct FORK_ARG
{
	HANDLE hEvent, hThread;
	union
	{
		pThreadFunc threadcode;
		pThreadFuncEx threadcodeex;
	};
	void *arg, *owner;
};

/////////////////////////////////////////////////////////////////////////////////////////
// forkthread - starts a new thread

DWORD WINAPI forkthread_r(void *arg)
{
	FORK_ARG *fa = (FORK_ARG*)arg;
	pThreadFunc callercode = fa->threadcode;
	void *cookie = fa->arg;
	HANDLE hThread = fa->hThread;
	Thread_Push((HINSTANCE)callercode);
	SetEvent(fa->hEvent);

	callercode(cookie);

	CloseHandle(hThread);
	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	Thread_Pop();
	return 0;
}

MIR_CORE_DLL(HANDLE) mir_forkthread(void(__cdecl *threadcode)(void*), void *arg)
{
	FORK_ARG fa;
	fa.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);
	fa.threadcode = threadcode;
	fa.arg = arg;

	DWORD threadID;
	fa.hThread = CreateThread(nullptr, 0, forkthread_r, &fa, 0, &threadID);
	if (fa.hThread != nullptr)
		WaitForSingleObject(fa.hEvent, INFINITE);

	CloseHandle(fa.hEvent);
	return fa.hThread;
}

/////////////////////////////////////////////////////////////////////////////////////////
// forkthreadex - starts a new thread with the extended info and returns the thread id

DWORD WINAPI forkthreadex_r(void * arg)
{
	struct FORK_ARG *fa = (struct FORK_ARG *)arg;
	pThreadFuncEx threadcode = fa->threadcodeex;
	pThreadFuncOwner threadcodeex = (pThreadFuncOwner)fa->threadcodeex;
	void *cookie = fa->arg;
	void *owner = fa->owner;
	unsigned long rc = 0;

	Thread_Push((HINSTANCE)threadcode, fa->owner);
	SetEvent(fa->hEvent);
	if (owner)
		rc = threadcodeex(owner, cookie);
	else
		rc = threadcode(cookie);

	SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
	Thread_Pop();
	return rc;
}

MIR_CORE_DLL(HANDLE) mir_forkthreadex(pThreadFuncEx aFunc, void* arg, unsigned *pThreadID)
{
	struct FORK_ARG fa = {};
	fa.threadcodeex = aFunc;
	fa.arg = arg;
	fa.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	DWORD threadID = 0;
	HANDLE hThread = CreateThread(nullptr, 0, forkthreadex_r, &fa, 0, &threadID);
	if (hThread != nullptr)
		WaitForSingleObject(fa.hEvent, INFINITE);

	if (pThreadID != nullptr)
		*pThreadID = threadID;

	CloseHandle(fa.hEvent);
	return hThread;
}

MIR_CORE_DLL(HANDLE) mir_forkthreadowner(pThreadFuncOwner aFunc, void *owner, void *arg, unsigned *pThreadID)
{
	struct FORK_ARG fa = {};
	fa.threadcodeex = (pThreadFuncEx)aFunc;
	fa.arg = arg;
	fa.owner = owner;
	fa.hEvent = CreateEvent(nullptr, TRUE, FALSE, nullptr);

	DWORD threadID = 0;
	HANDLE hThread = CreateThread(nullptr, 0, forkthreadex_r, &fa, 0, &threadID);
	if (hThread != nullptr)
		WaitForSingleObject(fa.hEvent, INFINITE);

	if (pThreadID != nullptr)
		*pThreadID = threadID;

	CloseHandle(fa.hEvent);
	return hThread;
}

/////////////////////////////////////////////////////////////////////////////////////////

MIR_CORE_DLL(void) KillObjectThreads(void* owner)
{
	if (owner == nullptr)
		return;

	WaitForSingleObject(hStackMutex, INFINITE);

	HANDLE *threadPool = (HANDLE*)alloca(threads.getCount()*sizeof(HANDLE));
	int threadCount = 0;

	for (int j = threads.getCount(); j--;) {
		THREAD_WAIT_ENTRY *p = threads[j];
		if (p->pObject == owner)
			threadPool[threadCount++] = p->hThread;
	}
	ReleaseMutex(hStackMutex);

	// is there anything to kill?
	if (threadCount > 0) {
		if (WaitForMultipleObjects(threadCount, threadPool, TRUE, 5000) == WAIT_TIMEOUT) {
			// forcibly kill all remaining threads after 5 secs
			WaitForSingleObject(hStackMutex, INFINITE);
			for (int j = threads.getCount() - 1; j >= 0; j--) {
				THREAD_WAIT_ENTRY *p = threads[j];
				if (p->pObject == owner) {
					char szModuleName[MAX_PATH];
					GetModuleFileNameA(p->hOwner, szModuleName, sizeof(szModuleName));
					Netlib_Logf(nullptr, "Killing object thread %s:%p", szModuleName, p->dwThreadId);
					TerminateThread(p->hThread, 9999);
					CloseHandle(p->hThread);
					threads.remove(j);
					mir_free(p);
				}
			}
			ReleaseMutex(hStackMutex);
		}
	}
}

/////////////////////////////////////////////////////////////////////////////////////////

static void CALLBACK KillAllThreads(HWND, UINT, UINT_PTR, DWORD)
{
	if (MirandaWaitForMutex(hStackMutex)) {
		for (int j = 0; j < threads.getCount(); j++) {
			THREAD_WAIT_ENTRY *p = threads[j];
			char szModuleName[MAX_PATH];
			GetModuleFileNameA(p->hOwner, szModuleName, sizeof(szModuleName));
			Netlib_Logf(nullptr, "Killing thread %s:%p (%p)", szModuleName, p->dwThreadId, p->pEntryPoint);
			TerminateThread(p->hThread, 9999);
			CloseHandle(p->hThread);
			mir_free(p);
		}

		threads.destroy();

		ReleaseMutex(hStackMutex);
		SetEvent(hThreadQueueEmpty);
	}
}

MIR_CORE_DLL(void) Thread_Wait(void)
{
	// acquire the list and wake up any alertable threads
	if (MirandaWaitForMutex(hStackMutex)) {
		for (int j = 0; j < threads.getCount(); j++)
			QueueUserAPC(DummyAPCFunc, threads[j]->hThread, 0);
		ReleaseMutex(hStackMutex);
	}

	// give all unclosed threads 5 seconds to close
	SetTimer(nullptr, 0, 5000, KillAllThreads);

	// wait til the thread list is empty
	MirandaWaitForMutex(hThreadQueueEmpty);
}

/////////////////////////////////////////////////////////////////////////////////////////

typedef LONG (WINAPI *pNtQIT)(HANDLE, LONG, PVOID, ULONG, PULONG);
#define ThreadQuerySetWin32StartAddress 9

static void* GetCurrentThreadEntryPoint()
{
	pNtQIT NtQueryInformationThread = (pNtQIT)GetProcAddress(GetModuleHandle(L"ntdll.dll"), "NtQueryInformationThread");
	if (NtQueryInformationThread == nullptr)
		return nullptr;

	HANDLE hDupHandle, hCurrentProcess = GetCurrentProcess();
	if (!DuplicateHandle(hCurrentProcess, GetCurrentThread(), hCurrentProcess, &hDupHandle, THREAD_QUERY_INFORMATION, FALSE, 0)) {
		SetLastError(ERROR_ACCESS_DENIED);
		return nullptr;
	}
	
	DWORD_PTR dwStartAddress;
	LONG ntStatus = NtQueryInformationThread(hDupHandle, ThreadQuerySetWin32StartAddress, &dwStartAddress, sizeof(DWORD_PTR), nullptr);
	CloseHandle(hDupHandle);

	return (ntStatus != ERROR_SUCCESS) ? nullptr : (void*)dwStartAddress;
}

MIR_CORE_DLL(INT_PTR) Thread_Push(HINSTANCE hInst, void* pOwner)
{
	ResetEvent(hThreadQueueEmpty); // thread list is not empty
	if (WaitForSingleObject(hStackMutex, INFINITE) == WAIT_OBJECT_0) {
		THREAD_WAIT_ENTRY *p = (THREAD_WAIT_ENTRY*)mir_calloc(sizeof(THREAD_WAIT_ENTRY));

		DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &p->hThread, 0, FALSE, DUPLICATE_SAME_ACCESS);
		p->dwThreadId = GetCurrentThreadId();
		p->pObject = pOwner;
		if (pluginListAddr.getIndex(hInst) != -1)
			p->hOwner = hInst;
		else
			p->hOwner = GetInstByAddress((hInst != nullptr) ? (PVOID)hInst : GetCurrentThreadEntryPoint());
		p->pEntryPoint = hInst;

		threads.insert(p);

		ReleaseMutex(hStackMutex);
	}
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////

MIR_CORE_DLL(INT_PTR) Thread_Pop()
{
	if (WaitForSingleObject(hStackMutex, INFINITE) == WAIT_OBJECT_0) {
		DWORD dwThreadId = GetCurrentThreadId();
		for (int j = 0; j < threads.getCount(); j++) {
			THREAD_WAIT_ENTRY *p = threads[j];
			if (p->dwThreadId == dwThreadId) {
				SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_TIME_CRITICAL);
				CloseHandle(p->hThread);
				threads.remove(j);
				mir_free(p);

				if (!threads.getCount()) {
					threads.destroy();
					ReleaseMutex(hStackMutex);
					SetEvent(hThreadQueueEmpty); // thread list is empty now
					return 0;
				}

				ReleaseMutex(hStackMutex);
				return 0;
			}
		}
		ReleaseMutex(hStackMutex);
	}
	return 1;
}

/////////////////////////////////////////////////////////////////////////////////////////

const DWORD MS_VC_EXCEPTION=0x406D1388;

#pragma pack(push,8)
typedef struct tagTHREADNAME_INFO
{
	DWORD dwType; // Must be 0x1000.
	LPCSTR szName; // Pointer to name (in user addr space).
	DWORD dwThreadID; // Thread ID (-1=caller thread).
	DWORD dwFlags; // Reserved for future use, must be zero.
} THREADNAME_INFO;
#pragma pack(pop)

MIR_CORE_DLL(void) Thread_SetName(const char *szThreadName)
{
	THREADNAME_INFO info;
	info.dwType = 0x1000;
	info.szName = szThreadName;
	info.dwThreadID = GetCurrentThreadId();
	info.dwFlags = 0;

	__try {
		RaiseException(MS_VC_EXCEPTION, 0, sizeof(info) / sizeof(ULONG_PTR), (ULONG_PTR*)&info);
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{}
}
