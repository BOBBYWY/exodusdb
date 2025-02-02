#include <exodus/library.h>
libraryinit()

#include <sys_common.h>

var logfilename;
var logfilelength;//num
var temposfilename83;
var logfile;
var datax;
var time2;//num

function main(in msg0, io time0) {
	//c sys in,io

	var interactive = false; //not(SYSTEM.a(33));
	if (interactive) {
		return 0;
	}

	#define sep_ " "

	if (VOLUMES) {
		//logfilename=field(FIELD2(@rollout.file,'\',-1),'.',1):'.LOG'
		logfilename = ROLLOUTFILE;
		logfilename.splicer(-3, 3, "log");
		logfilelength = logfilename.osfile().a(1);
		if (not logfilelength) {
			call oswrite("", logfilename);
		}
		temposfilename83 = logfilename;
		if (not(logfile.osopen(logfilename))) {
			return 0;
		}
	} else {
		logfile = "";
	}

	if (msg0 eq "GETLASTLOG") {

		if (not(VOLUMES)) {
			return "";
		}

		//try multiple start points in case hit middle of multibyte character
		for (var ptr = logfilelength - 1024; ptr <= logfilelength - 1021; ptr++) {
		//for (const var ptr : range(logfilelength - 1024, logfilelength - 1021)) {
			call osbread(datax, logfile, ptr, 1024);
			//if any data then break out of loop
			if (datax.length()) {
				ptr = logfilelength;
			}
		} //ptr;

		logfile.osclose();
		datax.converter("\r\n", FM ^ FM);
		time0 = logfilename ^ " " ^ field2(datax, FM, -3, 3);
		return 0;

	}

	time2 = ostime();
	if (time0.unassigned()) {
		time0 = time2;
	}

	var entry = "";
	if (TERMINAL) {
		//entry:=oconv(date(),'D2-J')
		entry = var().date().oconv("D2-E");
		entry = entry.substr(-2, 2) ^ "-" ^ entry.substr(1, 5);
		entry ^= sep_ ^ time2.oconv("MTS") ^ sep_;
		//entry:=field(time2,'.',2) 'MD20P'
	}
	//similar in listen and log2
	entry ^= SYSTEM.a(24) ^ ": " ^ (time2 - time0).oconv("MD20P");
	//entry:=sep:sep:sep:sep
	entry ^= sep_ ^ msg0;

	if (TERMINAL) {
		printl(entry);
	}

	entry ^= "\r\n";

	time0 = time2;

	if (logfile) {
		call osbwrite(entry, logfile, logfilelength);
		logfile.osclose();
	}

	return 0;
}

libraryexit()
