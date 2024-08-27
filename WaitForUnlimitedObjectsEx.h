#ifndef WINDOWS_WAITFORUNLIMITEDOBJECTSEX_H
#define WINDOWS_WAITFORUNLIMITEDOBJECTSEX_H

#include <Windows.h>

// WaitForUnlimitedObjectsEx
//  - WaitForMultipleObjects(Ex) but UNLIMITED, unrestricted by MAXIMUM_WAIT_OBJECTS
//     - actual (OS) limitations here would make the limit about 8 million handles
//  - the signature was changed to avoid bugs when directly replacing WaitForMultipleObjects(Ex)
//  - return value:
//     - TRUE on successfully signalled event
//        - 'dwIndexOfSignalledObject' is set to index of the first signalled object
//     - FALSE on timeout, APC, or error
//        - call 'GetLastError()' to get the actual reason:
//            - WAIT_TIMEOUT (on timeout)
//            - WAIT_IO_COMPLETION (when APC interrupted the wait)
//            - other errors mean actual failure
//  - IMPORTANT DIFFERENCES:
//     - does NOT support acquiring Mutexes!
//     - does NOT support waiting for all to become signalled
//     - return value does NOT match WaitForMultipleObjectsEx
//
_Success_ (return != FALSE)
BOOL WINAPI WaitForUnlimitedObjectsEx (
    _Out_opt_ DWORD * dwIndexOfSignalledObject,
    _In_ DWORD nCount,
    _In_reads_ (nCount) CONST HANDLE * lpHandles,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable
);

#endif
