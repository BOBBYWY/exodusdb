#include <exodus/library.h>
libraryinit()

function main(in type, in inx, in mode, out outx) {

	if (inx == "") {
		outx = "";
		return 0;
	}

	// nospaces=index(mode,'*',1)
	// if nospaces then convert '*' to '' in mode
	// IF MODE THEN
	// IF MODE='4' THEN MODE=@DATE.FORMAT;MODE[2,1]='4'
	// END ELSE
	// IF @DATE.FORMAT THEN MODE=@DATE.FORMAT ELSE MODE='D2/E'
	// END

	var datemode = "[DATE," ^ mode.field(",", 1) ^ "]";
	var timemode = mode.field(",", 2);

	let status = 0;
	if (type == "OCONV") {

		var in1 = inx.field(".", 1);
		var in2 = inx.field(".", 2);

		// find all DOS in accessible columns
		// find all DOS related code by searching GBP for /86400
		if (datemode.contains("DOS")) {
			datemode.replacer("DOS", "");
			// in1-=24873
			in1 -= 24837;
		}

		// DOS time is 0-.99999 means 00:00-24:00
		// convert to 0-86400 seconds basis
		var dostime = 0;
		if (timemode.contains("DOS")) {
			dostime = 1;
			timemode.replacer("DOS", "");
			in2 = 86400 * ("." ^ in2);

			// fix bug where datetime was set like datetime=(date():'.':time() 'R(0)#5')+0
		} else if (in2.len() != 5) {
			in2 = (in2 ^ "00000").first(5);
		}

		// move time and date into the local date time
		if (SW.f(1)) {
			in2			= (in2 + SW.f(1) + 0.5).floor();
			let in2orig = in2;
			in2			= in2.mod(86400);
			if (in2 < in2orig) {
				in1 += 1;
			} else if (in2 > in2orig) {
				in1 -= 1;
			}
		}

		// use MT since date and time are now local
		if (timemode == "") {
			timemode = "MT";

			// numeric means just convert to local date time numeric ddddd.ttttt
		} else if (timemode == "NUMERIC") {
			if (dostime) {
				outx = (in1 + in2 / 86400).oconv("MD50P");
			} else {
				outx = in1 ^ "." ^ in2.oconv("R(0)#5");
			}
			return 0;
		}

		outx = oconv(in1, datemode) ^ " " ^ oconv(in2, timemode);

	} else if (type == "ICONV") {
		outx	  = inx;
		let odate = inx.field(" ", 1);
		let otime = inx.field(" ", 2);
		outx	  = iconv(odate, "[DATE]");
		if (outx and otime) {
			let itime = iconv(otime, "[TIME2]");
			if (itime) {
				outx ^= "." ^ itime.oconv("R(0)#5");
			}
		}
	}
	return 0;
}

libraryexit()
