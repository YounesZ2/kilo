/*** Include ***/
#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOUCE

#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>
#include <termios.h>

/*** Define ***/
#define KILO_VERSION "0.0.1" //version 0.0.1 baby!!!

#define CTRL_KEY(k) ((k) & 0x1f)
#define POS_BYTE 3
#define CLEARSCREENBYTE 4

enum editorKey{
  ARROW_LEFT = 1000,
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
  int size;
  char *chars;
}erow;

struct editorConfig{ //CONFIGURATION IN TERMINAL SUCH AS ROWS COLS LINE VIEWER
  int cx, cy; //cursor positions
  int rowoff; //row offset meaning the current screen ex top of the screen would be row 5
  int coloff;
  int screenrows;
  int screencols;
  int numrows;
  erow *row; //editor row that helps use enter a text that is a dynamic string.
  struct termios orig_termios;
};
struct editorConfig E;
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
  while((nread = read(STDIN_FILENO, &c, 1)) != 1){
    if(nread == -1 && errno != EAGAIN) 
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
void editorAppendRow(char *s, size_t len){
  E.row = realloc(E.row, sizeof(erow) * (E.numrows + 1));

  int at = E.numrows;
  E.row[at].size = len;
  E.row[at].chars = malloc(len + 1);
  memcpy(E.row[at].chars, s, len);
  E.row[at].chars[len] = '\0';
  E.numrows++;
}

/*** File I/O***/
void editorOpen(char *filename){
  FILE *fp = fopen(filename, "r");
  if(!fp) die("fopen");


  char *line = NULL;
  size_t linecap = 0;
  ssize_t linelen;
  linelen = getline(&line, &linecap, fp);
  while((linelen = getline(&line, &linecap, fp)) != -1){
    while(linelen > 0 && (line[linelen-1] == '\n' || line[linelen-1] == '\r'))
      linelen--;
    editorAppendRow(line, linelen);
  }
  free(line);
  fclose(fp);
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
  if(E.cy < E.rowoff){
    E.rowoff = E.cy;
  }
  if(E.cy >= E.rowoff + E.screenrows){
    E.rowoff = E.cy - E.screenrows + 1;
  }
  if(E.cx < E.coloff){
    E.coloff = E.cx;
  }
  if(E.cx >= E.coloff + E.screencols){
    E.coloff = E.cx - E.screencols + 1;

  }
}


void editorDrawRows(struct abuf *ab){
  int y;
  for (y = 0; y < E.screenrows; y++){
    int filerow = y + E.rowoff;
    if(filerow >= E.numrows){
      if(E.numrows == 0 && y == E.screenrows / 3){
        char welcome[80];
        int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor --version %s", KILO_VERSION);
        if(welcomelen > E.screencols) welcomelen = E.screencols;
        int padding = (E.screencols - welcomelen)/2;
        if(padding){
          abAppend(ab, "~", 1);
          padding--;
        }
        while(padding--) abAppend(ab, " ", 1);
        abAppend(ab, welcome, welcomelen);
      }
      else{
        abAppend(ab, "~", 1);
      }
    } else {
      int len = E.row[filerow].size - E.coloff;
      if(len < 0) len = 0;
      if (len > E.screencols) len = E.screencols;
      abAppend(ab, &E.row[filerow].chars[E.coloff], len);
    }
    abAppend(ab, "\x1b[K", 3);
    if(y < E.screenrows - 1){
      abAppend(ab, "\r\n", 2);
    }  
  }
}


void editorRefreshScreen(){ //refresh screen with updated buffer overtime.
  editorScroll();

  struct abuf ab = ABUF_INIT; //initialize ab with macro that makes char *b = NULL; and the int 0.
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  char buf[32];
  snprintf(buf, sizeof(buf), "\x1b[%d;%dH", (E.cy - E.rowoff) + 1, (E.cx - E.coloff) + 1);
  abAppend(&ab, buf, strlen(buf));
  abAppend(&ab, "\x1b[?25h", 6);
  
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
  
}

/*** input ***/
void editorMoveCursor(int key)
{
  switch(key){
  case ARROW_LEFT:
    if(E.cx != 0){
      E.cx--;
    }
    break;
  case ARROW_RIGHT:
      E.cx++;
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
}

void editorProcessKeypress(){
  int c = editorReadKey();
  switch(c){
  case CTRL_KEY('q'):
    escapeSequences(clearScreenVar, CLEARSCREENBYTE);
    escapeSequences(positionVar, POS_BYTE);
    exit(0);
    break;
  case HOME_KEY:
    E.cx = 0;
    break;
  case END_KEY:
    E.cx = E.screencols - 1;
    break;
  case PAGE_UP:
  case PAGE_DOWN:
    {
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
  }

}

/*** init ***/

void initEditor(){
  E.cx = 0;
  E.cy = 0;
  E.rowoff = 0;
  E.coloff = 0;
  E.numrows = 0;
  E.row = NULL;


  if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main(int argc, char *argv[]){
  enableRawMode();
  initEditor();
  if(argc >= 2){
  editorOpen(argv[1]);
  }
  while(1){
    editorRefreshScreen();
    editorProcessKeypress();
    
  }
  return 0;
}
