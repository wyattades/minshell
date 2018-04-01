%{
// Maximum number of args
#define _NUMARGS 64
// helper function to add a token when encountering a built-in command
#define _ADD_TOK(T) _tok[1] = T; _add_arg(_tok)

char *_args[_NUMARGS];
int _argcount = 0, *_status;
char _tok[3] = "\n ";
const char // built-in commands
  T_CD = 'c',
  T_EXIT = 'e';

void _add_arg(char *_text) {
  if (_argcount < _NUMARGS-1) {
    _args[_argcount++] = (char *)strdup(_text);
    _args[_argcount] = NULL;
  }
}
%}
WORD [~a-zA-Z0-9\/\.-]+
SPECIAL [><|]
%%
  _argcount = 0; _args[0] = NULL;
"||" /* ignore */
exit _ADD_TOK(T_EXIT);
cd _ADD_TOK(T_CD);
{WORD}|{SPECIAL} _add_arg(yytext);
[\n]   *_status = 1; return (int)_args;
[;] *_status = 0; return (int)_args;
<<EOF>> *_status = -1; return (int)_args;
[ \t]+
.
%%
char **getline(int *status) {
  _status = status;
  return (char **) yylex();
}
