#include <exodus/program.h>
programinit()

function main() {

	const var EXODUS_FUNCTOR_MAXNARGS = 20;
	//var outfilename="genfunctors.h";

	var head =
		"#ifndef ExodusFunctorSubroutine1_H\n"
		"#define ExodusFunctorSubroutine1_H\n"
		"\n"
		"//generated by genfunctors.cpp\n"
		"\n"
		"#include <exodus/mvfunctor.h>\n"
		"\n"
		"template<class T1>\n"
		"class ExodusFunctorSubroutine1 : private ExodusFunctorBase\n"
		"{\n"
		"public:\n"
		" ExodusFunctorSubroutine1(const std::string libname,const std::string funcname)\n"
		"	: ExodusFunctorBase(libname,funcname){}\n"
		"\n";

	var body =
		" VOIDORVAR operator() (T1 arg1)\n"
		" {\n"
		"	checkload();\n"
		"	typedef VOIDORVAR (*ExodusDynamic)(T1 arg1);\n"
		"	FUNCTIONRETURN ((ExodusDynamic) _pfunction)(arg1);\n"
		"SUBROUTINERETURN"
		" }\n"
		"\n";

	var foot =
		"};\n"
		"#endif\n";

	return 0;
}

/*
* output one templated class to implementing a functor
* mode: [in] is "function" (default) or "subroutine"
* nargs: [in] the number of arguments that the functor has
*/
subroutine genfunctor(in mode, in nargs) {

	var text = "";

	//template parameters
	var templatedecl = "";
	for (var argn = 1; argn <= nargs; ++argn) {
		templatedecl ^= ", class T" ^ argn;
	}
	var head2 = head;
	head2.swapper("<class T1>", "<" ^ templatedecl.substr(3) ^ ">");
	if (mode == "subroutine")
		head2.swapper("Subroutine1", "S" ^ nargs);
	else
		head2.swapper("Subroutine1", "F" ^ nargs);

	text ^= head2;

	//for each number of arguments build an operator
	for (int nargs2 = 0; nargs2 <= nargs; ++nargs2) {

		//build the argument declarations and definitions
		var operatordecl = "";
		var functiondecl = "";
		var functionargs = "";
		var argn;
		for (argn = 1; argn <= nargs2; ++argn) {
			operatordecl ^= ", T" ^ argn ^ " arg" ^ argn;
			functiondecl ^= ", T" ^ argn ^ " arg" ^ argn;
			functionargs ^= ", arg" ^ argn;
		}
		//add missing default parameters, but not to operator decl
		for (; argn <= nargs; ++argn) {
			functiondecl ^= ", T" ^ argn ^ " arg" ^ argn;
			functionargs ^= ", T" ^ argn ^ "()";
		}

		var body2 = body;
		body2.swapper("() (T1 arg1)", "() (" ^ operatordecl.substr(3) ^ ")");
		body2.swapper(")(T1 arg1)", ")(" ^ functiondecl.substr(3) ^ ")");
		body2.swapper("(arg1)", "(" ^ functionargs.substr(3) ^ ")");

		if (mode == "subroutine") {
			//subroutine
			body2.swapper("VOIDORVAR", "void");
			body2.swapper("FUNCTIONRETURN", "");
			body2.swapper("SUBROUTINERETURN", "\treturn;\n");
		} else {
			//function
			body2.swapper("VOIDORVAR", "var");
			body2.swapper("FUNCTIONRETURN", "return ");
			body2.swapper("SUBROUTINERETURN", "");
		}

		text ^= body2;
	}

	text ^= foot;

	var outputfilename = "xfunctor" ^ mode.substr(1, 1) ^ nargs ^ ".h";
	printl(outputfilename);
	oswrite(text, outputfilename);

	return 0;
}

//change to program to run as command line
//program() {
subroutine genfunctors() {

	for (int nargs = 0; nargs <= EXODUS_FUNCTOR_MAXNARGS; ++nargs) {
		genfunctor("function", nargs);
		genfunctor("subroutine", nargs);
	}
}

programexit()
