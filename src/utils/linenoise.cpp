// Solis Programming Language - Line Editing Utility
// Copyright (c) 2025 Chris M. Perez
// Licensed under the MIT License

/* linenoise.c -- guerrilla line editing library against the idea that a
 * line editing lib needs to be 20,000 lines of C code.
 *
 * You can find the latest source code at:
 *
 *   http://github.com/antirez/linenoise
 *
 * Does a number of crazy assumptions that happen to be true in 99.9999% of
 * the 2010 world computers.
 *
 * ------------------------------------------------------------------------
 *
 * Copyright (c) 2010-2016, Salvatore Sanfilippo <antirez at gmail dot com>
 * Copyright (c) 2010-2013, Pieter Noordhuis <pcnoordhuis at gmail dot com>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *  *  Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *
 *  *  Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * ------------------------------------------------------------------------
 *
 * References:
 * - http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * - http://www.3waylabs.com/nw/WWW/products/wizcon/vt220.html
 *
 * Todo list:
 * - Filter bogus Ctrl+<char> combinations.
 * - Win32 support
 *
 * Bloat:
 * - History search like Ctrl+R.
 *
 * List of escape sequences used by this program, we do everything just
 * with three sequences. In order to be so cheap, we does not notify
 * the terminal about the size of the screen, not does we query it, but
 * we assume that the terminal is wide enough to hold the longest line
 * of the prompt.
 *
 * - \x1b[J: clear screen
 * - \x1b[2K: clear line
 * - \x1b[1;1H: cursor to 0,0
 * - \x1b[0G: cursor to 0
 */

#include "utils/linenoise.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

#define LINENOISE_DEFAULT_HISTORY_MAX_LEN 100
#define LINENOISE_MAX_LINE 4096
static const char* unsupported_term[] = {"dumb", "cons25", "emacs", NULL};
static linenoiseCompletionCallback* completionCallback = NULL;
static linenoiseHintsCallback* hintsCallback = NULL;
static linenoiseFreeHintsCallback* freeHintsCallback = NULL;

static int maskmode = 0;
static int mlmode = 0; /* Multi line mode. Default is single line. */
static int history_max_len = LINENOISE_DEFAULT_HISTORY_MAX_LEN;
static int history_len = 0;
static char** history = NULL;

/* The linenoiseState structure represents the state during line editing.
 * We pass this state to functions implementing specific editing
 * functionalities. */
struct linenoiseState {
  int ifd;            /* Terminal stdin file descriptor. */
  int ofd;            /* Terminal stdout file descriptor. */
  char* buf;          /* Edited line buffer. */
  size_t buflen;      /* Edited line buffer size. */
  const char* prompt; /* Prompt to display. */
  size_t plen;        /* Prompt length. */
  size_t pos;         /* Current cursor position. */
  size_t oldpos;      /* Previous refresh cursor position. */
  size_t len;         /* Current edited line length. */
  size_t cols;        /* Number of columns in terminal. */
  size_t maxrows;     /* Maximum num of rows used so far (multiline mode) */
};

enum KEY_ACTION {
  KEY_NULL = 0,   /* NULL */
  CTRL_A = 1,     /* Ctrl+a */
  CTRL_B = 2,     /* Ctrl+b */
  CTRL_C = 3,     /* Ctrl+c */
  CTRL_D = 4,     /* Ctrl+d */
  CTRL_E = 5,     /* Ctrl+e */
  CTRL_F = 6,     /* Ctrl+f */
  CTRL_H = 8,     /* Ctrl+h */
  TAB = 9,        /* Tab */
  CTRL_K = 11,    /* Ctrl+k */
  CTRL_L = 12,    /* Ctrl+l */
  ENTER = 13,     /* Enter */
  CTRL_N = 14,    /* Ctrl+n */
  CTRL_P = 16,    /* Ctrl+p */
  CTRL_T = 20,    /* Ctrl+t */
  CTRL_U = 21,    /* Ctrl+u */
  CTRL_W = 23,    /* Ctrl+w */
  ESC = 27,       /* Escape */
  BACKSPACE = 127 /* Backspace */
};

static void linenoiseAtExit(void);
int linenoiseHistoryAdd(const char* line);
void linenoiseRefreshLine(struct linenoiseState* l);

/* Debugging function. */
/* static void lndebug(const char *fmt, ...) {
    va_list ap;
    FILE *fp = fopen("/tmp/lndebug.txt", "a");
    va_start(ap, fmt);
    vfprintf(fp, fmt, ap);
    va_end(ap);
    fclose(fp);
} */

// Low level terminal handling functions
// Terminal control and raw mode management

/* Set if to use or not the multi line mode. */
void linenoiseSetMultiLine(int ml) {
  mlmode = ml;
}

/* Return true if the terminal name is in the list of terminals we know are
 * not able to understand basic escape sequences. */
static int isUnsupportedTerm(void) {
  char* term = getenv("TERM");
  int j;

  if (term == NULL)
    return 0;
  for (j = 0; unsupported_term[j]; j++)
    if (!strcasecmp(term, unsupported_term[j]))
      return 1;
  return 0;
}

/* Raw mode: 1960 magic shit. */
static int enableRawMode(int fd) {
  struct termios raw;

  if (!isatty(STDIN_FILENO))
    goto fatal;
  if (tcgetattr(fd, &raw) == -1)
    goto fatal;

  /* input modes: no break, no CR to NL, no parity check, no strip char,
   * no start/stop output control. */
  raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  /* output modes - disable post processing */
  raw.c_oflag &= ~(OPOST);
  /* control modes - set 8 bit chars */
  raw.c_cflag |= (CS8);
  /* local modes - choing off, canonical off, no extended functions,
   * no signal chars (^Z,^C) */
  raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
  /* control chars - set return condition: min number of bytes and timer.
   * We want read to return every single byte, without timeout. */
  raw.c_cc[VMIN] = 1;
  raw.c_cc[VTIME] = 0; /* 1 byte, no timer */

  /* put terminal in raw mode after flushing */
  if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
    goto fatal;
  return 0;

fatal:
  errno = ENOTTY;
  return -1;
}

static void disableRawMode(int fd) {
  struct termios raw;

  if (!isatty(STDIN_FILENO))
    return;
  if (tcgetattr(fd, &raw) == -1)
    return;
  raw.c_iflag |= (BRKINT | ICRNL | INPCK | ISTRIP | IXON);
  raw.c_oflag |= (OPOST);
  raw.c_cflag |= (CS8);
  raw.c_lflag |= (ECHO | ICANON | IEXTEN | ISIG);
  if (tcsetattr(fd, TCSAFLUSH, &raw) < 0)
    return;
}

/* Use the ESC [6n escape sequence to query the horizontal cursor position
 * and return it. On error -1 is returned, on success the position of the
 * cursor. */
static int getCursorPosition(int ifd, int ofd) {
  char buf[32];
  int cols, rows;
  unsigned int i = 0;

  if (write(ofd, "\x1b[6n", 4) != 4)
    return -1;

  while (i < sizeof(buf) - 1) {
    if (read(ifd, buf + i, 1) != 1)
      break;
    if (buf[i] == 'R')
      break;
    i++;
  }
  buf[i] = '\0';

  if (buf[0] != 27 || buf[1] != '[')
    return -1;
  if (sscanf(buf + 2, "%d;%d", &rows, &cols) != 2)
    return -1;
  return cols;
}

/* Try to get the number of columns in the current terminal, or assume 80
 * if it fails. */
static int getColumns(int ifd, int ofd) {
  struct winsize ws;

  if (ioctl(1, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
    /* ioctl() failed. Try to query the terminal itself. */
    int start, cols;

    /* Get the initial position so we can restore it later. */
    start = getCursorPosition(ifd, ofd);
    if (start == -1)
      goto failed;

    /* Go to right margin and get position. */
    if (write(ofd, "\x1b[999C", 6) != 6)
      goto failed;
    cols = getCursorPosition(ifd, ofd);
    if (cols == -1)
      goto failed;

    /* Restore position. */
    if (cols > start) {
      char seq[32];
      snprintf(seq, 32, "\x1b[%dD", cols - start);
      if (write(ofd, seq, strlen(seq)) == -1) {
        /* Can't recover... */
      }
    }
    return cols;
  } else {
    return ws.ws_col;
  }

failed:
  return 80;
}

/* Clear the screen. Used to handle ctrl+l */
void linenoiseClearScreen(void) {
  if (write(STDOUT_FILENO, "\x1b[H\x1b[2J", 7) <= 0) {
    /* nothing to do, just to avoid warning. */
  }
}

/* Beep, used for completion when there is nothing to complete or when all
 * the choices were already shown. */
static void linenoiseBeep(void) {
  fprintf(stderr, "\x7");
  fflush(stderr);
}

// Tab completion functionality
// Handles completion suggestions and display

/* Free a list of completion option populated by linenoiseAddCompletion(). */
static void freeCompletions(linenoiseCompletions* lc) {
  size_t i;
  for (i = 0; i < lc->len; i++)
    free(lc->cvec[i]);
  if (lc->cvec != NULL)
    free(lc->cvec);
}

/* Register a callback function to be called for tab-completion. */
void linenoiseSetCompletionCallback(linenoiseCompletionCallback* fn) {
  completionCallback = fn;
}

/* Register a hits callback to be called to show hints to the user at the
 * right of the prompt. */
void linenoiseSetHintsCallback(linenoiseHintsCallback* fn) {
  hintsCallback = fn;
}

/* Register a function to free the hints returned by the hints callback
 * registered with linenoiseSetHintsCallback(). */
void linenoiseSetFreeHintsCallback(linenoiseFreeHintsCallback* fn) {
  freeHintsCallback = fn;
}

/* This is an helper function for linenoiseEdit() and is called when the
 * user types the <tab> key in order to complete the string currently in the
 * input.
 *
 * The state of the editing is encapsulated into the pointed linenoiseState
 * structure as described in the structure definition. */
static int completeLine(struct linenoiseState* ls) {
  linenoiseCompletions lc = {0, NULL};
  int nread, nwritten;
  char c = 0;

  completionCallback(ls->buf, &lc);
  if (lc.len == 0) {
    linenoiseBeep();
  } else {
    size_t stop = 0, i = 0;

    while (!stop) {
      /* Show completion or original buffer */
      if (i < lc.len) {
        struct linenoiseState saved = *ls;

        ls->len = ls->pos = strlen(lc.cvec[i]);
        ls->buf = lc.cvec[i];
        linenoiseRefreshLine(ls);
        ls->len = saved.len;
        ls->pos = saved.pos;
        ls->buf = saved.buf;
      } else {
        linenoiseRefreshLine(ls);
      }

      nread = read(ls->ifd, &c, 1);
      if (nread <= 0) {
        freeCompletions(&lc);
        return -1;
      }

      switch (c) {
      case TAB:
        i = (i + 1) % (lc.len + 1);
        if (i == lc.len)
          linenoiseBeep();
        break;
      case ESC:
        /* Re-show original buffer */
        if (i < lc.len)
          linenoiseRefreshLine(ls);
        stop = 1;
        break;
      default:
        /* Update buffer and return */
        if (i < lc.len) {
          nwritten = snprintf(ls->buf, ls->buflen, "%s", lc.cvec[i]);
          ls->len = ls->pos = nwritten;
        }
        stop = 1;
        break;
      }
    }
  }

  freeCompletions(&lc);
  return c; /* Return last read character */
}

/* Register a new completion for the current string. */
void linenoiseAddCompletion(linenoiseCompletions* lc, const char* str) {
  size_t len = strlen(str);
  char *copy, **cvec;

  copy = (char*)malloc(len + 1);
  if (copy == NULL)
    return;
  memcpy(copy, str, len + 1);
  cvec = (char**)realloc(lc->cvec, sizeof(char*) * (lc->len + 1));
  if (cvec == NULL) {
    free(copy);
    return;
  }
  lc->cvec = cvec;
  lc->cvec[lc->len++] = copy;
}


/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */


// Line editing core functionality
// Main editing loop and key handling

/* We define a very simple "append buffer" structure, that is an heap
 * allocated string where we can append to. This is useful in order to
 * write all the escape sequences in a buffer and flush them to the standard
 * output in a single call, to avoid flickering effects. */
struct abuf {
  char* b;
  int len;
};

static void abInit(struct abuf* ab) {
  ab->b = NULL;
  ab->len = 0;
}

static void abAppend(struct abuf* ab, const char* s, int len) {
  char* newPtr = (char*)realloc(ab->b, ab->len + len);

  if (newPtr == NULL)
    return;
  memcpy(newPtr + ab->len, s, len);
  ab->b = newPtr;
  ab->len += len;
}

static void abFree(struct abuf* ab) {
  free(ab->b);
}

/* Helper of linenoiseRefreshLine() to escape characters for visibility. */
static void snprintf_safe(char* buf, size_t buflen, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, buflen, fmt, ap);
  va_end(ap);
}

/* Single line low level refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshSingleLine(struct linenoiseState* l) {
  char seq[64];
  size_t plen = strlen(l->prompt);
  int fd = l->ofd;
  char* buf = l->buf;
  size_t len = l->len;
  size_t pos = l->pos;
  struct abuf ab;

  while ((plen + pos) >= l->cols) {
    buf++;
    len--;
    pos--;
  }
  while (plen + len > l->cols) {
    len--;
  }

  abInit(&ab);
  /* Cursor to left edge */
  snprintf(seq, 64, "\r");
  abAppend(&ab, seq, strlen(seq));
  /* Write the prompt and the current buffer content */
  abAppend(&ab, l->prompt, strlen(l->prompt));
  if (maskmode == 1) {
    while (len--)
      abAppend(&ab, "*", 1);
  } else {
    abAppend(&ab, buf, len);
  }
  /* Show hints if any. */
  if (hintsCallback && plen + len < l->cols) {
    int color = -1, bold = 0;
    char* hint = hintsCallback(buf, &color, &bold);
    if (hint) {
      if (color != -1 || bold != 0)
        snprintf(seq, 64, "\033[%d;%d;49m", bold, color);
      else
        seq[0] = '\0';
      abAppend(&ab, seq, strlen(seq));
      abAppend(&ab, hint, strlen(hint));
      if (color != -1 || bold != 0)
        snprintf(seq, 64, "\033[0m");
      else
        seq[0] = '\0';
      abAppend(&ab, seq, strlen(seq));
      /* Call the callback to free the hint pointer. */
      if (freeHintsCallback)
        freeHintsCallback(hint);
    }
  }
  /* Erase to right */
  snprintf(seq, 64, "\x1b[0K");
  abAppend(&ab, seq, strlen(seq));
  /* Move cursor to original position. */
  snprintf(seq, 64, "\r\x1b[%dC", (int)(pos + plen));
  abAppend(&ab, seq, strlen(seq));
  if (write(fd, ab.b, ab.len) == -1) {
  } /* Can't recover from write error. */
  abFree(&ab);
}

/* Multi line low level refresh.
 *
 * Rewrite the currently edited line accordingly to the buffer content,
 * cursor position, and number of columns of the terminal. */
static void refreshMultiLine(struct linenoiseState* l) {
  char seq[64];
  int plen = strlen(l->prompt);
  int rows = (plen + l->len + l->cols - 1) / l->cols; /* rows used by current buf. */
  int rpos = (plen + l->oldpos + l->cols) / l->cols;  /* cursor relative row. */
  int rpos2;                                          /* rpos after refresh. */
  int col;                                            /* colum position, zero-based. */
  int old_rows = l->maxrows;
  int fd = l->ofd, j;
  struct abuf ab;

  /* Update maxrows if needed. */
  if (rows > (int)l->maxrows)
    l->maxrows = rows;

  /* First step: clear all the lines used before. To do so start by
   * going to the last row. */
  abInit(&ab);
  if (old_rows - rpos > 0) {
    snprintf(seq, 64, "\x1b[%dB", old_rows - rpos);
    abAppend(&ab, seq, strlen(seq));
  }

  /* Now for every row clear it, go up. */
  for (j = 0; j < old_rows - 1; j++) {
    snprintf(seq, 64, "\r\x1b[0K\x1b[1A");
    abAppend(&ab, seq, strlen(seq));
  }

  /* Clean the top line. */
  snprintf(seq, 64, "\r\x1b[0K");
  abAppend(&ab, seq, strlen(seq));

  /* Write the prompt and the current buffer content */
  abAppend(&ab, l->prompt, strlen(l->prompt));
  if (maskmode == 1) {
    unsigned int i;
    for (i = 0; i < l->len; i++)
      abAppend(&ab, "*", 1);
  } else {
    abAppend(&ab, l->buf, l->len);
  }

  /* Show hints if any. */
  if (hintsCallback && plen + l->len < l->cols) {
    int color = -1, bold = 0;
    char* hint = hintsCallback(l->buf, &color, &bold);
    if (hint) {
      if (color != -1 || bold != 0)
        snprintf(seq, 64, "\033[%d;%d;49m", bold, color);
      else
        seq[0] = '\0';
      abAppend(&ab, seq, strlen(seq));
      abAppend(&ab, hint, strlen(hint));
      if (color != -1 || bold != 0)
        snprintf(seq, 64, "\033[0m");
      else
        seq[0] = '\0';
      abAppend(&ab, seq, strlen(seq));
      /* Call the callback to free the hint pointer. */
      if (freeHintsCallback)
        freeHintsCallback(hint);
    }
  }

  /* If we are at the very end of the screen with our prompt, we need to
   * emit a newline and move the prompt to the first column. */
  if (l->pos && l->pos == l->len && (l->pos + plen) % l->cols == 0) {
    abAppend(&ab, "\n", 1);
    snprintf(seq, 64, "\r");
    abAppend(&ab, seq, strlen(seq));
    rows++;
    if (rows > (int)l->maxrows)
      l->maxrows = rows;
  }

  /* Move cursor to right position. */
  rpos2 = (plen + l->pos + l->cols) / l->cols; /* current cursor relative row. */

  /* Go up till we reach the expected positon. */
  if (rows - rpos2 > 0) {
    snprintf(seq, 64, "\x1b[%dA", rows - rpos2);
    abAppend(&ab, seq, strlen(seq));
  }

  /* Set column. */
  col = (plen + (int)l->pos) % (int)l->cols;
  if (col)
    snprintf(seq, 64, "\r\x1b[%dC", col);
  else
    snprintf(seq, 64, "\r");
  abAppend(&ab, seq, strlen(seq));

  l->oldpos = l->pos;

  if (write(fd, ab.b, ab.len) == -1) {
  } /* Can't recover from write error. */
  abFree(&ab);
}

/* Calls the two low level functions refreshSingleLine() or
 * refreshMultiLine() according to the selected mode. */
void linenoiseRefreshLine(struct linenoiseState* l) {
  if (mlmode)
    refreshMultiLine(l);
  else
    refreshSingleLine(l);
}

/* Insert the character 'c' at cursor current position.
 *
 * On error writing to the terminal -1 is returned, otherwise 0. */
int linenoiseEditInsert(struct linenoiseState* l, char c) {
  if (l->len < l->buflen) {
    if (l->len == l->pos) {
      l->buf[l->pos] = c;
      l->pos++;
      l->len++;
      l->buf[l->len] = '\0';
      if ((!mlmode && l->plen + l->len < l->cols && !hintsCallback)) {
        /* Avoid a full update of the line in the
         * trivial case. */
        if (maskmode == 1)
          c = '*';
        if (write(l->ofd, &c, 1) == -1)
          return -1;
      } else {
        linenoiseRefreshLine(l);
      }
    } else {
      memmove(l->buf + l->pos + 1, l->buf + l->pos, l->len - l->pos);
      l->buf[l->pos] = c;
      l->len++;
      l->pos++;
      l->buf[l->len] = '\0';
      linenoiseRefreshLine(l);
    }
  }
  return 0;
}

/* Move cursor on the left. */
void linenoiseEditMoveLeft(struct linenoiseState* l) {
  if (l->pos > 0) {
    l->pos--;
    linenoiseRefreshLine(l);
  }
}

/* Move cursor on the right. */
void linenoiseEditMoveRight(struct linenoiseState* l) {
  if (l->pos != l->len) {
    l->pos++;
    linenoiseRefreshLine(l);
  }
}

/* Move cursor to the start of the line. */
void linenoiseEditMoveHome(struct linenoiseState* l) {
  if (l->pos != 0) {
    l->pos = 0;
    linenoiseRefreshLine(l);
  }
}

/* Move cursor to the end of the line. */
void linenoiseEditMoveEnd(struct linenoiseState* l) {
  if (l->pos != l->len) {
    l->pos = l->len;
    linenoiseRefreshLine(l);
  }
}

/* Substitute the currently edited line with the next or previous history
 * entry as specified by 'dir'. */
#define LINENOISE_HISTORY_NEXT 0
#define LINENOISE_HISTORY_PREV 1
void linenoiseEditHistoryNext(struct linenoiseState* l, int dir) {
  if (history_len > 1) {
    /* Update the current history entry before to
     * overwrite it with the next one. */
    free(history[history_len - 1 - l->oldpos]);
    history[history_len - 1 - l->oldpos] = strdup(l->buf);
    /* Show the new entry */
    l->oldpos += (dir == LINENOISE_HISTORY_PREV) ? 1 : -1;
    if ((int)l->oldpos < 0) {
      l->oldpos = 0;
      return;
    } else if ((size_t)l->oldpos >= (size_t)history_len) {
      l->oldpos = history_len - 1;
      return;
    }
    strncpy(l->buf, history[history_len - 1 - l->oldpos], l->buflen);
    l->buf[l->buflen - 1] = '\0';
    l->len = l->pos = strlen(l->buf);
    linenoiseRefreshLine(l);
  }
}

/* Delete the character at the right of the cursor without altering the cursor
 * position. Basically this is what happens with the "Delete" keyboard key. */
void linenoiseEditDelete(struct linenoiseState* l) {
  if (l->len > 0 && l->pos < l->len) {
    memmove(l->buf + l->pos, l->buf + l->pos + 1, l->len - l->pos - 1);
    l->len--;
    l->buf[l->len] = '\0';
    linenoiseRefreshLine(l);
  }
}

/* Backspace implementation. */
void linenoiseEditBackspace(struct linenoiseState* l) {
  if (l->pos > 0 && l->len > 0) {
    memmove(l->buf + l->pos - 1, l->buf + l->pos, l->len - l->pos);
    l->pos--;
    l->len--;
    l->buf[l->len] = '\0';
    linenoiseRefreshLine(l);
  }
}

/* Delete the previosu word, maintaining the cursor at the start of the
 * current word. */
void linenoiseEditDeletePrevWord(struct linenoiseState* l) {
  size_t old_pos = l->pos;
  size_t diff;

  while (l->pos > 0 && l->buf[l->pos - 1] == ' ')
    l->pos--;
  while (l->pos > 0 && l->buf[l->pos - 1] != ' ')
    l->pos--;
  diff = old_pos - l->pos;
  memmove(l->buf + l->pos, l->buf + old_pos, l->len - old_pos + 1);
  l->len -= diff;
  linenoiseRefreshLine(l);
}

/* This function is the core of the line editing capability of linenoise.
 * It expects 'fd' to be the standard input file descriptor, and the
 * 'prompt' to be displayed on the terminal. 'buf' and 'buflen' is
 * where the user input is stored and the maximum length of the buffer.
 *
 * The function returns the length of the current buffer. */
static int linenoiseEdit(
    int stdin_fd, int stdout_fd, char* buf, size_t buflen, const char* prompt) {
  struct linenoiseState l;

  /* Populate the linenoise state that we pass to functions implementing
   * specific editing functionalities. */
  l.ifd = stdin_fd;
  l.ofd = stdout_fd;
  l.buf = buf;
  l.buflen = buflen;
  l.prompt = prompt;
  l.plen = strlen(prompt);
  l.oldpos = l.pos = 0;
  l.len = 0;
  l.cols = getColumns(stdin_fd, stdout_fd);
  l.maxrows = 0;

  /* Buffer starts empty. */
  l.buf[0] = '\0';
  l.buflen--; /* Make sure there is always space for the nulterm */

  /* The latest history entry is always our current buffer, that
   * initially is just an empty string. */
  linenoiseHistoryAdd("");

  if (write(l.ofd, prompt, l.plen) == -1)
    return -1;
  while (1) {
    char c;
    int nread;
    char seq[3];

    nread = read(l.ifd, &c, 1);
    if (nread <= 0)
      return l.len;

    /* Only autocomplete when the callback is set. It returns < 0 when
     * there was an error reading from fd. Otherwise it will return the
     * character that should be handled next. */
    if (c == 9 && completionCallback != NULL) {
      c = completeLine(&l);
      /* Return on errors */
      if (c < 0)
        return l.len;
      /* Read next character when 0 */
      if (c == 0)
        continue;
    }

    switch (c) {
    case ENTER: /* enter */
      history_len--;
      free(history[history_len]);
      if (mlmode)
        linenoiseEditMoveEnd(&l);
      if (hintsCallback) {
        /* Force a refresh without hints to leave the previous
         * line as the user typed it after a newline. */
        linenoiseHintsCallback* hc = hintsCallback;
        hintsCallback = NULL;
        linenoiseRefreshLine(&l);
        hintsCallback = hc;
      }
      return (int)l.len;
    case CTRL_C: /* ctrl-c */
      errno = EAGAIN;
      return -1;
    case BACKSPACE: /* backspace */
    case 8:         /* ctrl-h */
      linenoiseEditBackspace(&l);
      break;
    case CTRL_D: /* ctrl-d, remove char at right of cursor, or if the
                    line is empty, act as end-of-file. */
      if (l.len > 0) {
        linenoiseEditDelete(&l);
      } else {
        history_len--;
        free(history[history_len]);
        return -1;
      }
      break;
    case CTRL_T: /* ctrl-t, swaps current character with previous. */
      if (l.pos > 0 && l.pos < l.len) {
        int aux = buf[l.pos - 1];
        buf[l.pos - 1] = buf[l.pos];
        buf[l.pos] = aux;
        if (l.pos != l.len - 1)
          l.pos++;
        linenoiseRefreshLine(&l);
      }
      break;
    case CTRL_B: /* ctrl-b */
      linenoiseEditMoveLeft(&l);
      break;
    case CTRL_F: /* ctrl-f */
      linenoiseEditMoveRight(&l);
      break;
    case CTRL_P: /* ctrl-p */
      linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
      break;
    case CTRL_N: /* ctrl-n */
      linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
      break;
    case ESC: /* escape sequence */
      /* Read the next two bytes representing the escape sequence.
       * Use two calls to handle slow terminals returning the two
       * chars at different times. */
      if (read(l.ifd, seq, 1) == -1)
        break;
      if (read(l.ifd, seq + 1, 1) == -1)
        break;

      /* ESC [ sequences. */
      if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9') {
          /* Extended escape, read additional byte. */
          if (read(l.ifd, seq + 2, 1) == -1)
            break;
          if (seq[2] == '~') {
            switch (seq[1]) {
            case '3': /* Delete key. */
              linenoiseEditDelete(&l);
              break;
            }
          }
        } else {
          switch (seq[1]) {
          case 'A': /* Up */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_PREV);
            break;
          case 'B': /* Down */
            linenoiseEditHistoryNext(&l, LINENOISE_HISTORY_NEXT);
            break;
          case 'C': /* Right */
            linenoiseEditMoveRight(&l);
            break;
          case 'D': /* Left */
            linenoiseEditMoveLeft(&l);
            break;
          }
        }
      }

      /* ESC O sequences. */
      else if (seq[0] == 'O') {
        switch (seq[1]) {
        case 'H': /* Home */
          linenoiseEditMoveHome(&l);
          break;
        case 'F': /* End */
          linenoiseEditMoveEnd(&l);
          break;
        }
      }
      break;
    default:
      if (linenoiseEditInsert(&l, c))
        return -1;
      break;
    case CTRL_U: /* Ctrl+u, delete the whole line. */
      buf[0] = '\0';
      l.pos = l.len = 0;
      linenoiseRefreshLine(&l);
      break;
    case CTRL_K: /* Ctrl+k, delete from current to end of line. */
      buf[l.pos] = '\0';
      l.len = l.pos;
      linenoiseRefreshLine(&l);
      break;
    case CTRL_A: /* Ctrl+a, go to the start of the line */
      linenoiseEditMoveHome(&l);
      break;
    case CTRL_E: /* ctrl+e, go to the end of the line */
      linenoiseEditMoveEnd(&l);
      break;
    case CTRL_L: /* ctrl+l, clear screen */
      linenoiseClearScreen();
      linenoiseRefreshLine(&l);
      break;
    case CTRL_W: /* ctrl+w, delete previous word */
      linenoiseEditDeletePrevWord(&l);
      break;
    }
  }
  return l.len;
}

/* This function calls the line editing function linenoiseEdit() using
 * the STDIN file descriptor set in raw mode. */
static int linenoiseRaw(char* buf, size_t buflen, const char* prompt) {
  int count;

  if (buflen == 0) {
    errno = EINVAL;
    return -1;
  }

  if (enableRawMode(STDIN_FILENO) == -1)
    return -1;
  count = linenoiseEdit(STDIN_FILENO, STDOUT_FILENO, buf, buflen, prompt);
  disableRawMode(STDIN_FILENO);
  printf("\n");
  return count;
}

/* This function is called when linenoise() is called with the standard
 * input file descriptor not attached to a TTY. So for example when the
 * program is called in pipe or with a file redirected to its standard
 * input. In this case, we want to be able to return the line regardless
 * of its length (by default we are limited to 4k). */
static char* linenoiseNoTTY(void) {
  char* line = NULL;
  size_t len = 0, maxlen = 0;

  while (1) {
    if (len == maxlen) {
      if (maxlen == 0)
        maxlen = 16;
      else
        maxlen *= 2;
      char* oldval = line;
      line = (char*)realloc(line, maxlen);
      if (line == NULL) {
        if (oldval)
          free(oldval);
        return NULL;
      }
    }
    int c = fgetc(stdin);
    if (c == EOF || c == '\n') {
      if (c == EOF && len == 0) {
        free(line);
        return NULL;
      } else {
        line[len] = '\0';
        return line;
      }
    }
    line[len] = c;
    len++;
  }
}

/* The high level function that is the main API of the linenoise library.
 * This function checks if the terminal has basic capabilities, just checking
 * for a blacklist of stupid terminals, and later either calls the line
 * editing function or uses dummy fgets() so that you will be able to type
 * something even in the most desperate of the conditions. */
char* linenoise(const char* prompt) {
  char buf[LINENOISE_MAX_LINE];
  int count;

  if (!isatty(STDIN_FILENO)) {
    /* Not a tty: read from file / pipe. In this mode we don't want any
     * limit to the line size, so we call a function to handle that. */
    return linenoiseNoTTY();
  } else if (isUnsupportedTerm()) {
    size_t len;

    printf("%s", prompt);
    fflush(stdout);
    if (fgets(buf, LINENOISE_MAX_LINE, stdin) == NULL)
      return NULL;
    len = strlen(buf);
    while (len && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      len--;
      buf[len] = '\0';
    }
    return strdup(buf);
  } else {
    count = linenoiseRaw(buf, LINENOISE_MAX_LINE, prompt);
    if (count == -1)
      return NULL;
    return strdup(buf);
  }
}

/* This is just a wrapper the user may want to call in order to register a
 * callback function that handles the completion. We need this wrapper
 * because the function (which is static in this file) needs to know
 * the state of the line editing (that is passed to it) in order to
 * call the user callback. */
/* static void completionCallbackWrapper(const char *buf, linenoiseCompletions *lc) {
    if (completionCallback) completionCallback(buf, lc);
} */

/* Register a callback function to be called for tab-completion. */
/* void linenoiseSetCompletionCallback(linenoiseCompletionCallback *fn) {
    completionCallback = fn;
} */

// Command history management
// History storage, navigation, and persistence

/* Free the history, but does not reset it. Only used when we have to
 * exit() to avoid memory leaks are reported by valgrind & co. */
void linenoiseFreeHistory(void) {
  if (history) {
    int j;

    for (j = 0; j < history_len; j++)
      free(history[j]);
    free(history);
  }
}

/* At exit we'll try to fix the terminal to the initial conditions. */
static void linenoiseAtExit(void) {
  disableRawMode(STDIN_FILENO);
  linenoiseFreeHistory();
}

/* This is the API call to add a new entry in the linenoise history.
 * It uses a fixed array of char pointers that are shifted (memmoved)
 * when the history max length is reached in order to remove the older
 * entry and make room for the new one. */
int linenoiseHistoryAdd(const char* line) {
  char* linecopy;

  if (history_max_len == 0)
    return 0;

  /* Initialization on first call. */
  if (history == NULL) {
    history = (char**)malloc(sizeof(char*) * history_max_len);
    if (history == NULL)
      return 0;
    memset(history, 0, (sizeof(char*) * history_max_len));
  }

  /* Don't add duplicated lines. */
  if (history_len && !strcmp(history[history_len - 1], line))
    return 0;

  linecopy = strdup(line);
  if (!linecopy)
    return 0;
  if (history_len == history_max_len) {
    free(history[0]);
    memmove(history, history + 1, sizeof(char*) * (history_max_len - 1));
    history_len--;
  }
  history[history_len] = linecopy;
  history_len++;
  return 1;
}

/* Set the maximum length for the history. This function can be called even
 * if there is already some history, the function will make sure to retain
 * just the latest 'len' elements if the new history length value is smaller
 * than the amount of items already inside the history. */
int linenoiseHistorySetMaxLen(int len) {
  char** newPtr;

  if (len < 1)
    return 0;
  if (history) {
    int tocopy = history_len;

    newPtr = (char**)malloc(sizeof(char*) * len);
    if (newPtr == NULL)
      return 0;

    /* If we can't copy everything, free the elements we'll not use. */
    if (len < tocopy) {
      int j;

      for (j = 0; j < tocopy - len; j++)
        free(history[j]);
      tocopy = len;
    }
    memset(newPtr, 0, sizeof(char*) * len);
    memcpy(newPtr, history + (history_len - tocopy), sizeof(char*) * tocopy);
    free(history);
    history = newPtr;
  }
  history_max_len = len;
  if (history_len > history_max_len)
    history_len = history_max_len;
  return 1;
}

/* Save the history in the specified file. On success 0 is returned
 * otherwise -1 is returned. */
int linenoiseHistorySave(const char* filename) {
  struct termios old_termios;
  mode_t old_umask = umask(S_IXUSR | S_IXGRP | S_IXOTH);
  FILE* fp;
  int j;

  fp = fopen(filename, "w");
  umask(old_umask);
  if (fp == NULL)
    return -1;
  chmod(filename, S_IRUSR | S_IWUSR);
  for (j = 0; j < history_len; j++)
    fprintf(fp, "%s\n", history[j]);
  fclose(fp);
  return 0;
}

/* Load the history from the specified file. If the file does not exist
 * zero is returned and no operation is performed.
 *
 * If the file exists and the operation succeeded 0 is returned, otherwise
 * on error -1 is returned. */
int linenoiseHistoryLoad(const char* filename) {
  FILE* fp = fopen(filename, "r");
  char buf[LINENOISE_MAX_LINE];

  if (fp == NULL)
    return -1;

  while (fgets(buf, LINENOISE_MAX_LINE, fp) != NULL) {
    char* p;

    p = strchr(buf, '\r');
    if (!p)
      p = strchr(buf, '\n');
    if (p)
      *p = '\0';
    linenoiseHistoryAdd(buf);
  }
  fclose(fp);
  return 0;
}

void linenoiseMaskModeEnable(void) {
  maskmode = 1;
}

void linenoiseMaskModeDisable(void) {
  maskmode = 0;
}
