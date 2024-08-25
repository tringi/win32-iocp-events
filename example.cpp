#include <Windows.h>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <set>

#include "win32-iocp-events.h"

HANDLE hQuit = NULL;
std::vector <HANDLE> events;
std::vector <HANDLE> waits;
std::set <DWORD> verification;

enum TestType {
    TestEvents = 0,
    TestSemaphores,
    TestThreads,
} test_type {};

void signal_handler (int) {
    std::printf ("Stopping...\n");
    SetEvent (hQuit);
}

DWORD WINAPI blank_thread (LPVOID) {
    std::printf ("Thread %u\n", GetCurrentThreadId ());
    return 0;
}

DWORD WINAPI producer (LPVOID) {
    while (WaitForSingleObject (hQuit, 1) == WAIT_TIMEOUT) {

        // select random handle
        auto h = events [std::rand () % events.size ()];

        // signal it
        switch (test_type) {
            case TestEvents:
                SetEvent (h);
                break;

            case TestSemaphores:
                ReleaseSemaphore (h, 1, NULL);
                break;

            case TestThreads:
                ResumeThread (h);
                break;
        }
    }
    return 0;
}

#define EVENT_KEY_BASE 0xE0000000
auto N = 2048u;

int main (int argc, char ** argv) {

    if (argc > 1) {
        N = std::strtoul (argv [1], nullptr, 0);
    }
    if (argc > 2) {
        if (!std::strcmp (argv [2], "evt")) test_type = TestEvents;
        if (!std::strcmp (argv [2], "sem")) test_type = TestSemaphores;
        if (!std::strcmp (argv [2], "thr")) test_type = TestThreads;
    }

    std::printf ("TESTING %u", N);
    switch (test_type) {
        case TestEvents: std::printf (" events\n"); break;
        case TestSemaphores: std::printf (" semaphores\n"); break;
        case TestThreads: std::printf (" threads\n"); break;
    }


    std::signal (SIGINT, signal_handler);
    std::signal (SIGTERM, signal_handler);
    std::signal (SIGBREAK, signal_handler);

    SetLastError (0);
    hQuit = CreateEvent (NULL, TRUE, FALSE, NULL);

    if (auto hIOCP = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0, 0)) {

        // create a lot of events

        events.reserve (N + 1);
        waits.reserve (N);

        for (auto i = 0u; i != N; ++i) {

            HANDLE hEvent = NULL;

            switch (test_type) {
                case TestEvents:
                    hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
                    break;
                case TestSemaphores:
                    hEvent = CreateSemaphore (NULL, 0, 1, NULL);
                    break;
                case TestThreads:
                    hEvent = CreateThread (NULL, 65536, blank_thread, NULL, CREATE_SUSPENDED, NULL);
                    break;
            }

            if (hEvent) {
                events.push_back (hEvent);
            } else {
                std::printf ("Object %u creation failed, error %lu\n", i, GetLastError ());
                break;
            }
        }

        if (!events.empty ()) {

            // register to get those events through IOCP

            auto i = 0u;
            for (auto & event : events) {
                auto h = ReportEventAsCompletion (hIOCP, event, 0, EVENT_KEY_BASE + i++, NULL);
                if (h) {
                    waits.push_back (h);
                } else {
                    std::printf ("ReportEventAsCompletion %u failed, error %lu\n", i, GetLastError ());
                }
            }
            if (!ReportEventAsCompletion (hIOCP, hQuit, 0, 0, NULL)) {
                std::printf ("ReportEventAsCompletion for hQuit failed, error %lu\n", GetLastError ());
            }

            // start thread signalling our events

            auto hThread = CreateThread (NULL, 0, producer, NULL, 0, NULL);

            // process IOCP events
            //  - key 0 means hQuit

            OVERLAPPED_ENTRY completion;
            ULONG n;
            do {
                BOOL success = GetQueuedCompletionStatusEx (hIOCP, &completion, 1, &n, INFINITE, FALSE);

                verification.insert ((DWORD) completion.lpCompletionKey);

                std::printf ("Signal (%u): %llx %x %p /*%llu*/ distinct objects: %llu/%llu\n",
                             success,
                             (unsigned long long) completion.lpCompletionKey, completion.dwNumberOfBytesTransferred, completion.lpOverlapped,
                             (unsigned long long) completion.Internal,
                             (unsigned long long) verification.size (),
                             (unsigned long long) N);

                if (completion.lpCompletionKey >= EVENT_KEY_BASE) {
                    HANDLE & hPacket = waits [completion.lpCompletionKey - EVENT_KEY_BASE];
                    HANDLE & hEvent = events [completion.lpCompletionKey - EVENT_KEY_BASE];

                    switch (test_type) {
                        case TestThreads:
                            CloseHandle (hEvent);
                            hEvent = CreateThread (NULL, 65536, blank_thread, NULL, CREATE_SUSPENDED, NULL);
                            break;
                    }

                    if (!RestartEventCompletion (hPacket, hIOCP, hEvent, &completion)) {
                        std::printf ("RestartWait failed, error %lu\n", GetLastError ());
                    }
                }

            } while (completion.lpCompletionKey);

            // wait for producer to finish

            if (hThread) {
                WaitForSingleObject (hThread, INFINITE);
            }
        }

        // cleanup waits

        for (auto & wait : waits) {
            if (!CancelEventCompletion (wait, true)) {
                std::printf ("RestartWait failed, error %lu\n", GetLastError ());
            }
            CloseHandle (wait);
        }

        // cleanup events

        for (auto & event : events) {
            CloseHandle (event);
        }
        CloseHandle (hIOCP);
    } else {
        std::printf ("CreateIoCompletionPort failed, error %lu\n", GetLastError ());
    }
    return (int) GetLastError ();
}
