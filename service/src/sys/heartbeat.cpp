#include <exodus/library.h>
libraryinit()

#include <sysmsg.h>

#include <sys_common.h>

var status;
var processno;//num

function main(in mode, in status0="") {
	//c sys in,""

	//called from
	//1. LISTEN every 10 seconds or per potential request
	//2. INPUT.CHAR and INPUT.BOX in maintenance mode every 5 seconds
	//3. TODO should be called from giveway in interruptable running processes?

	//performs
	//1. saves process info and status in processes file
	//2. checks for .end flags and performs logoff/shutdown if present

	#include <system_common.h>
	if (status.unassigned()) {
		status = "";
	} else {
		status = status0;
	}

	//mode is always CHECK
	if (mode ne "CHECK") {
		return 0;
	}

	//1. save process info in PROCESSES

	var processes;
	if (processes.open("PROCESSES", "")) {

		//determine user lock code
		//WARNING: Before selection of dataset/init.general this is not available

		//userlockid=sysvar('GET',109,134)
		var userlockid = PROCESSNO;

		//on advanced revelation the pattern is U7906nnnn where 7906
		// where 7906 appears to be the last 4 digits of the DOS serial() number
		//and nn is a simple incrementing number (excluding all numbers ending 0!)
		//based on REV using some pattern of os locks on the REVBOOT file
		//eg U79061-U79069 first nine process
		//eg U790611-U790619 2nd nine process
		//eg U790621-U790629 3rd nine process

		//otherwise exodus will provide a serial number starting from 1
		//based on the first free lockable byte of /tmp/exodus file
		//such locks being maintained per exodus mvenvironment
		//so multiple processes and threads on the same host
		//will get unique process numbers
		//but all functions sharing the same mvenvironment will have the same no
		//determine process number

		if (userlockid.substr(1, 5) eq "U7906") {
			processno = userlockid.substr(6, 9999);
			processno -= (processno / 10).floor();

		} else if (userlockid.isnum()) {
			processno = userlockid;

		} else {
			var tt;
			if (not(tt.read(DEFINITIONS, "ERROR*PROCESSNO"))) {
				tt = "";
			}
			if (tt.a(1).count(VM) lt 10) {
				tt(1, -1) = processno;
				tt.write(DEFINITIONS, "ERROR*PROCESSNO");
				call sysmsg(processno.quote() ^ "Non-numeric processno in HEARTBEAT " ^ userlockid);
			}
		}

		var process = SYSTEM;
		//remove environment variables because mostly the same and waste space
		process(12) = "";
		process(13) = "";
		//garbagecollect;
		process(27) = (var().date() + 24873 + var().time() / 86400).oconv("MD50P");
		process(51) = APPLICATION;
		//can never be closed while this is running
		process(52) = "";
		process(53) = status;

		process.write(processes, processno);

	}

	//2. check if should logoff

	var parentdir = "../../";
	parentdir.converter("/", OSSLASH);
	if ((((SYSTEM.a(17).lcase() ^ ".end").osfile()) or var("global.end").osfile()) or ((parentdir ^ SYSTEM.a(123).lcase() ^ ".end").osfile())) {
		perform("OFF");
		var().logoff();
	}

	return 0;
}

libraryexit()
