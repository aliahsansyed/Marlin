#ifndef CARDREADER_H
#define CARDREADER_H

#ifdef SDSUPPORT

#define MAX_DIR_DEPTH 10          // Maximum folder depth

#include "SdFile.h"
enum LsAction { LS_SerialPrint, LS_Count, LS_GetFilename };

class CardReader {
public:
  CardReader();

  void initsd();
  void write_command(char *buf);
  //files auto[0-9].g on the sd card are performed in a row
  //this is to delay autostart and hence the initialisaiton of the sd card to some seconds after the normal init, so the device is available quick after a reset

  void checkautostart(bool x);
  void openFile(char* name,bool read,bool replace_current=true);
  void openLogFile(char* name);
  void removeFile(char* name);
  void closefile(bool store_location=false);
  void release();
  void startFileprint();
  void pauseSDPrint();
  void getStatus();
  void printingHasFinished();

  void getfilename(uint16_t nr, const char* const match=NULL);
  uint16_t getnrfilenames();

  void getAbsFilename(char *t);

  void ls();
  void chdir(const char * relpath);
  void updir();
  void setroot();

  #ifdef SDCARD_SORT_ALPHA
    void presort();
    void getfilename_sorted(const uint16_t nr);
    #if SORT_ONOFF
      FORCE_INLINE void setSortOn(bool b) { sort_alpha = b; }
      FORCE_INLINE void setSortFolders(int i) { sort_folders = i; }
      //FORCE_INLINE void setSortReverse(bool b) { sort_reverse = b; }
    #endif
  #endif

  FORCE_INLINE bool isFileOpen() { return file.isOpen(); }
  FORCE_INLINE bool eof() { return sdpos >= filesize; }
  FORCE_INLINE int16_t get() { sdpos = file.curPosition(); return (int16_t)file.read(); }
  FORCE_INLINE void setIndex(long index) { sdpos = index; file.seekSet(index); }
  FORCE_INLINE uint8_t percentDone() { return (isFileOpen() && filesize) ? sdpos / ((filesize + 99) / 100) : 0; }
  FORCE_INLINE char* getWorkDirName() { workDir.getFilename(filename); return filename; }

public:
  bool saving, logging, sdprinting, cardOK, filenameIsDir;
  char filename[FILENAME_LENGTH], longFilename[LONG_FILENAME_LENGTH];
  int autostart_index;
private:
  SdFile root, *curDir, workDir, workDirParents[MAX_DIR_DEPTH];
  uint16_t workDirDepth;
  #ifdef SDCARD_SORT_ALPHA
    uint16_t sort_count;
    #if SORT_ONOFF
      bool sort_alpha;
      int sort_folders;
      //bool sort_reverse;
    #endif
    #if SORT_USES_RAM
      uint8_t *sort_order;
      #if SORT_USES_MORE_RAM
        char **sortshort;
        char **sortnames;
      // #elif SORT_USES_MALLOC == false
      //   char sortshort[SORT_LIMIT][FILENAME_LENGTH];
      //   char sortnames[SORT_LIMIT][LONG_FILENAME_LENGTH];
      #endif
    #else
      uint8_t sort_order[SORT_LIMIT];
    #endif
    #if (FOLDER_SORTING || SORT_ONOFF) && SORT_USES_RAM && SORT_USES_MORE_RAM
      #if SORT_USES_MALLOC
        uint8_t *isDir;
      #else
        uint8_t isDir[(SORT_LIMIT+7)>>3];
      #endif
    #endif
  #endif
  Sd2Card card;
  SdVolume volume;
  SdFile file;
  #define SD_PROCEDURE_DEPTH 1
  #define MAXPATHNAMELENGTH (FILENAME_LENGTH*MAX_DIR_DEPTH + MAX_DIR_DEPTH + 1)
  uint8_t file_subcall_ctr;
  uint32_t filespos[SD_PROCEDURE_DEPTH];
  char filenames[SD_PROCEDURE_DEPTH][MAXPATHNAMELENGTH];
  uint32_t filesize;
  millis_t next_autostart_ms;
  uint32_t sdpos;

  bool autostart_stilltocheck; //the sd start is delayed, because otherwise the serial cannot answer fast enought to make contact with the hostsoftware.

  LsAction lsAction; //stored for recursion.
  uint16_t nrFiles; //counter for the files in the current directory and recycled as position counter for getting the nrFiles'th name in the directory.
  char* diveDirName;
  void lsDive(const char *prepend, SdFile parent, const char * const match=NULL);

  #ifdef SDCARD_SORT_ALPHA
    void flush_presort();
  #endif
};

extern CardReader card;

#define IS_SD_PRINTING (card.sdprinting)

#if (SDCARDDETECT > -1)
  #ifdef SDCARDDETECTINVERTED
    #define IS_SD_INSERTED (READ(SDCARDDETECT) != 0)
  #else
    #define IS_SD_INSERTED (READ(SDCARDDETECT) == 0)
  #endif
#else
  //No card detect line? Assume the card is inserted.
  #define IS_SD_INSERTED true
#endif

#else

#define IS_SD_PRINTING (false)

#endif //SDSUPPORT

#endif //__CARDREADER_H
