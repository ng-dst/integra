#include "event.h"

#define SVC_EVENT_CODE 0


void SvcReportEvent(WORD wType, LPCTSTR szEventMsg) {
    /**
     * @brief Log message to Event Log
     */
    HANDLE hEventSource;
    LPCTSTR lpszStrings[2];

    hEventSource = RegisterEventSource(NULL, SVCNAME);

    if (hEventSource) {
        lpszStrings[0] = SVCNAME;
        lpszStrings[1] = szEventMsg;

        ReportEvent(hEventSource,        // event log handle
                    wType,                         // event type
                    0,                   // event category
                    SVC_EVENT_CODE,      // event identifier
                    NULL,                 // no security identifier
                    2,                 // size of lpszStrings array
                    0,                  // no binary data
                    lpszStrings,          // array of strings
                    NULL);               // no binary data

        DeregisterEventSource(hEventSource);
    }
}