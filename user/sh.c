#include "lib.h"
#include <args.h>

int debug = 0;

//
// get the next token from string s
// set *p1 to the beginning of the token and
// *p2 just past the token.
// return:
//	0 for end-of-string
//	> for >
//	| for |
//	w for a word
//
// eventually (once we parse the space where the nul will go),
// words get nul-terminated.
#define WHITESPACE " \t\r\n"
#define SYMBOLS "<|>&;()"

int
_gettoken(char *s, char **p1, char **p2)
{
	int t;

	if (s == 0) {
		//if (debug > 1) writef("GETTOKEN NULL\n");
		return 0;
	}

	//if (debug > 1) writef("GETTOKEN: %s\n", s);

	*p1 = 0;
	*p2 = 0;

	while(strchr(WHITESPACE, *s))
		*s++ = 0;
	if(*s == 0) {
	//	if (debug > 1) writef("EOL\n");
		return 0;
	}
	if(strchr(SYMBOLS, *s)){
		t = *s;
		*p1 = s;
		*s++ = 0;
		*p2 = s;
//		if (debug > 1) writef("TOK %c\n", t);
		return t;
	}
	*p1 = s;
	while(*s && !strchr(WHITESPACE SYMBOLS, *s))
		s++;
	*p2 = s;
	if (debug > 1) {
		t = **p2;
		**p2 = 0;
//		writef("WORD: %s\n", *p1);
		**p2 = t;
	}
	return 'w';
}

int
gettoken(char *s, char **p1)
{
	static int c, nc;
	static char *np1, *np2;

	if (s) {
		nc = _gettoken(s, &np1, &np2);
		return 0;
	}
	c = nc;
	*p1 = np1;
	nc = _gettoken(np2, &np1, &np2);
	return c;
}

#define MAXARGS 16

void
runcmd(char *s)
{
	char *argv[MAXARGS], *t;
	int argc, c, i, r, p[2], fd, rightpipe;
	int fdnum;
//	writef(" got into runcmd\n");
	rightpipe = 0;
	gettoken(s, 0);
	again:
	argc = 0;
	for(;;){
		c = gettoken(0, &t);
		switch(c){
			case 0:
				goto runit;
			case 'w':
				if(argc == MAXARGS){
					writef("too many arguments\n");
					exit();
				}
				argv[argc++] = t;
				break;
			case '<':
				if(gettoken(0, &t) != 'w'){
					writef("syntax error: < not followed by word\n");
					exit();
				}
				fdnum = open(t, O_RDONLY);
                if (fdnum < 0) {
                    writef("[ERR] sh.c : runcmd : case < : open : err%d\n", fdnum);
                    exit();
                } else if (fdnum == 0) {
                    writef("[ERR] sh.c : runcmd : case < : open : got a zero fdnum\n");
                    exit();
                }
				r = dup(fdnum, 0);
                if (r < 0) {
                    writef("[ERR] sh.c : runcmd : case < : dup : err%d\n", r);
                    exit();
                }
				close(fdnum);
				break;
			case '>':
                if(gettoken(0, &t) != 'w'){
                    writef("syntax error: < not followed by word\n");
                    exit();
                }
                fdnum = open(t, O_WRONLY);
                if (fdnum < 0) {
                    writef("[ERR] sh.c : runcmd : case > : open : err%d\n", fdnum);
                    exit();
                } else if (fdnum == 1) {
                    writef("[ERR] sh.c : runcmd : case > : open : got a `1` fdnum\n");
                    exit();
                }
                r = dup(fdnum, 1);
                if (r < 0) {
                    writef("[ERR] sh.c : runcmd : case > : dup : err%d\n", r);
                    exit();
                }
                close(fdnum);
				break;
			case '|':
                r = pipe(p);
                if (r < 0) {
                    writef("[ERR] sh.c : runcmd : case | : pipe : err%d\n", r);
                    exit();
                }
                rightpipe = fork();
                if (rightpipe < 0) {
                    writef("[ERR] sh.c : runcmd : case | : fork : err%d\n", r);
                    exit();
                }

                if (rightpipe == 0) {
                    r = dup(p[0], 0);
                    if (r < 0) {
                        writef("[ERR] sh.c : runcmd : case | : child : dup : err%d\n", r);
                        exit();
                    }
                    close(p[0]);
                    close(p[1]);
                    goto again;
                } else {
                    r = dup(p[1], 1);
                    if (r < 0) {
                        writef("[ERR] sh.c : runcmd : case | : parent : dup : err%d\n", r);
                        exit();
                    }
                    close(p[0]);
                    close(p[1]);
                    goto runit;
                }
				break;
		}
	}

	runit:
	if(argc == 0) {
		return;
	}
	argv[argc] = 0;
	if ((r = spawn(argv[0], argv)) < 0)
		writef("spawn %s: %e\n", argv[0], r);
	close_all();
	if (r >= 0) {
		wait(r);
	}
	if (rightpipe) {
		wait(rightpipe);
	}
	exit();
}


void
readline(char *buf, u_int n)
{
	int i, r;

	r = 0;
	for(i=0; i<n; i++){
		if((r = read(0, buf+i, 1)) != 1){
			if(r < 0)
				writef("read error: %e", r);
			exit();
		}
		if(buf[i] == '\b'){
			if(i > 0)
				i -= 2;
			else
				i = 0;
		}
		if(buf[i] == '\r'){
			buf[i] = 0;
			return;
		}
	}
	writef("line too long\n");
	while((r = read(0, buf, 1)) == 1 && buf[0] != '\n')
		;
	buf[0] = 0;
}	

char buf[1024];

void
usage(void)
{
	writef("usage: sh [-dix] [command-file]\n");
	exit();
}

void
umain(int argc, char **argv)
{
//	static int shnum = 0;
	int r, interactive, echocmds;
	interactive = '?';
	echocmds = 0;
	writef("\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	writef("::                                                         ::\n");
	writef("::              Super Shell  V0.0.0_1                      ::\n");
	writef("::                                                         ::\n");
	writef(":::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n");
	ARGBEGIN{
	case 'd':
		debug++;
		break;
	case 'i':
		interactive = 1;
		break;
	case 'x':
		echocmds = 1;
		break;
	default:
		usage();
	}ARGEND

	if(argc > 1)
		usage();
	if(argc == 1){
		close(0);
		if ((r = open(argv[1], O_RDONLY)) < 0)
			user_panic("open %s: %e", r);
		user_assert(r==0);
	}
	if(interactive == '?')
		interactive = iscons(0);
	for(;;){
		if (interactive)
			fwritef(1, "\n$ ");
		readline(buf, sizeof buf);
		
		if (buf[0] == '#')
			continue;
		if (echocmds)
			fwritef(1, "# %s\n", buf);
		//writef("haha\n");
		if ((r = fork()) < 0)
			user_panic("fork: %e", r);
//		writef("r = %d",r);
		if (r == 0) {
		//	spawnl("/init", "init", "initarg1", "initarg2", (char*)0);
			runcmd(buf);
			exit();
			return;
		} else
			wait(r);
	//	sys_yield();
	}
}

