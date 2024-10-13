#ifndef WINDOWS_UNLIMITEDWAIT_H
#define WINDOWS_UNLIMITEDWAIT_H

#include <Windows.h>

typedef VOID (WINAPI * PUNLIMITED_WAIT_CALLBACK) (PVOID lpWaitContext);
typedef BOOL (WINAPI * PUNLIMITED_WAIT_OBJECT_CALLBACK) (PVOID lpObjectContext, HANDLE hObject);

struct UnlimitedWait;

// CreateUnlimitedWait
//  - creates new 'UnlimitedWait' object and preallocates some object slots
//  - parameters:
//     - lpWaitContext - user-defined value, that is passed to callback functions
//     - nPreAllocatedSlots - number of slots for object handles to prepare in advance
//     - pfnTimeoutCallback - called with 'lpWaitContext' by WaitUnlimitedWait(Ex) on timeout
//     - pfnApcWakeCallback - called with 'lpWaitContext' by WaitUnlimitedWait(Ex) after APC interrupted the wait
//  - returns:
//     - 'handle' to the UnlimitedWait object to be used in the remaining functions
//     - NULL on error - call 'GetLastError()' to get the underlying reason which can also be HRESULT
//
_Success_ (return != NULL)
UnlimitedWait * WINAPI CreateUnlimitedWait (
    _In_opt_ PVOID lpWaitContext,
    _In_     DWORD nPreAllocatedSlots,
    _In_opt_ PUNLIMITED_WAIT_CALLBACK pfnTimeoutCallback,
    _In_opt_ PUNLIMITED_WAIT_CALLBACK pfnApcWakeCallback
);

// DeleteUnlimitedWait
//  - destroys the object and releases all resources
//  - there is no need to remove individual waited-on object handles
//  - returns: TRUE - on successful cleanup
//             FALSE - when any subcomponent failed to cleanup and memory/handles could've leaked
//                   - note that object that failed to be fully deleted can no longer be used
//
_Success_ (return != FALSE)
BOOL WINAPI DeleteUnlimitedWait (
    _In_ UnlimitedWait * hUnlimitedWait
);

// AddUnlimitedWaitObject
//  - adds object handle to 'UnlimitedWait' and starts consuming signalled state changes
//  - parameters:
//     - 'hObjectHandle' - handle to supported kernel objects (event, semaphore, process or thread)
//     - 'ptrCallbackFunction' - optional function called by WaitUnlimitedWait(Ex) 
//                               when the associated object signalled status is retrieved
//     - 'lpObjectContext' - pointer, that is passed to 'ptrCallbackFunction' (if provided) and
//                           returned by WaitUnlimitedWait(Ex) to identify signalled objects
//  - returns: TRUE - on success
//             FALSE - on failure, call GetLastError () to get more information:
//                   - may contain HRESULT on unexpected failures
//
_Success_ (return != FALSE)
BOOL WINAPI AddUnlimitedWaitObject (
    _In_     UnlimitedWait * hUnlimitedWait,
    _In_     HANDLE          hObjectHandle,
    _In_opt_ PUNLIMITED_WAIT_OBJECT_CALLBACK ptrCallbackFunction,
    _In_opt_ PVOID           lpObjectContext
);

// RemoveUnlimitedWaitObject
//  - removes object from 'UnlimitedWait' and stops consuming signalled state changes
//  - parameters:
//     - 'hObjectHandle' - handle to kernel object already added
//     - 'bKeepSignalsEnqueued' - TRUE - WaitUnlimitedWait(Ex) will still retrieve remaining signals that
//                                       occured before call to 'RemoveUnlimitedWaitObject'
//                              - FALSE - all unretrieved signals that occured before this call are deleted
//  - returns: TRUE - on successful removal
//             FALSE - on failure, call GetLastError () to get more information:
//                   - ERROR_FILE_NOT_FOUND - the 'hObjectHandle' is not associated with this UnlimitedWait,
//                                            possibly removed by callback function returning FALSE
//                   - may contain HRESULT on unexpected failures
//
_Success_ (return != FALSE)
BOOL WINAPI RemoveUnlimitedWaitObject (
    _In_ UnlimitedWait * hUnlimitedWait,
    _In_ HANDLE          hObjectHandle,
    _In_ BOOL            bKeepSignalsEnqueued
);

// WaitUnlimitedWait
//  - retrieves one (the oldest) object signalled status notifications
//  - calls 'ptrCallbackFunction' for that signalled object, if set
//  - parameters:
//     - lpSignalledObjectContext - receives context for the signalled object
//     - dwMilliseconds - when should the function fail (with WAIT_TIMEOUT error) if there's no notification
//                      - the 'pfnTimeoutCallback' function is called when that happens
//     - bAlertable - whether the function should process User APCs (and then fail with WAIT_IO_COMPLETION error)
//                  - the 'pfnApcWakeCallback' function is called when that happens
//  - return value models standard Windows wait APIs:
//     - TRUE if a notification was retrieved
//     - FALSE if no object was signalled, call GetLastError () to get more information:
//        - successes
//           - WAIT_IO_COMPLETION - User APC was retrieved and processed, due to 'bAlertable' being TRUE
//           - WAIT_TIMEOUT - 'dwMilliseconds' elapsed without any completion or User APC
//        - all other errors are hard values and :
//           - ERROR_ABANDONED_WAIT_0 - UnlimitedWait was deleted from underneath the function
//
_Success_ (return != FALSE)
BOOL WINAPI WaitUnlimitedWait (
    _In_      UnlimitedWait * hUnlimitedWait,
    _Out_opt_ PVOID *         lpSignalledObjectContext,
    _In_      DWORD           dwMilliseconds,
    _In_      BOOL            bAlertable
);

// WaitUnlimitedWaitEx
//  - retrieves up to 'ulCount' of object signalled status notifications
//  - calls 'ptrCallbackFunction' for each signalled object a notification was retrieved; if set
//  - parameters:
//     - lpSignalledObjectContexts - array that receives contexts of all retrieved objects
//     - lpTemporaryBuffer - to improve efficiency and reduce allocations, application may provide this scratch buffer
//                         - the buffer size must be 32 bytes × 'ulCount'
//     - ulCount - maxmimum number of notifications to retrieve
//     - ulNumEntriesProcessed - actual number of signal notifications retrieved
//                             - only this number of 'lpSignalledObjectContexts' array items is set
//     - dwMilliseconds - when should the function fail (with WAIT_TIMEOUT error) if there's no notification
//                      - the 'pfnTimeoutCallback' function is called when that happens
//     - bAlertable - whether the function should process User APCs (and then fail with WAIT_IO_COMPLETION error)
//                  - the 'pfnApcWakeCallback' function is called when that happens
//  - return value models standard Windows wait APIs:
//     - TRUE on successfully 
//     - FALSE if no object was signalled, call GetLastError () to get more information:
//        - successes
//           - WAIT_IO_COMPLETION - User APC was retrieved and processed, due to 'bAlertable' being TRUE
//           - WAIT_TIMEOUT - 'dwMilliseconds' elapsed without any completion or User APC
//        - all other errors are hard values and :
//           - ERROR_ABANDONED_WAIT_0 - UnlimitedWait was deleted from underneath the function
//
_Success_ (return != FALSE)
BOOL WINAPI WaitUnlimitedWaitEx (
    _In_ UnlimitedWait * hUnlimitedWait,
    _Out_writes_to_opt_ (ulCount,*ulNumEntriesProcessed) PVOID * lpSignalledObjectContexts, // array of 'ulCount'
    _Out_writes_bytes_all_opt_ (32 * ulCount)            PVOID lpTemporaryBuffer, // 32 * ulCount buffer
    _In_ _In_range_ (1, ULONG_MAX)                       ULONG ulCount,
    _Out_opt_                                            ULONG * ulNumEntriesProcessed,
    _In_ DWORD dwMilliseconds,
    _In_ BOOL bAlertable
);


#endif
