#include <exodus/library.h>
libraryinit()

#include <rtp57.h>

var databasecode;
var usercode;  // num
var lockprefix;
var result;
//var xx;
//var yy;
//var zz;

function main(in databasecode0 = "", in usercode0 = "") {

	// returns the number of other users of EXODUS

	if (SENTENCE.field(" ", 1) == "OTHERUSERS") {
		databasecode = SENTENCE.field(" ", 2);
	} else {
		if (databasecode0.unassigned()) {
			databasecode = "";
		} else {
			databasecode = databasecode0;
		}
	}
	if (usercode0.unassigned()) {
		usercode = "";
	} else {
		usercode = usercode0;
	}

	var processes = "";

	// curruserlockid.sys134=sysvar('GET',109,134)
	let curruserlockid = THREADNO;

	var returndata	   = 0;
	var otherusercodes = "";

	// IF @STATION # '' THEN

	// we are at rev 2.0
	// IF revRELEASE()>= 2.1 THEN
	// lockmode=36
	// unlockmode=37
	// END ELSE
	let lockmode   = 23;
	let unlockmode = 24;
	// END

	if (curruserlockid.isnum()) {
		lockprefix = "";
	} else {
		lockprefix = "U" ^ var("99999").last(4);
	}

	// FOR lockno = 1 TO RUNTIME();*SYSE3_NUSERS

	for (const var lockno : range(1, 100)) {

		var lockid = lockprefix ^ lockno;

		// skip current user
		if (lockid == curruserlockid) {
			continue;  // lockno
		}

		// skip 10,20,100 etc because they appear to be equivalent to
		// their equivalents without trailing zeroes
		if ((lockno.ends("0")) and lockprefix) {
			continue;  // lockno
		}

		lockid = lockprefix ^ lockno;
		// IF lockid # SYS134 THEN

		// attempt to lock
		result = "";
		var dummy1 = "";
		var dummy2 = "";
		var dummy3 = "";
		call rtp57(lockmode, "", dummy1, lockid, "", dummy2, result);

		// if successful, then unlock
		if (result) {
			dummy1 = "";
			dummy2 = "";
			dummy3 = "";
			call rtp57(unlockmode, "", dummy1, lockid, "", dummy2, dummy3);
			continue;  // lockno
		}

		// check process records

		// skip processes in wrong database or wrong usercode
		if (databasecode or usercode) {

			if (processes == "") {
				if (not processes.open("PROCESSES", "")) {
					processes = 0;
				}
			}
			if (processes) {
				let processno = lockno - (lockno / 10).floor();
				var process;
				if (not process.read(processes, processno)) {
					// if no process record then assume no process
					// and failed lock because another OTHERUSERS is testing the same lock
					// could really skip further checking since should not be
					// any higher processes but lock fail on missing process is infrequent
					// but does happen when with many eg 10+ processes on diff dbs
					continue;  // lockno
				}

				if (databasecode and process.f(17) != databasecode) {
					continue;  // lockno
				}

				if (usercode and process.f(40, 10) != usercode) {
					continue;  // lockno
				}
			}

			// end of database or user code provided
		}

		otherusercodes(1, -1) = lockid;

		returndata += 1;

	}  // lockno;

	returndata -= 1;
	if (returndata < 0) {
		returndata = 0;
	}

	if (returndata) {
		returndata(2) = otherusercodes;
		returndata(3) = curruserlockid;
	}

	for (const var ii : range(1, 9999)) {
		usercode = returndata.f(2, ii);
		// /BREAK;
		if (not usercode)
			break;
		usercode.cutter(5);
		if (not curruserlockid.isnum()) {
			usercode -= (usercode / 10).floor();
		}
		returndata(2, ii) = "PROCESS" ^ usercode;
	}  // ii;

	if (SENTENCE.field(" ", 1) == "OTHERUSERS") {
		call note(returndata.f(1) ^ " other users");
	}

	return returndata;
}

libraryexit()
