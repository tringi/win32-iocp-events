#include <Windows.h>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <set>

#include "UnlimitedWait.h"

HANDLE hQuit = CreateEvent (NULL, TRUE, FALSE, NULL);

std::vector <HANDLE> events;
UnlimitedWait * volatile wait = NULL;

void signal_handler (int) {
    std::printf ("Stopping...\n");
    SetEvent (hQuit);
    // DeleteUnlimitedWait (wait);
    // wait = nullptr;
}
VOID CALLBACK BlankAPCProc (_In_ ULONG_PTR dwParam) {
    std::printf ("Inside APC...\n");
}

auto N = 2048u;

DWORD WINAPI producer (LPVOID hMainThread) {
    std::srand (GetTickCount ());
    
    Sleep (100);
    while (WaitForSingleObject (hQuit, 0) == WAIT_TIMEOUT) {
    //while (wait != nullptr) {
        switch (std::rand () % 16) {
            case 0:
                if (hMainThread) {
                    QueueUserAPC (BlankAPCProc, hMainThread, NULL);
                }
                break;
            case 1:
                Sleep (50);
                break;
            default:
                auto i = std::rand () % N;
                std::printf ("Signalling %u...\n", i);
                SetEvent (events [i]);
        }
        Sleep (0);
    }
    return 0;
}

VOID WINAPI OnTimeoutCallback (PVOID lpWaitContext) {
    std::printf ("Wait timeouted.\n");
}
VOID WINAPI OnApcCallback (PVOID lpWaitContext) {
    std::printf ("Wait interrupted by APC.\n");
}
BOOL WINAPI OnObjectSignalled (PVOID lpObjectContext, HANDLE hObject) {
    std::printf ("Processed signal from %d.\n", (int) (std::ptrdiff_t) lpObjectContext);
    return TRUE;
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
    wait = CreateUnlimitedWait (NULL, N / 2, OnTimeoutCallback, OnApcCallback);
    if (wait) {

        events.reserve (N + 1);

        for (auto i = 0u; i != N; ++i) {
            HANDLE hEvent = CreateEvent (NULL, FALSE, FALSE, NULL);
            if (hEvent) {

                if (AddUnlimitedWaitObject (wait, hEvent, OnObjectSignalled, (PVOID) (std::uintptr_t) i)) {
                    events.push_back (hEvent);
                } else {
                    std::printf ("Object %u failed to AddUnlimitedWaitObject, error %lu\n", i, GetLastError ());
                    break;
                }
            } else {
                std::printf ("Object %u creation failed, error %lu\n", i, GetLastError ());
                break;
            }
        }

        if (!events.empty ()) {

            // start thread signalling our events

            HANDLE hMainThread = NULL;
            DuplicateHandle (GetCurrentProcess (), GetCurrentThread (), 
                             GetCurrentProcess (), &hMainThread, 0, FALSE, DUPLICATE_SAME_ACCESS);

            auto hThread = CreateThread (NULL, 0, producer, hMainThread, 0, NULL);

            // wait

            constexpr auto WAIT_N = 16;

            ULONG nresults;
            void * results [WAIT_N];
            char tmp_buffer [32 * WAIT_N];

            while (WaitForSingleObject (hQuit, 0) == WAIT_TIMEOUT) {
            //while (wait != NULL) {
                BOOL result = WaitUnlimitedWaitEx (wait, results, tmp_buffer, WAIT_N, &nresults, 25, TRUE);

                if (result) {
                    std::printf ("%u objects signalled.\n", nresults);

                } else {
                    switch (GetLastError ()) {
                        case WAIT_TIMEOUT:
                            break;
                        case WAIT_IO_COMPLETION:
                            break;
                        default:
                            std::printf ("Wait error %lu\n", GetLastError ());
                    }
                }
            }

            // wait for producer to finish

            if (hThread) {
                WaitForSingleObject (hThread, INFINITE);
            }

            for (auto i = 0u; i != N; ++i) {
                if (!RemoveUnlimitedWaitObject (wait, events [i], FALSE)) {
                    std::printf ("Failed RemoveUnlimitedWaitObject %u, error %lu\n", i, GetLastError ());
                }
            }
        }
        DeleteUnlimitedWait (wait);
    } else {
        std::printf ("UnlimitedWait creation failed, error %lu\n", GetLastError ());
    }

    // cleanup events

    for (auto & event : events) {
        CloseHandle (event);
    }

    return (int) GetLastError ();
}
