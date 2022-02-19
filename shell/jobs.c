#include "shell.h"

typedef struct proc {
  pid_t pid;    /* process identifier */
  int state;    /* RUNNING or STOPPED or FINISHED */
  int exitcode; /* -1 if exit status not yet received */
} proc_t;

typedef struct job {
  pid_t pgid;            /* 0 if slot is free */
  proc_t *proc;          /* array of processes running in as a job */
  struct termios tmodes; /* saved terminal modes */
  int nproc;             /* number of processes */
  int state;             /* changes when live processes have same state */
  char *command;         /* textual representation of command line */
} job_t;

static job_t *jobs = NULL;          /* array of all jobs */
static int njobmax = 1;             /* number of slots in jobs array */
static int tty_fd = -1;             /* controlling terminal file descriptor */
static struct termios shell_tmodes; /* saved shell terminal modes */

static void sigchld_handler(int sig) {
  int old_errno = errno;
  pid_t pid;
  int status;
  /* TODO: Change state (FINISHED, RUNNING, STOPPED) of processes and jobs.
   * Bury all children that finished saving their status in jobs. */
#ifdef STUDENT
  /*
    Child process wysyła SIGCHLD gdy zmieniło stan
    pidy takich dzieci uzyska waitpidem
    -1 czekaj na dowolne dziecko
    algorytm:
    1. czekam na zmiane statusu
    2. szukamy procesu, który zmienił status
    3. odnajduje ten proces i zmeniam informacje w jego strukturze
  */
  bool change_job_state;
  int compare_state;
  // WNOHANG | WUNTRACED | WCONTINUED wracamy tylko jak zmieniło stan
  pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
  while (pid > 0) {
    for (int i = 0; i < njobmax; i++) {
      for (int j = 0; j < jobs[i].nproc; j++) {
        if (pid == jobs[i].proc[j].pid) {
          if (WIFEXITED(status) || WIFSIGNALED(status)) {
            jobs[i].proc[j].state = FINISHED;
            jobs[i].proc[j].exitcode = status;
          } else if (WIFSTOPPED(status))
            jobs[i].proc[j].state = STOPPED;
          else if (WIFCONTINUED(status))
            jobs[i].proc[j].state = RUNNING;
          // sprawdzam czy stan zadania też należy zmienic(patrz linia 14
          // jobs.c)
          compare_state = jobs[i].proc[j].state;
          change_job_state = true;
          for (int k = 0; k < jobs[i].nproc; k++) {
            if (compare_state != jobs[i].proc[k].state)
              change_job_state = false;
          }
          if (change_job_state)
            jobs[i].state = compare_state;
          break;
        }
      }
    }
    pid = waitpid(-1, &status, WNOHANG | WUNTRACED | WCONTINUED);
  }
#endif /* !STUDENT */
  errno = old_errno;
}

/* When pipeline is done, its exitcode is fetched from the last process. */
static int exitcode(job_t *job) {
  return job->proc[job->nproc - 1].exitcode;
}

static int allocjob(void) {
  /* Find empty slot for background job. */
  for (int j = BG; j < njobmax; j++)
    if (jobs[j].pgid == 0)
      return j;

  /* If none found, allocate new one. */
  jobs = realloc(jobs, sizeof(job_t) * (njobmax + 1));
  memset(&jobs[njobmax], 0, sizeof(job_t));
  return njobmax++;
}

static int allocproc(int j) {
  job_t *job = &jobs[j];
  job->proc = realloc(job->proc, sizeof(proc_t) * (job->nproc + 1));
  return job->nproc++;
}

int addjob(pid_t pgid, int bg) {
  int j = bg ? allocjob() : FG;
  job_t *job = &jobs[j];
  /* Initial state of a job. */
  job->pgid = pgid;
  job->state = RUNNING;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
  job->tmodes = shell_tmodes;
  return j;
}

static void deljob(job_t *job) {
  assert(job->state == FINISHED);
  free(job->command);
  free(job->proc);
  job->pgid = 0;
  job->command = NULL;
  job->proc = NULL;
  job->nproc = 0;
}

static void movejob(int from, int to) {
  assert(jobs[to].pgid == 0);
  memcpy(&jobs[to], &jobs[from], sizeof(job_t));
  memset(&jobs[from], 0, sizeof(job_t));
}

static void mkcommand(char **cmdp, char **argv) {
  if (*cmdp)
    strapp(cmdp, " | ");

  for (strapp(cmdp, *argv++); *argv; argv++) {
    strapp(cmdp, " ");
    strapp(cmdp, *argv);
  }
}

void addproc(int j, pid_t pid, char **argv) {
  assert(j < njobmax);
  job_t *job = &jobs[j];

  int p = allocproc(j);
  proc_t *proc = &job->proc[p];
  /* Initial state of a process. */
  proc->pid = pid;
  proc->state = RUNNING;
  proc->exitcode = -1;
  mkcommand(&job->command, argv);
}

/* Returns job's state.
 * If it's finished, delete it and return exitcode through statusp. */
static int jobstate(int j, int *statusp) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  int state = job->state;

  /* TODO: Handle case where job has finished. */
#ifdef STUDENT
  /* Jeśli state == Finished:
     1. zwracamy exitcode przez starsup
     2. czyścimy strukture job
  */
  if (state == FINISHED) {
    *statusp = exitcode(job);
    deljob(job);
  }
#endif /* !STUDENT */

  return state;
}

char *jobcmd(int j) {
  assert(j < njobmax);
  job_t *job = &jobs[j];
  return job->command;
}

/* Continues a job that has been stopped. If move to foreground was requested,
 * then move the job to foreground and start monitoring it. */
bool resumejob(int j, int bg, sigset_t *mask) {
  if (j < 0) {
    for (j = njobmax - 1; j > 0 && jobs[j].state == FINISHED; j--)
      continue;
  }

  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;

    /* TODO: Continue stopped job. Possibly move job to foreground slot. */
#ifdef STUDENT
  /* Jeżeli bg == FG przenosimy zadanie na pierwszy plan, potem wysyłamy
     sigconta, aby kontynuować zatrzymane zadaniem, a następnie je monitorujemy
     wpp, tylko kontynuujemy zadanie
  */
  if (bg == FG) {
    setfgpgrp(jobs[j].pgid);
    movejob(j, FG);
    Kill((-jobs[FG].pgid), SIGCONT);
    msg("continue '%s'\n", jobs[FG].command);
    monitorjob(mask);
  } else {
    Kill((-jobs[j].pgid), SIGCONT);
  }
#endif /* !STUDENT */

  return true;
}

/* Kill the job by sending it a SIGTERM. */
bool killjob(int j) {
  if (j >= njobmax || jobs[j].state == FINISHED)
    return false;
  debug("[%d] killing '%s'\n", j, jobs[j].command);

  /* TODO: I love the smell of napalm in the morning. */
#ifdef STUDENT
  /* 1. wysyłamy sigterm(nasz napalm)
     2. wysyłamy sigcont aby do całej grupy,
        aby obudzić(bo to jednak poranek) zastopowane procesy, aby go obsłużyły.
   */
  Kill(-jobs[j].pgid, SIGTERM);
  Kill(-jobs[j].pgid, SIGCONT);
#endif /* !STUDENT */

  return true;
}

/* Report state of requested background jobs. Clean up finished jobs. */
void watchjobs(int which) {
  for (int j = BG; j < njobmax; j++) {
    if (jobs[j].pgid == 0)
      continue;

      /* TODO: Report job number, state, command and exit code or signal. */
#ifdef STUDENT
    /* Musimy zaraportować informacje o zadaniach drugo planowych.
      1. Twore dodatkowe zmienne dla lepszej czytelności
      2. sprawdzam  czy nasze zadanie pasuje do tego które powłoka chce
      wyświetlić jeżeli tak to je wypisuje gdy zadanie jest zakończone czyszcze
      informacje o nim(deljob(),podpowiedź wykładowcy)

       wpp iteruje dalej
   */
    job_t *job = &jobs[j];
    int state = job->state;
    // stan zadania nie pasuje
    if (state != which && which != ALL) {
      continue;
    } else {
      char *cmd = job->command;
      if (state == RUNNING)
        msg("[%d] running '%s'\n", j, cmd);
      else if (state == STOPPED)
        msg("[%d] suspended '%s'\n", j, cmd);
      else if (state == FINISHED) {
        int exit_code = exitcode(job);
        if (WIFSIGNALED(exit_code)) {
          msg("[%d] killed '%s' by signal %d\n", j, cmd, WTERMSIG(exit_code));
        } else {
          msg("[%d] exited '%s', status=%d\n", j, cmd, WEXITSTATUS(exit_code));
        }
        deljob(job);
      }
    }
#endif /* !STUDENT */
  }
}

/* Monitor job execution. If it gets stopped move it to background.
 * When a job has finished or has been stopped move shell to foreground. */
int monitorjob(sigset_t *mask) {
  int exitcode = 0, state;

  /* TODO: Following code requires use of Tcsetpgrp of tty_fd. */
#ifdef STUDENT
  /* Funkcja monitoruje wykonywanie zadania. Kroki:
     1. przekazujemy kontrole nad terminalem naszemu zadaniu
     2. sprawdzamy stan i dopóki działa(tzn. jest w stanie running) to czekamy
     3. jeśli stan zmienił się na STOPPED tzn. zadanie zostało zatrzymane to
     przenosimy je na drugi plan(background)
     4. jeśli stan zmienił się na FINISHED tzn. zadanie zostało zakończone
     musimy zebrać jego exitcode
     5. przenosimy shell na pierwszy plan(foreground)
  */
  setfgpgrp(jobs[FG].pgid);
  int status;
  state = jobstate(FG, &status);
  while (state == RUNNING) {
    Sigsuspend(mask);
    state = jobstate(FG, &status);
  }
  if (state == STOPPED) {
    int bg_job = allocjob();
    movejob(FG, bg_job);
  } else if (state == FINISHED) {
    if (WIFEXITED(status)) {
      exitcode = WEXITSTATUS(status);
    } else {
      exitcode = WTERMSIG(status);
    }
  }
  setfgpgrp(getpgrp());

#endif /* !STUDENT */

  return exitcode;
}

/* Called just at the beginning of shell's life. */
void initjobs(void) {
  struct sigaction act = {
    .sa_flags = SA_RESTART,
    .sa_handler = sigchld_handler,
  };

  /* Block SIGINT for the duration of `sigchld_handler`
   * in case `sigint_handler` does something crazy like `longjmp`. */
  sigemptyset(&act.sa_mask);
  sigaddset(&act.sa_mask, SIGINT);
  Sigaction(SIGCHLD, &act, NULL);

  jobs = calloc(sizeof(job_t), 1);

  /* Assume we're running in interactive mode, so move us to foreground.
   * Duplicate terminal fd, but do not leak it to subprocesses that execve. */
  assert(isatty(STDIN_FILENO));
  tty_fd = Dup(STDIN_FILENO);
  fcntl(tty_fd, F_SETFD, FD_CLOEXEC);

  /* Take control of the terminal. */
  Tcsetpgrp(tty_fd, getpgrp());

  /* Save default terminal attributes for the shell. */
  Tcgetattr(tty_fd, &shell_tmodes);
}

/* Called just before the shell finishes. */
void shutdownjobs(void) {
  sigset_t mask;
  Sigprocmask(SIG_BLOCK, &sigchld_mask, &mask);

  /* TODO: Kill remaining jobs and wait for them to finish. */
#ifdef STUDENT
  /* iterujemy po zadaniach, uśmiercamy je, czekamy jak się skończą */
  for (int i = 0; i < njobmax; i++) {
    killjob(i);
    int state = jobs[i].state;
    while (state != FINISHED) {
      Sigsuspend(&mask);
      state = jobs[i].state;
      // sigchld_handler(s)
    }
  }
#endif /* !STUDENT */

  watchjobs(FINISHED);

  Sigprocmask(SIG_SETMASK, &mask, NULL);

  Close(tty_fd);
}

/* Sets foreground process group to `pgid`. */
void setfgpgrp(pid_t pgid) {
  Tcsetpgrp(tty_fd, pgid);
}
