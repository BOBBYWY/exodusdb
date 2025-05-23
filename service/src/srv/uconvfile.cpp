#include <exodus/library.h>
libraryinit()

#include <shell2.h>

#include <service_common.h>

#include <srv_common.h>

var encoding1;
var encoding2;
var exe;
//var ii;
var encoding;
var errors;

function main(in inputfilename, in encoding1i, in encoding2i, out result, out msg) {

	// currently only used by convcsv

	// uses the unicode uconv to convert file format
	//
	// http://www.microsoft.com/globaldev/reference/cphome.mspx

	if (encoding1i.unassigned()) {
		encoding1 = "";
	} else {
		encoding1 = encoding1i;
	}
	if (encoding2i.unassigned()) {
		encoding2 = "";
	} else {
		encoding2 = encoding2i;
	}
	result = 0;

	// make cygwin command
	// look for local or cygwin wget.exe otherwise quit
	if (oscwd().contains(":")) {
		exe = ".exe";
	} else {
		exe = "";
	}
	var cmd = "uconv" ^ exe;
	if (not cmd.osfile()) {
		cmd.prefixer(SYSTEM.f(50));
	}
	if (not cmd.osfile()) {
		msg = "UCONVFILE: Cannot find " ^ cmd;
		// indicate failed but because of lack of uconv.exe file
		result = "";

		return 0;
	}

	// determine the encoding1
	if (encoding1 == "CODEPAGE") {
		//call osgetenv("CODEPAGE", encoding1);
		if (not osgetenv("CODEPAGE", encoding1)) {
			//abort(lasterror());
			loglasterror();
		}
		var	 oemcodepages = "437]720]737]775]850]852]855]857]858]862]866]874]932]936]949]950]1258"_var;
		var	 wincodepages = "1252]1256]1253]1257]1252]1252]1251]1254]1252]1255]1251]874]932]936]949]950]874"_var;
		var codepagen;
		if (oemcodepages.locate(encoding1, codepagen)) {
			encoding1 = wincodepages.f(1, codepagen);
		}
	}
	if (not encoding1) {
		msg = "UCONVFILE: Missing encoding1";
		return 0;
	}

	// dont convert if latin
	// hopefully to reduce chance of screwups/reduce filesize when latin
	if (encoding1 == 1252) {
		result = 1;
		return 0;
	}

	// determine the encoding2
	if (encoding2 == "CODEPAGE") {
		//call osgetenv("CODEPAGE", encoding2);
		if (not osgetenv("CODEPAGE", encoding2)) {
			//abort(lasterror());
			loglasterror();
		}
	}
	if (not encoding2) {
		msg = "UCONVFILE: Missing encoding2";
	}

	if (encoding1 != encoding) {

		// invent a temporary filename
		var tempfilename = inputfilename;
		tempfilename.paster(-3, 3, "$CP");

		cmd ^= " -f windows=" ^ encoding1 ^ " -t " ^ encoding2;

		cmd ^= " -o " ^ tempfilename;
		cmd ^= " " ^ inputfilename;

		// run the conversion command
		result = shell2(cmd, errors);
		if (errors) {
			msg = "UCONVFILE: uconv " ^ errors;
			return 0;
		}

		// overwrite the input file with the temporary
//		if (VOLUMES) {
//			cmd = "xcopy " ^ tempfilename ^ " " ^ inputfilename ^ " /y";
//		} else {
			cmd = "cp " ^ tempfilename ^ " " ^ inputfilename;
//		}
		result = shell2(cmd, errors);
		if (errors) {
			msg = "UCONVFILE: xcopy " ^ errors;
			return 0;
		}

		// delete the temporary
		//tempfilename.osremove();
		if (osfile(tempfilename) and not tempfilename.osremove()) {
			loglasterror();
		}
	}

	result = 1;

	return 0;
}

libraryexit()
