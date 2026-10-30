// port_stormlib_names.h -- StormLib's public names, each given an Sl prefix
// (File).
//
// StormLib names its API after Storm's (SFileOpenArchive, SFileReadFile,
// SErrSetLastError and the rest) with signatures of its own, and the port
// defines Storm's names for the game. tools/build_stormlib.bat compiles every
// StormLib source with this header forced in (/FI), so the library's own
// definitions and calls use the prefixed names; port_stormlib.cpp includes it
// before StormLib.h, and nothing else in the port sees StormLib. pklib's names
// are those step 3 gave it, since the engine links step 3's pklib objects and
// StormLib's compression calls them.
#ifndef PORT_STORMLIB_NAMES_H
#define PORT_STORMLIB_NAMES_H

#define implode PkImplode
#define explode PkExplode
#define crc32_pklib PkCrc32

#define SCompCompress SlCompCompress
#define SCompDecompress SlCompDecompress
#define SCompDecompress2 SlCompDecompress2
#define SCompExplode SlCompExplode
#define SCompImplode SlCompImplode
#define SErrGetLastError SlErrGetLastError
#define SErrSetLastError SlErrSetLastError
#define SFileAddFile SlFileAddFile
#define SFileAddFileEx SlFileAddFileEx
#define SFileAddListFile SlFileAddListFile
#define SFileAddListFileEntries SlFileAddListFileEntries
#define SFileAddWave SlFileAddWave
#define SFileCloseArchive SlFileCloseArchive
#define SFileCloseFile SlFileCloseFile
#define SFileCompactArchive SlFileCompactArchive
#define SFileCreateArchive SlFileCreateArchive
#define SFileCreateArchive2 SlFileCreateArchive2
#define SFileCreateFile SlFileCreateFile
#define SFileEnumLocales SlFileEnumLocales
#define SFileExtractFile SlFileExtractFile
#define SFileFindClose SlFileFindClose
#define SFileFindFirstFile SlFileFindFirstFile
#define SFileFindNextFile SlFileFindNextFile
#define SFileFinishFile SlFileFinishFile
#define SFileFlushArchive SlFileFlushArchive
#define SFileFreeFileInfo SlFileFreeFileInfo
#define SFileGetAttributes SlFileGetAttributes
#define SFileGetFileArchive SlFileGetFileArchive
#define SFileGetFileChecksums SlFileGetFileChecksums
#define SFileGetFileInfo SlFileGetFileInfo
#define SFileGetFileName SlFileGetFileName
#define SFileGetFileSize SlFileGetFileSize
#define SFileGetLocale SlFileGetLocale
#define SFileGetMaxFileCount SlFileGetMaxFileCount
#define SFileHasFile SlFileHasFile
#define SFileIsPatchedArchive SlFileIsPatchedArchive
#define SFileOpenArchive SlFileOpenArchive
#define SFileOpenFileArchive SlFileOpenFileArchive
#define SFileOpenFileEx SlFileOpenFileEx
#define SFileOpenPatchArchive SlFileOpenPatchArchive
#define SFileReadFile SlFileReadFile
#define SFileRemoveFile SlFileRemoveFile
#define SFileRenameFile SlFileRenameFile
#define SFileSetAddFileCallback SlFileSetAddFileCallback
#define SFileSetArchiveMarkers SlFileSetArchiveMarkers
#define SFileSetAttributes SlFileSetAttributes
#define SFileSetCompactCallback SlFileSetCompactCallback
#define SFileSetDataCompression SlFileSetDataCompression
#define SFileSetDownloadCallback SlFileSetDownloadCallback
#define SFileSetFileLocale SlFileSetFileLocale
#define SFileSetFilePointer SlFileSetFilePointer
#define SFileSetLocale SlFileSetLocale
#define SFileSetMaxFileCount SlFileSetMaxFileCount
#define SFileSignArchive SlFileSignArchive
#define SFileUpdateFileAttributes SlFileUpdateFileAttributes
#define SFileVerifyArchive SlFileVerifyArchive
#define SFileVerifyFile SlFileVerifyFile
#define SFileVerifyRawData SlFileVerifyRawData
#define SFileWriteFile SlFileWriteFile
#define SListFileFindClose SlListFileFindClose
#define SListFileFindFirstFile SlListFileFindFirstFile
#define SListFileFindNextFile SlListFileFindNextFile
#define SMemFileNameToUTF8 SlMemFileNameToUTF8
#define SMemUTF8ToFileName SlMemUTF8ToFileName

#endif
