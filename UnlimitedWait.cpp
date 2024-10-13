#include "UnlimitedWait.h"
#include <Winternl.h>
#include <ntstatus.h>

extern "C" {
    WINBASEAPI NTSTATUS WINAPI NtCreateWaitCompletionPacket (
        _Out_ PHANDLE WaitCompletionPacketHandle,
        _In_ ACCESS_MASK DesiredAccess,
        _In_opt_ POBJECT_ATTRIBUTES ObjectAttributes
    );
    WINBASEAPI NTSTATUS WINAPI NtAssociateWaitCompletionPacket (
        _In_ HANDLE WaitCompletionPacketHandle,
        _In_ HANDLE IoCompletionHandle,
        _In_ HANDLE TargetObjectHandle,
        _In_opt_ PVOID KeyContext,
        _In_opt_ PVOID ApcContext,
        _In_ NTSTATUS IoStatus,
        _In_ ULONG_PTR IoStatusInformation,
        _Out_opt_ PBOOLEAN AlreadySignaled
    );
    WINBASEAPI NTSTATUS WINAPI NtCancelWaitCompletionPacket (
        _In_ HANDLE WaitCompletionPacketHandle,
        _In_ BOOLEAN RemoveSignaledPacket
    );

    DWORD ConvertHResult (HRESULT hr, HANDLE h) {
        switch (hr) {
            case STATUS_NO_MEMORY:
                return ERROR_OUTOFMEMORY;

            case STATUS_INVALID_HANDLE: // not valid handle passed for hIOCP
            case STATUS_OBJECT_TYPE_MISMATCH: // incorrect handle passed for hIOCP
            case STATUS_INVALID_PARAMETER_1:
            case STATUS_INVALID_PARAMETER_2:
                return ERROR_INVALID_PARAMETER;

            case STATUS_INVALID_PARAMETER_3:
                if (h) {
                    return ERROR_INVALID_HANDLE;
                } else {
                    return ERROR_INVALID_PARAMETER;
                }
        }
        return hr;
    }

    struct UnlimitedWaitSlot {
        HANDLE hWaitPacket;
        HANDLE hObject;
    };
}

struct UnlimitedWait {
    HANDLE  hIOCP;
    SRWLOCK srwLock;
    PVOID   lpWaitContext;
    PUNLIMITED_WAIT_CALLBACK pfnTimeoutCallback;
    PUNLIMITED_WAIT_CALLBACK pfnApcWakeCallback;
    UnlimitedWaitSlot *      slots;
};

_Success_ (return != NULL)
UnlimitedWait * WINAPI CreateUnlimitedWait (
    _In_opt_ PVOID lpWaitContext,
    _In_     DWORD nPreAllocatedSlots,
    _In_opt_ PUNLIMITED_WAIT_CALLBACK pfnTimeoutCallback,
    _In_opt_ PUNLIMITED_WAIT_CALLBACK pfnApcWakeCallback
) {
    HANDLE hHeap = GetProcessHeap ();
    UnlimitedWait * instance = (UnlimitedWait *) HeapAlloc (hHeap, 0, sizeof (UnlimitedWait));

    if (instance) {
        instance->hIOCP = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (instance->hIOCP) {
            instance->srwLock = SRWLOCK_INIT;
            instance->lpWaitContext = lpWaitContext;
            instance->pfnTimeoutCallback = pfnTimeoutCallback;
            instance->pfnApcWakeCallback = pfnApcWakeCallback;

            if (nPreAllocatedSlots == 0) {
                nPreAllocatedSlots = 8;
            }

            instance->slots = (UnlimitedWaitSlot *) HeapAlloc (hHeap, 0, nPreAllocatedSlots * sizeof (UnlimitedWaitSlot));
            if (instance->slots) {

                HRESULT hr = 0;
                DWORD nCreatedPackets = 0;
                while (SUCCEEDED (hr = NtCreateWaitCompletionPacket (&instance->slots [nCreatedPackets].hWaitPacket, GENERIC_ALL, NULL))) {
                    instance->slots [nCreatedPackets].hObject = NULL;

                    if (++nCreatedPackets == nPreAllocatedSlots) {
                        return instance;
                    }
                }

                while (nCreatedPackets--) {
                    CloseHandle (instance->slots [nCreatedPackets].hWaitPacket);
                }
                HeapFree (hHeap, 0, instance->slots);
                SetLastError (ConvertHResult (hr, NULL));
            } else {
                SetLastError (ERROR_OUTOFMEMORY);
            }

            CloseHandle (instance->hIOCP);
        }
        HeapFree (hHeap, 0, instance);
    } else {
        SetLastError (ERROR_OUTOFMEMORY);
    }
    return NULL;
}

_Success_ (return != FALSE)
BOOL WINAPI DeleteUnlimitedWait (
    _In_ UnlimitedWait * instance
) {
    if (!instance) {
        SetLastError (ERROR_INVALID_HANDLE);
        return FALSE;
    }

    BOOL result = TRUE;
    HANDLE hHeap = GetProcessHeap ();

    AcquireSRWLockExclusive (&instance->srwLock);

    if (instance->slots) {
        SIZE_T nSlots = HeapSize (hHeap, 0, instance->slots) / sizeof (UnlimitedWaitSlot);
        while (nSlots--) {
            CloseHandle (instance->slots [nSlots].hWaitPacket);
        }

        if (!HeapFree (hHeap, 0, instance->slots)) {
            result = FALSE;
        }
    }

    ReleaseSRWLockExclusive (&instance->srwLock);

    if (!CloseHandle (instance->hIOCP)) {
        result = FALSE;
    }
    if (!HeapFree (hHeap, 0, instance)) {
        result = FALSE;
    }
    return result;
}

namespace {
    BOOL SetAssociation (UnlimitedWait * instance, SIZE_T i, HANDLE hObjectHandle, PVOID ptrCallbackFunction, PVOID lpObjectContext) {
        
        HRESULT hr = NtAssociateWaitCompletionPacket (instance->slots [i].hWaitPacket, instance->hIOCP, hObjectHandle,
                                                      ptrCallbackFunction, lpObjectContext, 0, i, NULL);
        if (SUCCEEDED (hr)) {
            instance->slots [i].hObject = hObjectHandle;
            return TRUE;

        } else {
            SetLastError (ConvertHResult (hr, hObjectHandle));
            return FALSE;
        }
    }
}

_Success_ (return != FALSE)
BOOL WINAPI AddUnlimitedWaitObject (
    _In_ UnlimitedWait * instance,
    _In_ HANDLE hObjectHandle,
    _In_opt_ PUNLIMITED_WAIT_OBJECT_CALLBACK ptrCallbackFunction,
    _In_opt_ PVOID lpObjectContext
) {
    if (!instance) {
        SetLastError (ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!hObjectHandle) {
        SetLastError (ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    AcquireSRWLockExclusive (&instance->srwLock);

    HANDLE hHeap = GetProcessHeap ();
    SIZE_T nSlots = HeapSize (hHeap, 0, instance->slots) / sizeof (UnlimitedWaitSlot);

    for (SIZE_T i = 0; i != nSlots; ++i) {
        if ((instance->slots [i].hWaitPacket != NULL) && (instance->slots [i].hObject == NULL)) {
            BOOL result = SetAssociation (instance, i, hObjectHandle, (PVOID) ptrCallbackFunction, (PVOID) lpObjectContext);

            ReleaseSRWLockExclusive (&instance->srwLock);
            return result;
        }
    }

    if (auto newSlots = HeapReAlloc (hHeap, 0, instance->slots, (nSlots + 1) * sizeof (UnlimitedWaitSlot))) {
        instance->slots = (UnlimitedWaitSlot *) newSlots;
        instance->slots [nSlots].hWaitPacket = NULL;
        instance->slots [nSlots].hObject = NULL;

        HRESULT hr = NtCreateWaitCompletionPacket (&instance->slots [nSlots].hWaitPacket, GENERIC_ALL, NULL);
        if (SUCCEEDED (hr)) {

            if (SetAssociation (instance, nSlots, hObjectHandle, (PVOID) ptrCallbackFunction, (PVOID) lpObjectContext)) {
                ReleaseSRWLockExclusive (&instance->srwLock);
                return TRUE;
            }

            NtCancelWaitCompletionPacket (instance->slots [nSlots].hWaitPacket, TRUE);

        } else {
            if (auto revertedSlots = HeapReAlloc (hHeap, 0, instance->slots, nSlots * sizeof (UnlimitedWaitSlot))) {
                instance->slots = (UnlimitedWaitSlot *) revertedSlots;
            } else {
                SetLastError (ERROR_OUTOFMEMORY);
            }
        }
    } else {
        SetLastError (ERROR_OUTOFMEMORY);
    }

    ReleaseSRWLockExclusive (&instance->srwLock);
    return FALSE;
}

_Success_ (return != FALSE)
BOOL WINAPI RemoveUnlimitedWaitObject (
    _In_ UnlimitedWait * instance,
    _In_ HANDLE hObjectHandle,
    _In_ BOOL bKeepSignalsEnqueued
) {
    if (!instance) {
        SetLastError (ERROR_INVALID_HANDLE);
        return FALSE;
    }
    if (!hObjectHandle) {
        SetLastError (ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    AcquireSRWLockExclusive (&instance->srwLock);

    SIZE_T nSlots = HeapSize (GetProcessHeap (), 0, instance->slots) / sizeof (UnlimitedWaitSlot);
    for (SIZE_T i = 0; i != nSlots; ++i) {

        if (instance->slots [i].hObject == hObjectHandle) {
            HRESULT hr = NtCancelWaitCompletionPacket (instance->slots [i].hWaitPacket, !bKeepSignalsEnqueued);
            BOOL result = SUCCEEDED (hr);

            if (result) {
                instance->slots [i].hObject = NULL;
            }

            ReleaseSRWLockExclusive (&instance->srwLock);

            if (!result) {
                SetLastError (ConvertHResult (hr, NULL));
            }
            return result;
        }
    }

    ReleaseSRWLockExclusive (&instance->srwLock);
    SetLastError (ERROR_FILE_NOT_FOUND);
    return FALSE;
}

static
BOOL WINAPI WaitUnlimitedWaitExImplementation (
    _In_ UnlimitedWait * instance,
    _Out_writes_to_opt_ (ulCount, *ulNumEntriesProcessed) PVOID * lpSignalledObjectContexts,
    _In_ ULONG ulCount,
    _Out_opt_ ULONG * ulNumEntriesProcessed,
    _Out_writes_all_ (ulCount) OVERLAPPED_ENTRY * oResults,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable
) {
    if (!instance) {
        SetLastError (ERROR_INVALID_HANDLE);
        return FALSE;
    }

    AcquireSRWLockShared (&instance->srwLock);

    DWORD nCompletions;
    if (GetQueuedCompletionStatusEx (instance->hIOCP, oResults, ulCount, &nCompletions, dwMilliseconds, bAlertable)) {
        
        if (ulNumEntriesProcessed) {
            *ulNumEntriesProcessed = nCompletions;
        }

        BOOL result = TRUE;
        for (ULONG i = 0; i != nCompletions; ++i) {

            BOOL bReRegister;
            if (oResults [i].lpCompletionKey) {

                // TODO: user may want to call add/remove inside the callback

                bReRegister = ((PUNLIMITED_WAIT_OBJECT_CALLBACK) oResults [i].lpCompletionKey) (oResults [i].lpOverlapped,
                                                                                                instance->slots [oResults [i].dwNumberOfBytesTransferred].hObject);
            } else {
                bReRegister = TRUE;
            }

            if (bReRegister) {
                if (!SetAssociation (instance, oResults [i].dwNumberOfBytesTransferred,
                                     instance->slots [oResults [i].dwNumberOfBytesTransferred].hObject,
                                     (PVOID) oResults [i].lpCompletionKey, (PVOID) oResults [i].lpOverlapped)) {

                    result = FALSE;
                }
            } else {
                instance->slots [oResults [i].dwNumberOfBytesTransferred].hObject = NULL;
            }

            if (lpSignalledObjectContexts) {
                lpSignalledObjectContexts [i] = (PVOID) oResults [i].lpOverlapped;
            }
        }

        ReleaseSRWLockShared (&instance->srwLock);
        return result;

    } else {
        if (ulNumEntriesProcessed) {
            *ulNumEntriesProcessed = 0;
        }

        DWORD error = GetLastError ();
        switch (error) {
            case WAIT_TIMEOUT:
                if (instance->pfnTimeoutCallback) {
                    instance->pfnTimeoutCallback (instance->lpWaitContext);
                }
                break;
            case WAIT_IO_COMPLETION:
                if (instance->pfnApcWakeCallback) {
                    instance->pfnApcWakeCallback (instance->lpWaitContext);
                }
                break;

            case ERROR_ABANDONED_WAIT_0:
                // object deleted, srwLock is no longer valid, cannot unlock
                return FALSE;

            default:
                ReleaseSRWLockShared (&instance->srwLock);
                return FALSE;
        }
        ReleaseSRWLockShared (&instance->srwLock);
        SetLastError (error);
        return FALSE;
    }
}

_Success_ (return != FALSE)
BOOL WINAPI WaitUnlimitedWait (
    _In_ UnlimitedWait * instance,
    _Out_opt_ PVOID * lpSignalledObjectContext,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable
) {
    OVERLAPPED_ENTRY oResult = {};
    return WaitUnlimitedWaitExImplementation (instance, lpSignalledObjectContext, 1, NULL, &oResult, dwMilliseconds, bAlertable);
}

_Success_ (return != FALSE)
BOOL WINAPI WaitUnlimitedWaitEx (
    _In_ UnlimitedWait * instance,
    _Out_writes_to_opt_ (ulCount, *ulNumEntriesProcessed) PVOID * lpSignalledObjectContexts,
    _Out_writes_bytes_all_opt_ (32 * ulCount) PVOID lpTemporaryBuffer,
    _In_ _In_range_ (1, ULONG_MAX) ULONG ulCount,
    _Out_opt_ ULONG * ulNumEntriesProcessed,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable
) {
    HANDLE hHeap = NULL;
    OVERLAPPED_ENTRY * oResults;
    
    if (lpTemporaryBuffer) {
        oResults = (OVERLAPPED_ENTRY *) lpTemporaryBuffer;
    } else {
        hHeap = GetProcessHeap ();
        oResults = (OVERLAPPED_ENTRY *) HeapAlloc (hHeap, 0, ulCount * sizeof (OVERLAPPED_ENTRY));
        if (!oResults) {
            SetLastError (ERROR_OUTOFMEMORY);
            return FALSE;
        }
    }

    BOOL bResult = WaitUnlimitedWaitExImplementation (instance, lpSignalledObjectContexts, ulCount, ulNumEntriesProcessed, oResults, dwMilliseconds, bAlertable);

    if (!lpTemporaryBuffer) {
        HeapFree (hHeap, 0, oResults);
    }
    return bResult;
}
