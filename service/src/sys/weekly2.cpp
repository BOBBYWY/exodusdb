#include <exodus/library.h>
libraryinit()

var firstmonth;
var firstdayofweek;
var maxperiod;//num
var temp;
var year;//num
var period;//num
var idate;//num

function main(in type, in input0, in mode, out output) {
	//c sys in,in,in,out
	//global all

	//eg mode is WEEKLY2,7,1,4
	//ie year starts on the first monday of july

	//[WEEKLY2,1,6] year starts january, week starts saturday

	//if no input0 then no output
	if (input0 eq "") {
		output = "";
		return 0;
	}

	//extract parameters
	firstmonth = mode.field(",", 1);
	firstdayofweek = mode.field(",", 2);
	maxperiod = 12;

	//if oconv then convert internal date to year:period
	///////////////////////////////////////////////////
	if (type eq "OCONV") {
		//get the calendar month and year
		temp = input0.oconv("D2/E");
		year = temp.substr(-2, 2);
		period = temp.substr(4, 2);

		//get the first day of the period for that calender month and year
		gosub getfirstdom();

		//if the date is less than the first day of that period
		// then put into the previous period
		if (input0 lt idate) {
			period -= 1;
			if (period lt 1) {
				period = 12;
				year -= 1;
				year = year.oconv("R(0)#2");
			}
		}

		output = year.oconv("R(0)#2") ^ period.oconv("R(0)#2");

		return 0;

	}

	//if iconv then convert period (MM/YY or YYMM) to internal last date of month
	////////////////////////////////////////////////////////////////////////////
	//return the last day of the period (internal format)
	if (input0.index("/")) {
		period = input0.field("/",1);
		year = input0.field("/", 2);
	} else {
		year = input0.substr(1, 2);
		period = input0.substr(-2, 2);
	}

	//get the next period
	period += 1;
	while (true) {
		///BREAK;
		if (not(period gt maxperiod)) break;
		period -= maxperiod;
		year += 1;
		year = year.oconv("R(0)#2");
	}//loop;

	//get the first day of the next period
	//if period=1 then
	// idate=iconv('1/1/':year,'D2/E')
	//end else
	gosub getfirstdom();
	// end

	//and subtract 1
	idate -= 1;

	output = idate;

	return 0;
}

subroutine getfirstdom() {

	//find the first day of the period
	idate = ("1/" ^ period ^ "/" ^ year).iconv("D2/E");
	//if period<>1 then
	while (true) {
		///BREAK;
		if (not((idate - 1).mod(7) + 1 ne firstdayofweek)) break;
		idate += 1;
	}//loop;
	// end
	return;
}

libraryexit()
