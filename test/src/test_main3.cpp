#undef NDEBUG //because we are using assert to check actual operations that cannot be skipped in release mode testing
#include <cassert>
#include <sstream>
//#include <cmath>

#include <exodus/program.h>

// TESTING
// =======
//
// if it runs ok with no static assertion failures
//
//   compile test_main && test_main
//
// then test no changes in output with the following
//
//   test_main1 &> t_test_main1 && diff test_main1.out t_test_main1
//
// there will be a limited number of differences due to time/date/random output

// DEVELOPING
// ==========
//
// if you add to or change the output in any way,
// which includes adding comments at the top due since program line numbers are part of output during error reporting
// then update the expected output file
//
//   test_main1 &> test_main1.out

programinit()

function main()
{

	{
		var unitx;
		//printl("x", amountunit("USD",ID), "y", ID.quote());
		var amount = amountunit("USD",unitx);
		printl("amount:", amount.quote(), "unit:", unitx.quote());

		assert(test_amountunit("1.23456E-6USD", 1.23456E-6, "USD"));
		assert(test_amountunit("", "", ""));
		assert(test_amountunit(" ", "", " "));
		assert(test_amountunit("E", "", "E"));
		assert(test_amountunit("E-", "", "E-"));
		assert(test_amountunit("E-3", "E-3", ""));
		assert(test_amountunit("123", 123, ""));
		assert(test_amountunit("123X", 123, "X"));
		assert(test_amountunit("123 ", 123, " "));

		// Check spaces
		assert(test_amountunit("123.456XYZZZZ", 123.456, "XYZZZZ"));
		assert(test_amountunit(" 123.456XYZZZZ", " 123.456", "XYZZZZ"));
		assert(test_amountunit("123.456 XYZZZZ", 123.456, " XYZZZZ"));
		assert(test_amountunit("123.456XYZZZZ ", 123.456, "XYZZZZ "));

	}

	{
	    //stop("Test passed");

	    print(AT(-1));
	    var cursor=getcursor();
	    //no cursor if no terminal
	    assert(cursor eq "\x1b[1;1H" or cursor eq "");

	    //should show 1;1 in top left
	    for (const var ii : range(0, 12)) {
	        print(AT(ii,ii));
	        print(getcursor().substr(2));
	    }

	    print(AT(7,24));
	    cursor=getcursor();
	    assert(cursor eq "\x1b[24;7H" or cursor eq "");

	    //check that we cannot assign or copy from an unassigned variable
	    {
	        var x1;
	        try {
	            var x3=x1;//unassigned var
	            assert(false && "var x3 = x1 should throw unassigned error)");
	        }
	        catch (MVUnassigned e) {
	            assert(true || "var v3 = v1) does give unassigned error)");
	        }
	    }

	    //check that we cannot assign or copy from an unassigned dim element
	    {
	        var x1;
	        try {
	            var x2(x1);//unassigned dim element
	            assert(false && "var x2(x1) should throw unassigned error)");
	        }
	        catch (MVUnassigned e) {
	            assert(true || "x2(x1) does give unassigned error)");
	        }
	    }

	    //output time and date to stderr
	    errputl("Using Exodus library version:" ^ var().version());
	    date().oconv("D").errputl("Date is:");
	    time().oconv("MTS").errputl("Time is:");

	    //14:30:46 04 JAN 2021
	    assert(timedate().match("\\d{2}:\\d{2}:\\d{2} \\d{2} [A-Z]{3} \\d{4}"));

	    printl("----------------------------------------------");


	    assert("abc"_var == "abc");
	    assert(12345_var == 12345);
	    assert(1234.567_var == 1234.567);

	    var v=123.456;
	    assert(int(v) != v);
	    assert(int(v) == 123);
	    assert(double(v) == v);
	    assert(double(v) == 123.456);
	    assert(floor(v) != v);
	    assert(floor(v) == int(v));

	    v="123.456";
	    assert(int(v) != v);
	    assert(int(v) == 123);
	    assert(double(v) == v);
	    assert(double(v) == 123.456);
	    assert(floor(v) != v);
	    assert(floor(v) == int(v));

	    v=123456;
	    v=v/1000;
		TRACE(v)
	    assert(int(v) != v);
	    assert(int(v) == 123);
	    assert(double(v) == v);
	    assert(double(v) == 123.456);
	    assert(floor(v) != v);
	    assert(floor(v) == int(v));

	    double d=1234.567;
	    //false && d;
	    assert(var(d)=="1234.567");
	    assert(var(-d)=="-1234.567");
	    assert(int(var(d))==1234);
	    assert(int(var(-d))==-1235);


	    //test accessing var as a range of fields separated by FM
	    {
	        var fields = "a1" _FM_ "b2" _FM_ "c3";
	        var fieldn=0;
	        for (var field : fields) {
	            fieldn++;
	            if (fieldn==1)
	                assert(field=="a1");
	            else if (fieldn==2)
	                assert(field=="b2");
	            if (fieldn==3)
	                assert(field=="c3");
	        }
	    }

	}

	{

	    printl("sizeof");
	    printl("int:      ",(int)sizeof(int));
	    printl("long:     ",(int)sizeof(long));
	    printl("long int: ",(int)sizeof(long int));
	    printl("long long:",(int)sizeof(long long));
	    printl("float:    ",(int)sizeof(float));
	    printl("double:   ",(int)sizeof(double));
	    printl();
	    printl("string:   ",(int)sizeof(std::string));
	    printl("long long:",(int)sizeof(long long));
	    printl("double:   ",(int)sizeof(double));
	    printl("uint:     ",(int)sizeof(uint));
	    printl("uint:     ",(int)sizeof(uint),      " padding1");
	    printl("long long:",(int)sizeof(long long), " padding2");
	    printl("           ==");
	    printl("var:      ",(int)sizeof(var));

	    auto size = sizeof(std::string);
	    auto capacity = std::string().capacity();
	    auto small = std::string(capacity, '*');
	    auto big = std::string(capacity + 1, '*');

	    std::cout << "\nstd:string implementation\n";
	    std::cout << "sizeof  : " << size << std::endl;
	    std::cout << "Capacity: " << capacity << std::endl;
	    std::cout << "Small   : " << small.capacity() << std::endl;
	    std::cout << "Big     : " << big.capacity() << std::endl;

	    //test tcase and fcase
	    assert(var("top of the world").tcase()=="Top Of The World");
	    printl(var("top of the world").tcase().fcase());

	    printl(round(var("6000.50")/20,2));
	    assert(round(var("6000.50")/20,2)==300.03);

	}


    {
        //exodus currently configured to use 2. for very great speed (see mv.cpp USE_RYU) BUT it will include over accurate figures without any rounding
        //1. precision 16 using sstring. SLOW (1850ns)
        //2. full precision using VERY FAST ryu algorithm (450ns)
        //full accuracy of ryu shows why calculations always must be rounded after every calculation because every calculation introduces more inaccuracies.
        TRACE(var(10.0/3.0))
        printl(var(10.0/3.0).length());

        //ryu full accuracy shows the inevitable inaccuracies inherent in using doubles for financial calculations
        //assert(var(10.0/3.0).toString() == "3.3333333333333335");

        //old exodus crude reduction in precision to 16 using sstream hides inaccuracies when there are only few calculations.
        //assert(var(10.0/3.0).toString() == "3.333333333333333");

        assert(
            var(10.0/3.0).toString()
            //old exodus crude reduction in precision to 16 using sstream hides inaccuracies when there are only few calculations.
            ==  "3.333333333333333"

            ||
            var(10.0/3.0).toString()
            //ryu full accuracy shows the inevitable inaccuracies inherent in using doubles for financial calculations
            ==  "3.3333333333333335"

//          ||
//          var(10.0/3.0).toString()
//          //g++11 to_chars implementation full accuracy shows the inevitable inaccuracies inherent in using doubles for financial calculations
//          ==  "3.333333333333335"
        );

        assert(var(10.0/3.0*2.0).toString() == "6.666666666666667");
        assert(var(10.0/3.0*3.0).toString() == "10");

        var x=10;
        var y=3;
        var z=3;
        assert(x/y*z==x);
        assert(x/y*z==10);
        assert(x/y*z==10.0);
        assert(x/y*z=="10");
        assert(x/y*z=="10.0");

        x=10.0;
        y=3.0;
        z=3.0;
        assert(x/y*z==x);
        assert(x/y*z==10);
        assert(x/y*z==10.0);
        assert(x/y*z=="10");
        assert(x/y*z=="10.0");

        x="10.0";
        y="3.0";
        z="3.0";
        assert(x/y*z==x);
        assert(x/y*z==10);
        assert(x/y*z==10.0);
        assert(x/y*z=="10");
        assert(x/y*z=="10.0");

        x="10";
        y="3";
        z="3";
        assert(x/y*z==x);
        assert(x/y*z==10);
        assert(x/y*z==10.0);
        assert(x/y*z=="10");
        assert(x/y*z=="10.0");
    }

	{
	    assert(capitalise("aa \"AAA\"") == "Aa \"AAA\"");
	    assert(capitalise("aa \"aAa\"") == "Aa \"aAa\"");
	    assert(capitalise("aa 'AAA'") == "Aa 'AAA'");
	    assert(capitalise("aa 'aAa'") == "Aa 'aAa'");
	    assert(capitalise("1aA 2AA 3Aa 4aa") == "1AA 2AA 3AA 4AA");

	    //raw strings
	    var value=R"('1')";
	    assert(value.swap("'",R"(\')")=="\\'1\\'");

	    //check conversion of unprintable field marks to unusual ASCII characters
	    //except TM which is ESC
	    std::ostringstream stringstr;
	    stringstr << var(_RM_ _FM_ _VM_ _SM_ _TM_  _STM_);
	    std::cout << stringstr.str() << std::endl;
	    //assert(var(stringstr.str()) == "~^]\\[|");
	    assert(var(stringstr.str()) == "~^]}" "\x1B" "|");
	}


	{
		/* now using epsilon to judge small numbers and differences see MVeq()

		//no longer applicable
		//exodus comparisions and conversion to book ignores numbers less than 1e-13
		//pick/arev bool and equality checks on doubles ignore less than 0.0001
		//but comparison operators work exactly (to max binary precision?)
		var smallestno = 1e-10;
		var toosmallno = smallestno/10;
		assert(smallestno);
		assert(smallestno>0);
		assert(smallestno!=0);
		assert(!toosmallno);
		assert(toosmallno==0);
		assert(!(toosmallno>0));
		assert(smallestno == SMALLEST_NUMBER);
		*/

		var d1=1.2;
		d1++;
		assert(d1==2.2);
		++d1;
		assert(d1==3.2);

		var i1=1;
		i1++;
		assert(i1==2.0);
		++i1;
		assert(i1==3.0);

		d1=1.2;
		d1+=1;
		assert(d1==2.2);
		d1+=1.1;
		assert(d1==3.3);

		//exodus accidentally equipped std:string with most of the attributes of a var (this could be viewed as a good thing)
		// the feature was removed by using "hidden friends" ie placing all global friend operator functions inside the body of the var class
		//
		//assert(d1.toString()==3.3);//strange that this compiles and works (no longer after switching to "hidden friends" in exodus mv.h
		//printl(std::string("1.1")+3.3);//strange this compiles! we have equipped std:string with the ability to convert to numeric for addition with a double!

		i1=1;
		i1+=1;
		assert(i1==2.0);
		i1+=1.1;
		assert(i1==3.1);

		printl(1e-14);
		printl(var(0));
		//assert(1e-14==var(0));

		printl();
		printl("=== print ===");

		printl("l");
		printl();
		printl("l");
		printt();
		printl("l");
		printl("l");

		print("p");
		print("pl");
		printl();
		printl("l");
		printt("t");
		printl("l");

		print("0", "p");
		print("0", "pl");
		printl();
		printl("0", "l");
		printt("0", "t");
		printl("0", "l");

		print("0", "1", "2", "p");
		print("0", "1", "2", "pl");
		printl();
		printl("0", "1", "2", "l");
		printt("0", "1", "2", "t");
		printl("0", "1", "2", "l");

		print<'-'>("0", "1", "2", "p");
		print<'-'>("0", "1", "2", "pl");
		printl();
		printl<'-'>("0", "1", "2", "l");
		printt<'-'>("0", "1", "2", "t");
		printl<'-'>("0", "1", "2", "l");

		print(RM, FM, VM, SM, TM, STM);
		printl(RM, FM, VM, SM, TM, STM);

		{
			static char const sep[] = ", ";
			print<sep>("0", "1", "2", "p");
			print<sep>("0", "1", "2", "pl");
			printl();
			printl<sep>("0", "1", "2", "l");
			printt<sep>("0", "1", "2", "t");
			printl<sep>("0", "1", "2", "l");
		}

		output();
		output(0);
		output("0", "1", "2", "p");
		output("0", "1", "2", "pl");
		output(RM, FM, VM, SM, TM, STM);
		outputl();
		outputl(0);
		outputl("0", "1", "2", "l");
		outputl("0", "1", "2", "l");
		outputl(RM, FM, VM, SM, TM, STM);

		printl();
		printl("=== errput ===");

		//errput();
		errputl("l");
		errputl();
		errputl("l");
		errputl("l");
		errputl("l");

		errput("p");
		errput("pl");
		errputl();
		errputl("l");
		errputl("l");

		errput("0", "p");
		errput("0", "pl");
		errputl();
		errputl("0", "l");
		errputl("0", "l");

		errput("0", "1", "2", "p");
		errput("0", "1", "2", "pl");
		errputl();
		errputl("0", "1", "2", "l");
		errputl("0", "1", "2", "l");

		printl();
		printl("=== logput ===");

		logputl("l");
		logputl();
		logputl("l");
		logputl("l");
		logputl("l");

		logput("p");
		logput("pl");
		logputl();
		logputl("l");
		logputl("l");

		logput("0", "p");
		logput("0", "pl");
		logputl();
		logputl("0", "l");
		logputl("0", "l");

		logput("0", "1", "2", "p");
		logput("0", "1", "2", "pl");
		logputl();
		logputl("0", "1", "2", "l");
		logputl("0", "1", "2", "l");

		assert(crop(_VM_ _FM_) eq "");
		assert(crop("xxx" _VM_ _FM_) eq "xxx");
		assert(crop("aaa" _VM_ _FM_ "bbb") eq ("aaa" _FM_ "bbb"));
		assert(crop("aaa" _FM_ "bbb" _FM_ _VM_ _SM_ _FM_ "ddd") eq ("aaa" _FM_ "bbb" _FM_ _FM_ "ddd"));
		assert(crop("aaa" _FM_ "bbb" _FM_ _VM_ _SM_ _FM_ _RM_ "ddd") eq ("aaa" _FM_ "bbb" _RM_ "ddd"));

		assert(crop("aa" _VM_ _FM_ "bb" _FM_)=="aa" _FM_ "bb");
		assert(crop("aa" _SM_ _VM_ _FM_ "bb" _FM_)=="aa" _FM_ "bb");
		assert(crop(_FM_ "aa" _VM_ _FM_ "bb" _FM_)==_FM_ "aa" _FM_ "bb");
		assert(crop(_FM_ "aa" _VM_ _FM_ "bb" _FM_ _VM_)==_FM_ "aa" _FM_ "bb");
		assert(crop(_FM_ "aa" _VM_ _FM_ _VM_ "bb" _FM_ _VM_)==_FM_ "aa" _FM_ _VM_ "bb");
		assert(crop(_FM_ "aa" _VM_ _FM_ "bb" _FM_ _RM_)==_FM_ "aa" _FM_ "bb");
		assert(crop(_FM_ _RM_ "aa" _VM_ _FM_ "bb" _FM_ _RM_)==_RM_ "aa" _FM_ "bb");

		var errmsg;
		//if (not dbcreate("steve",errmsg))
		//	errmsg.outputl();
		//if (not dbdelete("steve",errmsg))
		//	errmsg.outputl();
	}

	{

		COMMAND.outputl("COMMAND-");
		assert(COMMAND eq "service" or COMMAND.substr(1,9) eq "test_main");

		//test int/string changes after inc/dec (should really check MANY other ops)
		var nn=0;
		assert(nn.toString() eq "0");//nn now has string available internally
		++nn;//this should destroy the internal string
		assert(nn.toString() eq "1");//the string should be recalculated after the ++
		nn++;
		assert(nn.toString() eq "2");
		--nn;
		assert(nn.toString() eq "1");
		nn--;
		assert(nn.toString() eq "0");
		nn+=1;
		assert(nn.toString() eq "1");
		nn-=1;
		assert(nn.toString() eq "0");

		//same for float/string
		nn=0.1;
		printl(nn);
		assert(nn.toString() eq "0.1");//nn now has string available internally
		++nn;//this should destroy the internal string
		printl(nn);
		assert(nn.toString() eq "1.1");//the string should be recalculated after the ++
		nn++;
		printl(nn);
		assert(nn.toString() eq "2.1");
		--nn;
		printl(nn);
		assert(nn.toString() eq "1.1");
		nn--;
		printl(nn);
		//assert(nn.toString() eq "0.1"); //fails at precision 15 = 0.1000000000000001
		assert(nn eq "0.1"); //succeeds numerically
		nn+=1.0;
		printl(nn);
		assert(nn.toString() eq "1.1");
		nn-=1.0;
		printl(nn);
		//assert(nn.toString() eq "0.1"); //fails at precision 15 = 0.1000000000000001
		assert(nn eq "0.1"); //succeeds numerically

		//test remove

		var rem="abc"^FM^"xyz";
		var ptr=2;
		var sep;

		//bc|5|2
		//xyz|9|0
		//abc|5|2
		//xyz|9|0
		//|999|0
		//|999|0

		var result=rem.substr2(ptr,sep);
		assert(var(result ^ "|" ^ ptr ^ "|" ^ sep).outputl() eq "bc|5|2");

		result=rem.substr2(ptr,sep);
		assert(var(result ^ "|" ^ ptr ^ "|" ^ sep).outputl() eq "xyz|9|0");

		ptr=0;

		result=rem.substr2(ptr,sep);
		assert(var(result ^ "|" ^ ptr ^ "|" ^ sep).outputl() eq "abc|5|2");

		result=rem.substr2(ptr,sep);
		assert(var(result ^ "|" ^ ptr ^ "|" ^ sep).outputl() eq "xyz|9|0");

		ptr=999;

		result=rem.substr2(ptr,sep);
		assert(var(result ^ "|" ^ ptr ^ "|" ^ sep).outputl() eq "|999|0");

		result=rem.substr2(ptr,sep);
		assert(var(result ^ "|" ^ ptr ^ "|" ^ sep).outputl() eq "|999|0");

		//test unquote
		assert(unquote("\"This is quoted?\"") eq "This is quoted?");

	}

	printl("Test passed");

	return 0;
}

bool test_amountunit(in input, in amount, in unitcode) {

	printl("amountunit :", input.quote(), "amount :", amount.quote(), "unit :", unitcode.quote());

	// Check amount
	var unitcode2;
	if (amountunit(input, unitcode2) ne amount)
		return false;

	// Check unit
	return unitcode2 eq unitcode;

}

programexit()
