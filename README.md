# minshell
A minimalist implementation of a shell (command line interpreter)

## Installation

Run `$ make` to create the `minshell` binary
Run `$ ./minshell` to run the shell

## Usage

minshell can run UNIX commands as any shell would, but only supports 2 built-in commands: `cd` and `exit`.
The only special characters supported are: 
- `;` - separating multiple commands
- `<` - stdin file redirect
- `>` - stdout file redirect
- `|` - piping (multiple commands being piped is supported)

Any combination of the above special characters is supported (compatible with BASH), and all other special characters are ignored.

## Concepts

The basic idea of minshell is to repeatedly read, parse, and execute input in the same fashion as a normal shell. The program will continue running until it reads an 'exit' command, EOF, or forcibly exits with an exit signal.

The program parses raw command-line inputs using Flex (`lex.l`) and passes the parsed arguments to `minshell.c` to run as forked child processes.

See `minshell.c` and `lex.l` for sourcecode and documentation.
