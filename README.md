# Lab 8: Integrity control service

## Codename: Integra

_Integra_ is a service that monitors integrity of objects specified in _Object List_. \
Here, _Object_ stands for either File / folder or Registry key / value, including all subfolders / subkeys.

## Vision

### Object

An _Object_ is either a folder (file) or registry key (value). 

### Object List

Make utility to work with _Object List (OL)_ based on hash snapshot of specified paths / keys.

Supposed usage: 

* `integra list`
* `integra list add <registry/folder> <path>`
* `integra list remove <registry/folder> <path>`

On add, utility makes a hash snapshot of object's current state and adds it to list.

### List format

List consists of _Hash Trees_:

```
struct HashTree {
    string object_name;
    char type;
    HashNode root;
    int cbSize;
    string path;
}

struct HashNode {
    Hash hash;
    List<HashNode> slaves;
    int cbSize;
    string name;
}
```

Object List itself:

```
List<HashTree> objectList
```

### List in JSON

It was proposed to use JSON to store OL. We'll use separate lists for files and registry.

```
[
    {
        "object_name": "Installation of MyService",
        "type": "r",                                            // "r" (registry) or "f" (files)
        "path": "HKLM\SYSTEM\CurrentControlSet\MyService",
        "root": {
            "name": null,                                       // root always has null name (or empty?)
            "hash": "ab12cd6e90fed692f1ca08a7",
            "slaves": [
                {
                    "name": "Parameters",
                    "hash": null,                               // ignore listing, i.e. allow new keys
                    "slaves": [
                        {
                            "name": "param1",
                            "hash": "26cd887a0bfd6a55bc67210e",
                            "slaves": null                      // null - means it's a leaf
                        }
                    ]
                },
                {
                    "name": "Security",
                    "hash": "12cc0f834eedb7faa4a2dc90",
                    "slaves": []                                // empty list - means it's a folder
                }
            ]
        }
    },
    ...
]
```

### Expansion & improvements

* Multiple lists
* Detailed snapshot setup (certain scope and options), GUI setup?
* ???

### Service

Service that periodically checks hashes (_Hash Tree_) of objects in list.

#### Service template 

See in Microsoft docs (i will use lab 7)

#### Algorithm

1. Run service. Main thread creates Secondary thread that does all the work. Main thread sits there and waits for stop signal.
2. Secondary thread waits for _Check delay_
3. Reads registry to get path to OL. Then reads OL from file (parses JSON)
4. For each HashTree in OL, go DFS
5. Leaves: read files, compute hash, compare (if hash is set). Report on mismatch or missing files
6. Nodes: compute hash from leaves' names, report new / missing files out of scope (if hash is set). Report on mismatch or missing folders


### Functions and procedures

#### Create snapshot

* `ObjectList InitObjectList()`
* `HashTree SnapshotObject(wObjectType, szPath, bIgnoreListing)`
* `HashNode SnapshotNode(wObjectType, szPath, bIgnoreListing)`  - recursive, report on mismatch, can be interrupted?

#### Verify against snapshot

* `void VerifyObject(htHashTree)`
* `void VerifyNode(wObjectType, szPath, htHashNode)`  - recursive, report on mismatch, can be interrupted

#### Service

* `SvcMain()`
* `SvcInit()` - creates secondary thread
* `SvcReportStatus()`
* `SvcCtrlHandler()` - called by SCM
* `ReportSvcEvent()`
* `IntegrityCheckWorker()` - in secondary thread

## To be continued...