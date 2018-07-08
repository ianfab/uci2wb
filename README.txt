                         UCI2WB relelease notes

UCI2WB is an adapter for running USI (Shogi) engines under WinBoard.
Install USI2WB in WinBoard as if it was the engine, with one or two arguments
on its command line, to indicate the name of the real engine executable,
and (optionally) its working directory. 

E.g. when you have placed USI2WB.exe in a sub-folder 'Adapters' inside the WinBoard folder,
you could include the following line in winboard.ini amongst the firstChessPrograms:

"UCI2WB -s USI_ENGINE.exe ENGINE_FOLDER" /fd="./Adapters" /variant=shogi

UCI2WB was developed with gcc under Cygwin. It can also be used as a (dumb) UCI2WB adapter. 
"Dumb" here means that the adapter does not know anything about the game state, 
and just passes on the moves and position FENs as it receives them from engine or GUI. 
As this can be done without any knowledge of the game rules, or even of the board size, 
such a dumb adapter can in principle be used for any variant. To use it for UCI protocol
(both the Chess or Xiangqi dialects), use it without the -s flag (or with a -c flag).
  As of version 2.0 UCI2WB also supports the UCCI protocol for Xiangqi, for which it was
made slightly less dumb: in UCCI mode it keeps track of the board position on a (Xiangqi)
board, so it can recognize capture moves, and send only the moves after it (with an
appropriate FEN to start them from).
  The general syntax of the UCI2WB command is:

UCI2WB [debug] [-var VARIANTLIST] [-s|-c|-x] ENGINE.exe [ENGINEFOLDER]

Presence of the 'debug' argument causes UCI2WB to report everything it receives from the engine,
as well as the 'position' and 'go' commands sent to it, as debug output (prefixed with '#')
to the GUI. This has the same effect as switching the option 'UCI2WB debug output' on,
except that it forces the option to be on from the very beginning, so that the engine
startup will also be reported.
  The '-var' option overrules the list of variants UCI2WB says it supports with the given list,
like 'feature variants="VARIANTLIST"'.


This package includes the source code. To compile on Windows under Cygwin, use the commands

windres --use-temp-file -O coff UCI2WB.rc -o rosetta.o
gcc -O2 -s -mno-cygwin UCI2WB.c rosetta.o -o UCI2WB.exe

To compile under Linux, use

gcc -O2 -s UCI2WB.c -lpthread -o UCI2WB

Have fun,
H.G.Muller




Change log:

24/12/2016 3.0
Implement UCI_AnalyseMode option
Support egtpath command for Nalimov, Gaviota and Syzygy
Allow ?, quit, force and result commands to terminate thinking
Stop search during setoption commands, or buffer those until engine is done thinking
Explicitly report when engine dies, through GUI popup (tellusererror)
Make sure reporting of mated-in-0 score causes resign, even without PV
Fix eclipsing of -var option with engines that have UCI_Chess960 option
Fix empty default of string options

8/11/2016 2.3
Implement handling of 'UCI_Variant' option for variant announcement and selection
Pass 'info string variant' line as 'setup' command to allow engine-defined variants
Set 'UCI_Opponent' option in accordance with CECP 'name' and 'computer' commands
Fix option setting during analysis (MultiPV!)

22/11/2016 2.2
Use USI gameover command to relay game result
Handle USI win claims
Correct wtime/btime for byoyomi

7/11/2016 2.1
Make Linux version SIGTERM-proof
Recognize forward Pawn pushes as irreversible in UCCI
Block input from GUI during thinking
Use uxinewgame
Implement support for pre-standard UCI Chess960 engines ('Arena960 dialect')
Fix hash-size setting in UCCI
Combine name and version when engine gives them in separate 'id' commands
Fake time and node count for engines that do not report it
Add interactive options for byoyomi work-around

28/10/2014 2.0
Implement UCCI support

4/12/2012 1.10
Implement pause / resume commands

9/5/2012
Fix bug in converting shogi moves, introduced in v1.8

19/4/2012 1.9
Remove S-Chess move translation, to parallel change in UCI S-Chess 'standard'
Implement WB exclude feature

17/4/2012 1.8
Wait for uciok before processing GUI commands for setting options
Implement move translations required for variant seirawan
Make supported-variants string configurable from command line.

15/4/2012 v1.7
Take 30ms safety margin in translating st command to movetime

14/1/2011 v1.6
Replaced all polling by blocking synchronization (through pipes).
Implemented ping (using isready/readyok)
Made sending of debug info to GUI subject to option feature / command-line argument.
Fixed myname feature to handle names containing spaces.

14/10/2010
Port v1.5 to Linux

26/9/2010 v1.5
Add mini-Shogi ("variant 5x5+5_shogi") in USI mode; make coordinate translation board-size dependent.
Translate FEN in setboard to SFEN (does not fully work for holdings yet).

23/9/2010 v1.4
Translate USI engine PV to standard coordinates (no provision for deferred promotion yet).

??/?/2010 v1.3
Add work-around for non-compliant USI engines that do not understand winc, binc, movestogo.
Send btime before wtime, to avoid crashing USI engines with flakey (Shogidogoro) USI support.

1/8/2010 v1.2
Suppress mate claim on mate-in-1 score in Xiangqi (cyclone dialect),
as some engines use this score when they reach repeats they would win if continued.

31/7/2010 v1.1
Add WB remove command

30/7/2010 v1.0
Allow spaces in option names.
Refactor StopPonder into separate subroutine.
Refactor LoadPos into separate subroutine.
Send stop-ponder commands on exit and force.
Added icon.

29/7/2010 v0.9
Fixed analysis, which was broken after refacoring (newline after 'go infinite')

27/7/2010 v0.8
Refactored sendng of go command into separate routine
Send times with 'go ponder'.
Measure time spend on own move, and correct time left for it (2% safety margin).
Do adjust time left for new session or move time.

26/7/2010 v0.7
Fix bug w.r.t. side to move on setboard.
Print version number with -v option.

25/7/2010 v0.6
Undo implemented.
Analyze mode implemented. Seems to work for Glaurung 2.2 and Cyclone 2.1.1.
Periodic updates still use fictitious total move count of 100.

18/7/2010 v0.5
Switching between USI and UCI is now done at run time nased on a -s flag argument
Recognize WB variant command
In Xiangqi the position keyword is omitted, and a FEN is sent even for the start pos
Recognize 'null' as best move
Recognize scores without cp
Corrrect thinking time to centi-sec

17/7/2010 v0.4:
Introduced  compiler switch that enables some macros for everything that is different
in USI compared to UCI.
Fixed pondering.
Fixed setboard (for UCI).
Added result claims on checkmate / stalemate (for engines that say 'bestmove (none)').

16/7/2010 v0.3:
This is the first version for which the basics seem to work.
It could play a game of Blunder against itself, ending in resign.
Options of all types should work now.
Only classical time control tested.
Pondering not tested. (Blunder does not give a ponder move?)
Setboard not tested. (Probably does not work due to FEN format discrepancy.)
No analyze mode yet.
No SMP yet.
