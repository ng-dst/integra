#include <windows.h>
#include <stdio.h>
#include "service.h"
#include "event.h"
#include "cfg.h"

#pragma comment(lib, "advapi32.lib")


int main(int argc, char** argv) {
    // Initialize registry paths *
    InitRegPaths();

    // "install" - Install service *
    if (argc > 1 && !strcmpi(argv[1], "install")) {
        SvcInstall();
        return EXIT_SUCCESS;
    }

    // "list path [path]" - Get / set absolute path for OL file
    if (argc > 2 && !strcmpi(argv[1], "list") && !strcmpi(argv[2], "path")) {
        // no path specified, print existing
        if (argc == 3) {
            TCHAR* path = GetOLFilePath();
            if (!path) printf("No Object List path set\n");
            else {
                printf("%s\n", path);
                free(path);
            }
            return EXIT_SUCCESS;
        }
            // path specified, set path *
        else if (SetOLFilePath(argv[3])) {
            printf("OK\n");
            return EXIT_SUCCESS;
        }
        else {
            printf("Failed. Try to run as administrator\n");
            return EXIT_FAILURE;
        }
    }

    // "h", "help" - Print help message
    if (argc > 1 && (!strcmpi(argv[1], "h") ||
                     !strcmpi(argv[1], "help"))) {
        printf("Lab 8: Integrity control service\n"
               "Available commands:\n"
               "\tinstall                -  Install service (run as admin)\n"
               "\tlist path [path]       -  Get or set absolute path (like C:\\log.txt) for log file\n"
               "\t   TBD    \n"
               "\th, help                -  Print this message\n");
        return EXIT_SUCCESS;
    }

    if (argc > 1) {
        printf("Unknown command. Use 'h' or 'help' for help\n");
        return EXIT_FAILURE;
    }

    // No args. Run service
    SERVICE_TABLE_ENTRY DispatchTable[] =
            {
                    {SVCNAME, (LPSERVICE_MAIN_FUNCTION) SvcMain},
                    {NULL, NULL}
            };

    if (!StartServiceCtrlDispatcher(DispatchTable))
        SvcReportEvent(EVENTLOG_ERROR_TYPE, "StartServiceCtrlDispatcher failed");

    return EXIT_SUCCESS;
}

