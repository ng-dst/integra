# Lab 8: Integrity control service

## Codename: Integra

_Integra_ is a service that monitors integrity of objects specified in _Object List_. \
Here, _Object_ stands for either File / folder or Registry key / value, including all subfolders / subkeys.

## Vision

### Object

An _Object_ is either a folder (file) or registry key (value). Integrity of the object is verified against so called _Hash Tree_.

### Object List

Make utility to work with _Object List (OL)_ based on hash snapshot of specified paths / keys.

Supposed usage: 

* `integra list`
* `integra list add <registry/folder> <path>`
* `integra list remove <registry/folder> <path>`

On add, utility makes a hash snapshot of object's current state and adds it to list.

### List format

List consists of _Hash Trees_ (probably will be represented as cJSON's so custom structs are not needed)

```
struct HashTree {
    string object_name;
    WORD type;
    HashNode root;
    string path;
}

struct HashNode {
    Hash hash;
    List<HashNode> slaves;
    string name;
}
```

Object List itself:

```
List<HashTree> objectList
```

### List in JSON

It was proposed to use JSON to store OL. I'll go with [cJSON](https://github.com/DaveGamble/cJSON).

OL will have the following structure. (_possibly subject to change_)

```
[
    {
        "object_name": "Installation of MyService",
        "type": OBJECT_REGISTRY,                                // int: registry or file
        "path": "HKLM\\SYSTEM\\CurrentControlSet\\MyService",
        "root": {
            "name": null,                                       // root always has null name (or empty? idk)
            "hash": "ab12cd6e90fed692f1ca08a7",
            "slaves": [
                {
                    "name": "Parameters",
                    "hash": null,                               // null if we ignore listing, i.e. allow new keys
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
                    "slaves": []                                // empty list - means it's an empty folder
                }
            ]
        }
    },
    ...
]
```

### Expansion & improvements

* Multiple lists (currently possible by changing OL path, though its only one OL at a time)
* Detailed snapshot setup (certain scope and options), _GUI-based setup?_
* ???

### Service

Service that periodically checks hashes (_Hash Tree_) of objects in list.

#### Service template 

See in Microsoft docs (i will use lab 7 as a basis)

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
* `void VerifyNode(wObjectType, szPath, htHashNode)`  - recursive, report on mismatch, can be interrupted?

#### Service

* `SvcMain()`
* `SvcInit()`
* `SvcReportStatus()`
* `SvcCtrlHandler()` - called by SCM to exit
* `ReportSvcEvent()`
* `ServiceLoop()`

## To be continued...