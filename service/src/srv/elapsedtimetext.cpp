#include <exodus/library.h>
libraryinit()

function main(in fromdate, in fromtime, io uptodate, io uptotime) {

	var text = "";

	if (uptodate.unassigned()) {
		uptodate = date();
	}
	if (uptotime.unassigned()) {
		uptotime = ostime();
	}
	// uptodate=date()
	// CALL DOSTIME(uptotime)

	// NSECS=INT(uptotime-fromTIME)
	var nsecs = uptotime - fromtime;
	// IF NSECS ELSE NSECS=1
	// uptodate=date()
	if (fromdate != uptodate) {
		nsecs += (uptodate - fromdate) * 24 * 3600;
	}

	// cater for bug where start date isnt known and time has crossed midnight
	// so the 2nd time is less than the first
	if (nsecs < 0) {
		nsecs += 86400;
	}

	let weeks = (nsecs / 604800).floor();
	nsecs -= weeks * 604800;

	let days = (nsecs / 86400).floor();
	nsecs -= days * 86400;

	let hours = (nsecs / 3600).floor();
	nsecs -= hours * 3600;

	let minutes = (nsecs / 60).floor();
	nsecs -= minutes * 60;

	if (weeks) {
		text(-1) = weeks ^ " week";
		if (weeks != 1) {
			text ^= "s";
		}
	}
	if (days) {
		text(-1) = days ^ " day";
		if (days != 1) {
			text ^= "s";
		}
	}
	if (hours) {
		text(-1) = hours ^ " hour";
		if (hours != 1) {
			text ^= "s";
		}
	}
	if (minutes) {
		text(-1) = minutes ^ " min";
		if (minutes != 1) {
			text ^= "s";
		}
	}
	if (not(hours) and minutes < 5) {
		if (nsecs) {
			if (minutes or (nsecs - 10 > 0)) {
				nsecs = nsecs.oconv("MD00P");
			} else {
				// nsecs=(nsecs 'MD40P')+0
				nsecs = (nsecs.oconv("MD30P")) + 0;
				if (nsecs.starts(".")) {
					nsecs.prefixer("0");
				}
			}
			if (nsecs) {
				text(-1) = nsecs ^ " sec";
				if (nsecs != 1) {
					text ^= "s";
				}
			} else if (not minutes) {
zero:
				text(-1) = "< 1 ms";
			} else {
				text(-1) = "exactly";
			}
		} else {
			if (not minutes) {
				goto zero;
			}
			text(3) = "exactly";
		}
	}

	text.replacer(FM ^ FM ^ FM, FM);
	text.replacer(FM ^ FM, FM);
	text.replacer(FM, ", ");

	return text;
}

libraryexit()
