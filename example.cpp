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
std::set <WORD> verification;

void signal_handler (int) {
    std::printf ("Stopping...\n");
    SetEvent (hQuit);
}

DWORD WINAPI producer (LPVOID) {
    while (WaitForSingleObject (hQuit, 1) == WAIT_TIMEOUT) {
        SetEvent (events [std::rand () % events.size ()]);
    }
    return 0;
}

#define N 2048u
#define EVENT_KEY_BASE 0xE000

int main () {
    std::signal (SIGINT, signal_handler);
    std::signal (SIGTERM, signal_handler);
    std::signal (SIGBREAK, signal_handler);

    SetLastError (0);
    hQuit = CreateEvent (NULL, TRUE, FALSE, NULL);

    if (auto hIOCP = CreateIoCompletionPort (INVALID_HANDLE_VALUE, NULL, 0x1234, 0)) {

        // create a lot of events

        events.reserve (N + 1);
        waits.reserve (N);

        for (auto i = 0u; i != N; ++i) {
            if (auto hEvent = CreateEvent (NULL, FALSE, FALSE, NULL)) {
                events.push_back (hEvent);
            } else {
                std::printf ("CreateEvent %u failed, error %lu\n", i, GetLastError ());
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
                }
            }
            ReportEventAsCompletion (hIOCP, hQuit, 0, 0, NULL);

            // start thread signalling our events

            auto hThread = CreateThread (NULL, 0, producer, NULL, 0, NULL);

            // process IOCP events
            //  - key 0 means hQuit

            OVERLAPPED_ENTRY completion;
            ULONG n;
            do {
                BOOL success = GetQueuedCompletionStatusEx (hIOCP, &completion, 1, &n, INFINITE, FALSE);

                verification.insert ((WORD) completion.lpCompletionKey);

                std::printf ("Signal (%u): %llx %x %p /*%llu*/ distinct events: %llu\n",
                             success,
                             (unsigned long long) completion.lpCompletionKey, completion.dwNumberOfBytesTransferred, completion.lpOverlapped,
                             (unsigned long long) completion.Internal,
                             (unsigned long long) verification.size ());

                if (completion.lpCompletionKey >= EVENT_KEY_BASE && completion.lpCompletionKey < EVENT_KEY_BASE + N) {
                    HANDLE hPacket = waits [completion.lpCompletionKey - EVENT_KEY_BASE];
                    HANDLE hEvent = events [completion.lpCompletionKey - EVENT_KEY_BASE];

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
            CancelEventCompletion (wait, true);
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
