# Win32: Processing Event Objects through I/O Completion Port

*Example of waiting for [Event Objects](https://learn.microsoft.com/en-us/windows/win32/sync/using-event-objects)
by associating them with a [I/O Completion Port](https://learn.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports) (IOCP),
effectively lifting [MAXIMUM_WAIT_OBJECTS](https://stackoverflow.com/questions/5131807/is-maximum-wait-objects-really-64) limit of
[WaitForMultipleObjects](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects)([Ex](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjectsex)) API.*

## The problem

Sometimes programs need to wait for a lot of objects.
But [WaitForMultipleObjects](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjects)([Ex](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-waitformultipleobjectsex)) APIs are limited to
64 ([MAXIMUM_WAIT_OBJECTS](https://stackoverflow.com/questions/5131807/is-maximum-wait-objects-really-64)).
To work around this limit, programs have to resolve to various complicated solutions, like starting multiple threads, refactor their logic, etc.

## The old, limited solutions

Up until Windows 8 the [Thread Pool API](https://learn.microsoft.com/en-us/windows/win32/procthread/thread-pool-api)
[wait operation](https://learn.microsoft.com/en-us/windows/win32/api/threadpoolapiset/nf-threadpoolapiset-createthreadpoolwait)
too used multiple threads. This is inefficient so it was refactored to use internal
[NtAssociateWaitCompletionPacket](https://learn.microsoft.com/en-us/windows/win32/devnotes/ntassociatewaitcompletionpacket) call.

You can now wait for thousands of events, but only in a threadpool.
If your program for some reason can't (isn't thread-safe, or uses own IOCP), you were out of luck.

## The new, proper solution

The full solution is to use that internal, barely documented, APIs. This repository shows how in several examples:

**[ReportEventAsCompletion](win32-iocp-events.h) set of APIs**  
implemented in [win32-iocp-events.cpp](win32-iocp-events.cpp) are direct simple abstractions of the NT functions.
These offer full control and are proper solution if the application already uses IOCPs.

* [example.cpp](example.cpp) shows how are they used directly by processing large number of events on a single thread

**[WaitForUnlimitedObjectsEx.h](WaitForUnlimitedObjectsEx.h)**  
is almost direct replacement of WaitForMultipleObjectsEx, but quite inefficient as the IOCP and associations are rebuilt for each call.
There is small optimization: early exit, when any of the objects is already signalled, with randomized order to make it more fair.

* [example-WaitForUnlimitedObjectsEx.cpp](example-WaitForUnlimitedObjectsEx.cpp) shows the straightforward usage

**[UnlimitedWait.h](UnlimitedWait.h)**  
is the most efficient way to use this facility along the lines of WaitForMultipleObjectsEx. But instead you build a UnlimitedWait object,
add event handles, and then repeatedly retrieve signals using a single call. It also allows user to set up callback functions.

* [example-UnlimitedWait.cpp](example-UnlimitedWait.cpp) shows how to construct and use of the batch retrieval

## Notes

* Implementations provided are experimental, not thoroughly tested, and certainly not ready for production!
* These implementations have different semantics to WaitForMultipleObjectsEx. Most importantly signalled object statuses are not coalesced.
* The API supports waiting for Semaphores, Threads and Processes, not just Events.
* The API does NOT support acquiring Mutexes.
* The API does NOT support waiting for ALL object to be set at the same time.

## Resources

* [All Windows threadpool waits can now be handled by a single thread](https://devblogs.microsoft.com/oldnewthing/20220406-00/?p=106434).
* [What if I need to wait for more than MAXIMUM_WAIT_OBJECTS threads?](https://devblogs.microsoft.com/oldnewthing/20240823-00/?p=110169)
