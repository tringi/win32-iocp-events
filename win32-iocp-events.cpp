#include "win32-iocp-events.h"
#include <Winternl.h>
#include <ntstatus.h>

#pragma warning (disable:4005) // macro redefinition

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
}

_Ret_maybenull_
HANDLE WINAPI ReportEventAsCompletion (_In_ HANDLE hIOCP, _In_ HANDLE hEvent,
                                       _In_opt_ DWORD dwNumberOfBytesTransferred, _In_opt_ ULONG_PTR dwCompletionKey, _In_opt_ LPOVERLAPPED lpOverlapped) {
    HANDLE hPacket = NULL;
    HRESULT hr = NtCreateWaitCompletionPacket (&hPacket, GENERIC_ALL, NULL);

    if (SUCCEEDED (hr)) {

        OVERLAPPED_ENTRY completion {};
        completion.dwNumberOfBytesTransferred = dwNumberOfBytesTransferred;
        completion.lpCompletionKey = dwCompletionKey;
        completion.lpOverlapped = lpOverlapped;

        if (!RestartEventCompletion (hPacket, hIOCP, hEvent, &completion)) {
            NtClose (hPacket);
            hPacket = NULL;
        }
    } else {
        switch (hr) {
            case STATUS_NO_MEMORY:
                SetLastError (ERROR_OUTOFMEMORY);
                break;
            default:
                SetLastError (hr);
        }
    }
    return hPacket;
}

BOOL WINAPI RestartEventCompletion (_In_ HANDLE hPacket, _In_ HANDLE hIOCP, _In_ HANDLE hEvent, _In_ const OVERLAPPED_ENTRY * completion) {
    if (!completion) {
        SetLastError (ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    BOOLEAN AlreadySignalled;
    HRESULT hr = NtAssociateWaitCompletionPacket (hPacket, hIOCP, hEvent,
                                                  (PVOID) completion->lpCompletionKey,
                                                  (PVOID) completion->lpOverlapped, 0,
                                                  completion->dwNumberOfBytesTransferred, &AlreadySignalled);
    if (SUCCEEDED (hr)) {
        return TRUE;

    } else {
        switch (hr) {
            case STATUS_NO_MEMORY:
                SetLastError (ERROR_OUTOFMEMORY);
                break;
            case STATUS_INVALID_HANDLE: // not valid handle passed for hIOCP
            case STATUS_OBJECT_TYPE_MISMATCH: // incorrect handle passed for hIOCP
            case STATUS_INVALID_PARAMETER_1:
            case STATUS_INVALID_PARAMETER_2:
                SetLastError (ERROR_INVALID_PARAMETER);
                break;
            case STATUS_INVALID_PARAMETER_3:
                if (hEvent) {
                    SetLastError (ERROR_INVALID_HANDLE);
                } else {
                    SetLastError (ERROR_INVALID_PARAMETER);
                }
                break;
            default:
                SetLastError (hr);
        }
        return FALSE;
    }
}

BOOL WINAPI CancelEventCompletion (_In_ HANDLE hWait, _In_ BOOL cancel) {
    HRESULT hr = NtCancelWaitCompletionPacket (hWait, cancel);
    if (SUCCEEDED (hr)) {
        return TRUE;
    } else {
        SetLastError (hr);
        return FALSE;
    }
}


