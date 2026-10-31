# DOS file backend

`DosDrive` presents a flat, case-insensitive C: drive over two host directories:
immutable original assets and an optional writable state layer. Reads prefer
state files. Opening an original file for writing copies it into state first;
creating/truncating a name affects only the state layer. A fresh drive instance
can read files saved by earlier runs.

`DosFile` owns a Linux descriptor and enforces DOS read/write access modes. It
supports shared-position reads/writes, signed relative seeks, and DOS's special
zero-length write, which truncates or extends a writable file at its current
position. The runtime's separate handle table controls guest handle numbers.

The state root cannot be the original directory or a child of it. DOS names
cannot traverse directories; final symlinks are not followed, and writable files
must be regular files with a single hard link. These checks keep state operations
from modifying originals through links. State directories are created lazily,
only when a guest requests a writable file.

Without a state root, missing-file opens still return DOS error 2, while writable
opens of existing originals and creation return error 5. Create supports ordinary,
archive, and read-only attributes; hidden/system/directory creation, arbitrary
DOS subdirectories, and file locking are not modeled yet.

Backend tests use temporary directories and cover copy-on-write, persistent
read precedence, creation/truncation/extension, mixed read/write positioning,
case-insensitive lookup, access modes, and preservation of linked/original files.
