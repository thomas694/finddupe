# finddupe

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0) [![Build status](https://ci.appveyor.com/api/projects/status/nt3tayragwr9qxnr/branch/master?svg=true)](https://ci.appveyor.com/project/thomas694/finddupe/branch/master)

Enhanced version of [finddupe](https://www.sentex.ca/~mwandel/finddupe/), a duplicate file detector and eliminator for Windows, originally by [Matthias Wandel](https://github.com/Matthias-Wandel).

## Reasons
I really like finddupe when I look for duplicate files. It is fast and clever. The match candidates are clustered according to the signature of the first 32k, then checked byte for byte. It can also create and find NTFS hard links. Creating hard links saves you disk space. Listing all existing hard links is very difficult otherwise.

Please refer to [Matthias' site](https://www.sentex.ca/~mwandel/finddupe/) for full description. My favourites are
`finddupe -bat d:\ImageLibray\Hardlinks_to_be_created.bat -ref d:\ImageLibray\originals1\** -ref d:\ImageLibray\originals2\** d:\ImageLibray\**\*.jpg` to remove duplicates in an image collection and `finddupe -listlink d:\ImageLibray` to list them.

However, Matthias' current version 1.23 is not supporting my requirements. And it is ASCII-only and fails on non-ASCII filenames, as is often the case nowadays.

## Enhancements
I added the following features to finddupe:
- multiple reference directories that shall not be touched (v1.24)
- unicode support (v1.25)
- alert message if order of options is wrong (v1.26)
- support for ignoring files by patterns (v1.26)
- checking for NTFS file system in batch and hardlink mode (v1.27)
- performance optimizations (especially for very large amounts of files) (v1.28)

It works for me, but some more testing is desirable.

I've udated the project to use Visual Studio 2019.

## Usage
```
finddupe v1.26 compiled Oct 18 2020
an enhanced version by thomas694 (@GH), originally by Matthias Wandel
This program comes with ABSOLUTELY NO WARRANTY. This is free software, and you
are welcome to redistribute it under certain conditions; view GNU GPLv3 for more.

Usage: finddupe [options] [-ign <substr> ...] [-ref <filepat> ...] <filepat>...
Options:
 -bat <file.bat> Create batch file with commands to do the hard
                 linking.  run batch file afterwards to do it
 -hardlink       Create hardlinks.  Works on NTFS file systems only.
                 Use with caution!
 -del            Delete duplicate files
 -v              Verbose
 -sigs           Show signatures calculated based on first 32k for each file
 -rdonly         Apply to readonly files also (as opposed to skipping them)
 -z              Do not skip zero length files (zero length files are ignored
                 by default)
 -u              Do not print a warning for files that cannot be read
 -p              Hide progress indicator (useful when redirecting to a file)
 -j              Follow NTFS junctions and reparse points (off by default)
 -listlink       hardlink list mode.  Not valid with -del, -bat, -hardlink,
                 or -rdonly, options
 -ign <substr>   Ignore file pattern, eg. .bak or .tmp (repeatable)
 -ref <filepat>  Following file pattern are files that are for reference, NOT to
                 be eliminated, only used to check duplicates against (repeatable)
 filepat         Pattern for files.  Examples:
                  c:\**        Match everything on drive C
                  c:\**\*.jpg  Match only .jpg files on drive C
                  **\foo\**    Match any path with component foo
                                from current directory down
```

## Download:

Latest release can be found [here](https://github.com/thomas694/finddupe/releases).

## Authors

- originator: [Matthias Wandel](https://www.sentex.ca/~mwandel/finddupe/)
- additional features: [thomas694](https://github.com/thomas694/finddupe)

## License <a rel="license" href="https://www.gnu.org/licenses/gpl-3.0"><img alt="GNU GPLv3 license" style="border-width:0" src="https://img.shields.io/badge/License-GPLv3-blue.svg" /></a>

<span xmlns:dct="http://purl.org/dc/terms/" property="dct:title">finddupe</span> by thomas694 
is licensed under <a rel="license" href="https://www.gnu.org/licenses/gpl-3.0">GNU GPLv3</a>.<br/>
Based on a work at <a xmlns:dct="http://purl.org/dc/terms/" href="https://www.sentex.ca/~mwandel/finddupe/" rel="dct:source">https://www.sentex.ca/~mwandel/finddupe/</a>.
