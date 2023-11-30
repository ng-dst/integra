# Lab 8: Integrity control service

## Codename: Integra

Integra is a service to control integrity of user-defined objects. An object is either a file, directory, or  registry key. Integrity is verified against object snapshots - something similar to OS restore points.

Service performs checks in a set time interval. Reports verification errors to Event Log.

## Features

* Monitor integrity of files, directories, registry keys and values in real time
* Update objects' state on demand
* Verify manually on demand
* Detect missing or modified files, directories, registry keys or values
* Detect modified contents of registry keys (i.e. new sub-keys or values)

## Options

* `install` &nbsp;&nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; Install service (run as admin)	
* `list path [path]` &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;&nbsp; Get or set* path for _Object List_. Default: `(same as exe)\objects.json`	
* `interval [delay_ms]` &nbsp;&ensp;&ensp; Get or set* time interval (ms) between checks. Default: `1800000` (30 min)
* `list`  &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;&ensp;&ensp;&nbsp; &nbsp; Print list of objects	
* `addFile <name> <path>` &nbsp; Add file or folder
* `addReg <name> <path>` &nbsp;&ensp; Add registry key
* `update <name>` &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; Update object's state	_(re-snapshot object and update hashes)_
* `remove <name>` &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp;  Remove object from list	
* `verify` &nbsp;&nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; Verify objects on-demand
* `h, help`  &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &nbsp; &ensp;  Print this message	

## Usage

1. Install _Integra_ service: `integra.exe install` *
2. _(optional)_ Change path to Object List: `integra.exe list path "C:\path\to\list.json"` *
3. _(optional)_ Change delay (ms) between checks: `integra.exe interval 1800000` * 
4. Start service from _Services_ menu or with `sc start Integra` *
5. To add file / directory: `integra.exe addFile "C:\path\folder"`
6. To add registry key: `integra.exe addReg "HKEY_SAMPLE_KEY\Path\Key"`
7. (optional) Verify on-demand from terminal: `integra.exe verify`
8. View results: open _EventVwr.exe_ (go to _Windows Logs/Application_)

_*_ requires administrative privileges (Run as Admin)

For security reasons, it is recommended to store Object List under write-protected location, where write access is restricted to administrators only.

## Registry

Service stores its config in registry. Base path is `HKLM\SYSTEM\CurrentControlSet\Services\Integra`.

Under this key:
* `Parameters\ `
  * `CheckIntervalMS` (_DWORD_) - Time interval between integrity checks
  * `ObjectListFile` (_REG_SZ_) - Path to Object List file (`.json`) 

## Object List

Path to Object List is stored in registry. The list is a JSON array of so-called _HashTree_ objects:

```
HashTree = {
    string object_name,     -  User-set name of object
    DWORD type,             -  Type of object: file/folder(0), registry(1)
    string path,            -  Absolute path to object (in file system or registry)
    HashNode root           -  Root node of tree
}
```

```
HashNode = {
    string name,            -  Relative name of file/folder or registry key/value
    Hash hash,              -  Hash of file, registry key or value
    Array<HashNode> slaves  -  Array of HashNodes of items under directory or registry key
}
```

### Example

```
[{
    "object_name": "include",
    "type": 0,
    "path": "\\\\?\\C:\\path\\to\\sysprog\\lab8\\include",
    "root": {
        "name":	null,
        "slaves": [{
                "name":	"cfg.h",
                "hash":	"295913a9dcb6862a5ccbc489e99f6363"
            }, {
                "name":	"utils.h",
                "hash":	"2ec331512a455df00d8071c8de553663"
            }]
        }
    }, {
    "object_name": "usbmon",
    "type": 1,
    "path": "HKEY_LOCAL_MACHINE\\SYSTEM\\CurrentControlSet\\Services\\UsbMonitor",
    "root": {
        "name": null,
        "hash": "c3e6a7d14cded199b21bbcea7d025d85",
        "slaves": [{
                "name":	"Parameters",
                "hash":	"4b2114233f867961bdeea8bb0d303244",
                "slaves": [{
                        "name":	"DenyList",
                        "hash":	"d67b27036c12a3d0a62602b3117b10fd",
                        "slaves": []
                    }, {
                        "name":	"LogFile",
                        "hash":	"3734280f802ec06ee6d5abc883b8d915"
                    }]
            }, {
                "name":	"Type",
                "hash":	"61960da1373899abfdc4ca21ce206e7e"
            }]
    }
}]
```

### JSON library

This project uses [cJSON](https://github.com/DaveGamble/cJSON) to process and store all necessary JSON objects.

### Hashes

Uses MD5 as a hashing algorithm. All hashes are stored as Hex strings. Below are formats of hash for each item type.

#### File:

         MD5( file contents )

#### Directory:

         null

         No hashing for directories. (i.e. new files and subdirectories are ignored)
       Generally integrity of a folder does not depend on any newly created files, only existing ones.

#### Registry value:
     
         MD5( dwType | rbValue )
   
       where  dwType   -  4-byte DWORD (usually little-endian),
              rbValue  -  byte buffer for value,
               |       -  concat operation
     
#### Registry key:
     
         MD5( MD5(valueName1)^...^MD5(valueNameN) ^ MD5[MD5(keyName1)^...^MD5(keyNameM)] )
   
       where  valueName1...valueNameN  -  values in key
              keyName1...keyNameM      -  sub-keys contained in key
               ^                       -  XOR operation

         This approach does not rely on Keys / Values order in enumeration (since XOR is commutative),
       while still being able to distinct Values and Keys of the same name.
      
         Unlike folders, Registry is very sensitive to newly created values or keys, hence we hash it.

## Service

Service was built based on [The Complete Service Sample](https://learn.microsoft.com/en-us/windows/win32/services/svc-cpp) by Microsoft.

Inside `SvcInit()`, control is transferred to `ServiceLoop()`:
* Get Object List path from registry
* Loop until stop event:
  * Read JSON from Object List file
  * For each object in list: call `VerifyObject()`
  * If running on-demand, return
  * Wait for stop event for a set _Time Interval_

## Functions

There are separate functions for making snapshots and verifying:

* `void VerifyObject()` - verify and report any errors to Event Log
* `void VerifyNodeFile()` - recursively verify HashNode
* `void VerifyNodeReg()`


* `cJSON* SnapshotObject()` - create HashTree of object in JSON
* `cJSON* SnapshotNodeFile()` - recursively create HashNode
* `cJSON* SnapshotNodeReg()`

### Alternative approach

Instead of implementing separate functions for creating and verifying HashTrees, make `VerifyObject()` call `SnapshotObject()` and then compare resulting JSON to expected HashTree recursively. 