#include <exodus/program.h>
programinit()

function main() {

	//Pass if no default database connection
	if (not connect()) {
		printl("No default db connection to perform db testing.");
		printl("Test passed");
		return 0;
	}

	var filenames = listfiles();

//	var dictconn;
//	if (dictconn.connect("exodus")) {
//		 ^ FM ^ connect("exodus").listfiles();

	for (var filename : filenames.unique()) {
		if (filename.index("xo_")) {
			printl("Deleting", filename);
			deletefile(filename);
		}
	}
	printl("Test passed");
	return 0;
}

programexit()

