#ifdef READLINE
#include <readline/readline.h>
#include <readline/history.h>
#endif

#define DEBUG 0
#include "shell.h"

sigset_t sigchld_mask;

static void sigint_handler(int sig) {
  /* No-op handler, we just need break read() call with EINTR. */
  (void)sig;
}

/* Rewrite closed file descriptors to -1,
 * to make sure we don't attempt do close them twice. */
static void MaybeClose(int *fdp) {
  if (*fdp < 0)
    return;
  Close(*fdp);
  *fdp = -1;
}

/* Consume all tokens related to redirection operators.
 * Put opened file descriptors into inputp & output respectively. */
static int do_redir(token_t *token, int ntokens, int *inputp, int *outputp) {
  token_t mode = NULL; /* T_INPUT, T_OUTPUT or NULL */
  int n = 0;           /* number of tokens after redirections are removed */

  for (int i = 0; i < ntokens; i++) {
    /* TODO: Handle tokens and open files as requested. */
#ifdef STUDENT

    /*
    Chce przekształcić ciąg znaków: {"grep", "foo", T_INPUT, "test.in",
    T_OUTPUT, "test.out"} W taki: {"grep", "foo", T_NULL, T_NULL, T_NULL,
    T_NULL} gdzie n to liczba tokenów oraz otworzyć deskryptory To robie:
    jesteśmy teraz w miejscu gdzie przechodzimy po kolei po tablicy tokenów
    Jeżeli token to T_INPUT lub T_OUTPUT to otwieremy odpowiedni deskryptory
    i zmienia wartosci w tablicy tokenow
    */
    mode = token[i];
    assert(mode != T_PIPE);
    if (mode == T_INPUT) {
      MaybeClose(inputp);
      *inputp = Open(token[i + 1], O_RDONLY, 0);
    } else if (mode == T_OUTPUT) {
      MaybeClose(outputp);
      *outputp = Open(token[i + 1], O_RDWR | O_CREAT, S_IRWXU);
    } else {
      n++;
      continue;
    }
    token[i] = T_NULL;
    i++;
    token[i] = T_NULL;
#endif /* !STUDENT */
  }

  token[n] = NULL;
  return n;
}

/* Execute internal command within shell's process or execute external command
 * in a subprocess. External command can be run in the background. */
static int do_job(token_t *token, int ntokens, bool bg) {
  int input = -1, output = -1;
  int exitcode = 0;

  ntokens = do_redir(token, ntokens, &input, &output);

  if (!bg) {
    if ((exitcode = builtin_command(token)) >= 0)
      return exitcode;
  }

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start a subprocess, create a job and monitor it. */
#ifdef STUDENT
  /*
      Jeżeli wprowadzony input nie był potokiem to zoastanie wykonany za pomocą
     tej funkci Wskazówki z zadania 2:
      1) przydatne funkcje: sigprocmask(),sigsuspend(), setpgid() i dup2().
      2) Uruchomione zadanie musi prawidłoworeagować na sygnały: «SIGTSTP»,
      «SIGTTIN» i «SIGTTOU»,
      3) powłoka musi zamykać niepotrzebne deskryptory plików.
      Implementując funkcje dość mocno inspirowałem się:
      https://www.gnu.org/software/libc/manual/html_node/Launching-Jobs.html
  */
  pid_t pid = Fork();
  if (pid == 0) {
    // child
    setpgid(0, 0);
    if (bg == FG) {
      setfgpgrp(getpid());
    }
    // ad. 2
    Sigprocmask(SIG_SETMASK, &mask, NULL);
    Signal(SIGTSTP, SIG_DFL);
    Signal(SIGINT, SIG_DFL);
    if (bg == BG) {
      // https://www.gnu.org/software/libc/manual/html_node/Job-Control-Signals.html
      Signal(SIGTTIN, SIG_DFL);
      Signal(SIGTTOU, SIG_DFL);
    }
    // ustawienia kanałów standard input/output procesu
    if (input != -1) {
      Dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      Dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }
    external_command(token);
  } else {
    // parent
    setpgid(pid, pid);
    // tworzenie nowego zadania
    int n_job = addjob(pid, bg);
    addproc(n_job, pid, token);
    if (bg == FG)
      exitcode = monitorjob(&mask);
    else
      msg("[%d] running '%s'\n", n_job, jobcmd(n_job));
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

/* Start internal or external command in a subprocess that belongs to pipeline.
 * All subprocesses in pipeline must belong to the same process group. */
static pid_t do_stage(pid_t pgid, sigset_t *mask, int input, int output,
                      token_t *token, int ntokens, bool bg) {
  ntokens = do_redir(token, ntokens, &input, &output);

  if (ntokens == 0)
    app_error("ERROR: Command line is not well formed!");

  /* TODO: Start a subprocess and make sure it's moved to a process group. */
  pid_t pid = Fork();
#ifdef STUDENT
  /* Wykonianie fragmentu potok, podobne do do_job */
  if (pid == 0) {
    setpgid(0, pgid); // dla 1 (0,0) dla reszty juz nie
    // if (bg == FG) {
    //  setfgpgrp(getpid());
    //}
    Sigprocmask(SIG_SETMASK, mask, NULL);
    Signal(SIGTSTP, SIG_DFL);
    if (bg) {
      Signal(SIGTTIN, SIG_DFL);
      Signal(SIGTTOU, SIG_DFL);
    }
    if (input != -1) {
      Dup2(input, STDIN_FILENO);
      MaybeClose(&input);
    }
    if (output != -1) {
      Dup2(output, STDOUT_FILENO);
      MaybeClose(&output);
    }
    // musimy sprawdzić czy to nie builtin_command w do_job robiliśmy to już
    // wcześniej tu dopiero tutaj
    if (builtin_command(token) < 1)
      external_command(token);
  } else {
    // parent
    if (pgid == 0)
      setpgid(pid, pid);
    else
      setpgid(pid, pgid);
    MaybeClose(&input);
    MaybeClose(&output);
  }
#endif /* !STUDENT */

  return pid;
}

static void mkpipe(int *readp, int *writep) {
  int fds[2];
  Pipe(fds);
  fcntl(fds[0], F_SETFD, FD_CLOEXEC);
  fcntl(fds[1], F_SETFD, FD_CLOEXEC);
  *readp = fds[0];
  *writep = fds[1];
}

/* Pipeline execution creates a multiprocess job. Both internal and external
 * commands are executed in subprocesses. */
static int do_pipeline(token_t *token, int ntokens, bool bg) {
  pid_t pid, pgid = 0;
  int job = -1;
  int exitcode = 0;

  int input = -1, output = -1, next_input = -1;

  mkpipe(&next_input, &output);

  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Start pipeline subprocesses, create a job and monitor it.
   * Remember to close unused pipe ends! */
#ifdef STUDENT
  /*
     do_pipeline zarządza tworzeniem i monitorowaniem potoku
     do_stage tworzy pojedyńczy proces potoku
     Wszystkie procesy potoku muszą należeć do jednej grupy procesów
     Plan:
     1. na początku obsługuje pierwszy proces przed | jest on wyjątkowy ponieważ
     tworzy całe zadanie
     2. obsługuje wszystkie procesy które znalazły się pomiędzy |...|...|
     3. obsługuje ostatni proces
     4. jeśli bg == FG to zaczynamy mmonitorować zadanie
  */
  int count = 0;
  bool first_stage = true;
  int i = 0;
  while (first_stage) {
    if (token[i] == T_PIPE) {
      pid = do_stage(pgid, &mask, input, output, token, count, bg);
      job = addjob(pid, bg);
      pgid = pid;
      first_stage = false;
      addproc(job, pid, token);
      if (bg == BG) // drugi plan
      {
        msg("[%d] running '%s'\n", job, jobcmd(job));
      }
    } else {
      count++;
    }
    i++;
  }
  count = 0;
  input = next_input;
  mkpipe(&next_input, &output);
  for (; i < ntokens; i++) {
    if (token[i] == T_PIPE) {
      pid = do_stage(pgid, &mask, input, output, token + i - count, count, bg);
      addproc(job, pid, token + i - count);
      input = next_input;
      mkpipe(&next_input, &output);
      count = 0;
    } else {
      count++;
      // continue;
    }
  }
  if (token[i] == T_NULL) {
    MaybeClose(&next_input);
    // output = -1;
    MaybeClose(&output);
    pid =
      do_stage(pgid, &mask, input, output, token + ntokens - count, count, bg);
    addproc(job, pid, token + ntokens - count);
  }
  if (bg == FG) // pierwszy plan
  {
    exitcode = monitorjob(&mask);
  }
#endif /* !STUDENT */

  Sigprocmask(SIG_SETMASK, &mask, NULL);
  return exitcode;
}

static bool is_pipeline(token_t *token, int ntokens) {
  for (int i = 0; i < ntokens; i++)
    if (token[i] == T_PIPE)
      return true;
  return false;
}

static void eval(char *cmdline) {
  bool bg = false;
  int ntokens;
  token_t *token = tokenize(cmdline, &ntokens);

  if (ntokens > 0 && token[ntokens - 1] == T_BGJOB) {
    token[--ntokens] = NULL;
    bg = true;
  }

  if (ntokens > 0) {
    if (is_pipeline(token, ntokens)) {
      do_pipeline(token, ntokens, bg);
    } else {
      do_job(token, ntokens, bg);
    }
  }

  free(token);
}

#ifndef READLINE
static char *readline(const char *prompt) {
  static char line[MAXLINE]; /* `readline` is clearly not reentrant! */

  write(STDOUT_FILENO, prompt, strlen(prompt));

  line[0] = '\0';

  ssize_t nread = read(STDIN_FILENO, line, MAXLINE);
  if (nread < 0) {
    if (errno != EINTR)
      unix_error("Read error");
    msg("\n");
  } else if (nread == 0) {
    return NULL; /* EOF */
  } else {
    if (line[nread - 1] == '\n')
      line[nread - 1] = '\0';
  }

  return strdup(line);
}
#endif

int main(int argc, char *argv[]) {
  /* `stdin` should be attached to terminal running in canonical mode */
  if (!isatty(STDIN_FILENO))
    app_error("ERROR: Shell can run only in interactive mode!");

#ifdef READLINE
  rl_initialize();
#endif

  sigemptyset(&sigchld_mask);
  sigaddset(&sigchld_mask, SIGCHLD);

  if (getsid(0) != getpgid(0))
    Setpgid(0, 0);

  initjobs();

  struct sigaction act = {
    .sa_handler = sigint_handler,
    .sa_flags = 0, /* without SA_RESTART read() will return EINTR */
  };
  Sigaction(SIGINT, &act, NULL);

  Signal(SIGTSTP, SIG_IGN);
  Signal(SIGTTIN, SIG_IGN);
  Signal(SIGTTOU, SIG_IGN);

  while (true) {
    char *line = readline("# ");

    if (line == NULL)
      break;

    if (strlen(line)) {
#ifdef READLINE
      add_history(line);
#endif
      eval(line);
    }
    free(line);
    watchjobs(FINISHED);
  }

  msg("\n");
  shutdownjobs();

  return 0;
}
