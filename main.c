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
               "\tlist path [path]       -  Get or set absolute path (like C:\\objects.json) for Object List\n"
               "\tlist                   -  Print list of objects\n"
               "\taddFile <name> <path>  -  Add file or folder to Object List\n"
               "\taddReg <name> <path>   -  Add registry key to Object List\n"
               "\tupdate <name>          -  Update object's state in Object List\n"
               "\tremove <name>          -  Exclude object from Object List\n"
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

