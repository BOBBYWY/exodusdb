#include <exodus/library.h>
libraryinit()

#include <listen2.h>
#include <autorun3.h>

#include <sys_common.h>
#include <win_common.h>

var xx;

function main() {
	//
	//c sys

	#include <system_common.h>
	//clearcommon();

	var options = SENTENCE.field("(", 2);
	var sentencex = SENTENCE.field("(", 1);

	//if not called with X option then reexecute with X option
	//so that after crash the original user and station can be restored
	if (not(options.index("X"))) {
		var origuser = USERNAME;
		var origstation = STATION;
		var origsysmode = SYSTEM.a(33);

		execute(sentencex ^ " (X" ^ options);

		//resume being the original user and station
		SYSTEM(33) = origsysmode;
		var connection = "VERSION 3";
		connection(2) = origstation;
		connection(3) = origstation;
		call listen2("BECOMEUSERANDCONNECTION", origuser, "", connection, xx);

		stop();
	}

	var docids = SENTENCE.field(" ", 2, 9999);

	call autorun3(docids, options);

	stop();

	//only for c++
	return 0;
}

libraryexit()
