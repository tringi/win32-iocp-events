#include <Windows.h>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <set>

#include "WaitForUnlimitedObjectsEx.h"

HANDLE hQuit = NULL;
std::vector <HANDLE> events;
std::set <DWORD> verification;

void signal_handler (int) {
    std::printf ("Stopping...\n");
    SetEvent (hQuit);
}
VOID CALLBACK BlankAPCProc (_In_ ULONG_PTR dwParam) {
    std::printf ("APC!\n");
}

auto N = 2048u;

DWORD WINAPI producer (LPVOID) {
    std::srand (GetTickCount ());
    
    Sleep (100);
    while (WaitForSingleObject (hQuit, 1) == WAIT_TIMEOUT) {
        // select random handle
        auto i = std::rand () % N;
        std::printf ("Signalling %u\n", i);
        SetEvent (events [i]);
    }
    return 0;
}

int main (int argc, char ** argv) {

    if (argc > 1) {
        N = std::strtoul (argv [1], nullptr, 0);
    }

    std::printf ("TESTING %u\n", N);

    std::signal (SIGINT, signal_handler);
    std::signal (SIGTERM, signal_handler);
    std::signal (SIGBREAK, signal_handler);

    SetLastError (0);
    hQuit = CreateEvent (NULL, TRUE, FALSE, NULL);
    if (hQuit) {

        events.reserve (N + 1);

        for (auto i = 0u; i != N; ++i) {
            HANDLE hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
            if (hEvent) {
                events.push_back (hEvent);
            } else {
                std::printf ("Object %u creation failed, error %lu\n", i, GetLastError ());
                break;
            }
        }

        if (!events.empty ()) {
            QueueUserAPC (BlankAPCProc, GetCurrentThread (), NULL);

            // start thread signalling our events

            auto hThread = CreateThread (NULL, 0, producer, NULL, 0, NULL);

            // wait

            DWORD signalled = 0;
            BOOL result = WaitForUnlimitedObjectsEx (&signalled, N, &events [0], 1000, FALSE);

            if (result) {
                std::printf ("Object %u signalled.\n", signalled);

            } else {
                switch (GetLastError ()) {
                    case WAIT_TIMEOUT:
                        std::printf ("Wait timeouted.\n");
                        break;
                    case WAIT_IO_COMPLETION:
                        std::printf ("Wait interrupted by APC.\n");
                        break;
                    default:
                        std::printf ("Wait error %lu\n", GetLastError ());
                }
            }

            // wait for producer to finish

            if (hThread) {
                SetEvent (hQuit);
                WaitForSingleObject (hThread, INFINITE);
            }
        }
    }

    // cleanup events

    for (auto & event : events) {
        CloseHandle (event);
    }
    return (int) GetLastError ();
}
