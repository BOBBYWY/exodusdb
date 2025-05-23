#include <exodus/library.h>
libraryinit()

#include <addcent4.h>

var period;	 // num
var year;		 // num

function main(in type, in input0, in mode, out output) {

	let firstmonth = mode.field(",", 1);
	let maxperiod  = mode.field(",", 2);

	// if iconv then convert period (MM/YY or YYMM) to internal last date of month
	if (type == "ICONV") {
		// return the last day of the period (internal format)
		if (input0.contains("/")) {
			period = input0.field("/", 1) + 1;
			year   = input0.field("/", 2);
		} else {
			year   = input0.first(2);
			period = input0.last(2) + 1;
		}
		if (firstmonth and firstmonth.isnum()) {
			period += firstmonth - 1;
		}
		if (period > 12) {
			period -= 12;
			year = (year + 1).oconv("R(0)#2");
		} else {
			if (period < 1) {
				period += 12;
				year = (addcent4(year - 1)).oconv("R(0)#2");
			}
		}

		if (firstmonth >= 7) {
			year -= 1;
		}

		output = ("1/" ^ period ^ "/" ^ ("00" ^ year).last(2)).iconv("DE") - 1;
		return 0;
	}

	// if oconv then convert internal date to year:period
	let temp = input0.oconv("D2-E");
	year	 = temp.last(2);
	period	 = temp.b(4, 2);
	if (firstmonth and firstmonth.isnum()) {
		period -= firstmonth - 1;
		if (period < 1) {
			period += 12;
			year = (addcent4(year - 1)).oconv("R(0)#2");
		}
		period = ("00" ^ period).last(2);
	}

	if (year and firstmonth >= 7) {
		year += 1;
	}

	output = ("00" ^ year).last(2) ^ period;

	return 0;
}

libraryexit()
