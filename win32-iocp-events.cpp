#include "win32-iocp-events.h"
#include <Winternl.h>

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

DWORD WIN32_FROM_HRESULT (HRESULT hr) {
    if ((hr & 0xFFFF0000) == MAKE_HRESULT (SEVERITY_ERROR, FACILITY_WIN32, 0)) {
        return HRESULT_CODE (hr);
    } else
        return hr;
}

HANDLE WINAPI ReportEventAsCompletion (HANDLE hIOCP, HANDLE hEvent,
                                       DWORD dwNumberOfBytesTransferred, ULONG_PTR dwCompletionKey, LPOVERLAPPED lpOverlapped) {
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
        SetLastError (WIN32_FROM_HRESULT (hr));
    }
    return hPacket;
}

BOOL WINAPI RestartEventCompletion (HANDLE hPacket, HANDLE hIOCP, HANDLE hEvent, OVERLAPPED_ENTRY * completion) {
    BOOLEAN AlreadySignalled;
    HRESULT hr = NtAssociateWaitCompletionPacket (hPacket, hIOCP, hEvent,
                                                  (PVOID) completion->lpCompletionKey,
                                                  (PVOID) completion->lpOverlapped, 0,
                                                  completion->dwNumberOfBytesTransferred, &AlreadySignalled);
    if (SUCCEEDED (hr)) {
        return TRUE;
    } else {
        SetLastError (WIN32_FROM_HRESULT (hr));
    }
    return FALSE;
}

BOOL WINAPI CancelEventCompletion (HANDLE hWait, BOOL cancel) {
    HRESULT hr = NtCancelWaitCompletionPacket (hWait, cancel);
    if (SUCCEEDED (hr)) {
        return TRUE;
    } else {
        SetLastError (WIN32_FROM_HRESULT (hr));
        return FALSE;
    }
}


