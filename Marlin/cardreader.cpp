#include "Marlin.h"
#include "cardreader.h"
#include "ultralcd.h"
#include "stepper.h"
#include "temperature.h"
#include "language.h"

#ifdef SDSUPPORT

#define LONGEST_FILENAME (longFilename[0] ? longFilename : filename)

CardReader::CardReader() {
  #ifdef SDCARD_SORT_ALPHA
    sort_count = 0;
    #if SORT_ONOFF
      sort_alpha = true;
      sort_folders = FOLDER_SORTING;
      //sort_reverse = false;
    #endif
  #endif
  filesize = 0;
  sdpos = 0;
  sdprinting = false;
  cardOK = false;
  saving = false;
  logging = false;
  workDirDepth = 0;
  file_subcall_ctr = 0;
  memset(workDirParents, 0, sizeof(workDirParents));

  autostart_stilltocheck = true; //the SD start is delayed, because otherwise the serial cannot answer fast enough to make contact with the host software.
  autostart_index = 0;

  //power to SD reader
  #if SDPOWER > -1
    OUT_WRITE(SDPOWER, HIGH);
  #endif //SDPOWER

  next_autostart_ms = millis() + 5000;
}

char *createFilename(char *buffer, const dir_t &p) { //buffer > 12characters
  char *pos = buffer;
  for (uint8_t i = 0; i < 11; i++) {
    if (p.name[i] == ' ') continue;
    if (i == 8) *pos++ = '.';
    *pos++ = p.name[i];
  }
  *pos++ = 0;
  return buffer;
}

void CardReader::lsDive(const char *prepend, SdFile parent, const char * const match/*=NULL*/) {
  dir_t p;
  uint8_t cnt = 0;

  while (parent.readDir(p, longFilename) > 0) {
    if (DIR_IS_SUBDIR(&p) && lsAction != LS_Count && lsAction != LS_GetFilename) { // hence LS_SerialPrint
      char path[FILENAME_LENGTH*2];
      char lfilename[FILENAME_LENGTH];
      createFilename(lfilename, p);

      path[0] = 0;
      if (prepend[0] == 0) strcat(path, "/"); //avoid leading / if already in prepend
      strcat(path, prepend);
      strcat(path, lfilename);
      strcat(path, "/");

      //Serial.print(path);

      SdFile dir;
      if (!dir.open(parent, lfilename, O_READ)) {
        if (lsAction == LS_SerialPrint) {
          SERIAL_ECHO_START;
          SERIAL_ECHOLN(MSG_SD_CANT_OPEN_SUBDIR);
          SERIAL_ECHOLN(lfilename);
        }
      }
      lsDive(path, dir);
      //close done automatically by destructor of SdFile
    }
    else {
      char pn0 = p.name[0];
      if (pn0 == DIR_NAME_FREE) break;
      if (pn0 == DIR_NAME_DELETED || pn0 == '.') continue;
      char lf0 = longFilename[0];
      if (lf0 == '.') continue;

      if (!DIR_IS_FILE_OR_SUBDIR(&p)) continue;

      filenameIsDir = DIR_IS_SUBDIR(&p);

      if (!filenameIsDir && (p.name[8] != 'G' || p.name[9] == '~')) continue;

      //if (cnt++ != nr) continue;
      createFilename(filename, p);
      if (lsAction == LS_SerialPrint) {
        SERIAL_PROTOCOL(prepend);
        SERIAL_PROTOCOLLN(filename);
      }
      else if (lsAction == LS_Count) {
        nrFiles++;
      }
      else if (lsAction == LS_GetFilename) {
        if (match != NULL) {
          if (strcasecmp(match, filename) == 0) return;
        }
        else if (cnt == nrFiles) return;
        cnt++;
      }
    }
  }
}

void CardReader::ls()  {
  lsAction = LS_SerialPrint;
  root.rewind();
  lsDive("", root);
}

void CardReader::initsd() {
  cardOK = false;
  if (root.isOpen()) root.close();

  #ifdef SDSLOW
    #define SPI_SPEED SPI_HALF_SPEED
  #else
    #define SPI_SPEED SPI_FULL_SPEED
  #endif

  if (!card.init(SPI_SPEED,SDSS)
    #if defined(LCD_SDSS) && (LCD_SDSS != SDSS)
      && !card.init(SPI_SPEED, LCD_SDSS)
    #endif
  ) {
    //if (!card.init(SPI_HALF_SPEED,SDSS))
    SERIAL_ECHO_START;
    SERIAL_ECHOLNPGM(MSG_SD_INIT_FAIL);
  }
  else if (!volume.init(&card)) {
    SERIAL_ERROR_START;
    SERIAL_ERRORLNPGM(MSG_SD_VOL_INIT_FAIL);
  }
  else if (!root.openRoot(&volume)) {
    SERIAL_ERROR_START;
    SERIAL_ERRORLNPGM(MSG_SD_OPENROOT_FAIL);
  }
  else {
    cardOK = true;
    SERIAL_ECHO_START;
    SERIAL_ECHOLNPGM(MSG_SD_CARD_OK);
  }
  workDir = root;
  curDir = &root;
  #ifdef SDCARD_SORT_ALPHA
    #if SORT_ONOFF
      if (sort_alpha) presort();
    #else
      presort();
    #endif
  #endif
  /*
  if (!workDir.openRoot(&volume)) {
    SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
  }
  */
}

void CardReader::setroot() {
  /*if (!workDir.openRoot(&volume)) {
    SERIAL_ECHOLNPGM(MSG_SD_WORKDIR_FAIL);
  }*/
  workDir = root;
  curDir = &workDir;
  #ifdef SDCARD_SORT_ALPHA
    #if SORT_ONOFF
      if (sort_alpha) presort();
    #else
      presort();
    #endif
  #endif
}

void CardReader::release() {
  sdprinting = false;
  cardOK = false;
}

void CardReader::startFileprint() {
  if (cardOK) {
    sdprinting = true;
    #ifdef SDCARD_SORT_ALPHA
      flush_presort();
    #endif
  }
}

void CardReader::pauseSDPrint() {
  if (sdprinting) sdprinting = false;
}

void CardReader::openLogFile(char* name) {
  logging = true;
  openFile(name, false);
}

void CardReader::getAbsFilename(char *t) {
  uint8_t cnt = 0;
  *t = '/'; t++; cnt++;
  for (uint8_t i = 0; i < workDirDepth; i++) {
    workDirParents[i].getFilename(t); //SDBaseFile.getfilename!
    while(*t && cnt < MAXPATHNAMELENGTH) { t++; cnt++; } //crawl counter forward.
  }
  if (cnt < MAXPATHNAMELENGTH - FILENAME_LENGTH)
    file.getFilename(t);
  else
    t[0] = 0;
}

void CardReader::openFile(char* name, bool read, bool replace_current/*=true*/) {
  if (!cardOK) return;
  if (file.isOpen()) { //replacing current file by new file, or subfile call
    if (!replace_current) {
     if (file_subcall_ctr > SD_PROCEDURE_DEPTH - 1) {
       SERIAL_ERROR_START;
       SERIAL_ERRORPGM("trying to call sub-gcode files with too many levels. MAX level is:");
       SERIAL_ERRORLN(SD_PROCEDURE_DEPTH);
       kill();
       return;
     }

     SERIAL_ECHO_START;
     SERIAL_ECHOPGM("SUBROUTINE CALL target:\"");
     SERIAL_ECHO(name);
     SERIAL_ECHOPGM("\" parent:\"");

     //store current filename and position
     getAbsFilename(filenames[file_subcall_ctr]);

     SERIAL_ECHO(filenames[file_subcall_ctr]);
     SERIAL_ECHOPGM("\" pos");
     SERIAL_ECHOLN(sdpos);
     filespos[file_subcall_ctr] = sdpos;
     file_subcall_ctr++;
    }
    else {
     SERIAL_ECHO_START;
     SERIAL_ECHOPGM("Now doing file: ");
     SERIAL_ECHOLN(name);
    }
    file.close();
  }
  else { //opening fresh file
    file_subcall_ctr = 0; //resetting procedure depth in case user cancels print while in procedure
    SERIAL_ECHO_START;
    SERIAL_ECHOPGM("Now fresh file: ");
    SERIAL_ECHOLN(name);
  }
  sdprinting = false;

  SdFile myDir;
  curDir = &root;
  char *fname = name;

  char *dirname_start, *dirname_end;
  if (name[0] == '/') {
    dirname_start = &name[1];
    while(dirname_start > 0) {
      dirname_end = strchr(dirname_start, '/');
      //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start - name));
      //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end - name));
      if (dirname_end > 0 && dirname_end > dirname_start) {
        char subdirname[FILENAME_LENGTH];
        strncpy(subdirname, dirname_start, dirname_end - dirname_start);
        subdirname[dirname_end - dirname_start] = 0;
        SERIAL_ECHOLN(subdirname);
        if (!myDir.open(curDir, subdirname, O_READ)) {
          SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
          SERIAL_PROTOCOL(subdirname);
          SERIAL_PROTOCOLCHAR('.');
          return;
        }
        else {
          //SERIAL_ECHOLN("dive ok");
        }

        curDir = &myDir;
        dirname_start = dirname_end + 1;
      }
      else { // the remainder after all /fsa/fdsa/ is the filename
        fname = dirname_start;
        //SERIAL_ECHOLN("remainder");
        //SERIAL_ECHOLN(fname);
        break;
      }
    }
  }
  else { //relative path
    curDir = &workDir;
  }

  if (read) {
    if (file.open(curDir, fname, O_READ)) {
      filesize = file.fileSize();
      SERIAL_PROTOCOLPGM(MSG_SD_FILE_OPENED);
      SERIAL_PROTOCOL(fname);
      SERIAL_PROTOCOLPGM(MSG_SD_SIZE);
      SERIAL_PROTOCOLLN(filesize);
      sdpos = 0;

      SERIAL_PROTOCOLLNPGM(MSG_SD_FILE_SELECTED);
      getfilename(0, fname);
      lcd_setstatus(longFilename[0] ? longFilename : fname);
    }
    else {
      SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
      SERIAL_PROTOCOL(fname);
      SERIAL_PROTOCOLCHAR('.');
    }
  }
  else { //write
    if (!file.open(curDir, fname, O_CREAT | O_APPEND | O_WRITE | O_TRUNC)) {
      SERIAL_PROTOCOLPGM(MSG_SD_OPEN_FILE_FAIL);
      SERIAL_PROTOCOL(fname);
      SERIAL_PROTOCOLCHAR('.');
    }
    else {
      saving = true;
      SERIAL_PROTOCOLPGM(MSG_SD_WRITE_TO_FILE);
      SERIAL_PROTOCOLLN(name);
      lcd_setstatus(fname);
    }
  }
}

void CardReader::removeFile(char* name) {
  if (!cardOK) return;

  file.close();
  sdprinting = false;

  SdFile myDir;
  curDir = &root;
  char *fname = name;

  char *dirname_start, *dirname_end;
  if (name[0] == '/') {
    dirname_start = strchr(name, '/') + 1;
    while (dirname_start > 0) {
      dirname_end = strchr(dirname_start, '/');
      //SERIAL_ECHO("start:");SERIAL_ECHOLN((int)(dirname_start - name));
      //SERIAL_ECHO("end  :");SERIAL_ECHOLN((int)(dirname_end - name));
      if (dirname_end > 0 && dirname_end > dirname_start) {
        char subdirname[FILENAME_LENGTH];
        strncpy(subdirname, dirname_start, dirname_end - dirname_start);
        subdirname[dirname_end - dirname_start] = 0;
        SERIAL_ECHOLN(subdirname);
        if (!myDir.open(curDir, subdirname, O_READ)) {
          SERIAL_PROTOCOLPGM("open failed, File: ");
          SERIAL_PROTOCOL(subdirname);
          SERIAL_PROTOCOLCHAR('.');
          return;
        }
        else {
          //SERIAL_ECHOLN("dive ok");
        }

        curDir = &myDir;
        dirname_start = dirname_end + 1;
      }
      else { // the remainder after all /fsa/fdsa/ is the filename
        fname = dirname_start;
        //SERIAL_ECHOLN("remainder");
        //SERIAL_ECHOLN(fname);
        break;
      }
    }
  }
  else { // relative path
    curDir = &workDir;
  }

  if (file.remove(curDir, fname)) {
    SERIAL_PROTOCOLPGM("File deleted:");
    SERIAL_PROTOCOLLN(fname);
    sdpos = 0;
    #ifdef SDCARD_SORT_ALPHA
      #if SORT_ONOFF
        if (sort_alpha) presort();
      #else
        presort();
      #endif
    #endif
  }
  else {
    SERIAL_PROTOCOLPGM("Deletion failed, File: ");
    SERIAL_PROTOCOL(fname);
    SERIAL_PROTOCOLCHAR('.');
  }
}

void CardReader::getStatus() {
  if (cardOK) {
    SERIAL_PROTOCOLPGM(MSG_SD_PRINTING_BYTE);
    SERIAL_PROTOCOL(sdpos);
    SERIAL_PROTOCOLCHAR('/');
    SERIAL_PROTOCOLLN(filesize);
  }
  else {
    SERIAL_PROTOCOLLNPGM(MSG_SD_NOT_PRINTING);
  }
}

void CardReader::write_command(char *buf) {
  char* begin = buf;
  char* npos = 0;
  char* end = buf + strlen(buf) - 1;

  file.writeError = false;
  if ((npos = strchr(buf, 'N')) != NULL) {
    begin = strchr(npos, ' ') + 1;
    end = strchr(npos, '*') - 1;
  }
  end[1] = '\r';
  end[2] = '\n';
  end[3] = '\0';
  file.write(begin);
  if (file.writeError) {
    SERIAL_ERROR_START;
    SERIAL_ERRORLNPGM(MSG_SD_ERR_WRITE_TO_FILE);
  }
}

void CardReader::checkautostart(bool force) {
  if (!force && (!autostart_stilltocheck || next_autostart_ms < millis()))
    return;

  autostart_stilltocheck = false;

  if (!cardOK) {
    initsd();
    if (!cardOK) return; // fail
  }

  char autoname[30];
  sprintf_P(autoname, PSTR("auto%i.g"), autostart_index);
  for (int8_t i = 0; i < (int8_t)strlen(autoname); i++) autoname[i] = tolower(autoname[i]);

  dir_t p;

  root.rewind();

  bool found = false;
  while (root.readDir(p, NULL) > 0) {
    for (int8_t i = 0; i < (int8_t)strlen((char*)p.name); i++) p.name[i] = tolower(p.name[i]);
    if (p.name[9] != '~' && strncmp((char*)p.name, autoname, 5) == 0) {
      char cmd[30];
      sprintf_P(cmd, PSTR("M23 %s"), autoname);
      enqueuecommand(cmd);
      enqueuecommands_P(PSTR("M24"));
      found = true;
    }
  }
  if (!found)
    autostart_index = -1;
  else
    autostart_index++;
}

void CardReader::closefile(bool store_location) {
  file.sync();
  file.close();
  saving = logging = false;

  if (store_location) {
    //future: store printer state, filename and position for continuing a stopped print
    // so one can unplug the printer and continue printing the next day.
  }
}

/**
 * Get the name of a file in the current directory by index
 */
void CardReader::getfilename(uint16_t nr, const char * const match/*=NULL*/) {
  #if defined(SDCARD_SORT_ALPHA) && SORT_USES_RAM && SORT_USES_MORE_RAM
    if (match != NULL) {
      while (nr < sort_count) {
        if (strcasecmp(match, sortshort[nr]) == 0) break;
        nr++;
      }
    }
    if (nr < sort_count) {
      strcpy(filename, sortshort[nr]);
      strcpy(longFilename, sortnames[nr]);
      filenameIsDir = (isDir[nr>>3] & BIT(nr & 0x07)) != 0;
      return;
    }
  #endif
  curDir = &workDir;
  lsAction = LS_GetFilename;
  nrFiles = nr;
  curDir->rewind();
  lsDive("", *curDir, match);
}

uint16_t CardReader::getnrfilenames() {
  curDir = &workDir;
  lsAction = LS_Count;
  nrFiles = 0;
  curDir->rewind();
  lsDive("", *curDir);
  //SERIAL_ECHOLN(nrFiles);
  return nrFiles;
}

void CardReader::chdir(const char * relpath) {
  SdFile newfile;
  SdFile *parent = &root;

  if (workDir.isOpen()) parent = &workDir;

  if (!newfile.open(*parent, relpath, O_READ)) {
    SERIAL_ECHO_START;
    SERIAL_ECHOPGM(MSG_SD_CANT_ENTER_SUBDIR);
    SERIAL_ECHOLN(relpath);
  }
  else {
    if (workDirDepth < MAX_DIR_DEPTH) {
      ++workDirDepth;
      for (int d = workDirDepth; d--;) workDirParents[d + 1] = workDirParents[d];
      workDirParents[0] = *parent;
    }
    workDir = newfile;
    #ifdef SDCARD_SORT_ALPHA
      #if SORT_ONOFF
        if (sort_alpha) presort();
      #else
        presort();
      #endif
    #endif
  }
}

void CardReader::updir() {
  if (workDirDepth > 0) {
    --workDirDepth;
    workDir = workDirParents[0];
    for (uint16_t d = 0; d < workDirDepth; d++)
      workDirParents[d] = workDirParents[d+1];
    #ifdef SDCARD_SORT_ALPHA
      #if SORT_ONOFF
        if (sort_alpha) presort();
      #else
        presort();
      #endif
    #endif
  }
}

#ifdef SDCARD_SORT_ALPHA

/**
 * Get the name of a file in the current directory by sort-index
 */
void CardReader::getfilename_sorted(const uint16_t nr) {
  #if SORT_ONOFF
    getfilename(sort_alpha && nr < sort_count ? sort_order[nr] : nr);
  #else
    getfilename(nr < sort_count ? sort_order[nr] : nr);
  #endif
}

/**
 * Read all the files and produce a sort key
 *
 * We can do this in 3 ways...
 *  - Minimal RAM: Read two filenames at a time sorting along...
 *  - Some RAM: Buffer the directory just for this sort
 *  - Most RAM: Buffer the directory and return filenames from RAM
 */
void CardReader::presort()
{
  flush_presort();

  uint16_t fileCnt = getnrfilenames();
  if (fileCnt > 0) {

    if (fileCnt > SORT_LIMIT) fileCnt = SORT_LIMIT;

    #if SORT_USES_RAM
      #if SORT_USES_MORE_RAM
        sortshort = new char*[fileCnt]; // keep in the heap
        sortnames = new char*[fileCnt]; // keep in the heap
        #if FOLDER_SORTING || SORT_ONOFF
          // More RAM option pre-allocates
          #if SORT_USES_MALLOC
            isDir = (uint8_t*)malloc((fileCnt+7)>>3); // in the heap
          #endif
        #endif
      #else
        char *sortnames[fileCnt]; // on the stack
        #if FOLDER_SORTING || SORT_ONOFF
          uint8_t isDir[(fileCnt+7)>>3]; // on the stack
        #endif
      #endif

      sort_order = new uint8_t[fileCnt];

    #else
      char name1[LONG_FILENAME_LENGTH+1]; // on the stack
    #endif

    if (fileCnt > 1) {

      // Init sort order. If using RAM then read all filenames now.
      for (uint16_t i = 0; i < fileCnt; i++) {
        sort_order[i] = i;
        #if SORT_USES_RAM
          getfilename(i);
          sortnames[i] = strdup(LONGEST_FILENAME); // malloc
          #if SORT_USES_MORE_RAM
            sortshort[i] = strdup(filename); // malloc
          #endif
          // char out[30];
          // sprintf_P(out, PSTR("---- %i %s %s"), i, filenameIsDir ? "D" : " ", sortnames[i]);
          // SERIAL_ECHOLN(out);
          #if FOLDER_SORTING || SORT_ONOFF
            uint16_t mod = i & 0x07, ind = i >> 3;
            if (mod == 0) isDir[ind] = 0x00;
            if (filenameIsDir) isDir[ind] |= BIT(mod);
          #endif
        #endif
      }

      // Bubble Sort
      #if SORT_ONOFF
        if (sort_alpha)
      #endif
      for (uint16_t i = fileCnt; --i;) {
        bool cmp, didSwap = false;
        for (uint16_t j = 0; j < i; ++j) {
          uint16_t s1 = j, s2 = j + 1, o1 = sort_order[s1], o2 = sort_order[s2];
          uint8_t ind1 = o1 >> 3, mod1 = o1 & 0x07, mask1 = 1 << mod1,
                  ind2 = o2 >> 3, mod2 = o2 & 0x07, mask2 = 1 << mod2;
          #if SORT_USES_RAM
            #if FOLDER_SORTING || SORT_ONOFF
              #if SORT_ONOFF
                if (sort_folders != 0) {
                  cmp = ((isDir[ind1] & mask1) != 0) == ((isDir[ind2] & mask2) != 0)
                    ? strcasecmp(sortnames[o1], sortnames[o2]) > 0
                    : (isDir[sort_folders > 0 ? ind1 : ind2] & (sort_folders > 0 ? mask1 : mask2)) != 0;
                }
                else {
                  cmp = strcasecmp(sortnames[o1], sortnames[o2]) > 0;
                }
              #else
                cmp = ((isDir[ind1] & mask1) != 0) == ((isDir[ind2] & mask2) != 0)
                  ? strcasecmp(sortnames[o1], sortnames[o2]) > 0
                  : (isDir[FOLDER_SORTING > 0 ? ind1 : ind2] & (FOLDER_SORTING > 0 ? mask1 : mask2)) != 0;
              #endif
            #else
              cmp = strcasecmp(sortnames[o1], sortnames[o2]) > 0;
            #endif
          #else // !SORT_USES_RAM
            getfilename(o1);
            strcpy(name1, LONGEST_FILENAME);
            #if FOLDER_SORTING || SORT_ONOFF
              bool dir1 = filenameIsDir;
            #endif
            getfilename(o2);
            char *name2 = LONGEST_FILENAME;
            #if FOLDER_SORTING || SORT_ONOFF
              #if SORT_ONOFF
                if (sort_folders != 0) {
                  cmp = (dir1 == filenameIsDir) ? (strcasecmp(name1, name2) > 0) : (sort_folders > 0 ? dir1 : !dir1);
                }
                else {
                  cmp = strcasecmp(name1, name2) > 0;
                }
              #else
                cmp = (dir1 == filenameIsDir) ? (strcasecmp(name1, name2) > 0) : (FOLDER_SORTING > 0 ? dir1 : !dir1);
              #endif
            #else
              cmp = strcasecmp(name1, name2) > 0;
            #endif
          #endif // !SORT_USES_RAM
          if (cmp) {
            sort_order[s1] = o2;
            sort_order[s2] = o1;
            didSwap = true;
          }
        }
        if (!didSwap) break;
      }
      // Using RAM but not keeping names around
      #if SORT_USES_RAM && !SORT_USES_MORE_RAM
        #if SORT_USES_MALLOC && (FOLDER_SORTING || SORT_ONOFF)
          free(isDir);
        #endif
        for (uint16_t i = 0; i < fileCnt; ++i) free(sortnames[i]);
      #endif
    }
    else {
      sort_order[0] = 0;
      #if SORT_USES_RAM && SORT_USES_MORE_RAM
        getfilename(0);
        sortnames = new char*[1];
        sortnames[0] = strdup(LONGEST_FILENAME); // malloc
        sortshort = new char*[1];
        sortshort[0] = strdup(filename); // malloc
        #if SORT_USES_MALLOC
          isDir = (uint8_t*)malloc(1);
        #endif
        isDir[0] = filenameIsDir ? 0x01 : 0x00;
      #endif
    }

    sort_count = fileCnt;
  }
}

void CardReader::flush_presort() {
  if (sort_count > 0) {
    #if SORT_USES_RAM && SORT_USES_MORE_RAM
      for (uint8_t i = 0; i < sort_count; ++i) {
        free(sortshort[i]); // strdup
        free(sortnames[i]); // strdup
      }
      delete sortshort;
      delete sortnames;
    #endif
    delete sort_order;
    sort_count = 0;
  }
}

#endif // SDCARD_SORT_ALPHA

void CardReader::printingHasFinished() {
  st_synchronize();
  if (file_subcall_ctr > 0) { // Heading up to a parent file that called current as a procedure.
    file.close();
    file_subcall_ctr--;
    openFile(filenames[file_subcall_ctr], true, true);
    setIndex(filespos[file_subcall_ctr]);
    startFileprint();
  }
  else {
    file.close();
    sdprinting = false;
    if (SD_FINISHED_STEPPERRELEASE) {
      //finishAndDisableSteppers();
      enqueuecommands_P(PSTR(SD_FINISHED_RELEASECOMMAND));
    }
    autotempShutdown();
    #ifdef SDCARD_SORT_ALPHA
      presort();
    #endif
  }
}

#endif //SDSUPPORT
