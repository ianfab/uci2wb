/****************************************************************************/
/*                           UCI2WB by H.G.Muller                           */
/*                                                                          */
/* UCI2WB is an adapter to run engines that communicate in various dialects */
/* of the Universal Chess Interface in a GUI that supports XBoard protocol  */
/* (CECP). It supports UCI (when used for Xiangqi: the 'Cyclone dialect'),  */
/* as well as USI and UCCI when used with the flags -s or -x, respectively. */
/* This version of UCI2WB is released under the GNU General Public License, */
/* of which you should have received a copy together with this file.        */
/****************************************************************************/

#define VERSION "3.0"

#include <stdio.h>
#include <stdlib.h>
#ifdef WIN32
#  include <windows.h>
#  include <io.h>
   HANDLE process;
   DWORD thread_id;
#else
#  include <pthread.h>
#  include <signal.h>
#  define NO_ERROR 0
#  include <sys/time.h>
   int GetTickCount() // with thanks to Tord
   { struct timeval t; gettimeofday(&t, NULL); return t.tv_sec*1000 + t.tv_usec/1000; }
#endif
#include <fcntl.h>
#include <string.h>

// Set VARIANTS for in WinBoard variant feature. (With -s option this will always be reset to use "shogi".)
#define VARIANTS ",normal,xiangqi"
#define STDVARS "chess,chess960,crazyhouse,3check,giveaway,suicide,losers,atomic,seirawan,shogi,xiangqi"
#define EGT ",gaviotaTbPath,syzygyPath,nalimovPath,robbotripleBaseDirectory,robbototalBaseDirectory,bitbases path,"

#define DPRINT if(debug) printf
#define EPRINT(X) { char f[999]; sprintf X; DPRINT("%s", f); fprintf(toE, "%s", f + 2*(*f == '#')); /* strip optional # prefix */ }

#define WHITE 0
#define BLACK 1
#define NONE  2
#define ANALYZE 3

char move[2000][10], iniPos[256], hashOpt[20], pause, pondering, suspended, ponder, post, hasHash, c, sc='c', suffix[81], varOpt, searching, *binary;
int mps, tc, inc, sTime, depth, myTime, hisTime, stm, computer = NONE, memory, oldMem=0, cores, moveNr, lastDepth, lastScore, startTime, debug, flob;
int statDepth, statScore, statNodes, statTime, currNr, size, collect, nr, sm, inex, on[500], frc, byo = -1, namOpt, comp;
char currMove[20], moveMap[500][10], /* for analyze mode */ canPonder[20], threadOpt[20], varList[8000], anaOpt[20], backLog[10000], checkOptions[8192] = "Ponder";
char pvs[99][999], board[100];  // XQ board for UCCI
char *nameWord = "name ", *valueWord = "value ", *wTime = "w", *bTime = "b", *wInc = "winc", *bInc = "binc", newGame; // keywords that differ in UCCI
int unit = 1, drawOffer, scores[99], mpvSP, maxDepth;
volatile int logLen, sentLen;

FILE *toE, *fromE, *fromF;
int pid;

char *strcasestr (char *p, char *q) { while(*p) { char *r=p++, *s=q; while(tolower(*r++) == tolower(*s) && *s) s++; if(!*s) return p-1; } return NULL; }

#ifdef WIN32
WinPipe(HANDLE *hRd, HANDLE *hWr)
{
  SECURITY_ATTRIBUTES saAttr;

  /* Set the bInheritHandle flag so pipe handles are inherited. */
  saAttr.nLength = sizeof(SECURITY_ATTRIBUTES);
  saAttr.bInheritHandle = TRUE;
  saAttr.lpSecurityDescriptor = NULL;

  /* Create a pipe */
  return CreatePipe(hRd, hWr, &saAttr, 0);
}
#endif

#define INIT 0
#define WAKEUP 1
#define PAUSE 2

void
Sync (int action)
{
#ifdef WIN32
	static HANDLE hWr, hRd; DWORD d; char c;
	switch(action) {
	    case INIT:   WinPipe(&hRd, &hWr); break;
	    case WAKEUP: WriteFile(hWr, "\n", 1, &d, NULL); break;
	    case PAUSE:  ReadFile(hRd, &c, 1, &d, NULL);
	}
#else
	static int syncPipe[2];	char c;
	switch(action) {
	    case INIT:   pipe(syncPipe); break;
	    case WAKEUP: write(syncPipe[1], "\n", 1); break;
	    case PAUSE:  read(syncPipe[0], &c, 1);
	}
#endif
}

void
FromFEN(char *fen)
{	int i=0;
	while(*fen) {
	    char c = *fen++;
	    if(c >= 'A') board[i++] = c; else
	    if(c == '/') i++; else
	    if(c == ' ') break; else
	    while(c-- > '0' && i < 99) board[i++] = 0;
	    if(i >= 99) break;
	}
}

char *
ToFEN(int stm)
{
	int i, n=0; static char fen[200]; char *p = fen;
	for(i=0; i<99; i++) {
	    char c = board[i];
	    if(c >= 'A')  { if(n) *p++ = '0' + n; n = 0;  *p++ = c; } else n ++;
	    if(i%10 == 8) { if(n) *p++ = '0' + n; n = -1; *p++ = '/'; }
	}
	sprintf(p-1, " %c - - 0 1", stm);
	return fen;
}

int
Sqr(char *m, int j)
{
	int n = m[j] - 'a' + 10*('9' - m[j+1]);
	if(n < 0) n = 0; else if(n > 99) n = 99; return n;
}

int
Play(int nr)
{
	int i, last = -1;
	FromFEN(iniPos + 4); // in XQ iniPos always has just "fen " prefix
	for(i=0; i<nr; i++) {
	    int from=Sqr(move[i], 0), to=Sqr(move[i], 2);
	    if(board[to] || (board[from]|32)  == 'p' && move[i][1] != move[i][3]) last = i;
	    board[to] = board[from]; board[from] = 0;
	}
	return last;
}

void
StartSearch(char *ponder)
{	// send the 'go' command to engine. Suffix by ponder.
	int x = (ponder[0] != 0);                   // during ponder stm is the opponent
	int black = (stm == BLACK ^ x ^ sc == 's'); // set if our color is what the engine calls black
	int nr = moveNr + x;                        // we ponder for one move ahead!
	int t = (flob ? inc + myTime/40 : 1000*byo*(byo>0)); // byoyomi time 
	if(sc == 'x') black = 1; else drawOffer = 0;// in UCCI 'black' refers to us and 'white' to opponent
	if(!x && drawOffer) ponder = " draw", drawOffer = 0; //pass draw offer only when not pondering
	EPRINT((f, "# go%s %stime %d %stime %d", ponder, bTime, (black ? myTime : hisTime) - t, wTime, (!black ? myTime : hisTime) - t))
	if(sTime > 0) EPRINT((f, " movetime %d", sTime)) else
	if(mps) EPRINT((f, " movestogo %d", mps*(nr/(2*mps)+1)-nr/2))
	if(flob || byo >= 0) sprintf(suffix, " byoyomi %d", t); // for engines running purely on byoyomi
	if(inc && !*suffix) EPRINT((f, " %s %d %s %d", wInc, inc, bInc, inc))
	if(depth > 0) EPRINT((f, " depth %d", depth))
        if(*suffix) EPRINT((f, suffix, inc))
	EPRINT((f, "\n")); maxDepth = mpvSP = 0;
}

void
StopPonder(int pondering)
{
	if(!pondering) return;
	pause = 1;
	EPRINT((f, "# stop\n")) fflush(toE); // note: 'pondering' remains set until engine acknowledges 'stop' with 'bestmove'
	Sync(PAUSE); // wait for engine to acknowledge 'stop' with 'bestmove'.
}

void
LoadPos(int moveNr)
{
	int j, lastCapt = 0; char *pos = iniPos, buf[200], stm;
	if(sc == 'x') { // UCCI: send only reversible moves
	    lastCapt = Play(moveNr); // find last capture (returns -1 if none!)
	    Play(++lastCapt);        // reconstruct board after last capture
	    stm = (!strstr(iniPos+4, " b ") ^ lastCapt & 1 ? 'w' :  'b');
	    sprintf(buf, "position fen %s", ToFEN(stm)); pos = buf; // send it as FEN (with "position" in UCCI!)
	}
	EPRINT((f, "# %s moves", pos))
	for(j=lastCapt; j<moveNr; j++) EPRINT((f, " %s", move[j]))
	EPRINT((f, "\n"))
}

void
StartPonder()
{
	if(!move[moveNr][0]) return; // no ponder move
	LoadPos(moveNr+1);
	pondering = 1; lastDepth = 1;
	StartSearch(" ponder");
}

void
Analyze(char *val)
{
    if(*anaOpt) EPRINT((f, "# setoption %s%s %s%s\n", nameWord, anaOpt, valueWord, val));
}

int
Release()
{   // send setoption commands backlogged during thinking to engine, aborting ponder or analysis search if necessary
    int len = logLen - sentLen, analyse = searching;
    if(len <= 0) return 0;
    StopPonder(pondering | searching); pondering = searching = 0; // force new search if settings change during analysis (multi-PV!)
    fwrite(backLog + sentLen, 1, len, toE); sentLen += len; DPRINT("# release %d\n", len);
    if(ponder && computer == 1 - stm) StartPonder(); // (re)start ponder search
    return analyse; // return 1 if analysis search should be restarted
}

char *Convert(char *pv)
{   // convert Shogi coordinates to WB
    char *p, *q, c;
    static char buf[10000];
    if(sc != 's') return pv;
    p = pv; q = buf;
    while(c = *p++) {
        if(c >= '0' && c <= '9' || c >= 'a' && c <= 'z') *q++ = 'a'+'0'+size - c; else *q++ = c;
    }
    *q++ = 0;
    return buf;
}

void
Move4GUI(char *m)
{
    if(sc == 's') {
      // convert USI move to WB format
      m[2] = 'a'+'0'+size - m[2];
      m[3] = 'a'+'0'+size - m[3];
      if(m[1] == '*') { // drop
	m[1] = '@';
      } else {
	m[0] = 'a'+'0'+size - m[0];
	m[1] = 'a'+'0'+size - m[1];
	if((stm == WHITE ? (m[1]>'0'+size-size/3 || m[3]>'0'+size-size/3)
                                : (m[1] <= '0'+size/3 || m[3] <= '0'+size/3)) && m[4] != '+')
	     m[4] = '=', m[5] = 0;
      }
    }
}

int
GetChar()
{
    int c;
    if(fromF) {
	if((c = fgetc(fromF)) != EOF) return c;
	fclose(fromF); fromF = 0; printf("# end fake\n");
    }
    return fgetc(fromE);
}

void *
Engine2GUI()
{
    char line[1024], command[512]; static char egts[999];

    if(fromF = fopen("DefectiveEngineOptions.ini", "r")) printf("# fake engine input\n");
    while(1) {
	int i=0, x; char *p, dummy, len;

	fflush(stdout); fflush(toE);
	while((line[i] = x = GetChar()) != EOF && line[i] != '\n') i++;
	line[++i] = 0;
	if(x == EOF) printf("tellusererror UCI2WB: %s died on me\n", binary), exit(0);
	DPRINT("# engine said: %s", line), fflush(stdout);
	if(sscanf(line, "%s", command) != 1) continue;
	if(!strcmp(command, "bestmove")) {
	    if(pause == 1) { pondering = pause = 0; Sync(WAKEUP); continue; } // bestmove was reply to ponder miss or analysis result; ignore.
	    else if(pondering) { pondering = 0; printf("%d 0 0 0 UCI violation! Engine moves during ponder\n", lastDepth+1); continue; } // ignore ponder search
	    // move was a move to be played
	    if(p = strstr(line+8, " draw")) *p = 0, printf("offer draw\n"); // UCCI
	    if(strstr(line+9, "resign")) { printf("resign\n"); computer = NONE; }
	    if(strstr(line+9, "win")) { printf("%s {claim}\n", stm== WHITE ? "1-0" :"0-1"); computer = NONE; } // USI
	    if(strstr(line+9, "(none)") || strstr(line+9, "null") ||
	       strstr(line+9, "0000")) { printf("%s\n", lastScore < -99999 ? "resign" : "1/2-1/2 {stalemate}"); computer = NONE; }
	    sscanf(line, "bestmove %s", move[moveNr++]);
	    Release(); // send setoption commands that arrived during search
	    myTime -= (GetTickCount() - startTime)*1.02 + inc; // update own clock, so we can give correct wtime, btime with ponder
	    if(mps && ((moveNr+1)/2) % mps == 0) myTime += tc; if(sTime) myTime = sTime; // new session or move starts
	    stm = WHITE+BLACK - stm;
	    // first start a new ponder search, if pondering is on and we have a move to ponder on
	    if(p = strstr(line+9, "ponder")) {
	      sscanf(p+7, "%s", move[moveNr]);
	      if(computer != NONE && ponder) {
		DPRINT("# ponder on %s\n", move[moveNr]);
		StartPonder();
	      }
	      p[-1] = '\n'; *p = 0; // strip off ponder move
	    } else move[moveNr][0] = 0;
	    Move4GUI(line+9);
	    printf("move %s\n", line+9); // send move to GUI
            if(move[moveNr][0]) printf("Hint: %s\n", move[moveNr]);
	    if(pause) { pause = 0; Sync(WAKEUP); } // release commands that came in during think
	    if(lastScore == 100001 && iniPos[0] != 'f') { printf("%s {mate}\n", stm == BLACK ? "1-0" : "0-1"); computer = NONE; }
	}
	else if(!strcmp(command, "info")) {
	    int d=0, s=0, t=(GetTickCount() - startTime)/10, n=1;
	    char *pv, varName[80];
	    if(sscanf(line+5, "string times @ %c", &dummy) == 1) { printf("# %s", line+12); continue; }
	    if(sscanf(line+5, "string variant %s", varName) == 1) {
		if(!strstr(STDVARS, varName)) {
		    int files = 8, ranks = 8, hand = 0; char parent[80];
		    if(p = strstr(line+18, " files ")) sscanf(p+7, "%d", &files);
		    if(p = strstr(line+18, " ranks ")) sscanf(p+7, "%d", &ranks);
		    if(p = strstr(line+18, " pocket ")) sscanf(p+8, "%d", &hand);
		    if(p = strstr(line+18, " template ")) sscanf(p+10, "%s", parent); else strcpy(parent, "fairy");
		    if(p = strstr(line+18, " startpos "))
			printf("setup (-) %dx%d+%d_%s %s", files, ranks, hand, parent, p+10);
		}
		continue;
	    }
	    if(collect && (pv = strstr(line+5, "currmove "))) {
		if(p = strstr(line+5, "currmovenumber ")) {
		    n = atoi(p+15);
		    if(collect == 1 && n != 1) continue; // wait for move 1
		    if(collect + (n == 1) > 2) { // done collecting
			if(inex && collect == 2) printf("%d 0 0 0 OK to exclude\n", lastDepth);
			collect = 3; continue;
		    }
		    collect = 2; on[nr=n] = 1; sscanf(pv+9, "%s", moveMap[n]); continue; // store move
		}
	    }
	    if(!post) continue;
	    if(sscanf(line+5, "string %c", &dummy) == 1) printf("%d 0 0 0 %s", lastDepth, line+12); else {
		if(p = strstr(line+4, " depth "))      sscanf(p+7, "%d", &d), statDepth = d;
		if(p = strstr(line+4, " score cp "))   sscanf(p+10, "%d", &s), statScore = s; else
		if(p = strstr(line+4, " score mate ")) sscanf(p+12, "%d", &s), s += s>0 ? 100000 : -100000, statScore = s; else
		if(p = strstr(line+4, " score "))      sscanf(p+7, "%d", &s), statScore = s;
		if(p = strstr(line+4, " nodes "))      sscanf(p+7, "%d", &n), statNodes = n;
		if(p = strstr(line+4, " time "))       sscanf(p+6, "%d", &t), t /= 10, statTime = t;
		if(p = strstr(line+4, " currmove "))   sscanf(p+10,"%s", currMove);
		if(p = strstr(line+4, " currmovenumber ")) sscanf(p+16,"%d", &currNr);
		if(pv = strstr(line+4, " pv ")) { // convert PV info to WB thinking output
		  if(d > maxDepth) maxDepth = d, mpvSP = 0; else if(d < maxDepth) continue; // ignore depth regressions
		  if(p = strstr(line+4, " upperbound ")) strcat(p, "?\n"); else
		  if(p = strstr(line+4, " lowerbound ")) strcat(p, "!\n");
		  for(i=0; i<mpvSP; i++) if(s == scores[i] && !strcmp(pvs[i], pv+4)) break; // check if duplicat
		  if(i >= mpvSP) strncpy(pvs[mpvSP], pv+4, 998), scores[mpvSP++] = s,       // emit as thinking output if not
		    printf("%3d  %6d %6d %10d %s", lastDepth=d, lastScore=s, t, n, Convert(pv+4));
		} else if(s == -100000) lastScore = s; // when checkmated score is valid even without PV (which might not come)
	    }
	}
	else if(!strcmp(command, "option")) { // USI option: extract data fields
	    char name[80], type[80], buf[1024], val[256], *q;
	    int min=0, max=1e9; *val = 0;
	    if(p = strstr(line+6, " type ")) sscanf(p+1, "type %s", type), *p = '\n';
	    if(p = strstr(line+6, " min "))  sscanf(p+1, "min %d", &min), *p = '\n';
	    if(p = strstr(line+6, " max "))  sscanf(p+1, "max %d", &max), *p = '\n';
	    if(p = strstr(line+6, " default "))  sscanf(p+1, "default %[^\n]*", val), *p = '\n';
	    if(!(p = strstr(line+6, " name "))) p = line+1; sscanf(p+6, "%[^\n]", name); // 'name' is omitted in UCCI
	    if(!strcasecmp(name, "UCI_Chess960")) { frc=2; continue; }
	    if(!strcasecmp(name, "UCI_Variant")) { if(p = strstr(line+6, " var ")) strcpy(varList, p); varOpt = 1; continue; }
	    if(!strcasecmp(name, "UCI_Opponent")) { namOpt = 1; continue; }
	    if(!strcasecmp(name+2, "I_AnalyseMode")) { strcpy(anaOpt, name); continue; }
	    if(frc< 0 && (strstr(name, "960") || strcasestr(name, "frc")) && !strcmp(type, "check")) {
		EPRINT((f, "# setoption name %s value true\n", name)) strcpy(val, "true"); // set non-standard suspected FRC options
	    }
	    if(!strcasecmp(name, "Threads")) { strcpy(threadOpt, name); continue; }
	    if(!strcasecmp(name, "Ponder") || !strcasecmp(name, "USI_Ponder")) { strcpy(canPonder, name); continue; }
	    if(!strcasecmp(name, "Hash") || !strcasecmp(name, "USI_Hash") || !strcasecmp(name, "hashsize")) {
		memory = oldMem = atoi(val); hasHash = 1; 
		strcpy(hashOpt, name);
		continue;
	    }
	    if(!strcasecmp(name, "newgame") && !strcmp(type, "button")) { newGame++; continue; }
	    if(!strcasecmp(name, "usemillisec")) { unit = (!strcmp(val, "false") ? 2 : 1); continue; }
	    sprintf(buf, ",%s,", name); if(p = strcasestr(EGT, buf)) { // collect EGT formats
		strcpy(buf, p); for(p=buf; *++p >='a';){} if(*p == ' ') strcpy(buf, ",scorpio"); *p = 0; strcat(egts, buf); continue; // clip at first non-lower-case
	    }
	    // pass on engine-defined option as WB option feature
	    if(!strcmp(type, "filename")) type[4] = 0;
	    else if(sc == 'c' && !strcmp(type, "string")) { // in UCI try to guess which strings are file or directory names
		if(strcasestr(name, "file")) strcpy(type, "file"); else
		if(strcasestr(name, "path") || strcasestr(name, "directory") || strcasestr(name, "folder")) strcpy(type, "path");
	    }
	    sprintf(buf, "feature option=\"%s -%s", name, type); q = buf + strlen(buf);
	    if(     !strcmp(type, "file")
	         || !strcmp(type, "string")) sprintf(q, " %s\"\n", val);
	    else if(!strcmp(type, "spin"))   sprintf(q, " %d %d %d\"\n", atoi(val), min, max);
	    else if(!strcmp(type, "check"))  sprintf(q, " %d\"\n", strcmp(val, "true") ? 0 : 1), strcat(checkOptions, name);
	    else if(!strcmp(type, "button")) sprintf(q, "\"\n");
	    else if(!strcmp(type, "combo")) {
		if(p = strstr(line+6, " default "))  sscanf(p+1, "default %s", type); // current setting
		min = 0; p = line+6;
		while(p = strstr(p, " var ")) {
		    sscanf(p += 5, "%s", val); // next choice
		    sprintf(buf + strlen(buf), "%s%s%s", min++ ? " /// " : " ", strcmp(type, val) ? "" : "*", val);
		}
		strcat(q, "\"\n");
	    }
	    else buf[0] = 0; // ignore unrecognized option types
	    if(buf[0]) printf("%s", buf);
	}
	else if(!strcmp(command, "id")) {
	    static char name[256], version[256];
	    if(sscanf(line, "id name %[^\n]", name) == 1) printf("feature myname=\"%s (U%cI2WB)\"\n", name, sc-32);
	    if(sscanf(line, "id version %[^\n]", version) == 1 && *name) printf("feature myname=\"%s %s (U%cI2WB)\"\n", name, version, sc-32);
	}
	else if(!strcmp(command, "readyok")) { pause = 0; Sync(WAKEUP); } // resume processing of GUI commands
	else if(sc == 'x'&& !strcmp(command, "ucciok") || sscanf(command, "u%ciok", &c)==1 && c==sc) {
	    char *p = varList, *q = varList;
	    while(*q && *q != '\n')  if(!strncmp(q, " var ", 5)) *p++ = ',', q +=5; // replace var keywords by commas
				else if(!strncmp(q-1, " chess ", 7)) strcpy(p, "normal"), p += 6, q += 5; // 'chess' is called 'normal' in CECP
				else *p++ = *q++; // copy other variant names unmodified
	    if(frc) sprintf(p, ",normal,fischerandom"), printf("feature oocastle=%d\n", frc<0); // unannounced FRC uses O-O castling
	    if(*varList) printf("feature variants=\"%s\"\n", varList+1); // from UCI_Variant combo and/or UCI_Chess960 check options
	    if(*egts) printf("feature egt=\"%s\"\n", egts+1);
	    printf("feature smp=1 memory=%d done=1\n", hasHash);
	    if(unit == 2) { unit = 1; EPRINT((f, "# setoption usemillisec true\n")) }
	    Sync(WAKEUP); // done with options
	}
    }
}

void
Move4Engine(char *m)
{
    if(sc == 's') {
      // convert input move to USI format
      if(m[1] == '@') { // drop
	m[1] = '*';
      } else {
	m[0] = 'a'+'0'+size - m[0];
	m[1] = 'a'+'0'+size - m[1];
      }
      m[2] = 'a'+'0'+size - m[2];
      m[3] = 'a'+'0'+size - m[3];
      if(m[4] == '=') m[4] = 0; // no '=' in USI format!
      else if(m[4]) m[4] = '+'; // cater to WB 4.4 bug :-(
    }
}

void
GUI2Engine()
{
    char line[256], command[256], *p, *q, *r, mySide, type[99];

    while(1) {
	int i, x, difficult, think=0;

	if((computer == stm || computer == ANALYZE && !searching) && !suspended) {
	    DPRINT("# start search\n");
	    LoadPos(moveNr); fflush(stdout); // load position
	    // and set engine thinking (note USI swaps colors!)
	    startTime = GetTickCount(); mySide = stm; // remember side we last played for
	    if(computer == ANALYZE) {
		EPRINT((f, "# go infinite")); maxDepth = mpvSP = 0;
		if(sm & 1) { // some moves are disabled
		    EPRINT((f, " searchmoves"))
		    for(i=1; i<nr; i++) if(on[i]) EPRINT((f, " %s", moveMap[i]))
		}
		EPRINT((f, "\n")) searching = 1; // suppresses spurious commands during analysis starting new searches
	    } else pause = think = 2, StartSearch(""); // request suspending of input processing while thinking
	}
      nomove:
       for(difficult=0; !difficult; ) { // read and handle commands that can (or must) be handled during thinking
	fflush(toE); fflush(stdout);
	i = 0; while((x = getchar()) != EOF && (line[i] = x) != '\n') i++;
	line[++i] = 0; if(x == EOF) { printf("# EOF\n"); sprintf(line, "quit -1\n"); }
	if(think && !pause) Sync(PAUSE), think = 0, Release(); // if no longer thinking, take dummy pause
	sscanf(line, "%s", command); DPRINT("# '%s' think=%d pause=%d log=%d sent=%d\n", command, think, pause, logLen, sentLen);
	if(!strcmp(command, "usermove")) { difficult--; break; } // for efficiency during game play, moves, time & otim are tried first
	else if(!strcmp(command, "time"))   sscanf(line+4, "%d", &myTime),  myTime  = (10*myTime)/unit;
	else if(!strcmp(command, "otim"))   sscanf(line+4, "%d", &hisTime), hisTime = (10*hisTime)/unit;
	else if(!strcmp(command, "offer")) drawOffer = 1; // backlogged anyway, so this can be done instantly
	else if(!strcmp(command, "post"))  post = 1;
	else if(!strcmp(command, "nopost"))post = 0;
	else if(!strcmp(command, "pause")) {
	    if(computer == stm) myTime -= GetTickCount() - startTime;
	    suspended = 1 + pondering; // remember if we were pondering, and stop search ignoring bestmove
	    StopPonder(pondering || computer == stm);
	}
	else { //convert easy & hard to "option" after treating their effect on the adapter
	  if(!strcmp(command, "easy")) {
	    if(*canPonder) ponder = 0, sprintf(command, "option"), sprintf(line, "option %s=0\n", canPonder); else continue;
	  }
	  else if(!strcmp(command, "hard")) {
	    if(*canPonder) ponder = 1, sprintf(command, "option"), sprintf(line, "option %s=1\n", canPonder); else continue;
	  }
	  if(!strcmp(command, "option")) {
	    char *p;
	    if(logLen == sentLen) logLen = 0, sentLen = 0; // engine is up to date; reset buffer
	    if(sscanf(line+7, "UCI2WB debug output=%d", &debug) == 1) ; else
	    if(sscanf(line+7, "Floating Byoyomi=%d", &flob) == 1) ; else
	    if(sscanf(line+7, "Byoyomi=%d", &byo) == 1) ; else
	    if(p = strchr(line, '=')) {
		*p++ = 0;
		if(strstr(checkOptions, line+7)) sprintf(p, "%s\n", atoi(p) ? "true" : "false");
		snprintf(backLog+logLen, 9999-logLen, "setoption %s%s %s%s", nameWord, line+7, valueWord, p);
	    } else snprintf(backLog+logLen, 9999-logLen, "setoption %s%s\n", nameWord, line+7);
	    DPRINT("# backlog: %s", backLog+logLen); logLen += strlen(backLog+logLen);
	    if(!think && Release()) break; // break will restart analysis; pondering is restarted by Release itself
	  }
	  else difficult = 1; // difficult command; terminate loop for easy ones
	}
       } // next command

	// some commands that should never come during thinking can be safely processed here
	if(difficult < 0) { // used as kludge to signal "usermove" was already matched
	    sscanf(line, "usermove %s", command); // strips off linefeed
	    Move4Engine(command);
	    stm = WHITE+BLACK - stm; collect = (computer == ANALYZE); sm = 0;
	    // when pondering we either continue the ponder search as normal search, or abort it
	    if(pondering || computer == ANALYZE) {
		if(pondering && !strcmp(command, move[moveNr])) { // ponder hit
		    char *draw = drawOffer ? " draw" : ""; drawOffer = 0;
		    pondering = 0; pause = 2; moveNr++; startTime = GetTickCount(); // clock starts running now
		    EPRINT((f, "# ponderhit%s\n", draw)) fflush(toE); fflush(stdout);
		    think = 2; // request blocking input during thinking
		    goto nomove;
		}
		StopPonder(1); searching = 0;
	    }
	    strcpy(move[moveNr++], command); // possibly overwrites ponder move
	    continue;
	}
	if(!strcmp(command, "resume")) {
	    if(suspended == 2) StartPonder(); // restart interrupted ponder search
	    suspended = think = 0; continue;  // causes thinking to start in normal way if on move or analyzing
	}
	if(think) { // command arrived during thinking; order abort for 'instant commands'
	    if(!strcmp(command, "?") || !strcmp(command, "quit") ||
	       !strcmp(command, "force") || !strcmp(command, "result")) { EPRINT((f, "# stop\n")); fflush(toE); }
	    Sync(PAUSE); Release(); // block processing of difficult commands during thinking; send backlog left because of race
	}
	if(!strcmp(command, "new")) {
	    computer = BLACK; moveNr = 0; depth = -1; move[0][0] = 0;
	    stm = WHITE; strcpy(iniPos, "position startpos"); frc &= ~1;
	    if(memory != oldMem && hasHash) EPRINT((f, "# setoption %s%s %s%d\n", nameWord, hashOpt, valueWord, memory))
	    oldMem = memory;
	    // we can set other options here
	    if(sc == 'x') { if(newGame) EPRINT((f, "# setoption newgame\n")) } else // optional in UCCI
	    if(varOpt) EPRINT((f, "# setoption name UCI_Variant value chess\n"))
	    pause = 1; // wait for option settings to take effect
	    EPRINT((f, "# isready\n")) fflush(toE);
	    Sync(PAUSE); // wait for readyok
	    EPRINT((f, "# u%cinewgame\n", sc)) fflush(toE);
	}
	else if(!strcmp(command, "level")) {
	    int sec = 0;
	    sscanf(line, "level %d %d:%d %d", &mps, &tc, &sec, &inc) == 4 ||
	    sscanf(line, "level %d %d %d", &mps, &tc, &inc);
	    tc = (60*tc + sec)*1000; inc *= 1000; sTime = 0; tc /= unit; inc /= unit;
	}
	else if(!strcmp(command, "protover")) {
	    if(!varList[0]) strcpy(varList, sc=='s' ? ",shogi,5x5+5_shogi" : VARIANTS);
	    printf("feature setboard=1 usermove=1 debug=1 ping=1 name=1 reuse=0 exclude=1 pause=1 sigint=0 sigterm=0 done=0\n");
	    printf("feature option=\"UCI2WB debug output -check %d\"\n", debug);
	    if(sc == 's') printf("feature option=\"Floating Byoyomi -check %d\"\nfeature option=\"Byoyomi -spin %d -1 1000\"\n", flob, byo);
	    EPRINT((f, sc == 'x' ? "# ucci\n" : "# u%ci\n", sc)) fflush(toE); // prompt UCI engine for options
	    Sync(PAUSE); // wait for uciok
	}
	else if(!strcmp(command, "setboard")) {
		stm = (strstr(line+9, " b ") ? BLACK : WHITE);
                if((p = strchr(line+9, '[')) && !varOpt) { char c;
                    *p++ = 0; q = strchr(p, ']'); *q = 0; r = q + 4; 
		    if(sc == 's') q[2] = 'w' + 'b' - q[2], strcpy(r=q+3, " 1\n"); // Shogi: reverse color
		    else r = strchr(strchr(q+4, ' ') + 1, ' '); // skip to second space (after e.p. square)
		    *r = 0; sprintf(command, "%s%s %s %s", line+9, q+1, p, r+1);
                } else strcpy(command, line+9);
		if(frc == -1 && (p = strchr(command, ' '))) strncpy(p+3, "KQkq", 4); // unannounced FRC
		sprintf(iniPos, "%s%sfen %s", iniPos[0]=='p' ? "position " : "", sc=='s' ? "s" : "", command);
		iniPos[strlen(iniPos)-1] = sm = 0; collect = (computer == ANALYZE);
	}
	else if(!strcmp(command, "variant")) {
		if(varOpt) {
		    EPRINT((f, "# setoption name UCI_Variant value %sucinewgame\nisready\n", line+8))
		    fflush(toE); Sync(PAUSE);
		}
		if(!strcmp(line+8, "shogi\n")) size = 9, strcpy(iniPos, "position startpos");
		if(!strcmp(line+8, "5x5+5_shogi\n")) size = 5, strcpy(iniPos, "position startpos");
		if(!strcmp(line+8, "xiangqi\n")) strcpy(iniPos, "fen rnbakabnr/9/1c5c1/p1p1p1p1p/9/9/P1P1P1P1P/1C5C1/9/RNBAKABNR r");
		if(!strcmp(line+8, "fischerandom\n")) { frc |= 1; if(frc > 0) EPRINT((f, "# setoption name UCI_Chess960 value true\n")) }
	}
	else if(!strcmp(command, "undo") && (i=1) || !strcmp(command, "remove") && (i=2)) {
	    if(pondering || computer == ANALYZE) StopPonder(1), searching = 0;
	    moveNr = moveNr > i ? moveNr - i : 0; collect = (computer == ANALYZE); sm = 0;
	}
	else if(!strcmp(command, ".")) {
	    printf("stat01: %d %d %d %d 100 %s\n", statTime, statNodes, statDepth, 100-currNr, currMove);
	    goto nomove;
	}
	else if(!strcmp(command+2, "clude") && collect > 2) { // include or exclude
	    int all = !strcmp(line+8, "all"), in = command[1] == 'n';
	    inex = 1; line[strlen(line)-1] = sm = 0; // strip LF and clear sm flag
	    for(i=1; i<nr; i++) { if(!strcmp(line+8, moveMap[i]) || all) on[i] = in; sm |= on[i]+1; } // sm: 2 = enabled, 1 = disabled
	    if(!(sm & 2)) goto nomove; // no moves enabled; continue current search
	    if(computer == ANALYZE) StopPonder(1), searching = 0; // abort old analysis
	}
	else if(!strcmp(command, "xboard")) ;
	else if(!strcmp(command, "analyze"))computer = ANALYZE, collect = 1, sm = 0, Analyze("true");
	else if(!strcmp(command, "exit"))   computer = NONE, StopPonder(1), searching = 0, Analyze("false");
	else if(!strcmp(command, "force"))  computer = NONE, StopPonder(pondering);
	else if(!strcmp(command, "go"))     computer = stm;
	else if(!strcmp(command, "ping"))   { /* static int done; if(!done) pause = 1, fprintf(toE, "isready\n"), fflush(toE), printf("# send isready\n"), fflush(stdout), Sync(PAUSE); done = 1;*/ printf("po%s", line+2); }
	else if(!strcmp(command, "memory")) sscanf(line, "memory %d", &memory);
	else if(!strcmp(command, "cores")&& !!*threadOpt) { sscanf(line, "cores %d", &cores); EPRINT((f, "# setoption %s%s %s%d\n", nameWord, threadOpt, valueWord, cores)) }
	else if(!strcmp(command, "egtpath")){
	    sscanf(line+8, "%s %[^\n]", type, command);
            if(p = strstr(EGT, type)) strcpy(type, p), p = strchr(type, ','), *p = 0; else strcpy(type, "bitbases path");
	    EPRINT((f, "# setoption name %s value %s\n", type, command));
	}
	else if(!strcmp(command, "sd"))     sscanf(line, "sd %d", &depth);
	else if(!strcmp(command, "st"))     sscanf(line, "st %d", &sTime), sTime = 1000*sTime - 30, inc = 0, sTime /= unit;
	else if(!strcmp(command, "name"))   { if(namOpt) EPRINT((f, "# setoption name UCI_Opponent value none none %s %s", comp ? "computer" : "human", line+5)) }
	else if(!strcmp(command, "computer")) comp = 1;
	else if(!strcmp(command, "result")) {
	    if(sc == 's') EPRINT((f, "# gameover %s\n", line[8] == '/' ? "draw" : (line[7] == '0') == mySide ? "win" : "lose"))
	    computer = NONE;
	}
	else if(!strcmp(command, "quit"))   { EPRINT((f, "# quit\n")) fflush(toE), exit(atoi(line+4)); }
    }
}

int
StartEngine(char *cmdLine, char *dir)
{
#ifdef WIN32
  HANDLE hChildStdinRd, hChildStdinWr,
    hChildStdoutRd, hChildStdoutWr;
  BOOL fSuccess;
  PROCESS_INFORMATION piProcInfo;
  STARTUPINFO siStartInfo;
  DWORD err;

  /* Create a pipe for the child's STDOUT. */
  if (! WinPipe(&hChildStdoutRd, &hChildStdoutWr)) return GetLastError();

  /* Create a pipe for the child's STDIN. */
  if (! WinPipe(&hChildStdinRd, &hChildStdinWr)) return GetLastError();

  SetCurrentDirectory(dir); // go to engine directory

  /* Now create the child process. */
  siStartInfo.cb = sizeof(STARTUPINFO);
  siStartInfo.lpReserved = NULL;
  siStartInfo.lpDesktop = NULL;
  siStartInfo.lpTitle = NULL;
  siStartInfo.dwFlags = STARTF_USESTDHANDLES;
  siStartInfo.cbReserved2 = 0;
  siStartInfo.lpReserved2 = NULL;
  siStartInfo.hStdInput = hChildStdinRd;
  siStartInfo.hStdOutput = hChildStdoutWr;
  siStartInfo.hStdError = hChildStdoutWr;

  fSuccess = CreateProcess(NULL,
			   cmdLine,	   /* command line */
			   NULL,	   /* process security attributes */
			   NULL,	   /* primary thread security attrs */
			   TRUE,	   /* handles are inherited */
			   DETACHED_PROCESS|CREATE_NEW_PROCESS_GROUP,
			   NULL,	   /* use parent's environment */
			   NULL,
			   &siStartInfo, /* STARTUPINFO pointer */
			   &piProcInfo); /* receives PROCESS_INFORMATION */

  if (! fSuccess) return GetLastError();

//  if (0) { // in the future we could trigger this by an argument
//    SetPriorityClass(piProcInfo.hProcess, GetWin32Priority(appData.niceEngines));
//  }

  /* Close the handles we don't need in the parent */
  CloseHandle(piProcInfo.hThread);
  CloseHandle(hChildStdinRd);
  CloseHandle(hChildStdoutWr);

  process = piProcInfo.hProcess;
  pid = piProcInfo.dwProcessId;
  fromE = (FILE*) _fdopen( _open_osfhandle((long)hChildStdoutRd, _O_TEXT|_O_RDONLY), "r");
  toE   = (FILE*) _fdopen( _open_osfhandle((long)hChildStdinWr, _O_WRONLY), "w");
#else
    char *argv[10], *p, buf[200];
    int i, toEngine[2], fromEngine[2];

    if (dir && dir[0] && chdir(dir)) { perror(dir); exit(1); }
    pipe(toEngine); pipe(fromEngine); // create two pipes

    if ((pid = fork()) == 0) { // Child
	dup2(toEngine[0], 0);   close(toEngine[0]);   close(toEngine[1]);   // stdin from toE pipe
	dup2(fromEngine[1], 1); close(fromEngine[0]); close(fromEngine[1]); // stdout into fromE pipe
	dup2(1, fileno(stderr)); // stderr into frome pipe

	strcpy(buf, cmdLine); p = buf;
	for (i=0;;) { argv[i++] = p; p = strchr(p, ' '); if (p == NULL) break; *p++ = 0; }
	argv[i] = NULL;
        execvp(argv[0], argv); // startup engine
	
	perror(argv[0]); exit(1); // could not start engine; quit.
    }
    signal(SIGPIPE, SIG_IGN);
    close(toEngine[0]); close(fromEngine[1]); // close engine ends of pipes in adapter
    
    fromE = (FILE*) fdopen(fromEngine[0], "r"); // make into high-level I/O
    toE   = (FILE*) fdopen(toEngine[1], "w");
#endif
  return NO_ERROR;
}

main(int argc, char **argv)
{
	char *dir = NULL, *p, *q; int e;


	if(argc == 2 && !strcmp(argv[1], "-v")) { printf("UCI2WB " VERSION " by H.G.Muller\n"); exit(0); }
	if(argc > 1 && !strcmp(argv[1], "debug")) { debug = 1; argc--; argv++; }
	if(argc > 1 && !strcmp(argv[1], "-var")) { strcpy(varList+1, argv[2]); *varList = ','; argc-=2; argv+=2; }
	if(argc > 1 && argv[1][0] == '-') { sc = argv[1][1]; argc--; argv++; }
	if(argc < 2) { printf("usage is: U%cI2WB [debug] [-s] <engine.exe> [<engine directory>]\n", sc-32); exit(-1); }
	if(argc > 2) dir = argv[2];
        if(argc > 3) strncpy(suffix, argv[3], 80);

        if(sc == 'x') nameWord = valueWord = bTime = "", wTime = "opp", bInc = "increment", wInc = "oppincrement", unit = 1000; // switch to UCCI keywords
	else if(sc == 'f' ) frc = -1, sc = 'c';   // UCI for unannounced Chess960
	else if(sc == 'n') sc = 'c'; // UCI for normal Chess

	// spawn engine proc
	if(StartEngine(binary = argv[1], dir) != NO_ERROR) { perror(argv[1]), exit(-1); }

	Sync(INIT);

	// create separate thread to handle engine->GUI traffic
#ifdef WIN32
	CreateThread(NULL, 0, (LPTHREAD_START_ROUTINE) Engine2GUI, (LPVOID) NULL, 0, &thread_id);
#else
        { pthread_t t; signal(SIGINT, SIG_IGN); signal(SIGTERM, SIG_IGN); pthread_create(&t, NULL, Engine2GUI, NULL); }
#endif

	// handle GUI->engine traffic in original thread
	GUI2Engine();
}
