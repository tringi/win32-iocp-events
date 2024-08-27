#include "WaitForUnlimitedObjectsEx.h"
#include <Winternl.h>

#pragma warning (disable:28159) // GetTickCount

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
}

_Success_ (return != FALSE)
BOOL WINAPI WaitForUnlimitedObjectsEx (
    _Out_opt_ DWORD * dwIndexOfSignalledObject,
    _In_ DWORD nCount,
    _In_reads_ (nCount) CONST HANDLE * lpHandles,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable
) {
    
    if ((nCount == 0) || (lpHandles == NULL)) {
        SetLastError (ERROR_INVALID_PARAMETER);
        return FALSE;
    }

    BOOL bResult = FALSE;
    DWORD dwError = 0;

    // allocate space for completion packet handles, one for each object handle

    HANDLE * hPackets = (HANDLE *) HeapAlloc (GetProcessHeap (), 0, nCount * sizeof (HANDLE));
    if (hPackets) {

        HANDLE hIOCP = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (hIOCP) {

            // create completion packets, one for each object

            DWORD nCreatedPackets = 0;
            while (SUCCEEDED (NtCreateWaitCompletionPacket (&hPackets [nCreatedPackets], GENERIC_ALL, NULL))) {
                if (++nCreatedPackets == nCount)
                    break;
            }
            if (nCreatedPackets == nCount) {
                
                // associate all packets with out IOCP

                static DWORD dwShiftNonce = 0;

                DWORD dwRandomShift = GetTickCount () + dwShiftNonce++;
                DWORD nAssociatedPackets = 0;

                for (; nAssociatedPackets != nCreatedPackets; ++nAssociatedPackets) {
                    
                    BOOLEAN bAlreadySignalled = FALSE;
                    DWORD index = (nAssociatedPackets + dwRandomShift) % nCreatedPackets;

                    if (SUCCEEDED (NtAssociateWaitCompletionPacket (hPackets [index], hIOCP,
                                                                    lpHandles [index], NULL, NULL, 0,
                                                                    index, &bAlreadySignalled))) {
                        
                        // if object is already signalled, report it and clean up
                        
                        if (bAlreadySignalled) {
                            if (dwIndexOfSignalledObject) {
                                *dwIndexOfSignalledObject = nAssociatedPackets;
                            }
                            bResult = TRUE;
                            break;
                        }

                    } else {
                        SetLastError (ERROR_OUTOFMEMORY);
                        break;
                    }
                }

                if ((nAssociatedPackets == nCreatedPackets) && !bResult) {

                    DWORD nCompletions;
                    OVERLAPPED_ENTRY oResult;

                    if (GetQueuedCompletionStatusEx (hIOCP, &oResult, 1, &nCompletions, dwMilliseconds, bAlertable)) {
                        if (dwIndexOfSignalledObject) {
                            *dwIndexOfSignalledObject = oResult.dwNumberOfBytesTransferred;
                        }
                        bResult = TRUE;
                    }
                }
            } else {
                SetLastError (ERROR_OUTOFMEMORY); // handle pool exhausted?
            }

            // destroy all objects create to wait
            //  - no need to call 'NtCancelWaitCompletionPacket' as both associated parties are going away

            while (nCreatedPackets--) {
                CloseHandle (hPackets [nCreatedPackets]);
            }
            CloseHandle (hIOCP);
        }
        HeapFree (GetProcessHeap (), 0, hPackets);
    }
    if (!bResult && dwError) {
        SetLastError (dwError);
    }
    return bResult;
}