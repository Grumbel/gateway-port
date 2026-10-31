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

## Runtime integration

`gateway` enables persistent state by default at `$XDG_STATE_HOME/gateway-port`
or, when unset, `$HOME/.local/state/gateway-port`. `--state-dir DIR` selects an
explicit directory, and `--read-only` disables guest file creation/modification.
`gateway-probe` stays read-only unless `--state-dir` is supplied. Neither runtime
creates a state directory until a writable file is requested.

DOS create/open/read/write/seek/close and attribute queries use this backend.
Create returns a read/write handle; zero-length file writes resize at the current
position. Expected I/O failures return DOS carry/error results instead of stopping
emulation. Executable overlay reads also see the same state-first drive view.

An original Escape-to-main replay creates a 21,757-byte `RESTART.DAT` in the
explicit state directory, then reaches DOS find-first (`AH=4Eh`). This confirms
runtime state generation; the restart-file schema is not yet decoded.

## Save-file searches

Find-first/find-next (`AH=4Eh/4Fh`) merge original assets and state files with
case-insensitive 8.3 wildcard matching. State names shadow originals and results
are returned in name order. The DTA contains attributes, packed host modification
date/time, size, and the NUL-terminated DOS filename. Its reserved bytes hold an
opaque search cookie, allowing a copied DTA to resume a search. No-match and
exhausted searches return error 18.

The current subset enumerates regular files only; DOS directories, volume
labels, hidden/system attribute persistence, and long filename aliases are not
modeled. Gateway's observed startup request is `GATE*.SAV` with attributes zero.
