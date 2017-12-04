## JSONFS

A toy FUSE filesystem that is serialized as JSON. A work-in-progress. Files and directories can be created and edited with a JSON text file mirroring the mounted directory in real-time.

```shell
[rabw@rarch jsonfs_fuse]$ mkdir mnt
[rabw@rarch jsonfs_fuse]$ make mount &
[1] 14267
[rabw@rarch jsonfs_fuse]$ ./jsonfs -s -f -json=/home/rabw/projects/jsonfs_fuse/jsonfs_image mnt

[rabw@rarch jsonfs_fuse]$ mkdir mnt/hello
[rabw@rarch jsonfs_fuse]$ echo "!" > mnt/hello/world
[rabw@rarch jsonfs_fuse]$ ls -R mnt
mnt:
hello

mnt/hello:
world
[rabw@rarch jsonfs_fuse]$ cat jsonfs_image
{
    "type": "directory",
    "data": {
        "hello":        {
            "type": "directory",
            "data": {
                "world":        {
                    "type": "regular",
                    "data": "!\n"
                }
            }
        }
    }
} 
```
