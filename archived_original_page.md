## FINDDUPE: Duplicate file detector and eliminator

<sub>Version 1.23 &nbsp;&nbsp;August 25 2010 &nbsp;&nbsp;([html source](https://www.sentex.ca/~mwandel/finddupe/) archived/converted 2025-02-07)</sub>

Finddupe is a tool for quick detecting of duplicate files on a hard drive under Windows. Duplicate files can be detected, hardlinked, or deleted. Finddupe can also search for hardlinked files on your computer.

Finddupe is a command line program. If you aren't familiar with the "command prompt" under windows, this program might be a little challenging for you to use.

Finddupe has several possible uses:

#### Deleting duplicate files

When working thru somebody else's photo collection, or MP3 collection, this tool is useful for deleting the files that are duplicate. Depending on how the media is organized, there can be a lot of duplicate files in a collection.

#### Freeing hard drive space

Sometimes its intentional to have certain media in multiple places. By running finddupe, and hard linking the identical files, you can keep the files in multiple places, while only having one physical copy on the hard drive.

#### Detecting changed files for backup

Finddupe is useful for detecting which files have changed and need backing up. Simply back up the media, and then run finddupe to eliminate those files in the copy that are already contained in a previous backup.

#### Detecting hardlinked groups of files

Unknown to most, the winodws NTFS file system has the ability to hard link files together. If you use finddupe with the -listlink option, finddupe will search for files that appear in multiple places as hard links.

### finddupe command line options

finddupe [options] [-ref] <filepat> [filepat]...

| Option | Description |
|-|-|
| -hardlink | Delete duplicate copies of file, and replace duplicates with hardlinks to other copy of the file. Works only on NTFS file systems, and with administrator privileges. (The C: drive under XP is almost always NTFS, and most people log in as administrator) |
| -del | Delete duplicate files |
| -sigs | Pring computed file signature of each file. The file signature is computed using a CRC of the first 32k of the file, as well as its length. The signature is used to detect files that are probably duplicates. Finddupe does a full binary file compare before taking any action. |
| -rdonly | Also operate on files that have the readonly bit set (these are normally skipped). I use this feature to eliminate shared files in large projects under version control at work. |
| -z | Do not skip zero length files (zero length files are ignored by default) |
| -u | Do not print a warning for files that cannot be read |
| -p | Hide progress indicator (useful when output is redirected to a file) |
| -j | Follow NTFS junctions and reparse points (not followed by default) |
| ‑bat&nbsp;&lt;batchfile&gt; | Do not hardlink or delete any files. Rather, create a batch file containing the actions to be performed. This can be useful if you want to inspect what finddupe will do. |
| -listlink | Puts finddupe in hardlink finding mode. In this mode, finddupe will list which groups of files are hardlinked together. All hardlinked instances found of a file are shown together. However, finddupe can only find instances of the hardlinked file that are within the search path. This option can only be combined with the -v option. |
| ‑ref&nbsp;&lt;filepat&gt; | The file or file pattern after the -ref is a reference. These files will be compared against, but not eliminated. Rather, other files on the command line will be considered duplicates of the reference files. |
| [filepat] | File pattern matching in finddupe is very powerful. It uses the same code as is used in jhead. For example, to specify c:\** would indicate every file on the entire C drive. Specifying C:\**\foo\*.jpg specifies any file that ends with .jpg that is in a subdirectory called foo anywhere on the hard drive, including such directories as c:\foo, c:\bar\foo, c:\hello\workd\foo and c:\foo\bar\foo. |

### Example uses

If you have a previous backup in a directory tree on c:\prev_backup, and just copied your work files to a directory tree on c:\new_backup, you can remove any files that are already in the previous backup with the following incantation:

    finddupe -del -ref c:\prev_backup c:\new_backup

If you have a large photo collection on c:\photos, and you wish to replace duplicates with hard links, you can run:

    finddupe -hardlink c:\photos

Note that this only works on NTFS file systems (such as the C drive under Windows XP). It won't work on FAT file systems, like the ones used on most external hard disks or USB flash drives.

If you want to know which files within a directory tree are hardlinked together, such as after running the above command, you can run:

    finddupe -listlink c:\photos

If you just want to know which files are common between two directory trees, you can run:

    finddupe -bat work.bat -del c:\media\** c:\media2\**

This will create the file "work bat" with file delete commands in it. The '-bat' option tells finddupe to not do anything, but rather store the actions to a batch file. This allows you to review what finddupe would do before taking any action. The '**' tells it to recursively do all the files.

### "Screenshot" - finddupe looks like while running:

```
C:\>finddupe testdir\*
Duplicate: 'testdir\aab.txt'
With:      'testdir\aab.zzz'
Duplicate: 'testdir\aab.bak'
With:      'testdir\aac.txt'
Duplicate: 'testdir\dup1'
With:      'testdir\foo2'
Duplicate: 'testdir\foo'
With:      'testdir\makefile'
Duplicate: 'testdir\foo'
With:      'testdir\makefile.bak'
Duplicate: 'testdir\dup1'
With:      'testdir\myglob.bak'
Duplicate: 'testdir\longdiff.bak'
With:      'testdir\nadine.txt'
Files:    23285 kBytes in    23 files
Dupes:     6971 kBytes in     7 files
```

### Compatibility

Finddupe has been tested on Windows 2000, XP and Vista. Hard linking does not work on Windows versions prior to Windows 2000.

### Why I wrote this program

I wanted to eliminate some duplicate files on my windows computer. Naturally, I searched the internet. But mostly, I could just find fancy payware, whereas all I wanted was a really simple command line based utility. So I eventually wrote one.

I also wrote it to be very fast. For large media files, this helps a lot. Finddupe will only read the first 32k of a file and compute a hash based on that. Only if that matches with another file will it even read the entire files. I use it mostly on various media, like jpegs and mp3s to find and eliminate duplicates I may have

### License

Finddupe is totally free. Do whatever you like with it. You can integrate it into GPL or BSD style licensed programs if you would like to.

### Bugs

Presently, finddupe does not check for NTFS file systems before attempting to hard link. If you run it on a non NTFS file system, it will stop on the first failed hardlink attempt, but not before deleting the file it meant to replace with a hardlink.

On modifying one file of a hardlinked pair or set, windows may not update the size of the other linked instances right away. Also, programs that move the old file to a backup copy tend to break (unlink) the hard link on saving. This behaviour may or may not be desirable.

### Downloads

| File | Description |
|-|-|
| [finddupe.exe](https://www.sentex.ca/~mwandel/finddupe/finddupe.exe) | finddupe executable (53k) |
| [finddupe-src.zip](https://www.sentex.ca/~mwandel/finddupe/finddupe-src.zip) | Source code in a zip file (11k). Build with Microsoft Visual C++ |

Got questions? Email me: <sub>![The address is in the PNG file so no robot can pick it up](https://www.sentex.ca/~mwandel/addr.png)</sub>

Other handy free utilities by Matthias Wandel:

&nbsp;&nbsp;&nbsp;[Ftpdmin](https://www.sentex.ca/~mwandel/ftpdmin/)&nbsp; A minimal install-free windows FTP server for ad-hoc file transfers

&nbsp;&nbsp;&nbsp;[Jhead](https://www.sentex.ca/~mwandel/jhead/)&nbsp; A program for examining and manipulating digicam image metadata
<p>&nbsp;</p>

[To Matthias Wandel's home page](https://www.sentex.ca/~mwandel/)
