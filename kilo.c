/*** Include ***/
#include <ctype.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <termios.h>

/*** Define ***/
#define KILO_VERSION "0.0.1" //version 0.0.1 baby!!!

#define CTRL_KEY(k) ((k) & 0x1f)
#define POS_BYTE 3
#define CLEARSCREENBYTE 4


/*** Global Variables ***/
const char *clearScreenVar = "\x1b[2J";
const char *positionVar = "\x1b[H";

/*** Char sequences ***/

void escapeSequences(const char *s, int bytes){ //A short cut function that lets me enter char literals like claer screen or position without actually writing them.
  write(STDOUT_FILENO, s, bytes);
}


/*** Data ***/
struct editorConfig{
  int screenrows;
  int screencols;
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

char editorReadKey(){
  int nread;
  char c;
  while((nread = read(STDIN_FILENO, &c, 1)) != 1){
    if(nread == -1 && errno != EAGAIN) 
      die("read");
  }
  return c;
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

void editorDrawRows(struct abuf *ab){
  int y;
  for (y = 0; y < E.screenrows; y++){
    if(y == E.screenrows / 3){
      char welcome[80];
      int welcomelen = snprintf(welcome, sizeof(welcome), "Kilo editor --version %s", KILO_VERSION);
      if(welcomelen > E.screencols) welcomelen = E.screencols;
      abAppend(ab, welcome, welcomelen);
    }
    else{
    abAppend(ab, "~", 1);
    }

    abAppend(ab, "\x1b[K", 3);
    if(y < E.screenrows - 1){
      abAppend(ab, "\r\n", 2);
    }  
  }
}


void editorRefreshScreen(){
  struct abuf ab = ABUF_INIT;
  abAppend(&ab, "\x1b[?25l", 6);
  abAppend(&ab, "\x1b[H", 3);

  editorDrawRows(&ab);

  abAppend(&ab, "\x1b[H", 3);
  abAppend(&ab, "\x1b[?25h", 6);
  
  write(STDOUT_FILENO, ab.b, ab.len);
  abFree(&ab);
  
}

/*** input ***/
void editorProcessKeypress(){
  char c = editorReadKey();
  switch(c){
  case CTRL_KEY('q'):
    escapeSequences(clearScreenVar, CLEARSCREENBYTE);
    escapeSequences(positionVar, POS_BYTE);
    exit(0);
    break;
  }

}

/*** init ***/

void initEditor(){
  if(getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");

}

int main(){
  enableRawMode();
  initEditor();
  while(1){
    editorRefreshScreen();
    editorProcessKeypress();
    
  }
  return 0;
}
