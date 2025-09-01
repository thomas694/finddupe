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
// Version 1.27  (c) Oct 2020  thomas694
//     file system checks (batch and hardlink mode)
// Version 1.28  (c) Jul 2022  thomas694
//     fixed bug (divided hardlink groups) in original listlink functionality
//     performance optimizations (especially for very large amounts of files)
// Version 1.29  (c) Aug 2023  thomas694
//     fixed a problem with large files
// Version 1.30  (c) Sep 2023  thomas694
//     added option to skip linked duplicates in output list
// Version 1.31  (c) Dec 2023  thomas694
//     fixed a problem with non-ASCII characters/code pages
// Version 1.32  (c) Jan 2024  thomas694
//     fixed a problem with non-ASCII characters/code pages on systems older than Win10
// Version 1.33  (c) Jun 2024  thomas694
//     fixed a problem writing filenames with special unicode characters to the batch file
//     fixed a memory problem with very large amounts of files
//     added a 64-bit version for addressing more memory
// Version 1.34  (c) Sep 2024  thomas694
//     fixed a display problem with the progress indicator
// Version 1.35  (c) Aug 2025  thomas694
//     fixed a problem with some special characters (e.g. right single/double quotation mark)
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

#define VERSION "1.35"

#define REF_CODE

#include <locale.h>
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
#include <fcntl.h>

#include "khash.h"

#define  S_IWUSR  0x80      // user has write permission
#define  S_IWGRP  0x10      // group has write permission
#define  S_IWOTH  0x02      // others have write permisson

static int FilesMatched;

DWORD totalCompare;
DWORD totalPrint;
DWORD totalFileInfo;
DWORD totalByteRead;
DWORD totalCRC;
DWORD totalCheck;
DWORD ticksCheck;

typedef struct {
    unsigned int Crc;
    unsigned int Sum;
}Checksum_t;

KHASH_MAP_INIT_INT64(hset, Checksum_t)
KHASH_MAP_INIT_INT64(hmap, UINT64)

// Data structure for file allocations:
typedef struct FileData_t FileData_t;
struct FileData_t {
    Checksum_t Checksum;
    struct {
        int High;
        int Low;
    }FileIndex;    
    int NumLinks; 
    UINT64 FileSize;
    TCHAR * FileName;
    FileData_t * Larger;    // Child pointer for larger child
    FileData_t * Smaller;   // Child pointer for smaller child
};
static FileData_t * FileData;
static int NumAllocated;
static int NumUnique;
static khash_t(hset) * FilenameSet;
static khash_t(hmap) * FileDataMap;

#define UNITS_PER_ALLOCATION 102400

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
    UINT64 TotalBytes;
    UINT64 DuplicateBytes;
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
int SkipLinkedDuplicates = 0; // Skip linked duplicates and show only unlinked ones

TCHAR* * IgnorePatterns;   // Patterns of filename to ignore (can be repeated, eg. .bak, .tmp)
int IgnorePatternsAlloc;   // Number of allocated ignore patterns
int IgnorePatternsCount;   // Number of specified ignore patterns
UINT m_old_code_page;
BOOL NewConsoleMode;

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
    if (ProgressIndicatorVisible) {
        _tprintf(NewConsoleMode ? TEXT("\33[2K\r") : TEXT("                                                                             \r"));
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

static INT64 CalcFilenameCRC(TCHAR* filename)
{
    unsigned int len = _tcslen(filename);
#ifdef UNICODE
    len = len * 2;
#endif
    char* charFileName = (char*)filename;

    Checksum_t checkSum = { .Crc = 0, .Sum = 0 };
    CalcCrc(&checkSum, charFileName, len);

    INT64 crc = (INT64)(((UINT64)checkSum.Crc) << 32 | ((UINT64)checkSum.Sum));
    return crc;
}

static khiter_t kh_get_fn(INT64 filenameCRC, int createNew, int* created)
{
    khint_t k = kh_get(hset, FilenameSet, filenameCRC);
    if (createNew && k == kh_end(FilenameSet))
    {
        *created = 1;
        k = kh_put_fn(filenameCRC);
    }
    else
        *created = 0;
    return k;
}

static khiter_t kh_put_fn(INT64 filenameCRC)
{
    int ret;
    khint_t k = kh_put(hset, FilenameSet, filenameCRC, &ret);
    if (ret == -1) {
        _ftprintf(stderr, TEXT("error storing new filename entry"));
        kh_destroy(hset, FilenameSet);
        exit(EXIT_FAILURE);
    }
    if (ret == 0) return k;

    Checksum_t chk = { 0 };
    kh_value(FilenameSet, k) = chk;
    return k;
}

static khiter_t kh_get_fd(UINT64 fileSize, int createNew, int* found)
{
    khint_t k = kh_get(hmap, FileDataMap, fileSize);
    if (k == kh_end(FileDataMap))
    {
        *found = 0;
        if (createNew) k = kh_put_fd(fileSize);
    }
    else
        *found = 1;
    return k;
}

static khiter_t kh_put_fd(UINT64 fileSize)
{
    int ret;
    khint_t k = kh_put(hmap, FileDataMap, fileSize, &ret);
    if (ret == -1) {
        _ftprintf(stderr, TEXT("error storing new file entry"));
        kh_destroy(hmap, FileDataMap);
        exit(EXIT_FAILURE);
    }
    if (ret == 0) return k;

    kh_value(FileDataMap, k) = NULL;
    return k;
}

void WriteStringToFile(FILE* file, TCHAR* text) {
    if (sizeof(TCHAR) == sizeof(char)) {
        _ftprintf(file, "%s", text);
    }
    else {
        size_t required_size = WideCharToMultiByte(CP_UTF8, 0, text, -1, NULL, 0, NULL, NULL);
        char* buffer = calloc(required_size, sizeof(char));
        WideCharToMultiByte(CP_UTF8, 0, text, -1, buffer, required_size, NULL, NULL);
        fprintf(file, "%s", buffer);
        free(buffer);
    }
}

void ftprintf(FILE* file, TCHAR* format, ...) {
    va_list _argList;
    va_start(_argList, format);

    int len = _vsntprintf(NULL, 0, format, _argList);
    TCHAR* s = calloc(len + 1, sizeof(TCHAR));
    _vsntprintf(s, len, format, _argList);

    WriteStringToFile(file, s);
    free(s);

    va_end(_argList);
}

//--------------------------------------------------------------------------
// Eliminate duplicates.
//--------------------------------------------------------------------------
static int EliminateDuplicate(FileData_t ThisFile, FileData_t DupeOf)
{
    // First compare whole file.  If mismatch, return 0.
    int IsDuplicate = 0;
    int IsError = 0;
    int Hardlinked = 0;
    int IsReadonly;
    struct _stat64 FileStat;
    int doCalc1 = 0, doCalc2 = 0;
    Checksum_t chk1 = { .Crc = 0,.Sum = 0 };
    Checksum_t chk2 = { .Crc = 0,.Sum = 0 };

    if (ThisFile.FileSize != DupeOf.FileSize) return 0;

    Hardlinked = 0;
    if (DupeOf.NumLinks && memcmp(&ThisFile.FileIndex, &DupeOf.FileIndex, sizeof(DupeOf.FileIndex)) == 0){
        Hardlinked = 1;
        goto dont_read;
    }

    if (DupeOf.NumLinks >= 1023) {
        // Do not link more than 1023 files onto one physical file (windows limit)
        return 0;
    }

    INT64 fnCRC1, fnCRC2;
    fnCRC1 = CalcFilenameCRC(ThisFile.FileName);
    fnCRC2 = CalcFilenameCRC(DupeOf.FileName);

    khint_t k1 = kh_get_fn(fnCRC1, 1, &doCalc1);
    if (!doCalc1) {
        chk1 = kh_value(FilenameSet, k1);
        if (chk1.Crc == 0) doCalc1 = 1;
    }
    khint_t k2 = kh_get_fn(fnCRC2, 1, &doCalc2);
    if (!doCalc2) {
        chk2 = kh_value(FilenameSet, k2);
        if (chk2.Crc == 0) doCalc2 = 1;
    }

    if (doCalc1 || doCalc2)
    {
        if (doCalc1 && !ReadFileAndCalculateCRC(ThisFile.FileName, ThisFile.FileSize, &chk1)) return 0;
        if (doCalc2 && !ReadFileAndCalculateCRC(DupeOf.FileName, DupeOf.FileSize, &chk2)) return 0;

        if (doCalc1) kh_value(FilenameSet, k1) = chk1;
        if (doCalc2) kh_value(FilenameSet, k2) = chk2;
    }
    //if (!doCalc1) chk1 = kh_value(FilenameSet, k1);
    //if (!doCalc2) chk2 = kh_value(FilenameSet, k2);

    if (memcmp(&chk1, &chk2, sizeof(Checksum_t)) == 0) IsDuplicate = 1;

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
            if (!(Hardlinked && SkipLinkedDuplicates)) {
                _tprintf(TEXT("Duplicate: '%s'\n"), DupeOf.FileName);
                _tprintf(TEXT("With:      '%s'\n"), ThisFile.FileName);
            }
            if (Hardlinked && !SkipLinkedDuplicates) {
                // If the files happen to be hardlinked, show that.
                _tprintf(TEXT("    (hardlinked instances of same file)\n"));
            }
        }
    }

    if (_tstat64(ThisFile.FileName, &FileStat) != 0){
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
            ftprintf(BatchFile, TEXT("del %s\"%s\"\n"), (IsReadonly ? TEXT("/F ") : TEXT("")),
                EscapeBatchName(ThisFile.FileName));
        if (!DelDuplicates){
            if (!Hardlinked){
                ftprintf(BatchFile, TEXT("fsutil hardlink create \"%s\" \"%s\"\n"),
                    ThisFile.FileName, DupeOf.FileName);
                if (IsReadonly){
                    // If original was readonly, restore that attribute
                    ftprintf(BatchFile, TEXT("attrib +r \"%s\"\n"), ThisFile.FileName);
                }
            }
        }else{
            ftprintf(BatchFile, TEXT("rem duplicate of \"%s\"\n"), DupeOf.FileName);
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

static int ReadFileAndCalculateCRC(TCHAR* fileName, UINT64 fileSize, Checksum_t* checksum)
{
    #define CHUNK_SIZE 0x10000
    FILE * File;
    UINT64 BytesLeft;
    size_t BytesToRead;
    char Buf[CHUNK_SIZE];
    int IsError = 0;

    File = _tfopen(fileName, TEXT("rb"));
    if (File == NULL) {
        return 0;
    }
    memset(checksum, 0, sizeof(Checksum_t));

    BytesLeft = fileSize;

    while (BytesLeft) {
        BytesToRead = (BytesLeft > CHUNK_SIZE) ? CHUNK_SIZE : BytesLeft;

        if (fread(Buf, 1, BytesToRead, File) != BytesToRead) {
            ClearProgressInd();
            _ftprintf(stderr, TEXT("Error doing full file read on '%s'\n"), fileName);
            IsError = 1;
            break;
        }

        CalcCrc(checksum, Buf, BytesToRead);

        BytesLeft -= BytesToRead;
    }

    fclose(File);
    
    return !IsError;
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
        if (_tcscmp(cmpPath, PathData[i]) == 0) {
            free(cmpPath);
            return 0;
        }
    }
    free(cmpPath);

    return 1;
}
#endif

static void StoreFileData(FileData_t ThisFile, INT64 filenameCRC)
{
    int currentIndex = NumUnique % UNITS_PER_ALLOCATION;

    if (NumUnique >= NumAllocated) {
        // Box is full, make a new one
        NumAllocated += UNITS_PER_ALLOCATION;
        FileData = (FileData_t*)malloc(sizeof(FileData_t) * UNITS_PER_ALLOCATION);
        if (FileData == NULL) {
            _ftprintf(stderr, TEXT("Malloc failure"));
            exit(EXIT_FAILURE);
        }
    }
    FileData[currentIndex] = ThisFile;
    NumUnique += 1;

    int found;
    khiter_t k = kh_get_fd(ThisFile.FileSize, 1, &found);
    if (!found)
        kh_value(FileDataMap, k) = &FileData[currentIndex];

    if (filenameCRC == 0)
        filenameCRC = CalcFilenameCRC(ThisFile.FileName);

    kh_put_fn(filenameCRC);
}

//--------------------------------------------------------------------------
// Check for duplicates.
//--------------------------------------------------------------------------
static void CheckDuplicate(FileData_t *Ptr, FileData_t ThisFile, INT64 filenameCRC)
{
    FileData_t *prevPtr = NULL;
    FileData_t * *Link;
    // Find where in the tree structure it belongs.
    //Ptr = 0;

    if (MeasureDurations) ticksCheck = GetTickCount();

    if (NumUnique == 0 || Ptr == NULL) goto store_it;

    /*
    int found;
    khiter_t k = kh_get_fd(ThisFile.FileSize, 0, &found);
    if (found)
        Ptr = kh_value(FileDataMap, k);
    else
        goto store_it;
    */

    int comp = 0, oldComp;
    for (;;) {
        oldComp = comp;
        comp = memcmp(&ThisFile.Checksum, &Ptr->Checksum, sizeof(Checksum_t));
        if (comp == 0) {
            // the same file
            if (_tcscmp(ThisFile.FileName, Ptr->FileName) == 0) {
                if (MeasureDurations) { ticksCheck = GetTickCount() - ticksCheck; totalCheck += ticksCheck; }
                return;
            }
            // Check for true duplicate.
            #ifdef REF_CODE
            if (!ReferenceFiles && !HardlinkSearchMode && IsNonRefPath(ThisFile.FileName)) {
            #else
            if (!ReferenceFiles && !HardlinkSearchMode) {
            #endif
                int r = EliminateDuplicate(ThisFile, *Ptr);
                if (r) {
                    if (r == 2) Ptr->NumLinks += 1; // Update link count.
                    // Its a duplicate for elimination.  Do not store info on it. New: store info for correct statistic calculation
                    goto store_it;
                }
            }
            // Build a chain on one side of the branch.
            // That way, we will check every checksum collision from here on.
            // Mark that we are on chain with equal checksums
            comp = 2;
        }
        else if (oldComp == 2) {
            // Insert it at the chain end before the next higher checksum so that the branch keeps sorted
            comp = 3;
        }

        if (comp) {
            if (comp > 0) {
                if (comp == 3)
                {
                    prevPtr->Larger = FileData + (NumUnique % UNITS_PER_ALLOCATION);
                    ThisFile.Larger = Ptr;
                    break;
                }
                else
                    Link = &Ptr->Larger;
            }
            else {
                Link = &Ptr->Smaller;
            }
            if (*Link == NULL) {
                // Link it to here.
                *Link = FileData + (NumUnique % UNITS_PER_ALLOCATION);
                break;
            } else {
                prevPtr = Ptr;
                Ptr = *Link;
            }
        }
    }

    store_it:

    if (MeasureDurations) { ticksCheck = GetTickCount() - ticksCheck; totalCheck += ticksCheck; }

    DupeStats.TotalFiles += 1;
    DupeStats.TotalBytes += (__int64)ThisFile.FileSize;

    StoreFileData(ThisFile, filenameCRC);
}

//--------------------------------------------------------------------------
// Walk the file tree after handling detect mode to show linked groups.
//--------------------------------------------------------------------------
static void WalkTree(FileData_t *item, FileData_t *linksFirst, int groupLen)
{
    int a;
    FileData_t *t;

    if (NumUnique == 0) return;

    if (item->Larger != NULL) {
        FileData_t *larger = item->Larger;
        if (memcmp(&larger->Checksum, &item->Checksum, sizeof(Checksum_t)) == 0) {
            // it continues the same group.
            WalkTree(item->Larger, linksFirst != NULL ? linksFirst : item, groupLen + 1);
            goto not_end;
        }
        else {
            WalkTree(item->Larger, NULL, 0);
        }
    }
    _tprintf(TEXT("\nHardlink group, %d of %d hardlinked instances found in search tree:\n"), groupLen + 1, item->NumLinks);
    t = linksFirst != NULL ? linksFirst : item;
    for (a = 0; a <= groupLen; a++) {
        _tprintf(TEXT("  \"%s\"\n"), t->FileName);
        t = t->Larger;
    }

    DupeStats.HardlinkGroups += 1;

not_end:
    if (item->Smaller != NULL) {
        WalkTree(item->Smaller, NULL, 0);
    }
}

Checksum_t ReadFileAndCalculateCRC32KB(HANDLE FileHandle, const TCHAR* FileName, UINT64 FileSize)
{
    Checksum_t CheckSum;
    char FileBuffer[BYTES_DO_CHECKSUM_OF];
    unsigned BytesRead, BytesToRead;
    memset(&CheckSum, 0, sizeof(CheckSum));

    int ticksByteRead, ticksCRC;
    if (MeasureDurations) ticksByteRead = GetTickCount();

    BytesToRead = (FileSize > BYTES_DO_CHECKSUM_OF) ? BYTES_DO_CHECKSUM_OF : FileSize;
    BOOL ret = ReadFile(FileHandle, FileBuffer, BytesToRead, &BytesRead, NULL);
    if (!ret) {
        if (!HideCantReadMessage) {
            ClearProgressInd();
            _ftprintf(stderr, TEXT("file read problem on '%s'\n"), FileName);
        }
        CloseHandle(FileHandle);
        return;
    }

    if (MeasureDurations) { ticksByteRead = GetTickCount() - ticksByteRead; totalByteRead += ticksByteRead; ticksCRC = GetTickCount(); }

    CalcCrc(&CheckSum, FileBuffer, BytesRead);

    if (MeasureDurations) { ticksCRC = GetTickCount() - ticksCRC; totalCRC += ticksCRC; }

    CheckSum.Sum += FileSize;
    if (PrintFileSigs) {
        ClearProgressInd();
        _tprintf(TEXT("%08x%08x %10llu %s\n"), CheckSum.Crc, CheckSum.Sum, FileSize, FileName);
    }

    return CheckSum;
}

BOOL OpenTheFile(const TCHAR* FileName, HANDLE* FileHandle)
{
    *FileHandle = CreateFile(FileName,
        GENERIC_READ,         // dwDesiredAccess
        FILE_SHARE_READ,      // dwShareMode
        NULL,                 // Security attributes
        OPEN_EXISTING,        // dwCreationDisposition
        FILE_ATTRIBUTE_NORMAL,// dwFlagsAndAttributes.  Ignored for opening existing files
        NULL);                // hTemplateFile.  Ignored for existing.
    if (*FileHandle == (void*)-1) {
    cant_read_file:
        DupeStats.CantReadFiles += 1;
        if (!HideCantReadMessage) {
            ClearProgressInd();
            _ftprintf(stderr, TEXT("Could not read '%s'\n"), FileName);
        }
        return FALSE;
    }
    return TRUE;
}

//--------------------------------------------------------------------------
// Do selected operations to one file at a time.
//--------------------------------------------------------------------------
static void ProcessFile(const TCHAR* FileName)
{
    UINT64 FileSize;
    Checksum_t CheckSum;
    DWORD ticksCompare = 0;
    DWORD ticksPrint = 0;
    DWORD ticksFileInfo = 0;
    DWORD ticksByteRead = 0;
    DWORD ticksCRC = 0;

    if (MeasureDurations) ticksCompare = GetTickCount();

    // replace linear list search with hashset lookup
    INT64 crc = CalcFilenameCRC(FileName);
    int created;
    khiter_t k = kh_get_fn(crc, 0, &created);
    if (k != kh_end(FilenameSet))
    {
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
                wmemset(ShowName, L'\0', sizeof(ShowName) / sizeof(ShowName[0]));
                #else
                memset(ShowName, ' ', sizeof(ShowName));
                #endif
                if (l > 53) l = 53;
                #ifdef UNICODE
                wmemcpy(ShowName, FileName, l);
                if (l >= 53) wmemcpy(ShowName + 53, L"…", 1);
                #else
                memcpy(ShowName, FileName, l);
                if (l >= 53) memcpy(ShowName + 53, "…", 1);
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

    // removed stat function was only used for getting file size, so use below FS access

    // skip if filename contains a ignore pattern
    for (int i = 0; i < IgnorePatternsCount; i++)
    {
        if (StrStrI(FileName, IgnorePatterns[i]))
        {
            DupeStats.IgnoredFiles++;
            ThisFile.FileName = _tcsdup(FileName);
            StoreFileData(ThisFile, crc);
            return;
        }
    }

    HANDLE FileHandle;
    {
        if (MeasureDurations) ticksFileInfo = GetTickCount();

        BY_HANDLE_FILE_INFORMATION FileInfo;
        if (!OpenTheFile(FileName, &FileHandle)) return;
        GetFileInformationByHandle(FileHandle, &FileInfo);

        //CloseHandle(FileHandle);

        if (MeasureDurations) { ticksFileInfo = GetTickCount() - ticksFileInfo; totalFileInfo += ticksFileInfo; }

        if (Verbose){
            ClearProgressInd();
            _tprintf(TEXT("Hardlinked (%d links) node=%08x %08x: %s\n"), FileInfo.nNumberOfLinks, 
                FileInfo.nFileIndexHigh, FileInfo.nFileIndexLow, FileName);
        }

        if (HardlinkSearchMode && FileInfo.nNumberOfLinks == 1){
            // File has only one link, so its not hardlinked.  Skip for hardlink search mode.
            CloseHandle(FileHandle);
            return;
        }

        //_tprintf(TEXT("    Info:  Index: %08x %08x\n"),FileInfo.nFileIndexHigh, FileInfo.nFileIndexLow);

        // Use the file index (which is NTFS equivalent of the iNode) instead of the CRC.
        ThisFile.FileIndex.Low      = FileInfo.nFileIndexLow;
        ThisFile.FileIndex.High     = FileInfo.nFileIndexHigh;
        ThisFile.NumLinks = FileInfo.nNumberOfLinks;
        ThisFile.Larger = NULL;
        ThisFile.Smaller = NULL;
        ULARGE_INTEGER ul;
        ul.HighPart = FileInfo.nFileSizeHigh;
        ul.LowPart = FileInfo.nFileSizeLow;
        FileSize = ul.QuadPart;
        ThisFile.FileSize = FileSize;

        if (FileSize == 0) {
            if (SkipZeroLength) {
                DupeStats.ZeroLengthFiles += 1;
                CloseHandle(FileHandle);
                return;
            }
        }

        if (HardlinkSearchMode){
            // For hardlink search mode, duplicates are detected by file index, not CRC,
            // so copy the file ID into the CRC.
            ThisFile.Checksum.Sum = ThisFile.FileIndex.Low;
            ThisFile.Checksum.Crc = ThisFile.FileIndex.High;
        }
    }

    FileData_t * Ptr = NULL;
    int found;
    khiter_t k_fd = kh_get_fd(ThisFile.FileSize, 0, &found);
    if (found)
        Ptr = kh_value(FileDataMap, k_fd);

    if (!HardlinkSearchMode) {
        if (found) {
            if (Ptr->Checksum.Crc == 0) {
                HANDLE rootHandle = 0;
                if (!OpenTheFile(Ptr->FileName, &rootHandle)) return;
                Ptr->Checksum = ReadFileAndCalculateCRC32KB(rootHandle, Ptr->FileName, Ptr->FileSize);
                CloseHandle(rootHandle);
            }

            ThisFile.Checksum = ReadFileAndCalculateCRC32KB(FileHandle, FileName, FileSize);
        }
    }
    CloseHandle(FileHandle);

    ThisFile.FileName = _tcsdup(FileName); // allocate the string last, so 
                                          // we don't waste memory on errors.

    CheckDuplicate(Ptr, ThisFile, crc);

    if (MeasureDurations) {
        _tprintf(TEXT("Cmp: %d / %d Print: %d / %d FS: 0 / 0 FI: %d / %d BR: %d / %d CRC: %d / %d CHK: %d / %d  =  %d\n"),
            ticksCompare, totalCompare, ticksPrint, totalPrint, ticksFileInfo, totalFileInfo, ticksByteRead, totalByteRead, ticksCRC, totalCRC, ticksCheck, totalCheck,
            (totalCompare + totalPrint + totalFileInfo + totalByteRead + totalCRC + totalCheck));
    }
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
           TEXT(" -sl             Skip linked duplicates and show only unlinked ones\n")
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
        ClearProgressInd();
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
    
    PrintDuplicates = 1;
    PrintFileSigs = 0;
    HardlinkSearchMode = 0;
    Verbose = 0;

    _tsetlocale(LC_CTYPE, TEXT(".UTF8"));
#ifdef UNICODE
    _setmode(_fileno(stdout), _O_U16TEXT);
    _setmode(_fileno(stderr), _O_U16TEXT);
#endif

    HANDLE hConsole = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    GetConsoleMode(hConsole, &mode);
    NewConsoleMode = SetConsoleMode(hConsole, mode | ENABLE_PROCESSED_OUTPUT | ENABLE_VIRTUAL_TERMINAL_PROCESSING);

    for (argn = 1; argn < argc; argn++) {
        arg = argv[argn];
        if (indexFirstRef == 0 && !_tcscmp(arg, TEXT("-ref"))) indexFirstRef = argn;
        if (indexFirstRef > 0 && (!_tcscmp(arg, TEXT("-bat")) || !_tcscmp(arg, TEXT("-v")) || !_tcscmp(arg, TEXT("-sigs")) || !_tcscmp(arg, TEXT("-hardlink")) ||
            !_tcscmp(arg, TEXT("-del")) || !_tcscmp(arg, TEXT("-rdonly")) || !_tcscmp(arg, TEXT("-listlink")) || !_tcscmp(arg, TEXT("-z")) || !_tcscmp(arg, TEXT("-u")) ||
            !_tcscmp(arg, TEXT("-sl")) || !_tcscmp(arg, TEXT("-p")) || !_tcscmp(arg, TEXT("-j")) || !_tcscmp(arg, TEXT("-ign"))) && argn > indexFirstRef) {
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
        }else if (!_tcscmp(arg,TEXT("-sl"))) {
            SkipLinkedDuplicates = 1;
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
        _ftprintf(stderr, TEXT("Missing argument!  Use -h for help\n"));
        exit(EXIT_FAILURE);
    }

    if (argn == argc){
        _ftprintf(stderr, TEXT("No files to process.   Use -h for help\n"));
        exit(EXIT_FAILURE);
    }

    if (HardlinkSearchMode){
        if (BatchFileName || MakeHardLinks || DelDuplicates || DoReadonly){
            _ftprintf(stderr, TEXT("listlink option is not valid with any other")
                TEXT(" options other than -v\n"));
            exit(EXIT_FAILURE);
        }
    }

    NumUnique = 0;
    NumAllocated = UNITS_PER_ALLOCATION;
    FileData = (FileData_t*)malloc(sizeof(FileData_t) * NumAllocated);
    if (FileData == NULL){
        _ftprintf(stderr, TEXT("Malloc failure"));
        exit(EXIT_FAILURE);
    }

    #ifdef REF_CODE
    PathUnique = 0;
    PathAllocated = 64;
    PathData = (TCHAR**) malloc(sizeof(TCHAR*)*PathAllocated);
    if (PathData == NULL){
        _ftprintf(stderr, TEXT("Malloc failure"));
        exit(EXIT_FAILURE);
    }
    #endif

    if (BatchFileName) {
#ifdef UNICODE
        BatchFile = _tfopen(BatchFileName, TEXT("wb"));
#else
        BatchFile = _tfopen(BatchFileName, TEXT("w"));
#endif
        if (BatchFile == NULL) {
            _ftprintf(stderr, TEXT("Unable to open task batch file '%s'\n"), BatchFileName);
            exit(EXIT_FAILURE);
        }
#ifdef UNICODE
        unsigned char bom[] = { 0xEF, 0xBB, 0xBF };
        fwrite(bom, sizeof(bom), 1, BatchFile);
        fclose(BatchFile);
        BatchFile = _tfopen(BatchFileName, TEXT("a"));
        if (BatchFile == NULL) {
            _ftprintf(stderr, TEXT("Unable to open task batch file '%s'\n"), BatchFileName);
            exit(EXIT_FAILURE);
        }
        _ftprintf(BatchFile, TEXT("\n"));
#endif
        _ftprintf(BatchFile, TEXT("@echo off\n"));
        _ftprintf(BatchFile, TEXT("REM Batch file for replacing duplicates with hard links\n"));
        _ftprintf(BatchFile, TEXT("REM created by finddupe program\n"));
#ifdef UNICODE
        _ftprintf(BatchFile, TEXT("if errorlevel 1 (\n"));
        _ftprintf(BatchFile, TEXT("  echo.\n"));
        _ftprintf(BatchFile, TEXT("  echo Set code page to 65001. Rerun script to execute hardlink commands.\n"));
        _ftprintf(BatchFile, TEXT("  chcp 65001\n"));
        _ftprintf(BatchFile, TEXT(") else (\n"));
#endif
        _ftprintf(BatchFile, TEXT("chcp 65001\n\n"));
    }

    memset(&DupeStats, 0, sizeof(DupeStats));

    {
        TCHAR CurrentDir[_MAX_PATH];
        _tgetcwd(CurrentDir, _MAX_PATH);
        DefaultDrive = tolower(CurrentDir[0]);
        CheckFileSystem(DefaultDrive);
    }

    FilenameSet = kh_init(hset);
    FileDataMap = kh_init(hmap);

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
                kh_destroy(hset, FilenameSet);
                kh_destroy(hmap, FileDataMap);
                return EXIT_FAILURE;
            }
        }

        if (_tcslen(argv[argn]) >= 2 && argv[argn][0] == '\\' && argv[argn][1] == '\\' && (BatchFileName || MakeHardLinks))
        {
            ClearProgressInd();
            _ftprintf(stderr, TEXT("Cannot make hardlinks on network shares\n"));
            kh_destroy(hset, FilenameSet);
            kh_destroy(hmap, FileDataMap);
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

    kh_destroy(hset, FilenameSet);

    if (HardlinkSearchMode){
        ClearProgressInd();
        _tprintf(TEXT("\n"));
        DupeStats.HardlinkGroups = 0;
        // get tree roots for each file size and walk it
        khint_t k;
        for (k = kh_begin(FileDataMap); k != kh_end(FileDataMap); ++k)
            if (kh_exist(FileDataMap, k))
                WalkTree(kh_value(FileDataMap, k), NULL, 0);
        _tprintf(TEXT("\nNumber of hardlink groups found: %d\n"), DupeStats.HardlinkGroups);
    }else{
        if (DupeStats.TotalFiles == 0){
            _ftprintf(stderr, TEXT("No files to process\n"));
            return EXIT_FAILURE;
        }

        if (BatchFile){
#ifdef UNICODE
            _ftprintf(BatchFile, TEXT(")\n"));
#endif
            fclose(BatchFile);
            BatchFile = NULL;
        }

        // Print summary data
        ClearProgressInd();
        UINT64 totalBytes = ((UINT64)(DupeStats.TotalBytes / 1024) == 0 && DupeStats.TotalBytes > 0) ? 1 : DupeStats.TotalBytes / 1024;
        UINT64 duplicateBytes = ((UINT64)(DupeStats.DuplicateBytes / 1024) == 0 && DupeStats.DuplicateBytes > 0) ? 1 : DupeStats.DuplicateBytes / 1024;
        _tprintf(TEXT("\n"));
        _tprintf(TEXT("Files: %8llu kBytes in %5d files\n"), 
                totalBytes, DupeStats.TotalFiles);
        _tprintf(TEXT("Dupes: %8llu kBytes in %5d files\n"), 
                duplicateBytes, DupeStats.DuplicateFiles);
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

    SetConsoleMode(hConsole, mode);

    kh_destroy(hmap, FileDataMap);

    return EXIT_SUCCESS;
}
