/*
 * Job manager for "jobber".
 */
#include <stdlib.h>
#include "jobber.h"
#include "task.h"
#include "rhelper.h"
#include<errno.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

void display_help();
typedef struct job_entry {
  int jobid;
  char * taskstr;
  int pgid;
  JOB_STATUS status;
  int alloc;
  TASK * taskp;
  int exit_status;
  int was_canceled;
}
job_entry;
struct job_entry * job_list[MAX_JOBS];
int job_count;

int get_free_entry();
pid_t Fork(void);
void unix_error(char * msg);
void sigchld_handler(int s);
int master_runner(int jobid);
job_entry * get_job_entrty(int _jobid);
TASK * get_task(int jobid);
void Close(int fd);
int wordlist_len(WORD_LIST * wordlist);
void convert_to_array(WORD_LIST * wordlist, char * argstwo[]);
void update_status(int jobid, JOB_STATUS _status);
int get_numeric_val(char * taskstrp);
int get_job_index(int jobid);
void attempt_run();
int process(char * input);
job_entry * get_job_entrty_by_pid(int pid);
volatile sig_atomic_t done;
int enable;
int active_runner;
int is_init = 0;

int handler() {
  // printf("*");
  if (done == 1) {
    if (enable > 0) {
      attempt_run();
    }
  }
  return 0;
}

int jobs_init() {
  sf_set_readline_signal_hook(handler);
  enable = 0;
  active_runner = 1;
  done = 1;
  for (int i = 0; i < MAX_JOBS; i++) {
    void * tmp = malloc(sizeof(job_entry));
    if (tmp == NULL) return -1;
    job_list[i] = (job_entry * ) tmp;
    job_list[i] -> alloc = 0;
    job_list[i] -> status = 0;
    job_list[i] -> jobid = -1;
    job_list[i] -> was_canceled = 0;
  }
  return 0;
}
/*
 * @brief  Finalize the job spooler.
 * @details  This function must be called once when job processing is to be terminated.
 * It cancels any remaining jobs, waits for them to terminate, expunges all jobs,
 * and frees any other memory or resources before returning.
 */
void jobs_fini(void) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> alloc == 1) {
      // printf("job ststus bef cancel : %s",job_status_names[job_list[i]->status]);
      job_cancel(job_list[i] -> jobid);
      // printf("job ststus after cancel : %s",job_status_names[job_list[i]->status]);
    }
  }
  sleep(1);
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> alloc == 1) {
      // printf("job ststus bef expunge : %s",job_status_names[job_list[i]->status]);
      job_expunge(job_list[i] -> jobid);
      // printf("job ststus bef after : %s",job_status_names[job_list[i]->status]);
      job_list[i] -> alloc = 0;
    }
  }
  time_t s = time(NULL);
  while (active_runner > 1) {
    //prevent infinite loop
    time_t e = time(NULL);
    if (difftime(e, s) > 3) {
      break;
    }
  }
  exit(0);
}
int jobs_set_enabled(int val) {
  int tmp = enable;
  enable = val;
  return tmp;
}
int jobs_get_enabled() {
  return enable;
}
int job_create(char * command) {
  int jobid = get_free_entry();

  // printf("job id: %d\n",jobid);
  if (jobid == -1) return -1;
  TASK * _taskp = parse_task( & command);
  if (_taskp == NULL) {
    debug("task parsing failed, rturning -1\n");
    return -1;
  }
  debug("task parsed\n");
  job_list[jobid] -> alloc = 1;
  job_list[jobid] -> taskstr = command;
  job_list[jobid] -> status = NEW;

  // JOB_STATUS waiting = WAITING;
  // debug("jobid: %d , old status %d , new status : %d",jobid, NEW, WAITING);
  // sf_job_status_change(jobid, NEW, waiting);
  job_list[jobid] -> taskp = _taskp;
  job_list[jobid] -> jobid = jobid;
  debug("update state %d, %d, %d", jobid, job_list[jobid] -> status, WAITING);
  sf_job_create(jobid);
  update_status(jobid, WAITING);
  debug("update state %d, %d, %d", jobid, job_list[jobid] -> status, WAITING);
  // job_list[jobid]->status = WAITING;
  return jobid;
}
int job_expunge(int jobid) {
  if (jobid < 0 || jobid > MAX_JOBS) {
    printf("Invalid JOBID\n");
    return -1;
  }
  int index = get_job_index(jobid);
  if (index == -1) {
    printf("No job with this jobid\n");
    return -1;
  }
  if (job_list[index] -> status == ABORTED || job_list[index] -> status == COMPLETED) {
    //successful expunge or all?
    sf_job_expunge(job_list[index] -> jobid);
    job_list[index] -> alloc = 0;
    job_list[index] -> jobid = -1;
    job_list[index] -> status = 0;
    free(job_list[index] -> taskp);
    return 0;
  }
  return -1;
}
int job_cancel(int jobid) {
  if (jobid < 0 || jobid > MAX_JOBS) {
    printf("Invalid JOBID\n");
    return -1;
  }
  int index = get_job_index(jobid);
  if (index == -1) {
    printf("No job with this jobid\n");
    return -1;
  }
  int jpgid = jpgid = job_list[index] -> pgid;
  if (jpgid == -1) {
    printf("No job present with given ID\n");
    return -1;
  }

  if (job_list[index] -> status != RUNNING && job_list[index] -> status != WAITING && job_list[index] -> status != PAUSED) {
    // job_list[index]->status = -1;
    debug("job is not in valid state to be canceled");
    return -1;
  }

  if (job_list[index] -> status == WAITING) {
    update_status(jobid, CANCELED);
    return 0;
  }
  update_status(jobid, CANCELED);
  job_list[index] -> was_canceled = 1;
  debug("killing job %d, process id : %d, ", jobid, jpgid);
  int res = killpg(jpgid, SIGKILL);
  if (res == -1) {
    debug("Error while stopping job %d", jobid);
    return -1;
  }
  debug("stopped job %d, process id : %d, ", jobid, jpgid);
  return 0;
}
int get_job_index(int jobid) {
  int i = 0;
  for (; i < MAX_JOBS; i++) {
    if (job_list[i] -> jobid == jobid) {
      return i;
    }
  }
  return -1;
}
int job_pause(int jobid) {
  //masking required
  if (jobid < 0 || jobid > MAX_JOBS) {
    printf("Invalid JOBID\n");
    return -1;
  }
  int index = get_job_index(jobid);
  if (index == -1) {
    printf("No job with this jobid\n");
    return -1;
  }
  int jpgid = job_list[index] -> pgid;
  if (jpgid == -1) {
    printf("No job present with given ID\n");
    return -1;
  }
  if (job_list[index] -> status != RUNNING) {
    debug("job is not in running state");
    return -1;
  }
  debug("stoping job %d, process id : %d, ", jobid, jpgid);

  int res = killpg(jpgid, SIGSTOP);
  sf_job_pause(jobid, jpgid);

  if (res == -1) {
    debug("Error while stopping job %d", jobid);
    return -1;
  }
  active_runner--;
  update_status(jobid, PAUSED);
  debug("stopped job %d, process id : %d, ", jobid, jpgid);
  return 0;
}
int job_resume(int jobid) {
  // printf("job res act runner: %d\n",active_runner );
  if (active_runner >= MAX_RUNNERS) {
    return -1;
  }
  //masking required
  if (jobid < 0 || jobid > MAX_JOBS) {
    printf("Invalid JOBID\n");
    return -1;
  }
  int index = get_job_index(jobid);
  if (index == -1) {
    printf("No job with this jobid\n");
    return -1;
  }
  int jpgid = job_list[index] -> pgid;
  if (jpgid == -1) {
    printf("No job present with given ID\n");
    return -1;
  }
  debug("starting job %d, process id : %d, ", jobid, jpgid);
  if (job_list[index] -> status != PAUSED) {
    debug("job is not in paused state");
    return -1;
  }
  int res = killpg(jpgid, SIGCONT);
  sf_job_resume(jobid, jpgid);
  //--//block sigchild
  if (res == -1) {
    debug("Error while restarting job %d", jobid);
    return -1;
  }
  update_status(jobid, RUNNING);
  //--
  debug("started job %d, process id : %d, ", jobid, jpgid);
  return 0;
}
int job_get_pgid(int jobid) {
  int jpgid = -1;
  int i = 0;
  for (; i < MAX_JOBS; i++) {
    if (job_list[i] -> jobid == jobid) {
      jpgid = job_list[i] -> pgid;
      break;
    }
  }
  if (jpgid == -1) return -1;
  if (job_list[i] -> status == RUNNING || job_list[i] -> status == WAITING || job_list[i] -> status == PAUSED) {
    return jpgid;
  }
  return -1;
}
JOB_STATUS job_get_status(int _jobid) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> alloc == 1 && job_list[i] -> jobid == _jobid) {
      printf("job %d [%s]: ", job_list[i] -> jobid, job_status_names[job_list[i] -> status]);
      unparse_task(job_list[i] -> taskp, stdout);
      printf("\n");
      return job_list[i] -> status;
    }
  }
  return -1;
}
int job_get_result(int jobid) {
  int index = get_job_index(jobid);
  if (index == -1) {
    printf("job not present\n");
    return -1;
  }
  if (job_list[index] -> status != COMPLETED) {
    printf("job not yet completed\n");
    return -1;
  }
  return job_list[index] -> exit_status;
}
int job_was_canceled(int jobid) {
  int index = get_job_index(jobid);
  if (index == -1) {
    printf("no job present\n");
    return 0;
  }
  if (job_list[index] -> status != ABORTED) {
    return 0;
  }
  if (job_list[index] -> was_canceled != 1) {
    return 0;
  }
  return 1;
}
char * job_get_taskspec(int jobid) {
  int index = get_job_index(jobid);
  if (index == -1) {
    printf("no job present\n");
    return NULL;
  }
  if (job_list[index] -> alloc == 0) {
    return NULL;
  }
  return job_list[index] -> taskstr;

}
//===================================================

void display_help() {
  printf("Available commands:\nhelp (0 args) Print this help message\nquit (0 args) Quit the program\nenable (0 args) Allow jobs to start\ndisable (0 args) Prevent jobs from starting\nspool (1 args) Spool a new job\npause (1 args) Pause a running job\nresume (1 args) Resume a paused job\ncancel (1 args) Cancel an unfinished job\nexpunge (1 args) Expunge a finished job\nstatus (1 args) Print the status of a job\njobs (0 args) Print the status of all jobs\n");
}
int get_free_entry() {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> alloc == 0) return i;
  }
  return -1;
}
void update_status(int jobid, JOB_STATUS _status) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> jobid == jobid) {
      sf_job_status_change(job_list[i] -> jobid, job_list[i] -> status, _status);
      job_list[i] -> status = _status;
    }
  }
  return;
}
int runner(int jobid) {
  sigset_t mask_all, mask_one, prev_one;
  sigfillset( & mask_all);
  sigemptyset( & mask_one);
  sigaddset( & mask_one, SIGCHLD);
  struct sigaction sa;
  sa.sa_handler = & sigchld_handler;
  sigemptyset( & sa.sa_mask);
  sa.sa_flags = SA_RESTART;
  if (sigaction(SIGCHLD, & sa, 0) == -1) {
    perror(0);
    exit(1);
  }
  debug("runner before check\n");
  if (enable > 0) {
    debug("runner enable passed\n");
    sigprocmask(SIG_BLOCK, & mask_one, & prev_one);
    if (active_runner > MAX_RUNNERS) {
      done = 0;
    }
    if (active_runner <= MAX_RUNNERS) {
      debug("update state %d, %d, %d", jobid, job_list[jobid] -> status, RUNNING);
      int r_pid = Fork();
      if (r_pid == 0) {
        int runner_pid = getpid();
        setpgid(runner_pid, runner_pid);
        debug("runner c pgid %d ", runner_pid);
        //this is runner process code block
        sigprocmask(SIG_SETMASK, & prev_one, NULL);
        debug("running runner process\n");
        //int m_exit_status =
        master_runner(jobid);
        debug("master runner process exit status : %d\n", m_exit_status);
        exit(EXIT_SUCCESS);
      }
      sigprocmask(SIG_BLOCK, & mask_all, NULL);
      job_list[jobid] -> pgid = r_pid;
      debug("new job created with pgid %d and jobid %d", r_pid, jobid);
      debug("main process\n");

      active_runner++;
      debug("runner changed to : %d", active_runner);
      debug("update state %d, %d, %d", jobid, job_list[jobid] -> status, RUNNING);
      sf_job_start(jobid, r_pid);
      update_status(jobid, RUNNING);
    }
    sigprocmask(SIG_SETMASK, & prev_one, NULL);
  }
  return 0;
}
int master_runner(int jobid) {
  debug("master runner");
  int m_stat;
  int m_pid = Fork();
  if (m_pid == 0) {
    debug("master runner pgid %d ", getpgid(getpid()));
    //this is master runner process code
    //check number of pipelines, fork that many child SEQUENTIALLY.
    TASK * taskp = get_task(jobid);
    debug("before pipelines process\n");
    while ((taskp -> pipelines) != NULL) {

      int pp_stat;
      int pp_pid = Fork();
      // debug("new pipeline created with pgid %d and jobid %d",pp_pid, jobid);
      if (pp_pid == 0) {
        debug("pipeline runner pgid %d ", getpgid(getpid()));
        debug("pipeline process code\n");
        //this is the individual pipeline process code
        PIPELINE * pipeline = (taskp -> pipelines) -> first;
        COMMAND_LIST * commandlist = pipeline -> commands;
        int in = STDIN_FILENO;
        if (pipeline -> input_path != NULL) {
          debug("input path specified : %s", pipeline -> input_path);
          int fd = open(pipeline -> input_path, O_RDONLY);
          if (fd == -1) {
            exit(EXIT_FAILURE);
          } in = fd;
          if (dup2(fd, STDIN_FILENO) == -1) {
            exit(EXIT_FAILURE);
          }
        }
        char * fcmd = NULL;
        COMMAND * command = NULL;
        WORD_LIST * wordlist = NULL;
        while (commandlist -> rest != NULL) {
          debug("should not reach here!!!!\n");
          command = commandlist -> first;
          wordlist = command -> words;
          fcmd = wordlist -> first;
          int len = wordlist_len(wordlist);
          char * argstwo[len];
          convert_to_array(wordlist, argstwo);
          int fd[2];
          pid_t pid;
          if (pipe(fd) == -1)
            debug("pipe error");
          else if ((pid = fork()) == -1)
            debug("fork error");
          else if (pid == 0) {
            Close(fd[0]);
            int out = fd[1];
            if ( in != STDIN_FILENO) {
              if (dup2( in , STDIN_FILENO) != -1)
                Close( in );
              else
                exit(EXIT_FAILURE);
            }
            if (out != STDOUT_FILENO) {
              if (dup2(out, STDOUT_FILENO) != -1)
                Close(out);
              else
                exit(EXIT_FAILURE);
            }
            debug("command to run: %s %s %s", fcmd, argstwo[0], argstwo[1]);
            int prever = errno;
            int k = execvp(fcmd, argstwo);
            if (errno != 0) {
              errno = prever;
              exit(EXIT_FAILURE);
            }
            if (k < 0) {
              exit(EXIT_FAILURE);
            }
            debug("execvp");
          } else {
            if (pid > 0) {
              Close(fd[1]);
              Close( in ); in = fd[0];
            }
          }
          commandlist = commandlist -> rest;
          if (commandlist -> rest == NULL) break;
        }
        debug("reached for last command");
        int fdo = STDOUT_FILENO;
        if (pipeline -> output_path != NULL) {
          fdo = open(pipeline -> output_path, O_RDWR | O_CREAT, 0666);

          if (fdo == -1) {
            exit(EXIT_FAILURE);
          }
          debug("output path specified 1: %s", pipeline -> output_path);
          if (dup2(fdo, STDOUT_FILENO) == -1) {
            exit(EXIT_FAILURE);
          }

          Close(fdo);
          debug("after dup output path specified 2: %s", pipeline -> output_path);
        }
        if ( in != STDIN_FILENO) {
          if (dup2( in , STDIN_FILENO) != -1)
            Close( in );
          else
            exit(EXIT_FAILURE);
        }
        wordlist = commandlist -> first -> words;
        fcmd = wordlist -> first;
        int len = wordlist_len(wordlist);
        char * argstwo[len];
        convert_to_array(wordlist, argstwo);
        debug("cmd : %s, args: %s %s \n", fcmd, argstwo[0], argstwo[1]);
        int preverrno = errno;
        if (execvp(fcmd, argstwo) < 0) {
          debug("inside\n");
          exit(EXIT_FAILURE);
        }
        if (errno != 0) {
          errno = preverrno;
          exit(EXIT_FAILURE);
        }
        debug("execvp ran\n");
      }
      debug("this is error : %d\n", errno);
      pid_t pp_res = waitpid(pp_pid, & pp_stat, 0);
      if (pp_res == -1) {}
      debug("pipeline process master fininsed with status %d\n", WEXITSTATUS(pp_stat));
      if (WEXITSTATUS(pp_stat) == 1) {
        exit(EXIT_FAILURE);
      }
      taskp -> pipelines = (taskp -> pipelines) -> rest;
    }
    debug("all pipelines successfully completed\n");
    exit(EXIT_SUCCESS);
  }
  //runner process should wait for master process
  pid_t m_res = waitpid(m_pid, & m_stat, 0);
  if (WEXITSTATUS(m_stat) == 1) {
    exit(EXIT_FAILURE);
  }
  debug("master process exited with code: %d\n", m_res);
  return m_res;

}
void convert_to_array(WORD_LIST * wordlist, char * argstwo[]) {
  int i = 0;
  // printf("conversion to array started\n");
  while (wordlist != NULL) {
    // printf("here\n");
    argstwo[i] = wordlist -> first;
    i++;
    wordlist = wordlist -> rest;
  }
  argstwo[i] = NULL;
  // printf("convert to array done\n");
  return;
}
void unix_error(char * msg) {
  fprintf(stderr, "%s: %s\n", msg, strerror(errno));
  exit(0);
}
pid_t Fork(void) {
  pid_t pid;
  if ((pid = fork()) < 0)
    unix_error("Fork error");
  return pid;
}
int process(char * input) {
  if (is_init == 0) {
    int k = jobs_init();
    if (k == -1) {
      return -1;
    }
    is_init = 1;
  }
  while (isspace( * input)) {
    input++;
  }
  if ( * input == '\0') return -1;
  char * cmdp = input;
  while (( * input) != '\0' && !isspace( * input)) {
    input++;
  }
  char * taskstrp = NULL;
  if ( * input != '\0') {
    * input = '\0';
    input++;
    while (isspace( * input)) {
      input++;
    }
    if ( * input == '\'') {
      input++;
      if ( * input == '\0' || * input == '\'') return -1;
    }
    while (isspace( * input)) {
      input++;
    }
    taskstrp = input;
    if ( * input != '\0') {
      char * prev;
      prev = input;
      while ( * input != '\0') {
        prev = input;
        input++;
      }
      if ( * prev == '\'') * prev = '\0';
    }
  }

  if (strcmp(cmdp, "spool") == 0) {
    debug("spool called\n");
    sigset_t mask_all, prev_one;
    sigfillset( & mask_all);
    sigprocmask(SIG_BLOCK, & mask_all, NULL);
    int jobid = job_create(taskstrp);
    sigprocmask(SIG_SETMASK, & prev_one, NULL);
    if (jobid == -1) {
      debug("job table full");
      return -1;
    }
    // runner(jobid);
  } else if (strcmp(cmdp, "help") == 0) {
    display_help();
  } else if (strcmp(cmdp, "quit") == 0) {
    jobs_fini();
  } else if (strcmp(cmdp, "status") == 0) {
    int jobid_num = get_numeric_val(taskstrp);
    if (jobid_num == -1) {
      return -1;
    }
    job_get_status(jobid_num);
  } else if (strcmp(cmdp, "jobs") == 0) {
    for (int i = 0; i < MAX_JOBS; i++) {
      if (job_list[i] -> alloc == 1) {
        printf("job %d [%s]: ", job_list[i] -> jobid, job_status_names[job_list[i] -> status]);
        unparse_task(job_list[i] -> taskp, stdout);
        printf("\n");
      }
    }
  } else if (strcmp(cmdp, "enable") == 0) {
    jobs_set_enabled(1);
    debug("prev en status %d", prev);
  } else if (strcmp(cmdp, "disable") == 0) {
    jobs_set_enabled(0);
    debug("prev en/db status %d", prev);
    debug("%d\n", prev);
    //wait for running process to terminate and then. block further addition
  } else if (strcmp(cmdp, "pause") == 0) {
    int jobid = get_numeric_val(taskstrp);
    if (jobid == -1) {
      printf("Invlaid JOBID\n");
    }
    return job_pause(jobid);
  } else if (strcmp(cmdp, "resume") == 0) {
    int jobid = get_numeric_val(taskstrp);
    if (jobid == -1) {
      printf("Invlaid JOBID\n");
    }
    return job_resume(jobid);
  } else if (strcmp(cmdp, "cancel") == 0) {
    int jobid = get_numeric_val(taskstrp);
    if (jobid == -1) {
      printf("Invlaid JOBID\n");
    }
    return job_cancel(jobid);
  } else if (strcmp(cmdp, "expunge") == 0) {
    int jobid = get_numeric_val(taskstrp);
    if (jobid == -1) {
      printf("Invlaid JOBID\n");
    }
    return job_expunge(jobid);
  } else {
    printf("Unsupported Command\n");
  }
  return 0;
}

void attempt_run() {
  sigset_t mask_all, prev_one;
  sigfillset( & mask_all);
  sigprocmask(SIG_BLOCK, & mask_all, & prev_one);
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> status == WAITING) {
      runner(job_list[i] -> jobid);
    }
  }
  sigprocmask(SIG_SETMASK, & prev_one, NULL);
  return;
}
int get_numeric_val(char * taskstrp) {
  if (taskstrp == NULL) {
    return -1;
  }
  while (isspace( * taskstrp)) {
    taskstrp++;
  }
  if ( * taskstrp == '\0' || strlen(taskstrp) == 0) {
    return -1;
  }
  int n = 0;
  while ( * taskstrp != '\0' && !isspace( * taskstrp)) {
    char x = * taskstrp;
    int k = x - '0';
    if (k >= 0 && k <= 9) {
      n = 10 * n + k;
    } else {
      return -1;
    }
    taskstrp++;
  }
  while (isspace( * taskstrp)) {
    taskstrp++;
  }
  if ( * taskstrp != '\0') {
    return -1;
  }
  return n;
}
int wordlist_len(WORD_LIST * wordlist) {
  WORD_LIST * temp = wordlist;
  int len = 1;
  while (temp != 0) {
    temp = temp -> rest;
    len++;
  }
  // debug("len %d\n", len);
  return len;
}

job_entry * get_job_entrty_by_pid(int pid) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> alloc == 1) {
      debug("jobs pid %d , returned pid %d", job_list[i] -> pgid, pid);
      if (job_list[i] -> pgid == pid) {
        return job_list[i];
      }
    }
  }
  return NULL;
}
void sigchld_handler(int s) {
  int olderrno = errno;
  int status;
  int pid;
  debug("current process of sigchld_handler: %d", getpid());
  while ((pid = waitpid(-1, & status, WNOHANG)) > 0) {
    // pid = waitpid(-1, &status, 0);
    // if(pid == -1) continue;
    // printf("sigchld_handler child pid: %d, child exit status %d\n", pid, status);
    job_entry * job = get_job_entrty_by_pid(pid);
    if (job == NULL) {
      debug("pgid %d is not present ", pid);
    } else {
      sf_job_end(job -> jobid, job -> pgid, status);
      if (WIFEXITED(status)) {
        debug("update state %d, %d COMPLETED", job -> jobid, job_list[job -> jobid] -> status);
        update_status(job -> jobid, COMPLETED);
        job -> exit_status = status;
        // active_runner--;
        debug("runner changed to : %d", active_runner);
        debug("WEXITSTATUS : %d\n", WEXITSTATUS(status));
      } else if (WIFSIGNALED(status)) {
        debug("update state %d, %d, ABORTED ", job -> jobid, job_list[job -> jobid] -> status);
        update_status(job -> jobid, ABORTED);

        job -> exit_status = status;
        // active_runner--;
        debug("runner changed to : %d", active_runner);
        debug("WIFSIGNALED : %d\n", WIFSIGNALED(status));
      }
      active_runner--;
      // done=1;
      // printf("active runner : %d\n", active_runner);
      errno = olderrno;
    }
  }
  done = 1;
}
job_entry * get_job_entrty(int _jobid) {
  for (int i = 0; i < MAX_JOBS; i++) {
    if (job_list[i] -> jobid == _jobid) {
      return job_list[i];
    }
  }
  return NULL;
}
TASK * get_task(int jobid) {
  job_entry * tp = get_job_entrty(jobid);
  if (tp != NULL) {
    return tp -> taskp;
  } else {
    debug("reuturining null task\n");
    return NULL;
  }
}
void Close(int fd) {
  int r = close(fd);
  if (r == -1) {
    perror("error while closing file descriptor");
    exit(1);
  }
}
