//--------------------------------------------------------------------------
// finddupe - duplicate file detector and eliminator
// 
// Find duplicate files and hard link, delete, or write batch files to do the same.
// Also includes a separate option to scan for and enumerate hardlinks in the search space.
//
// Version 1.23
// 
// Matthias Wandel Oct 2006 - Aug 2010
// 
// Version 1.24
// Copyright (C) May 2017  thomas694 (@GH 0CFD61744DA1A21C)
//     added support for multiple ref patterns
// Version 1.25
// Copyright (C) Jun 2017  thomas694
//     added unicode support
// Version 1.26
// Copyright (C) Oct 2020  thomas694
//     added support for ignore filename patterns
//
// finddupe is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//--------------------------------------------------------------------------

#define VERSION "1.26"

#define REF_CODE

#include <stdio.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h>
#include <tchar.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
#include <ctype.h>

#include <shlwapi.h> /* StrStrI */
#pragma comment(lib, "shlwapi.lib") /* unresolved external symbol __imp__StrStrIW@8 */

#include <process.h>
#include <io.h>
#include <sys/utime.h>
#define WIN32_LEAN_AND_MEAN // To keep windows.h bloat down.    
#define _WIN32_WINNT 0x0500
#include <windows.h>
#include <direct.h>

#define  S_IWUSR  0x80      // user has write permission
#define  S_IWGRP  0x10      // group has write permission
#define  S_IWOTH  0x02      // others have write permisson

static int FilesMatched;

int totalCompare = 0;
int totalPrint = 0;
int totalFileStat = 0;
int totalFileInfo = 0;
int totalByteRead = 0;
int totalCRC = 0;
int totalCheck = 0;
int ticksCheck = 0;

typedef struct {
    unsigned int Crc;
    unsigned int Sum;
}Checksum_t;

// Data structure for file allcoations:
typedef struct {
    Checksum_t Checksum;
    struct {
        int High;
        int Low;
    }FileIndex;    
    int NumLinks; 
    unsigned FileSize;
    TCHAR * FileName;
    int Larger; // Child index for larger child
    int Smaller;// Child index for smaller child
}FileData_t;
static FileData_t * FileData;
static int NumAllocated;
static int NumUnique;

#ifdef REF_CODE
TCHAR* * PathData;
int PathAllocated;
int PathUnique;
#endif

// Duplicate statistics summary
struct {
    int TotalFiles;
    int DuplicateFiles;
    int HardlinkGroups;
    int CantReadFiles;
    int ZeroLengthFiles;
    int IgnoredFiles;
    __int64 TotalBytes;
    __int64 DuplicateBytes;
}DupeStats;

// How many bytes to calculate file signature of.
#define BYTES_DO_CHECKSUM_OF 32768


// Parameters for what to do
FILE * BatchFile = NULL;        // Output a batch file
TCHAR * BatchFileName = NULL;

int PrintFileSigs;         // Print signatures of files
int PrintDuplicates;       // Print duplicates
int MakeHardLinks;         // Do the actual hard linking
int DelDuplicates;         // Delete duplicates (no hard linking)
int ReferenceFiles;        // Flag - do not touch present files parsed
int DoReadonly;            // Do it for readonly files also
int Verbose;
int HardlinkSearchMode;    // Detect hard links only (do not check duplicates)
int ShowProgress = 1;      // Show progressing file count...
int HideCantReadMessage= 0;// Hide the can't read file error
int SkipZeroLength = 1;    // Ignore zero length files.
int ProgressIndicatorVisible = 0; // Weither a progress indicator needs to be overwritten.
int FollowReparse = 0;     // Whether to follow reparse points (like unix softlinks for NTFS)
int MeasureDurations = 0;  // Measure how many ticks the different tasks take

TCHAR* * IgnorePatterns;   // Patterns of filename to ignore (can be repeated, eg. .bak, .tmp)
int IgnorePatternsAlloc;   // Number of allocated ignore patterns
int IgnorePatternsCount;   // Number of specified ignore patterns

int MyGlob(const TCHAR * Pattern, int FollowReparse, void (*FileFuncParm)(const TCHAR * FileName));


//--------------------------------------------------------------------------
// Calculate some 64-bit file signature.  CRC and a checksum
//--------------------------------------------------------------------------
static void CalcCrc(Checksum_t * Check, char * Data, unsigned NumBytes)
{
    unsigned a;
    unsigned Reg, Sum;
    Reg = Check->Crc;
    Sum = Check->Sum;
    for(a=0;a<NumBytes;a++){
        Reg = Reg ^ Data[a];
        Sum = Sum + Data[a];
        Reg = (Reg >> 8) ^ ((Reg & 0xff) << 24) ^ ((Reg & 0xff) << 9);
        Sum = (Sum << 1) + (Sum >> 31);
    }
    Check->Crc = Reg;
    Check->Sum = Sum;
}

//--------------------------------------------------------------------------
// Clear line (erase the progress indicator)
//--------------------------------------------------------------------------
void ClearProgressInd(void)
{
    if (ProgressIndicatorVisible){
        _tprintf(TEXT("                                                                          \r"));
        ProgressIndicatorVisible = 0;
    }
}

//--------------------------------------------------------------------------
// Escape names for batch files: % turns into %%
//--------------------------------------------------------------------------
TCHAR * EscapeBatchName(TCHAR * Name)
{
    static TCHAR EscName[_MAX_PATH*2];
    int a,b;
    b = 0;
    for (a=0;;){
        EscName[b++] = Name[a];
        if (Name[a] == '\0') break;
        if (Name[a] == '%') EscName[b++] = '%'; // Escape '%' with '%%' for batch files.
        a++;
    }
    return EscName;
}

//--------------------------------------------------------------------------
// Eliminate duplicates.
//--------------------------------------------------------------------------
static int EliminateDuplicate(FileData_t ThisFile, FileData_t DupeOf)
{
    // First compare whole file.  If mismatch, return 0.
    #define CHUNK_SIZE 0x10000
    FILE * File1, * File2;
    unsigned BytesLeft;
    unsigned BytesToRead;
    char Buf1[CHUNK_SIZE], Buf2[CHUNK_SIZE];
    int IsDuplicate = 1;
    int Hardlinked = 0;
    int IsReadonly;
    struct _stat FileStat;

    if (ThisFile.FileSize != DupeOf.FileSize) return 0;

    Hardlinked = 0;
    if (DupeOf.NumLinks && memcmp(&ThisFile.FileIndex, &DupeOf.FileIndex, sizeof(DupeOf.FileIndex)) == 0){
        Hardlinked = 1;
        goto dont_read;
    }

    if (DupeOf.NumLinks >= 1023){
        // Do not link more than 1023 files onto one physical file (windows limit)
        return 0;
    }

    File1 = _tfopen(ThisFile.FileName, TEXT("rb"));
    if (File1 == NULL){
        return 0;
    }
    File2 = _tfopen(DupeOf.FileName, TEXT("rb"));
    if (File2 == NULL){
        fclose(File1);
        return 0;
    }

    BytesLeft = ThisFile.FileSize;

    while(BytesLeft){
        BytesToRead = BytesLeft;
        if (BytesToRead > CHUNK_SIZE) BytesToRead = CHUNK_SIZE;

        if (fread(Buf1, 1, BytesToRead, File1) != BytesToRead){
            ClearProgressInd();
            _ftprintf(stderr, TEXT("Error doing full file read on '%s'\n"), ThisFile.FileName);
            IsDuplicate = 0;
            break;
        }

        if (fread(Buf2, 1, BytesToRead, File2) != BytesToRead){
            ClearProgressInd();
            _ftprintf(stderr, TEXT("Error doing full file read on '%s'\n"), DupeOf.FileName);
            IsDuplicate = 0;
            break;
        }

        BytesLeft -= BytesToRead;

        if (memcmp(Buf1, Buf2, BytesToRead)){
            IsDuplicate = 0;
            break;
        }
    }

    fclose(File1);
    fclose(File2);

    if (!IsDuplicate){
        // Full file duplicate check failed (CRC collision, or differs only after 32k)
        return 0;
    }

    DupeStats.DuplicateFiles += 1;
    DupeStats.DuplicateBytes += (__int64)ThisFile.FileSize;

dont_read:
    if (PrintDuplicates){
        if (!HardlinkSearchMode){
            ClearProgressInd();
            _tprintf(TEXT("Duplicate: '%s'\n"), DupeOf.FileName);
            _tprintf(TEXT("With:      '%s'\n"), ThisFile.FileName);
            if (Hardlinked){
                // If the files happen to be hardlinked, show that.
                _tprintf(TEXT("    (hardlinked instances of same file)\n"));
            }
        }
    }


    if (_tstat(ThisFile.FileName, &FileStat) != 0){
        // oops!
        _ftprintf(stderr, TEXT("stat failed on '%s'\n"), ThisFile.FileName);
        exit (EXIT_FAILURE);
    }
    IsReadonly = (FileStat.st_mode & S_IWUSR) ? 0 : 1;

    if (IsReadonly){
        // Readonly file.
        if (!DoReadonly && !Hardlinked){
            ClearProgressInd();
            _tprintf(TEXT("Skipping duplicate readonly file '%s'\n"), ThisFile.FileName);
            return 1;
        }
        if (MakeHardLinks || DelDuplicates){
            // Make file read/write so we can delete it.
            // We sort of assume we own the file.  Otherwise, not much we can do.
            _tchmod(ThisFile.FileName, FileStat.st_mode | S_IWUSR);
        }
    }

    if (BatchFile){
        // put command in batch file
        if (DelDuplicates || !Hardlinked)
            _ftprintf(BatchFile, TEXT("del %s \"%s\"\n"), IsReadonly ? TEXT("/F"):TEXT(""), 
                EscapeBatchName(ThisFile.FileName));
        if (!DelDuplicates){
            if (!Hardlinked){
                _ftprintf(BatchFile, TEXT("fsutil hardlink create \"%s\" \"%s\"\n"),
                    ThisFile.FileName, DupeOf.FileName);
            if (IsReadonly){
                // If original was readonly, restore that attribute
                    _ftprintf(BatchFile, TEXT("attrib +r \"%s\"\n"), ThisFile.FileName);
            }
            }
        }else{
            _ftprintf(BatchFile, TEXT("rem duplicate of \"%s\"\n"), DupeOf.FileName);
        }

    }else if (MakeHardLinks || DelDuplicates){
        if (MakeHardLinks && Hardlinked) return 0; // Nothign to do.

        if (_tunlink(ThisFile.FileName)){
            ClearProgressInd();
            _ftprintf(stderr, TEXT("Delete of '%s' failed\n"), DupeOf.FileName);
            exit (EXIT_FAILURE);
        }
        if (MakeHardLinks){
            if (CreateHardLink(ThisFile.FileName, DupeOf.FileName, NULL) == 0){
                // Uh-oh.  Better stop before we mess up more stuff!
                ClearProgressInd();
                _ftprintf(stderr, TEXT("Create hard link from '%s' to '%s' failed\n"),
                        DupeOf.FileName, ThisFile.FileName);
                exit(EXIT_FAILURE);
            }

            {
                // set Unix access rights and time to new file
                struct _utimbuf mtime;
                _tchmod(ThisFile.FileName, FileStat.st_mode);

                // Set mod time to original file's
                mtime.actime = FileStat.st_mtime;
                mtime.modtime = FileStat.st_mtime;
            
                _tutime(ThisFile.FileName, &mtime);
            }
            ClearProgressInd();
            _tprintf(TEXT("    Created hardlink\n"));
        }else{
            ClearProgressInd();
            _tprintf(TEXT("    Deleted duplicate\n"));
        }
    }
    return 2;
}

#ifdef REF_CODE
static int IsNonRefPath(TCHAR * filename)
{
    int i;
    TCHAR * cmpPath;
        
    i = _tcslen(filename)-1;
    for (i; i >= 0; i--)
    {
        if ((int)filename[i] == (int)'\\') break;
    }

    if (i == 0)
    {
        _ftprintf(stderr, TEXT("IsNonRefPath, path without any slash!?"));
        exit(EXIT_FAILURE);
    }

    cmpPath = (TCHAR*) malloc(sizeof(TCHAR) * (i+2));
    _tcsncpy(cmpPath, filename, i+1);
    cmpPath[i+1] = '\0';

    for (i = 0; i < PathUnique; i++)
    {
        if (_tcscmp(cmpPath, PathData[i]) == 0) return 0;
    }

    return 1;
}
#endif

static void StoreFileData(FileData_t ThisFile)
{
    if (NumUnique >= NumAllocated) {
        // Array is full.  Make it bigger
        NumAllocated = NumAllocated + NumAllocated / 2;
        FileData = (FileData_t*)realloc(FileData, sizeof(FileData_t) * NumAllocated);
        if (FileData == NULL) {
            fprintf(stderr, "Malloc failure");
            exit(EXIT_FAILURE);
        }
    }
    FileData[NumUnique] = ThisFile;
    NumUnique += 1;
}

//--------------------------------------------------------------------------
// Check for duplicates.
//--------------------------------------------------------------------------
static void CheckDuplicate(FileData_t ThisFile)
{
    int Ptr;
    int * Link;
    // Find where in the tree structure it belongs.
    Ptr = 0;

    if (NumUnique == 0) goto store_it;

    if (MeasureDurations) ticksCheck = GetTickCount();

    for(;;){
        int comp;
        comp = memcmp(&ThisFile.Checksum, &FileData[Ptr].Checksum, sizeof(Checksum_t));
        if (comp == 0) {
            // the same file
            if (_tcscmp(ThisFile.FileName, FileData[Ptr].FileName) == 0) {
                if (MeasureDurations) { ticksCheck = GetTickCount() - ticksCheck; totalCheck += ticksCheck; }
                return;
            }
            // Check for true duplicate.
            #ifdef REF_CODE
            if (!ReferenceFiles && !HardlinkSearchMode && IsNonRefPath(ThisFile.FileName)) {
            #else
            if (!ReferenceFiles && !HardlinkSearchMode) {
            #endif
                int r = EliminateDuplicate(ThisFile, FileData[Ptr]);
                if (r) {
                    if (r == 2) FileData[Ptr].NumLinks += 1; // Update link count.
                    // Its a duplicate for elimination.  Do not store info on it. New: store info for correct statistic calculation
                    if (MeasureDurations) { ticksCheck = GetTickCount() - ticksCheck; totalCheck += ticksCheck; }
                    StoreFileData(ThisFile);
                    return;
                }
            }
            // Build a chain on one side of the branch.
            // That way, we will check every checksum collision from here on.
            comp = 1;
        }

        if (comp){
            if (comp > 0){
                Link = &FileData[Ptr].Larger;
            }else{
                Link = &FileData[Ptr].Smaller;
            }
            if (*Link < 0){
                // Link it to here.
                *Link = NumUnique;
                break;
            }else{
                Ptr = *Link;
            }
        }
    }

    if (MeasureDurations) { ticksCheck = GetTickCount() - ticksCheck; totalCheck += ticksCheck; }

    DupeStats.TotalFiles += 1;
    DupeStats.TotalBytes += (__int64) ThisFile.FileSize;

    store_it:

    StoreFileData(ThisFile);
}

//--------------------------------------------------------------------------
// Walk the file tree after handling detect mode to show linked groups.
//--------------------------------------------------------------------------
static void WalkTree(int index, int LinksFirst, int GroupLen)
{
    int a,t;

    if (NumUnique == 0) return;

    if (FileData[index].Larger >= 0){
        int Larger = FileData[index].Larger;
        if (memcmp(&FileData[Larger].Checksum, &FileData[index].Checksum, sizeof(Checksum_t)) == 0){
            // it continues the same group.
            WalkTree(FileData[index].Larger,LinksFirst >= 0 ? LinksFirst : index, GroupLen+1);
            goto not_end;
        }else{
            WalkTree(FileData[index].Larger,-1,0);
        }
    }
    _tprintf(TEXT("\nHardlink group, %d of %d hardlinked instances found in search tree:\n"), GroupLen+1, FileData[index].NumLinks);
    t = LinksFirst >= 0 ? LinksFirst : index;
    for (a=0;a<=GroupLen;a++){
        _tprintf(TEXT("  \"%s\"\n"), FileData[t].FileName);
        t = FileData[t].Larger;
    }

    DupeStats.HardlinkGroups += 1;

not_end:    
    if (FileData[index].Smaller >= 0){
        WalkTree(FileData[index].Smaller,-1,0);
    }
}

//--------------------------------------------------------------------------
// Do selected operations to one file at a time.
//--------------------------------------------------------------------------
static void ProcessFile(const TCHAR * FileName)
{
    unsigned FileSize;
    Checksum_t CheckSum;
    struct _stat FileStat;
    int ticksCompare = 0;
    int ticksPrint = 0;
    int ticksFileStat = 0;
    int ticksFileInfo = 0;
    int ticksByteRead = 0;
    int ticksCRC = 0;

    if (MeasureDurations) ticksCompare = GetTickCount();

    for (int i = 0; i < NumUnique; i++)
    {
        if (_tcscmp(FileName, FileData[i].FileName) == 0) 
            return;
    }

    if (MeasureDurations) { ticksCompare = GetTickCount() - ticksCompare; totalCompare += ticksCompare; }

    FileData_t ThisFile;
    memset(&ThisFile, 0, sizeof(ThisFile));
    {
        static int LastPrint, Now;
        Now = GetTickCount();
        if ((unsigned)(Now-LastPrint) > 200){
            if (ShowProgress){
                TCHAR ShowName[55];
                int l = _tcslen(FileName);
                #ifdef UNICODE
                wmemset(ShowName, L' ', sizeof(ShowName)/sizeof(ShowName[0]));
                #else
                memset(ShowName, ' ', sizeof(ShowName));
                #endif
                ShowName[54] = 0;
                if (l > 50) l = 51;
                #ifdef UNICODE
                wmemcpy(ShowName, FileName, l);
                if (l >= 51) wmemcpy(ShowName+50,L"...",4);
                #else
                memcpy(ShowName, FileName, l);
                if (l >= 51) memcpy(ShowName+50,"...",4);
                #endif

                _tprintf(TEXT("Scanned %4d files: %s\r"), FilesMatched, ShowName);
                LastPrint = Now;
                ProgressIndicatorVisible = 1;
            }
            fflush(stdout);
            if (MeasureDurations) { ticksPrint = GetTickCount() - Now; totalPrint += ticksPrint; }
        }
    }

    FilesMatched += 1;

    if (BatchFileName && _tcscmp(FileName, BatchFileName) == 0) return;

    if (MeasureDurations) ticksFileStat = GetTickCount();

    if (_tstat(FileName, &FileStat) != 0){
        // oops!
        goto cant_read_file;
    }
    FileSize = FileStat.st_size;

    if (MeasureDurations) { ticksFileStat = GetTickCount() - ticksFileStat; totalFileStat += ticksFileStat; }

    if (FileSize == 0){
        if (SkipZeroLength){
            DupeStats.ZeroLengthFiles += 1;
            return;
        }
    }

    ThisFile.Larger = -1;
    ThisFile.Smaller = -1;
    ThisFile.FileSize = FileSize;

    {
        if (MeasureDurations) ticksFileInfo = GetTickCount();
        HANDLE FileHandle;
        BY_HANDLE_FILE_INFORMATION FileInfo;
        FileHandle = CreateFile(FileName, 
                        GENERIC_READ,         // dwDesiredAccess
                        FILE_SHARE_READ,      // dwShareMode
                        NULL,                 // Security attributes
                        OPEN_EXISTING,        // dwCreationDisposition
                        FILE_ATTRIBUTE_NORMAL,// dwFlagsAndAttributes.  Ignored for opening existing files
                        NULL);                // hTemplateFile.  Ignored for existing.
        if (FileHandle == (void *)-1){
cant_read_file:
            DupeStats.CantReadFiles += 1;
            if (!HideCantReadMessage){
                ClearProgressInd();
                _ftprintf(stderr, TEXT("Could not read '%s'\n"), FileName);
            }
            return;
        }

        GetFileInformationByHandle(FileHandle, &FileInfo);

        CloseHandle(FileHandle);

        if (MeasureDurations) { ticksFileInfo = GetTickCount() - ticksFileInfo; totalFileInfo += ticksFileInfo; }

        if (Verbose){
            ClearProgressInd();
            _tprintf(TEXT("Hardlinked (%d links) node=%08x %08x: %s\n"), FileInfo.nNumberOfLinks, 
                FileInfo.nFileIndexHigh, FileInfo.nFileIndexLow, FileName);
        }

        if (HardlinkSearchMode && FileInfo.nNumberOfLinks == 1){
            // File has only one link, so its not hardlinked.  Skip for hardlink search mode.
            return;
        }

        //printf("    Info:  Index: %08x %08x\n",FileInfo.nFileIndexHigh, FileInfo.nFileIndexLow);

        // Use the file index (which is NTFS equivalent of the iNode) instead of the CRC.
        ThisFile.FileIndex.Low      = FileInfo.nFileIndexLow;
        ThisFile.FileIndex.High     = FileInfo.nFileIndexHigh;
        ThisFile.NumLinks = FileInfo.nNumberOfLinks;

        if (HardlinkSearchMode){
            // For hardlink search mode, duplicates are detected by file index, not CRC,
            // so copy the file ID into the CRC.
            ThisFile.Checksum.Sum = ThisFile.FileIndex.Low;
            ThisFile.Checksum.Crc = ThisFile.FileIndex.High;
        }
    }

    if (!HardlinkSearchMode){
        FILE * infile;
        char FileBuffer[BYTES_DO_CHECKSUM_OF];
        unsigned BytesRead, BytesToRead;
        memset(&CheckSum, 0, sizeof(CheckSum));

        if (MeasureDurations) ticksByteRead = GetTickCount();

        infile = _tfopen(FileName, TEXT("rb"));

        if (infile == NULL) {
            if (!HideCantReadMessage){
                ClearProgressInd();
                _ftprintf(stderr, TEXT("can't open '%s'\n"), FileName);
            }
            return;
        }
    
        BytesToRead = FileSize;
        if (BytesToRead > BYTES_DO_CHECKSUM_OF) BytesToRead = BYTES_DO_CHECKSUM_OF;
        BytesRead = fread(FileBuffer, 1, BytesToRead, infile);
        if (BytesRead != BytesToRead){
            if (!HideCantReadMessage){
                ClearProgressInd();
                _ftprintf(stderr, TEXT("file read problem on '%s'\n"), FileName);
            }
            return;
        }
        fclose(infile);

        if (MeasureDurations) { ticksByteRead = GetTickCount() - ticksByteRead; totalByteRead += ticksByteRead; ticksCRC = GetTickCount(); }

        CalcCrc(&CheckSum, FileBuffer, BytesRead);
        
        if (MeasureDurations) { ticksCRC = GetTickCount() - ticksCRC; totalCRC += ticksCRC; }

        CheckSum.Sum += FileSize;
        if (PrintFileSigs){
            ClearProgressInd();
            _tprintf(TEXT("%08x%08x %10d %s\n"), CheckSum.Crc, CheckSum.Sum, FileSize, FileName);
        }

        ThisFile.Checksum = CheckSum;
        ThisFile.FileSize = FileSize;
    }

    ThisFile.FileName = _tcsdup(FileName); // allocate the string last, so 
                                          // we don't waste memory on errors.

    // skip if filename contains a ignore pattern
    for (int i = 0; i < IgnorePatternsCount; i++)
    {
        if (StrStrI(FileName, IgnorePatterns[i]))
        {
            DupeStats.IgnoredFiles++;
            StoreFileData(ThisFile);
            return;
        }
    }

    CheckDuplicate(ThisFile);

    if (MeasureDurations)
        _tprintf(TEXT("Cmp: %d / %d Print: %d / %d FS: %d / %d FI: %d / %d BR: %d / %d CRC: %d / %d CHK: %d / %d  =  %d\n"),
            ticksCompare, totalCompare, ticksPrint, totalPrint, ticksFileStat, totalFileStat, ticksFileInfo, totalFileInfo, ticksByteRead, totalByteRead, ticksCRC, totalCRC, ticksCheck, totalCheck,
            (totalCompare + totalPrint + totalFileStat + totalFileInfo + totalByteRead + totalCRC + totalCheck));
}

//--------------------------------------------------------------------------
// complain about bad state of the command line.
//--------------------------------------------------------------------------
static void Usage (void)
{
    _tprintf(TEXT("finddupe v%s compiled %s\n"), TEXT(VERSION), TEXT(__DATE__));
    _tprintf(TEXT("an enhanced version by thomas694 (@GH), originally by Matthias Wandel\n"));
    _tprintf(TEXT("This program comes with ABSOLUTELY NO WARRANTY. This is free software, and you\n"));
    _tprintf(TEXT("are welcome to redistribute it under certain conditions; view GNU GPLv3 for more.\n\n"));
    _tprintf(TEXT("Usage: finddupe [options] [-ign <substr> ...] [-ref <filepat> ...] <filepat>...\n"));
    _tprintf(TEXT("Options:\n")
           TEXT(" -bat <file.bat> Create batch file with commands to do the hard\n")
           TEXT("                 linking.  run batch file afterwards to do it\n")
           TEXT(" -hardlink       Create hardlinks.  Works on NTFS file systems only.\n")
           TEXT("                 Use with caution!\n")
           TEXT(" -del            Delete duplicate files\n")
           TEXT(" -v              Verbose\n")
           TEXT(" -sigs           Show signatures calculated based on first 32k for each file\n")
           TEXT(" -rdonly         Apply to readonly files also (as opposed to skipping them)\n")
           TEXT(" -z              Do not skip zero length files (zero length files are ignored\n")
           TEXT("                 by default)\n")
           TEXT(" -u              Do not print a warning for files that cannot be read\n")
           TEXT(" -p              Hide progress indicator (useful when redirecting to a file)\n")
           TEXT(" -j              Follow NTFS junctions and reparse points (off by default)\n")
           TEXT(" -listlink       hardlink list mode.  Not valid with -del, -bat, -hardlink,\n")
           TEXT("                 or -rdonly, options\n")
           TEXT(" -ign <substr>   Ignore file pattern, eg. .bak or .tmp (repeatable)\n")
           TEXT(" -ref <filepat>  Following file pattern are files that are for reference, NOT to\n")
           TEXT("                 be eliminated, only used to check duplicates against (repeatable)\n")
           TEXT(" filepat         Pattern for files.  Examples:\n")
           TEXT("                  c:\\**        Match everything on drive C\n")
           TEXT("                  c:\\**\\*.jpg  Match only .jpg files on drive C\n")
           TEXT("                  **\\foo\\**    Match any path with component foo\n")
           TEXT("                                from current directory down\n")
           
           );
    exit(EXIT_FAILURE);
}

static void CheckFileSystem(TCHAR drive)
{
    if (!(BatchFileName || MakeHardLinks)) return;

    TCHAR lpRootPathName[4];
    _tcsncpy(lpRootPathName, TEXT("C:\\\0"), 4);
    _tcsncpy(lpRootPathName, &drive, 1);
    TCHAR lpFileSystemNameBuffer[MAX_PATH + 1];
    #ifdef UNICODE
    wmemset(lpFileSystemNameBuffer, L' ', sizeof(lpFileSystemNameBuffer) / sizeof(lpFileSystemNameBuffer[0]));
    BOOL ret = GetVolumeInformationW(lpRootPathName, NULL, 0, 0, 0, 0, lpFileSystemNameBuffer, MAX_PATH + 1);
    #else
    memset(lpFileSystemNameBuffer, ' ', sizeof(lpFileSystemNameBuffer));
    BOOL ret = GetVolumeInformationW(lpRootPathName, NULL, 0, 0, 0, 0, lpFileSystemNameBuffer, MAX_PATH + 1);
    #endif
    if (_tcscmp(lpFileSystemNameBuffer, TEXT("NTFS")))
    {
        if (ProgressIndicatorVisible) ClearProgressInd();
        _ftprintf(stderr, TEXT("finddupe can only make hardlinks on NTFS filesystems\n"));
        exit(EXIT_FAILURE);
    }
}

//--------------------------------------------------------------------------
// The main program.
//--------------------------------------------------------------------------
int _tmain (int argc, TCHAR **argv)
{
    int argn;
    TCHAR * arg;
    TCHAR DefaultDrive;
    TCHAR DriveUsed = '\0';
    int indexFirstRef = 0;
    
    PrintDuplicates = 0;
    PrintFileSigs = 0;
    HardlinkSearchMode = 0;
    Verbose = 0;

    for (argn = 1; argn < argc; argn++) {
        arg = argv[argn];
        if (indexFirstRef == 0 && !_tcscmp(arg, TEXT("-ref"))) indexFirstRef = argn;
        if (indexFirstRef > 0 && (!_tcscmp(arg, TEXT("-bat")) || !_tcscmp(arg, TEXT("-v")) || !_tcscmp(arg, TEXT("-sigs")) || !_tcscmp(arg, TEXT("-hardlink")) ||
            !_tcscmp(arg, TEXT("-del")) || !_tcscmp(arg, TEXT("-rdonly")) || !_tcscmp(arg, TEXT("-listlink")) || !_tcscmp(arg, TEXT("-z")) ||
            !_tcscmp(arg, TEXT("-u")) || !_tcscmp(arg, TEXT("-p")) || !_tcscmp(arg, TEXT("-j")) || !_tcscmp(arg, TEXT("-ign"))) && argn > indexFirstRef) {
            _ftprintf(stderr, TEXT("Wrong order of options!  Use -h for help\n"));
            exit(EXIT_FAILURE);
        }
    }

    for (argn=1;argn<argc;argn++){
        arg = argv[argn];
        if (arg[0] != '-') break; // Filenames from here on.

        if (!_tcscmp(arg,TEXT("-h"))){
            Usage();
            exit(EXIT_FAILURE);
        }else if (!_tcscmp(arg,TEXT("-bat"))){
            BatchFileName = argv[++argn];
        }else if (!_tcscmp(arg,TEXT("-v"))){
            PrintDuplicates = 1;
            PrintFileSigs = 1;
            Verbose = 1;
            HideCantReadMessage = 0;
        }else if (!_tcscmp(arg,TEXT("-sigs"))){
            PrintDuplicates = 0;
            PrintFileSigs = 1;
        }else if (!_tcscmp(arg,TEXT("-hardlink"))){
            MakeHardLinks = 1;
        }else if (!_tcscmp(arg,TEXT("-del"))){
            DelDuplicates = 1;
        }else if (!_tcscmp(arg,TEXT("-rdonly"))){
            DoReadonly = 1;
        }else if (!_tcscmp(arg,TEXT("-listlink"))){
            HardlinkSearchMode = 1;
        }else if (!_tcscmp(arg,TEXT("-ref"))){
            break;
        }else if (!_tcscmp(arg,TEXT("-z"))){
            SkipZeroLength = 0;
        }else if (!_tcscmp(arg,TEXT("-u"))){
            HideCantReadMessage = 1;
        }else if (!_tcscmp(arg,TEXT("-p"))){
            ShowProgress = 0;
        }else if (!_tcscmp(arg,TEXT("-j"))){
            FollowReparse = 1;
        }
        else if (!_tcscmp(arg, TEXT("-ign"))) {
            if (IgnorePatternsCount >= IgnorePatternsAlloc) {
                // Array is full.  Make it bigger
                IgnorePatternsAlloc = IgnorePatternsAlloc + 4;
                IgnorePatterns = realloc(IgnorePatterns, sizeof(TCHAR*) * IgnorePatternsAlloc);
                if (IgnorePatterns == NULL) {
                    _ftprintf(stderr, TEXT("Malloc failure"));
                    exit(EXIT_FAILURE);
                }
            };
            TCHAR* substr = _tcsdup(argv[++argn]);
            IgnorePatterns[IgnorePatternsCount++] = substr;
        }else{
            _tprintf(TEXT("Argument '%s' not understood.  Use -h for help.\n"), arg);
            exit(-1);
        }
    }

    if (argn > argc){
        fprintf(stderr, "Missing argument!  Use -h for help\n");
        exit(EXIT_FAILURE);
    }

    if (argn == argc){
        fprintf(stderr, "No files to process.   Use -h for help\n");
        exit(EXIT_FAILURE);
    }

    if (HardlinkSearchMode){
        if (BatchFileName || MakeHardLinks || DelDuplicates || DoReadonly){
            fprintf(stderr, "listlink option is not valid with any other"
                " options other than -v\n");            
            exit(EXIT_FAILURE);
        }
    }

    NumUnique = 0;
    NumAllocated = 1024;
    FileData = (FileData_t*) malloc(sizeof(FileData_t)*1024);
    if (FileData == NULL){
        fprintf(stderr, "Malloc failure");
        exit(EXIT_FAILURE);
    }

    #ifdef REF_CODE
    PathUnique = 0;
    PathAllocated = 64;
    PathData = (TCHAR**) malloc(sizeof(TCHAR*)*PathAllocated);
    if (PathData == NULL){
        fprintf(stderr, "Malloc failure");
        exit(EXIT_FAILURE);
    }
    #endif

    if (BatchFileName){
        BatchFile = _tfopen(BatchFileName, TEXT("w"));
        if (BatchFile == NULL){
            _tprintf(TEXT("Unable to open task batch file '%s'\n"), BatchFileName);
        }
        _ftprintf(BatchFile, TEXT("@echo off\n"));
        _ftprintf(BatchFile, TEXT("REM Batch file for replacing duplicates with hard links\n"));
        _ftprintf(BatchFile, TEXT("REM created by finddupe program\n\n"));
    }

    memset(&DupeStats, 0, sizeof(DupeStats));

    {
        TCHAR CurrentDir[_MAX_PATH];
        _tgetcwd(CurrentDir, _MAX_PATH);
        DefaultDrive = tolower(CurrentDir[0]);
        CheckFileSystem(DefaultDrive);
    }

    for (;argn<argc;argn++){
        int a;
        TCHAR Drive;
        FilesMatched = 0;

        if (!_tcscmp(argv[argn],TEXT("-ref"))){
            ReferenceFiles = 1;
            argn += 1;
            if (argn >= argc) continue;
        }else{
            ReferenceFiles = 0;
        }

        for (a=0;;a++){
            if (argv[argn][a] == '\0') break;
            if (argv[argn][a] == '/') argv[argn][a] = '\\';
        }

        if (argv[argn][1] == ':'){
            Drive = tolower(argv[argn][0]);
        }else{
            Drive = DefaultDrive;
        }
        if (DriveUsed == '\0') DriveUsed = Drive;
        if (DriveUsed != Drive){
            if (MakeHardLinks){
                _ftprintf(stderr, TEXT("Error: Hardlinking across different drives not possible\n"));
                return EXIT_FAILURE;
            }
        }

        if (_tcslen(argv[argn]) >= 2 && argv[argn][0] == '\\' && argv[argn][1] == '\\' && (BatchFileName || MakeHardLinks))
        {
            if (ProgressIndicatorVisible) ClearProgressInd();
            _ftprintf(stderr, TEXT("Cannot make hardlinks on network shares\n"));
            return EXIT_FAILURE;
        }
        else if (_tcslen(argv[argn]) >= 3 && argv[argn][1] == ':' && argv[argn][2] == '\\') {
            CheckFileSystem(argv[argn][0]);
        }

        // Use my globbing module to do fancier wildcard expansion with recursive
        // subdirectories under Windows.
        MyGlob(argv[argn], FollowReparse, ProcessFile);

        if (!FilesMatched){
            _ftprintf(stderr, TEXT("Error: No files matched '%s'\n"), argv[argn]);
        }
    }

    if (HardlinkSearchMode){
        ClearProgressInd();
        _tprintf(TEXT("\n"));
        DupeStats.HardlinkGroups = 0;
        WalkTree(0,-1,0);
        _tprintf(TEXT("\nNumber of hardlink groups found: %d\n"), DupeStats.HardlinkGroups);
    }else{
        if (DupeStats.TotalFiles == 0){
            _ftprintf(stderr, TEXT("No files to process\n"));
            return EXIT_FAILURE;
        }

        if (BatchFile){
            fclose(BatchFile);
            BatchFile = NULL;
        }

        // Print summary data
        ClearProgressInd();
        _tprintf(TEXT("\n"));
        _tprintf(TEXT("Files: %8u kBytes in %5d files\n"), 
                (unsigned)(DupeStats.TotalBytes/1024), DupeStats.TotalFiles);
        _tprintf(TEXT("Dupes: %8u kBytes in %5d files\n"), 
                (unsigned)(DupeStats.DuplicateBytes/1024), DupeStats.DuplicateFiles);
    }
    if (DupeStats.ZeroLengthFiles){
        _tprintf(TEXT("  %d files of zero length were skipped\n"), DupeStats.ZeroLengthFiles);
    }
    if (DupeStats.IgnoredFiles) {
        _tprintf(TEXT("  %d files were ignored\n"), DupeStats.IgnoredFiles);
    }
    if (DupeStats.CantReadFiles){
        _tprintf(TEXT("  %d files could not be opened\n"), DupeStats.CantReadFiles);
    }

    return EXIT_SUCCESS;
}
