/*** Include ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOUCE

#include <ctype.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <time.h>
#include <termios.h>

/*** Define ***/
#define KILO_VERSION "0.0.1" //version 0.0.1 baby!!!
#define KILO_TAB_STOP 8
#define KILO_QUIT_TIMES 3

#define CTRL_KEY(k) ((k) & 0x1f)
#define POS_BYTE 3
#define CLEARSCREENBYTE 4

enum editorKey{
  BACKSPACE = 127,
  ARROW_LEFT = 999,
  ARROW_RIGHT,
  ARROW_UP,
  ARROW_DOWN,
  DEL_KEY,
  HOME_KEY,
  END_KEY,
  PAGE_UP,
  PAGE_DOWN
};


/*** Global Variables ***/
const char *clearScreenVar = "\x1b[2J";
const char *positionVar = "\x1b[H";

/*** Char sequences ***/

void escapeSequences(const char *s, int bytes){ //A short cut function that lets me enter char literals like claer screen or position without actually writing them.
  write(STDOUT_FILENO, s, bytes);
}


/*** Data ***/
typedef struct erow{
  int size; //size of the buffer
  int rsize; //size of the text with special characters like tab
  char *chars; //actual data of the chars no special chars
  char *render; //actual data but when it has \t it adds two spaces so it's bigger than render if there is special chars.
}erow;

struct editorConfig{ //CONFIGURATION IN TERMINAL SUCH AS ROWS COLS LINE VIEWER
  int cx, cy; //cursor positions
  int rx;
  int rowoff; //row offset meaning the current screen ex top of the screen would be row 5
  int coloff; //column offset for the visible screen in where ever you are in the file.
  int screenrows; //screen row size
  int screencols;//screen col size
  int numrows; //how many rows of text is in the file.
  erow *row; //editor row that helps use enter a text that is a dynamic string.
  int dirty;
  char *filename; //file name (pretty obvious)
  char statusmsg[80]; //status message for our message in the bar.
  time_t statusmsg_time; //how much time it appears for.
  struct termios orig_termios;
};
struct editorConfig E;

/*** prototypes***/

void editorSetStatusMessage(const char *fmt, ...);

/*** Terminal ***/
void die(const char *s){
  escapeSequences(clearScreenVar, CLEARSCREENBYTE);
  escapeSequences(positionVar, POS_BYTE);
  
  perror(s);
  exit(1);
}

void disableRawMode(){ //RawMode disabler lets us retain previous terminal config so the user can retain their settings.
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
    die("tcsetattr");
}

void enableRawMode(){ //raw mode lets us not echo our text and let us freely type without being in cononical mode.


  if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)  die("tcgetattr");
  atexit(disableRawMode);
  
  struct termios raw = E.orig_termios;

  tcgetattr(STDIN_FILENO, &raw);

  
  raw.c_iflag &= ~(BRKINT|ICRNL|ICRNL|ISTRIP|IXON); /*Disable terminal translations of CTRL-M turning 13 '\r' to 10 '\n' IXON: disables CTRL-S (SUSPEND) and CTRL-Q(RESUME FROM SUSPENSION)*/
  raw.c_oflag &= ~(OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag &= ~(ECHO|ICANON|IEXTEN|ISIG); /* Disable ECHO, Canonical mode(cooked mode), IXTEN(CTRL-V) disable 3 byte write up with ctrl-c. ISIG(Input Signal) disabling ctrl-c*/
  raw.c_cc[VMIN] = 0;
  raw.c_cc[VTIME] = 1;
  
  if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
    die("tcsetattr");
  
}

int editorReadKey(){
  int nread;
  char c;
  while((nread = read(STDIN_FILENO, &c, 1)) != 1){ //loop until we got an input or fail.
    if(nread == -1 && errno != EAGAIN)  //if nread reports a -1  AND errno is not equal to EAGAIN
      die("read");
  }
  if(c == '\x1b'){
    char seq[3];

    if(read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if(read(STDIN_FILENO, &seq[1], 1) !=1) return '\x1b';


    if(seq[0] == '['){
      if(seq[1] >= '0' && seq[1] <= '9'){
        if(read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
        if(seq[2] == '~'){
          switch(seq[1]){
          case '1': return HOME_KEY;
          case '3': return DEL_KEY;
          case '4': return END_KEY;
          case '5': return PAGE_UP;
          case '6': return PAGE_DOWN;
          case '7': return HOME_KEY;
          case '8':return END_KEY;
          }
        }
      }
      else{
        switch(seq[1]){
        case 'A': //arrow up key which is a 3 byte char ex ESC [ A
          return ARROW_UP;
        case 'B':
          return ARROW_DOWN;
        case 'C':
          return ARROW_RIGHT;
        case 'D':
          return ARROW_LEFT;
        case 'H':
          return HOME_KEY;
        case 'F':
          return END_KEY;
        }
      }
    }else if(seq[0] == 'O'){
      switch(seq[1]){
      case 'H':
          return HOME_KEY;
      case 'F':
          return END_KEY;
    }
  }
    return '\x1b';
  }
  else{
    return c;
  }
}

int getCursorPosition(int *rows, int *cols) {
  char buf[32];
  unsigned int i = 0;
  
  if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) return -1;  
  while (i < sizeof(buf) - 1){
    if(read(STDIN_FILENO, &buf[i], 1) != 1)return -1;
    if(buf[i] == 'R') break;
    i++;
  }
  buf[i] = '\0';
  if(buf[0] != '\x1b' || buf[1] != '[') return -1;
  if(sscanf(&buf[2], "%d;%d", rows, cols) != 2) return -1;

  return 0;
}

int getWindowSize(int *rows, int *cols){
  struct winsize ws;
  
  if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
    if(write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) return -1;
    getCursorPosition(rows, cols);
    return -1;
  }
  else{
    *cols = ws.ws_col;
    *rows = ws.ws_row;
    return 0;
  }
}
/*** Row Operations***/

int editorRowCxToRx(erow *row, int cx){
  int rx = 0;
  int j;
  for(j = 0; j < cx; j++){
    if(row->chars[j] == '\t'){
      rx += (KILO_TAB_STOP - 1) - (rx % KILO_TAB_STOP); // so every line has a tab stop. Meaning every 8 multiple column subtracts from the column we are from the next tab stop if there is a '\t', and then adding with rx to get to that stop
    }
    rx++; //increment with the current + tab stops;
  }
  return rx;
}

void editorUpdateRow(erow *row){ //Loop through text and check special characters like tabs, back space, and so on.

  int tabs = 0; //tab counter initialized
  int j;

  for(j = 0; j < row->size; j++)
    if(row->chars[j] == '\t') tabs++;

  free(row->render);
  row->render = malloc(row->size + tabs*(KILO_TAB_STOP - 1) + 1);

  int idx = 0;

  for(j = 0; j < row->size; j++){ //lope through the current text size
    if(row->chars[j] == '\t'){ //while looping check if a text has a tab
      row->render[idx++] = ' '; //if there is a tab add a space.
      while(idx % KILO_TAB_STOP != 0) row->render[idx++] = ' ';
    }else{
      row->render[idx++] = row->chars[j];
    }
  }
  row->render[idx] = '\0';
  row->rsize = idx;
}

void editorAppendRow(char *s, size_t len){
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1)); //resize the whole file text data

  int at = E.numrows; //at equals to the end of the file.
  E.row[at].size = len; //makes the size = to the len in the parameter.
  E.row[at].chars = malloc(len + 1); //create a new text buffer here.
  memcpy(E.row[at].chars, s, len); //copy what ever was sent as an argument into the text buffer we currently created.
  E.row[at].chars[len] = '\0'; //add \0 at the end of the text buffer.

  E.row[at].rsize = 0; //rsize 0 currently, later on about to record special chars like \t
  E.row[at].render = NULL;// initialize to null (NULL pointer)
  editorUpdateRow(&E.row[at]); //send it to the row updater so special chars like tabs can be accounted for.

  E.numrows++; //increment the number of rows;
  E.dirty++;
}

void editorRowInsertChar(erow *row, int at, int c){// expand space for our new char
  if(at < 0 || at > row->size) at = row->size;//check if current pos is higher or less than size of the buffer
  row->chars = realloc(row->chars, row->size + 2); //expand new size by pointing to a buffer created or resized..
  memmove(&row->chars[at+1], &row->chars[at],row->size-at+1); //move memory src to destination to insert char in.
  row->size++; //increment size.
  row->chars[at] = c; //current pos characer is changed to be the c character.
  editorUpdateRow(row);
  E.dirty++;
}

/*** editor operations ***/

void editorInsertChar(int c){
  if(E.cy == E.numrows){
    editorAppendRow("", 0);
  }
  editorRowInsertChar(&E.row[E.cy], E.cx, c);
  E.cx++;
}

/*** File I/O***/

char *editorRowsToString(int *buflen){
  int totlen = 0;
  int j;
  for(j = 0; j < E.numrows; j++){
    totlen += E.row[j].size +1;
  }
  *buflen = totlen;

  char *buf = malloc(totlen);
  char *p = buf;
  for(j=0; j < E.numrows; j++){
    memcpy(p, E.row[j].chars, E.row[j].size); //copy memory data from the char in row struct to p which points to a memory allocated data.
    p += E.row[j].size;//add row size with the object address contained to shift starting position address. for ex char data[5] = "hello"; char *p = data how p points to the starting address h. doing p + 3 makes us now start at l
    *p = '\n'; //add new line here.
    p++; //increment after new line for next line buffer.
  }

  return buf;
}

void editorOpen(char *filename){
  free(E.filename);
  E.filename = strdup(filename);

  FILE *fp = fopen(filename, "r");
  if(!fp) die("fopen");


  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;//line length = the number of characters read during a line
  while((linelen = getline(&line, &linecap, fp)) != -1){
    while(linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r')) //remove special characters like \r and \n from the line
      linelen--; // one line less if there is \r or \n
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
  E.dirty = 0;
}

void editorSave(){
  if(E.filename == NULL) return;

  int len;
  char *buf = editorRowsToString(&len);
  int fd = open(E.filename, O_RDWR | O_CREAT, 0644);
  if(fd != -1){
    if(ftruncate(fd, len) != 1){
      if(write(fd, buf, len) == len){ // if the bytes relayed are exact then success!
        close(fd);
        free(buf);
        editorSetStatusMessage("%d bytes written to disk", len);
        E.dirty = 0;
        return;
      }
    }
    free(buf); //free if unsuccessful save
    editorSetStatusMessage("Can't save I/O error: %s", strerror(errno));
  }
}
/*** Append buffer ***/
struct abuf {
  char *b;
  int len;
};

#define ABUF_INIT {NULL,0}
  
void abAppend(struct abuf *ab, const char *s, int len){
  char *new = realloc(ab->b, ab->len + len);

  if(new == NULL) return;
  memcpy(&new[ab->len], s, len);
  ab->b = new;
  ab->len +=len;
}
void abFree(struct abuf *ab){
  free(ab->b);
}
  
/*** output ***/

void editorScroll(){
  E.rx = E.cx;

  if(E.cy < E.numrows){
    E.rx = editorRowCxToRx(&E.row[E.cy], E.cx);
  }

  if(E.cy < E.rowoff){
    E.rowoff = E.cy;
  }
  if(E.cy >= E.rowoff + E.screenrows){
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if(E.rx < E.coloff){
    E.coloff = E.rx;
  }
  if(E.rx >= E.coloff + E.screencols){
    E.coloff = E.rx - E.screencols + 1;
  }
}


void editorDrawRows(struct abuf *ab){ //helps us render the full screen one line at a time until the end of screen row display.
  int y;
  for (y = 0; y < E.screenrows; y++){
    int filerow = y + E.rowoff;
    if(filerow >= E.numrows){
      if(E.numrows == 0 && y == E.screenrows / 3){
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor --version %s", KILO_VERSION);
        if(welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen)/2;
        if(padding){ //add some padding for the welcome text
          abAppend(ab, "~", 1);
          padding--; //subtract padding by 1
        }
        while(padding--) abAppend(ab, " ", 1); //while padding add space so it pads the text
        abAppend(ab, welcome, welcomelen);
      }
      else{
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].rsize - E.coloff; // subtract the screens columns with the size of the current text buffer. This will tell us how much text we should write in the current screen.
      if(len < 0) len = 0; // we can't let len be less than 0, so we have to make it 0 if so.
      if (len > E.screencols) len = E.screencols; // if len is greater than the screen columns then make length equal to the screen column
      abAppend(ab, &E.row[filerow].render[E.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3);
    abAppend(ab, "\r\n", 2); //return carrier meaning that it should go to the next line at the start for each redraw in the current screen NOT FILE LOCATION.
  }
}

void editorDrawStatusBar(struct abuf *ab){ //the last line in the visible screen that documents info such as how many line in a file and the file name currently.
  abAppend(ab, "\x1b[7m", 4);
  char status[80], rstatus[80];
  char percent[80];

  int percentformula = 100*(E.cy + 1) / E.numrows;
  if(percentformula > 100) percentformula = 100;
  int percentlen = snprintf(percent, sizeof(percent), "%d%%", percentformula);

  int len = snprintf(status, sizeof(status), "%.20s - %d lines %s", E.filename ? E.filename : "[No Name]", E.numrows, E.dirty ? "(modified)" : "");
  int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy+1, E.numrows);
  if(len > E.screencols)len = E.screencols;
  abAppend(ab, status, len);
  while (len < E.screencols){
    if(len == E.screencols/2){
      abAppend(ab, percent, percentlen);
    }
    if(E.screencols - len - percentlen == rlen){
      abAppend(ab, rstatus, rlen);
      break;
      }else{
    abAppend(ab, " ", 1);
    len++; //temporarily add white spaces for our anomaly of a line.
      }
    }
  abAppend(ab, "\x1b[m", 3);
  abAppend(ab, "\r\n", 2);
}

void editorDrawMessageBar(struct abuf *ab){
  abAppend(ab, "\x1b[K", 3);
  int msglen = strlen(E.statusmsg);
  if(msglen > E.screencols) msglen = E.screencols;
  if(msglen && time(NULL) - E.statusmsg_time < 5)
    abAppend(ab, E.statusmsg, msglen);
}

void editorRefreshScreen(){ //refresh screen with updated buffer overtime.
  editorScroll();

  struct abuf ab = ABUF_INIT; //initialize ab with macro that makes char *b = NULL; and the int 0.
  abAppend(&ab, "\x1b[?25l", 6);//hides the cursor during terminal redraw.
  abAppend(&ab, "\x1b[H", 3);//positions the cursor to be at col 1 and row 1.

  editorDrawRows(&ab); //draws the row
  editorDrawStatusBar(&ab); //draws our status bar which includes information such as percent % of your current position in the file, or how long the file
  editorDrawMessageBar(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.rx - E.coloff) + 1); //store new cursor position in buf and then ab buffer.
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6); //reappear the cursor after the reposition of it and the redrawn screen
  
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
}

void editorSetStatusMessage(const char *fmt, ...){ //a variadic function with last variable before ...
  va_list ap; //start a variable;
  va_start(ap, fmt); //uses fmt address to access other variables after it.
  vsnprintf(E.statusmsg, sizeof(E.statusmsg), fmt, ap);
  va_end(ap);
  E.statusmsg_time = time(NULL);
}

/*** input ***/
void editorMoveCursor(int key) //move the cursor positions
{
  erow *row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy]; //if E.cy was greater than all the text rows than make it NULL otherwise if less than text rows make it position at the current y pos of the text.


  switch(key){
  case ARROW_LEFT:
    if(E.cx != 0){
      E.cx--;
    }
    else if(E.cy > 0){ //check if E.cy exceeds 0 AND if E.cx is in the 0 position then reposition to the previous line at the end of the text buffer(text).
      E.cy--;
      E.cx = E.row[E.cy].size;
    }
    break;
  case ARROW_RIGHT:
      if(row && E.cx < row->size){
        E.cx++;
      }else if(row && E.cx == row->size){
        E.cy++;
        E.cx = 0;
      }
      break;
  case ARROW_UP:
    if(E.cy != 0){
      E.cy--;
    }
    break;
  case ARROW_DOWN:
    if(E.cy < E.numrows){
      E.cy++;
    }
    break;
  }

  row = (E.cy >= E.numrows) ? NULL : &E.row[E.cy];
  int rowlen = row ? row->size : 0;
  if(E.cx > rowlen){
    E.cx = rowlen;
  }
}

void editorProcessKeypress(){
  static int quit_times = KILO_QUIT_TIMES;

  int c = editorReadKey();
  switch(c){
  case '\r':
    /* WIP/TODO */
    break;
  case CTRL_KEY('q'):
    if(E.dirty && quit_times > 0){
      editorSetStatusMessage("WARNING USER!!! File has unsaved changes. " "Press Ctrl-Q %d more times to quit", quit_times);
      quit_times--;
      return;
    }
    escapeSequences(clearScreenVar, CLEARSCREENBYTE);
    escapeSequences(positionVar, POS_BYTE);
    exit(0);
    break;
  case CTRL_KEY('s'):
    editorSave();
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    if(E.cy < E.numrows) //if the y cursor is less than the total number of rows in the file than proceed.
      E.cx = E.row[E.cy].size;
    break;
  case BACKSPACE:
  case CTRL_KEY('h'):
  case DEL_KEY:
    /* WIP/TODO */
    break;

  case PAGE_UP:
  case PAGE_DOWN:
    {
      if(c == PAGE_UP){
        E.cy = E.rowoff;
      }
      else if(c == PAGE_DOWN){
        E.cy = E.rowoff + E.screenrows + 1;
      }if(E.cy > E.numrows) E.cy = E.numrows;
      int times = E.screenrows;
      while(times--){
        editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
      }
    }
    break;
  case ARROW_UP:
  case ARROW_DOWN:
  case ARROW_LEFT:
  case ARROW_RIGHT:
    editorMoveCursor(c);
    break;

  case CTRL_KEY('l'):
  case '\x1b':
    break;

  default:
    editorInsertChar(c);
    break;
  }

  quit_times = KILO_QUIT_TIMES;
}

/*** init ***/

void initEditor(){
  E.cx = 0;
  E.cy = 0;
  E.rx = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;
  E.dirty = 0;
  E.filename = NULL;
  E.statusmsg[0] = '\0';
  E.statusmsg_time = 0;

  if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
  E.screenrows -= 2;
}

int main(int argc, char *argv[]){
  enableRawMode();
  initEditor();
  if(argc >= 2){
  editorOpen(argv[1]);
  }

  editorSetStatusMessage("HELP: Ctrl-S = save | Ctrl-Q = quit");

  while(1){
    editorRefreshScreen();
    editorProcessKeypress();
    
  }
  return 0;
}
