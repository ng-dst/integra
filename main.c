#include <windows.h>
#include <stdio.h>
#include "service.h"
#include "event.h"
#include "cfg.h"
#include "utils.h"
#include "integra.h"

#pragma comment(lib, "advapi32.lib")


int main(int argc, char** argv) {
    // Initialize registry paths *
    InitRegPaths();

    // "install" - Install service *
    if (argc > 1 && !strcmpi(argv[1], "install")) {
        SvcInstall();
        return EXIT_SUCCESS;
    }

    // "interval [delay_ms]" - Get / set* integrity check interval for service
    if (argc > 1 && !strcmpi(argv[1], "interval")) {
        // no interval specified, print existing
        if (argc == 2) {
            DWORD delay = GetCheckInterval();
            if (!delay) printf("Check interval is not set. Using default (30 minutes)\n");
            else printf("Check interval:  %lu ms (%luh %lum %lus)\n", delay, delay/3600000, (delay/60000)%60, (delay/1000)%60);
            return EXIT_SUCCESS;
        }
        else {
            DWORD delay = atol(argv[2]);
            if (!delay) {
                printf("Failed: Please enter valid delay (ms)\n");
                return EXIT_FAILURE;
            }
            if (SetCheckInterval(delay)) {
                printf("OK\n");
                return EXIT_SUCCESS;
            }
            else {
                printf("Failed. Try to run as administrator\n");
                return EXIT_FAILURE;
            }
        }
    }

    // "list path [path]" - Get / set* absolute path for OL file
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

    // "addFile <name> <path>" - Add object (file / folder) to OL
    if (argc > 3 && !strcmpi(argv[1], "addfile"))
        return AddObjectToOL(argv[2], OBJECT_FILE, argv[3]);

    // "addReg <name> <path>" - Add object (regisry key) to OL
    if (argc > 3 && !strcmpi(argv[1], "addreg"))
        return AddObjectToOL(argv[2], OBJECT_REGISTRY, argv[3]);

    // "remove <name>" - Remove object from OL
    if (argc > 2 && !strcmpi(argv[1], "remove"))
        return RemoveObjectFromOL(argv[2]);

    // "update <name>" - Update hashes for object in OL
    if (argc > 2 && !strcmpi(argv[1], "update"))
        return UpdateObjectInOL(argv[2]);

    // "list" - Show objects in OL
    if (argc == 2 && !strcmpi(argv[1], "list"))
        return PrintObjectsInOL();

    // "verify" - Verify on-demand
    if (argc == 2 && !strcmpi(argv[1], "verify")) {
        // run service without stop  =>  check once
        ServiceLoop(INTEGRA_CHECK_ONCE);
        printf("Verification complete. See Event Log for details\n");
        return EXIT_SUCCESS;
    }

    // "h", "help" - Print help message
    if (argc > 1 && (!strcmpi(argv[1], "h") ||
                     !strcmpi(argv[1], "help"))) {
        printf("Lab 8: Integrity control service\n"
               "Available commands:\n"
               "\tinstall                -  Install service (run as admin)\n"
               "\tverify                 -  Verify objects on-demand\n"
               "\tinterval [delay_ms]    -  Get or set time interval (ms) between checks. Default: 1800000 (30 min)\n"
               "\tlist path [path]       -  Get or set path for Object List. Default: (same as exe)\\integra-objects.json\n"
               "\tlist                   -  Print list of objects\n"
               "\taddFile <name> <path>  -  Add file or folder\n"
               "\taddReg <name> <path>   -  Add registry key\n"
               "\tupdate <name>          -  Update object's state\n"
               "\tremove <name>          -  Remove object from list\n"
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

    if (!StartServiceCtrlDispatcher(DispatchTable)) {
        SvcReportEvent(EVENTLOG_ERROR_TYPE, "StartServiceCtrlDispatcher failed");

        // could just run manually from terminal
        printf("Cannot run manually without arguments. Use 'h' or 'help' for help\n");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

