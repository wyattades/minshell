/**
 * @file minshell.c
 * @author Wyatt Ades
 *
 * A simple implementation of a command shell
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <errno.h>
#include <signal.h>
#include <fcntl.h>

// Terminal text styles
#define RESET   "\033[0m"
#define BOLD    "\033[1m"
#define RED     "\033[31m"
#define GREEN   "\033[32m"
#define YELLOW  "\033[33m"
#define BLUE    "\033[34m"
#define MAGENTA "\033[35m"
#define CYAN    "\033[36m"
#define WHITE   "\033[37m"

// Shell name
#define SHELL "minshell"

// Running a command will return one of the following
#define CMD_SUCCESS 0
#define CMD_FAILURE 1
#define CMD_EXIT 2

// Helper for printing unexpected token parsing error. returns 0
#define PARSE_ERR(token) (fprintf(stderr, \
  SHELL ": syntax error near unexpected token `%s'\n", token) && 0)

extern char 
  **getline(int *), // Fetches user input as seperated arguments
  T_CD, T_EXIT; // Tokens for detecting built-in shell commands

/**
 * @see Cmd new_cmd()
 */
typedef struct Cmd {
  char **args;  /**< Command arguments */
  int argn;     /**< Length of command arguments */
  char **files_in; /**< Filenames for input redirect */
  char **files_out; /**< Filenames for output redirect */
} Cmd;

// Declare function prototypes
int parse(char **, Cmd **, int *);
int spawn(Cmd *, int, int);
int exec(Cmd *);
int cmd_exit(Cmd *);
int cmd_cd(Cmd *);
int exec_redirect(char **, char **, int);
int exec_pipe(Cmd **, int);
int exec_sequential(Cmd **, int);
void free_cmd(Cmd *);
Cmd *new_cmd(char **, int, char **);

/**
 * Loop continuously, fetching arguments from stdin and parsing
 * and executing them until an exit signal is read (either EOF or `exit')
 */
int main() {
  char **args;
  int i, cmdn, running = 1, status = 1;

  // getline() returns at most 63 arguments,
  // so there can be a max of 32 seperate commands
  Cmd *cmds[32];

  while (running) {

    // getline() sets `status' which tells us when to print a new shell prompt
    // e.g. not when commands are seperated by a `;'
    // Also, stdin must be coming from the terminal (user written)
    if (isatty(STDIN_FILENO) && status)
      printf(CYAN SHELL BOLD " $ " RESET);

    args = getline(&status); 

    // EOF reached, stop running
    if (status == -1) running = 0;

    // Parse the given args to create cmds and cmdn (number of cmds)
    if (parse(args, cmds, &cmdn) && cmdn > 0) {
      // If there are multiple commands, pipe them together
      // else execute the one
      // If the result is CMD_EXIT, stop running
      if ((cmdn > 1 ? exec_pipe(cmds, cmdn) : exec(cmds[0])) == CMD_EXIT)
        running = 0;
    }

    // Free all dynamically allocated memory
    for (i = 0; i < cmdn; i++) free_cmd(cmds[i]);
    for (i = 0; args[i] != NULL; i++){
      free(args[i]);
      args[i] = NULL;
  }
}
}

/**
 * Creates a command, which optionally includes file redirects in and/or out
 * 
 * @param args: the arguments in the command, may include a `<' or `>'
 * @param argn: number of arguments in command
 * @param err: pointer to error token if new_cmd returns NULL
 * @return a new instance of Cmd, or NULL if parsing arguments failed
 */
Cmd *new_cmd(char **args, int argn, char **err) {
  int i, _argn = argn, last_index = 0,
    in_n = 0, files_in[argn - 2],
    out_n = 0, files_out[argn - 2];
  size_t size;
  char *arg, last_symbol = 0;
  Cmd *cmd;

  // Iterate over args, check if there exists a `<' or `>' in a
  // valid position. If invalid or adjacent, set err message and return NULL
  for (i = 0; i < argn; i++) {
    arg = args[i];
    if (arg[1] == '\0' && (arg[0] == '<' || arg[0] == '>')) {
      if (
        i == argn - 1 || // cant be last element
        i - last_index == 0 // cant be adjacent to another redirect symbol
      ) {
        *err = arg;
        return NULL;
      }

      // Set the new length of args when we see the first redirect symbol
      if (_argn == argn) _argn = i;

      // Store this index and symbol so we know if the filenames following
      // the symbol are for input or output
      last_symbol = arg[0];
      last_index = i + 1;
    } else if (last_symbol != 0) {
      // Store index of file, whether it is an input or output
      if (last_symbol == '<') files_in[in_n++] = i;
      else files_out[out_n++] = i;
    }
  }

  // Allocate memory for new Cmd
  if ((cmd = (Cmd *) malloc(sizeof(Cmd))) == NULL) {
    fprintf(stderr, "fatal: memory allocation failed\n");
    exit(1);
  }

  // Store new argn
  cmd->argn = _argn;

  // Allocate memory for arrays of input and output stream files
  size = (in_n + 1) * sizeof(char **);
  if ((cmd->files_in = (char **) malloc(size)) == NULL) {
    fprintf(stderr, "fatal: memory allocation failed\n");
    exit(1);
  }
  size = (out_n + 1) * sizeof(char **);
  if ((cmd->files_out = (char **) malloc(size)) == NULL) {
    fprintf(stderr, "fatal: memory allocation failed\n");
    exit(1);
  }

  // Create NULL terminating arrays from the previously stored indicies
  for (i = 0; i < in_n; i++) cmd->files_in[i] = args[files_in[i]];
  cmd->files_in[in_n] = NULL;
  for (i = 0; i < out_n; i++) cmd->files_out[i] = args[files_out[i]];
  cmd->files_out[out_n] = NULL;

  // Allocate memory for new copy of args
  size = (cmd->argn + 1) * sizeof(char **);
  if ((cmd->args = (char **) malloc(size)) == NULL) {
    fprintf(stderr, "fatal: memory allocation failed\n");
    exit(1);
  }

  // Copy args, limiting size to argn arguments
  memcpy(cmd->args, args, size);

  // Set last element to NULL so execvp() knows when to stop reading
  cmd->args[cmd->argn] = NULL;

  return cmd;
}

/**
 * Free all dynamic memory allocated by cmd
 * 
 * @param cmd: the instance of cmd to free
 */
void free_cmd(Cmd *cmd) {
  if (cmd != NULL) {
    free(cmd->args);
    free(cmd->files_in);
    free(cmd->files_out);
    free(cmd);
    cmd = NULL;
  }
}

/**
 * Parse the given args and create new commands (cmds) and 
 * a command count (cmdn). 
 * 
 * @param args: the arguments to parse, may include words or `|'
 * @param cmds: array of commands to add to
 * @param cmdn: pointer to number of commands
 * @return 1 on parse success, 0 on parse error
 */
int parse(char **args, Cmd **cmds, int *cmdn) {
  int i, last_cmd = 0;
  char *arg, argn, *err;

  // Reset cmdn
  *cmdn = 0;

  // No args
  if (*args == NULL) return 0;

  // Iterate over args and create a new Cmd at every encounter with `|'
  for (i = 0; (arg = args[i]) != NULL; i++) {
    if (arg[1] == '\0' && arg[0] == '|') { // arg = "|"
      
      // Empty command i.e. two adjacent pipes
      if ((argn = i - last_cmd) == 0) 
        return PARSE_ERR("|");

      if ((cmds[*cmdn] = new_cmd(args + last_cmd, argn, &err)) == NULL)
        return PARSE_ERR(err);

      // Only update cmdn AFTER affirming that new_cmd() succeeds
      (*cmdn)++;
      // Save location of last pipe
      last_cmd = i + 1;
    }
  }

  // Add a new Cmd after the last `|' in args
  if ((argn = i - last_cmd) > 0) {
    if ((cmds[*cmdn] = new_cmd(args + last_cmd, argn, &err)) == NULL)
      return PARSE_ERR(err);
    (*cmdn)++;
  } else return PARSE_ERR("|"); // pipe can't be last element in args

  return 1; // success
}

/**
 * Executes a command by spawning a child process to execute it,
 * or running it in this process if it is a built-in command
 * 
 * @param cmd: the command to run
 * @param file_in: file descriptor of input stream for new process
 * @param file_out: file descritor of output stream for new process
 * @return execution status exit, success, or failure
 */
int spawn(Cmd *cmd, int file_in, int file_out) {
  pid_t pid;
  char *arg0 = cmd->args[0], *file;
  int i;

  // Handle custom tokens as built-in shell commands
  // `exit' and `cd' do not use stdin or stdout, so we don't need to 
  // handle IO redirects or piping
  if (arg0[0] == '\n') { // custom tokens are precedeed by newline
    if (arg0[1] == T_EXIT) return cmd_exit(cmd);
    else if (arg0[1] == T_CD) return cmd_cd(cmd);
  }

  // Create a new child process which sets its input and output 
  // streams and executes a command
  if ((pid = fork()) == 0) {

    // Open files for redirect in. Only the last file descriptor will
    // be used as the process' input stream
    for (i = 0; (file = cmd->files_in[i]) != NULL; i++) {
      if ((file_in = open(file, O_RDONLY)) == -1) {
        fprintf(stderr, SHELL ": %s: %s\n", file, strerror(errno));
        exit(1);
      }
    }
    // Open files for redirect out. Only the last file descriptor will
    // be used as the process' output stream
    for (i = 0; (file = cmd->files_out[i]) != NULL; i++) {
      // 0666 = File permission -rw-rw-rw-
      if ((file_out = open(file, O_WRONLY|O_CREAT|O_TRUNC, 0666)) == -1) {
        fprintf(stderr, SHELL ": %s: %s\n", file, strerror(errno));
        exit(1);
      }
    }

    // Set process input stream
    if (file_in != STDIN_FILENO) {
      if (dup2(file_in, STDIN_FILENO) == -1) {
        perror(SHELL ": process dup error");
        exit(1);
      }
      close(file_in);
    }

    // Set process output stream
    if (file_out != STDOUT_FILENO) {
      if (dup2(file_out, STDOUT_FILENO) == -1) {
        perror(SHELL ": process dup error");
        exit(1);
      }
      close(file_out);
    }

    // Execute command
    execvp(arg0, cmd->args);

    // If execvp ever returns, an error has occurred
    perror(arg0);
    exit(1);
  } else if (pid == -1) { // handle fork error
    perror(SHELL ": process spawn error");
    return CMD_FAILURE;
  }

  return CMD_SUCCESS;
}

/**
 * Executes a single command by spawning a new process and
 * waiting for its result
 * 
 * @param cmd: command to execute
 * @return execution status exit, success, or failure
 */
int exec(Cmd *cmd) {
  int status = 0;

  // If command was exit, return immediately
  if (spawn(cmd, STDIN_FILENO, STDOUT_FILENO) == CMD_EXIT)
    return CMD_EXIT;

  while(wait(&status) > 0);
  
  return status == 0 ? CMD_SUCCESS : CMD_FAILURE;
}

/**
 * Executes a chain of commands to be piped and waits for the
 * their result
 * 
 * @param cmds: array of commands to chain pipe
 * @param cmdn: number of commands
 * @return execution status exit, success, or failure
 */
int exec_pipe(Cmd **cmds, int cmdn) {
  int i, pipefd[2], file_in, status = 0;

  // First input file is stdin
  file_in = STDIN_FILENO;

  for (i = 0; i < cmdn - 1; i++) {

    // Pipe between every command
    if (pipe(pipefd) == -1) {
      perror(SHELL ": pipe error");
      break;
    }

    if (spawn(cmds[i], file_in, pipefd[1]) == CMD_EXIT)
      return CMD_EXIT;

    // Release end of pipe
    close(pipefd[1]);

    // Set the next command's file_in to start of pipe
    file_in = pipefd[0];

  }

  // Last output file is stdout
  if (spawn(cmds[i], file_in, STDOUT_FILENO) == CMD_EXIT)
    return CMD_EXIT;
 
  while(wait(&status) > 0);

  return status == 0 ? CMD_SUCCESS : CMD_FAILURE;
}

/**
 * Handler for built-in command: `exit'
 * Signals program to stop execution
 * 
 * @param cmd
 * @return execution status exit
 */
int cmd_exit(Cmd *cmd) {
  return CMD_EXIT;
}

/**
 * Handler for built-in command: `cd'
 * Changes directory
 * 
 *   Usage: cd [path]
 *   if there are multiple arguments, fail
 *   else if path is a valid path, chdir(path)
 *   else if path is `~' or empty, try to go to user $HOME
 * 
 * @param cmd: contains arguments for `cd'
 * @return execution status success or failure
 */
int cmd_cd(Cmd *cmd) {
  char *path;

  // If no args provided, try to go HOME
  if ((path = cmd->args[1]) == NULL) {
    path = "~";
  // Multiple arguments is invalid
  } else if (cmd->argn > 2) {
    fprintf(stderr, "cd: too many arguments\n");
    return CMD_FAILURE;
  }

  // Handle tilde as argument
  if (path[0] == '~') {
    if (path[1] == '\0') { // path = "~"
      if ((path = getenv("HOME")) == NULL) {
        fprintf(stderr, "cd: $HOME env variable is invalid\n");
        return CMD_FAILURE;
      }
    } else { // path = "~..."
      fprintf(stderr, "cd: tilde expansion is not supported\n");
      return CMD_FAILURE;
    }
  }

  // Attempt to change directory
  if (chdir(path) != 0) {
    fprintf(stderr, "cd: %s: %s\n", path, strerror(errno));
    return CMD_FAILURE;
  }

  return CMD_SUCCESS;
}
