//this program checks that exodus functions return the same results as PICKOS
//given a file generated by PICKOS containing function names, arguments and results

#include <exodus/program.h>
programinit()

function main() {
	printl("regress says 'Hello World!'");
	let osfilename = field(SENTENCE, " ", 2);
	//var osfile;
	// Rename var to avoid conflict with function call vs field access
	// e.g osfile(filename) vs osfile(fn) = x
	var osfilex;
	//if (not osopen(osfilename, osfile))
	if (not osopen(osfilename, osfilex))
		stop("cant open " ^ osfilename);
	var data;
	if (not osread(data, osfilename))
		stop("cant open " ^ osfilename);

	data.converter("\r\n", _RM _RM);
	let bit = data.first(100);
	var ix	= 0;
	var delimiter;
	int ln = 0;
	while (true) {
		++ln;
		if (not mod(ln, 1000))
			printl(ln);
		let line = data.substr2(ix, delimiter);
		// 		line.outputl();
		let output = line.field("\t", 1);
		let op	   = line.field("\t", 2);
		let inputx = line.field("\t", 3);
		let arg1   = line.field("\t", 4);
		// op.outputl();
		var test;
		// if (op=="LOCATEUSING") {
		// 	// 0 1     LOCATEUSING     BCCCB   A               B               -1      -1      -1
		// 	test=locateusing(inputx.f(,
		// }
		if (op == "OCONV") {
			test = oconv(inputx, arg1);
		} else if (op == "ISNUM") {
			test = inputx.isnum();
		} else {
			printl(line);
			stop();
		}
		if (test != output) {
			printt(ln, op, inputx.convert(FM ^ VM ^ SM, "^]\\"), arg1, output.convert(FM ^ VM ^ SM, "^]\\"), test.convert(FM ^ VM ^ SM, "^]\\"));
			printl();
		}
		if (not delimiter)
			break;
	}
	return 0;
}

programexit()
