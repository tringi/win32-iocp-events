#ifndef WIN32_IOCP_EVENTS_H
#define WIN32_IOCP_EVENTS_H

#include <Windows.h>

// ReportEventAsCompletion
//  - associates Event with I/O Completion Port and requests a completion packet when signalled
//  - parameters order modelled after PostQueuedCompletionStatus
//  - parameters: hIOCP - handle to I/O Completion Port
//                hEvent - handle to the Event object
//                dwNumberOfBytesTransferred - user-specified value, provided back by GetQueuedCompletionStatus(Ex)
//                dwCompletionKey - user-specified value, provided back by GetQueuedCompletionStatus(Ex)
//                lpOverlapped - user-specified value, provided back by GetQueuedCompletionStatus(Ex)
//  - returns: I/O Packet HANDLE for the association
//             NULL on failure, call GetLastError () for details (TBD)
//  - call CloseHandle to free the returned I/O Packet HANDLE when no longer needed
//
_Ret_maybenull_
HANDLE WINAPI ReportEventAsCompletion (_In_ HANDLE hIOCP,
                                       _In_ HANDLE hEvent,
                                       _In_opt_ DWORD dwNumberOfBytesTransferred,
                                       _In_opt_ ULONG_PTR dwCompletionKey,
                                       _In_opt_ LPOVERLAPPED lpOverlapped);

// RestartEventCompletion
//  - use to wait again, after the event completion was consumed by GetQueuedCompletionStatus(Ex)
//  - parameters: hPacket - is HANDLE returned by 'ReportEventAsCompletion'
//                hIOCP - handle to I/O Completion Port
//                hEvent - handle to the Event object
//                oEntry - pointer to data provided back by GetQueuedCompletionStatus(Ex)
//  - returns: TRUE on success
//             FALSE on failure, call GetLastError () for details (TBD)
//
BOOL WINAPI RestartEventCompletion (_In_ HANDLE hPacket, _In_ HANDLE hIOCP, _In_ HANDLE hEvent, _In_ OVERLAPPED_ENTRY * oEntry);

// CancelEventCompletion
//  - stops the Event from completing into the I/O Completion Port
//  - call CloseHandle to free the I/O Packet HANDLE when no longer needed
//  - parameters: hPacket - is HANDLE returned by 'ReportEventAsCompletion'
//                cancel - if TRUE, if already signalled, the completion packet is removed from queue
//  - returns: TRUE on success
//             FALSE on failure, call GetLastError () for details (TBD)
// 
BOOL WINAPI CancelEventCompletion (_In_ HANDLE hPacket, _In_ BOOL cancel);

#endif

