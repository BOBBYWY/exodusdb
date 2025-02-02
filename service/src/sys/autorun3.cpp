#include <exodus/library.h>
libraryinit()

#include <holiday.h>
#include <generalalerts.h>
#include <authorised.h>
#include <listen2.h>
#include <sysmsg.h>
#include <sendmail.h>
#include <listen4.h>

#include <sys_common.h>
#include <win_common.h>

var docids;
var options;
var inpath;
var docn;//num
var docid;
var xx;
var forceemail;
var toaddress;
var useraddress;
var marketcode;
var market;
var agp;
var holidaytype;
var workdate;
var tt;//num
var authtasks;
var title;
var datax;
var fromtime;
var daysago;//num
var xdate;//num
var opendate;//num
var printfilename;
var body;
var attachfilename;
var errormsg;
var voccmd;
var tracing;//num
var linkfilename2;
var requeststarttime;//num
var requeststoptime;//num
var printfile;

function main(in docids0="", in options0="") {
	//c sys "",""

	//LISTEN calls this every minute
	//print 'autorun'

	//clearcommon();

	#include <system_common.h>
	//global agp,market,marketcode,useraddress,inpath,tt,forceemail,voccmd,tracing

	#define request_ USER0
	#define iodat_ USER1
	#define response_ USER3
	#define msg_ USER4

	if (docids0.unassigned()) {
		docids = "";
	} else {
		docids = docids0;
	}
	if (options0.unassigned()) {
		options = "";
	} else {
		options = options0;
	}

	//used to email EXODUS if EXODUS user in list of recipients
	var sysmsgatexodus = "sysmsg@neosys.com";

	//used to email if run as EXODUS unless document's forced email address is set
	var devatexodus = "dev@neosys.com";

	//logging=@username='EXODUS'
	var logging = 0;

	var suppressemail = options.index("S");

	var users;
	if (not(users.open("USERS", ""))) {
		call fsmsg();
		return 0;
	}

	if (not(sys.markets.open("MARKETS", ""))) {
		if (not(APPLICATION eq "ACCOUNTS")) {
			call fsmsg();
		}
		sys.markets = "";
		return 0;
	}

	var datasetcode = SYSTEM.a(17);
	if (not datasetcode) {
		printl("========== DATASETCODE MISSING ==========");
	}
	var islivedb = not(SYSTEM.a(61));

	//allow one autorun per database - hopefully this wont overload the server
	var lockfilename = "DOCUMENTS";
	var lockfile = sys.documents;
	var lockkey = "%" ^ datasetcode ^ "%";
	if (not(lockrecord(lockfilename, lockfile, lockkey, "", 1))) {

		return 0;

	}

	//path to output reports
	var webpath = "";
	//if webpath else webpath='..\':'data\'
	if (not webpath) {
		webpath = "../" "data/";
	}
	webpath.converter("/", OSSLASH);
	if (webpath[-1] ne OSSLASH) {
		webpath ^= OSSLASH;
	}
	inpath = webpath ^ datasetcode ^ OSSLASH;

	//initdoc:
	////////
	if (docids) {
		docids.swapper(",", FM);
		docn = 0;
	} else {
		select(sys.documents);
	}
	var locked = 0;
	var ndocsprocessed = 0;

nextdoc:
////////

	if (locked) {
		call unlockrecord("DOCUMENTS", sys.documents, docid);
		locked = 0;
	}

	//dont process all documents one after the other within one
	//call of autorun to avoid overloading the subroutine stack cache etc
	//so docexit doesnt goto nextdoc .. it goes to exit
	if (ndocsprocessed gt 1) {
		gosub exit(lockfilename, lockfile, lockkey);
		return 0;
	}
	if (docids) {
		docn += 1;
		docid = docids.a(docn);
		if (not docid) {
			gosub exit(lockfilename, lockfile, lockkey);
			return 0;
		}
	} else {
		if (not(readnext(docid))) {
			gosub exit(lockfilename, lockfile, lockkey);
			return 0;
		}
	}

readdoc:
////////
	//to save processing time, dont lock initially until looks like it needs processing
	//then lock/read/check again in case other processes doing the same
	//depending on the autorunkey then this may be redundant

	//get document
	if (not(sys.document.read(sys.documents, docid))) {
		printl(docid.quote(), " document doesnt exist in AUTORUN3");
		//call ossleep(1000*1)
		goto nextdoc;
	}

	//only do saved and enabled documents for now
	//0/1 saved disabled/enabled. Blank=Ordinary documents
	if (not(sys.document.a(12))) {
		goto nextdoc;
	}

	if (logging) {
		print(docid, " ");
	}

	//determine current datetime
currdatetime:
/////////////
	var itime = var().time();
	var idate = var().date();
	//handle rare case where passes midnight between time() and date()
	if (var().time() lt itime) {
		goto currdatetime;
	}
	var currdatetime = idate + itime / 86400;

	//skip if scanning all docs and not time to process yet
	//allow processing individual docs manually despite timing/scheduling
	if (docids eq "") {

		//would be faster to work out nextdatetime once initially - but how to do it?
		//if not(docids) and currdatetime<nextdatetime then goto nextdoc

		var lastdatetime = sys.document.a(13);

		//skip if already run in the last 60 minutes. this is an easy way
		//to avoid reruns but maximum scheduling frequency is hourly
		if ((currdatetime - lastdatetime).abs() le 1 / 24.0) {
			goto nextdoc;
		}

		//scheduling in document all can be multivalued
		//21 min 0-59 (not used currently)
		//22 hour 0-23 (minimum hour if only one)
		//23 day of month 1-31 means 1st-31st
		//24 month of year 1-12 means Jan-Dec
		//25 day of week 1-7 means Mon-Sun
		//26 date
		//27 max number of times

		var restrictions = trim(sys.document.field(FM, 21, 7), FM, "B");
		restrictions.converter(",", VM);

		//skip if no restrictions applied yet
		if (restrictions eq "") {
			goto nextdoc;
		}

		//only run scheduled reports on live data (but do run queued reports)
		//queued reports have only maxtimes=1 set
		if (((restrictions ne (FM.str(6) ^ 1)) and not(islivedb)) and not(var("exodus.id").osfile())) {
			if (logging) {
				printl("scheduled report but not live db");
			}
			goto nextdoc;
		}

		//hour of day restrictions
		var hours = restrictions.a(2);
		if (hours ne "") {
			var hournow = itime.oconv("MT").substr(1, 2) + 0;

			//if one hour then treat it as a minimum hour
			if (hours.isnum()) {
				if (hournow lt hours.mod(24)) {
					if (logging) {
						printl("not yet hour");
					}
					goto nextdoc;
				} else {
					//ensure not done already today in a previous hour
					goto preventsameday;
				}

			//or specific multiple hours
			} else {
				if (not(hours.locate(hournow, xx))) {
					if (logging) {
						printl("wrong hour");
					}
					goto nextdoc;
				}
			}

		//if no hourly restrictions then skip if already run today
		} else {
preventsameday:
			if (currdatetime.floor() eq lastdatetime.floor()) {
				if (logging) {
					printl("already run today");
				}
				goto nextdoc;
			}
		}

		var date = idate.oconv("D/E");

		//day of month restrictions
		if (restrictions.a(3)) {
			if (not(restrictions.a(3).locate(date.field("/", 1) + 0, xx))) {
				if (logging) {
					printl("wrong day of month");
				}
				goto nextdoc;
			}
		}

		//month of year restrictions
		if (restrictions.a(4)) {
			if (not(restrictions.a(4).locate(date.field("/", 2) + 0, xx))) {
				if (logging) {
					printl("wrong month");
				}
				goto nextdoc;
			}
		}

		//day of week restrictions
		if (restrictions.a(5)) {
			if (not(restrictions.a(5).locate((idate - 1).mod(7) + 1, xx))) {
				if (logging) {
					printl("wrong day of week");
				}
				goto nextdoc;
			}
		}

		//date restrictions
		if (restrictions.a(6)) {
			if (not(restrictions.a(6).locate(idate, xx))) {
				if (logging) {
					printl("wrong date");
				}
				goto nextdoc;
			}
		}

		//would be better to work out next run time once - but how to work it out
		//if not(docids) and currdatetime<nextdatetime then goto nextdoc

	}

	//lock documents that need processing
	//but reread after lock in case another process has not started processing it
	if (not locked) {
		if (not(lockrecord("DOCUMENTS", sys.documents, docid))) {
			if (logging) {
				printl("locked");
			}
			goto nextdoc;
		}
		locked = 1;
		goto readdoc;
	}

	//register that the document has been processed
	//even if nobody present to be emailed
	sys.document(13) = currdatetime;
	if (sys.document.a(27) ne "") {
		sys.document(27) = sys.document.a(27) - 1;
	}

	// Delete once-off documents
	if (sys.document.a(27) ne "" and not(sys.document.a(27))) {
		sys.documents.deleterecord(docid);

	} else {
		sys.document.write(sys.documents, docid);
	}

	//force all emails to be routed to test address
	//if on development system they are ALWAYS routed
	//so this is mainly for testing on client systems
	forceemail = sys.document.a(30);
	//if not(forceemail) and @username='EXODUS' then forceemail=devATexodus

	// On disabled systems all autorun documents except once-off documents
	// go to neosys.com and do NOT go to the actual users
	// TODO Use a configurable email from SYSTEM
	if (not(sys.document.a(27)) && var("../../disabled.cfg").osfile())
		forceemail = devatexodus;

	//report is always run as the document owning user
	var runasusercode = sys.document.a(1);
	var userx;
	if (not(userx.read(users, runasusercode))) {
		if (not(runasusercode eq "EXODUS")) {
			printl("runas user ", runasusercode, " doesnt exist");
			goto nextdoc;
		}
		userx = "";
	}
	//allow running as EXODUS and emailing to sysmsg@neosys.com
	if (userx.a(7) eq "" and runasusercode eq "EXODUS") {
		userx = "EXODUS";
		userx(7) = sysmsgatexodus;
	}

	//HAS RECIPIENTS
	//if there are any recipients then
	//build email addresses of all recipients
	//depending on weekends/personal holidays etc
	//and skip if no recipients
	//recipients may get nothing if it is a
	//dynamically emailed report
	//they may get just summary report

	//HASNT RECIPIENTS
	//dynamically emailed reports need no recipents
	//The runasuser (if have emailaddress) will get
	//any output regardless of if they are on holiday

	var ccaddress = "";
	var usercodes = sys.document.a(14);
	if (usercodes eq "") {
		toaddress = userx.a(7);
	} else {
		toaddress = "";
		var nusers = usercodes.count(VM) + 1;
		var backwards = 1;
		for (var usern = nusers; usern >= 1; --usern) {

			//get the user record
			var usercode = usercodes.a(1, usern);
			if (not(userx.read(users, usercode))) {
				if (not(usercode eq "EXODUS")) {
					goto nextuser;
				}
				userx = "EXODUS";
			}

			//skip if user has no email address
			if (userx.a(7) eq "" and usercode eq "EXODUS") {
				userx(7) = sysmsgatexodus;
			}
			useraddress = userx.a(7);
			if (useraddress) {

				//if running as EXODUS always add user EXODUS
				//regardless of holidays - to allow testing on weekends etc
				//if usercode='EXODUS' then
				if (USERNAME eq "EXODUS" and usercode eq "EXODUS") {
					goto adduseraddress;

				//optionally skip people on holiday (even EXODUS unless running as EXODUS)
				} else {

					marketcode = userx.a(25);
					if (not marketcode) {
						marketcode = sys.company.a(30, 1);
					}
					market = marketcode;
					if (sys.markets) {
						if (not(market.read(sys.markets, marketcode))) {
							{}
						}
					}

					idate = var().date();
					agp = "";
					call holiday("GETTYPE", idate, usercode, userx, marketcode, market, agp, holidaytype, workdate);

					if (not(holidaytype)) {
adduseraddress:
						if (backwards) {
							toaddress.inserter(1, useraddress);
						} else {
							toaddress(-1) = useraddress;
						}
					}
				}

			}
nextuser:;
		} //usern;

		//skip if nobody to email to
		if (not toaddress) {
			if (logging) {
				printl("nobody to email");
			}
			goto nextdoc;
		}

		toaddress.converter(FM, ";");

	}

	//before running the document refresh the title, request and data
	var module = sys.document.a(31);
	var alerttype = sys.document.a(32);

	if (module and alerttype) {

		tt = module ^ ".ALERTS";

		//c++ variation
		if (not(VOLUMES)) {
			tt.lcaser();
			tt.converter(".", "");
		}

		generalalerts = tt;
		call generalalerts(alerttype, runasusercode, authtasks, title, request_, datax);

		//update the document and documents file if necessary
		call cropper(sys.document);
		var origdocument = sys.document;

		sys.document(2) = title;
		sys.document(5) = lower(module ^ "PROXY" ^ FM ^ USER0);
		sys.document(6) = lower(datax);

		call cropper(datax);
		if (sys.document ne origdocument) {
			sys.document.write(sys.documents, docid);
		}

	} else {
		authtasks = "";
	}

	//check if runasuser is authorised to run the task
	if (authtasks) {
		var ntasks = authtasks.count(VM) + 1;
		for (const var taskn : range(1, ntasks)) {
			var task = authtasks.a(1, taskn);
			if (not(authorised(task, msg_, "", runasusercode))) {
				USER4 = runasusercode.quote() ^ " is not authorised to do " ^ task;
				printl(msg_);
				goto nextdoc;
			}
		} //taskn;
	}

	//docinit:
	////////
	if (logging) {
		printl("running as ", runasusercode);
	}

	var fromdate = var().date();
	fromtime = ostime();

	ndocsprocessed += 1;

	//become the user so security is relative to the document "owner"
	var connection = "VERSION 3";
	connection(2) = "0.0.0.0";
	connection(3) = "SERVER";
	connection(4) = "";
	connection(5) = "";
	call listen2("BECOMEUSERANDCONNECTION", runasusercode, "", connection, xx);

	//request='EXECUTE':fm:'GENERAL':fm:'GETREPORT':fm:docid
	//voccmd='GENERALPROXY'
	request_ = raise("EXECUTE" ^ VM ^ sys.document.a(5));

	iodat_ = raise(sys.document.a(6));

	//override the saved period with a current period

	//get today's period
	var runtimeperiod = var().date().oconv("D2/E").substr(4, 5);
	if (runtimeperiod[1] eq "0") {
		runtimeperiod.splicer(1, 1, "");
	}
	//should backdate period to maximum open period for all selected companies
	//to avoid "year is not open" type messages
	//TODO

	USER1.swapper("{RUNTIME_PERIOD}", runtimeperiod);
	iodat_.swapper("{TODAY}", var().date());
	USER1.swapper("{7DAYSAGO}", var().date() - 7);
	iodat_.swapper("{14DAYSAGO}", var().date() - 14);
	USER1.swapper("{21DAYSAGO}", var().date() - 21);
	iodat_.swapper("{28DAYSAGO}", var().date() - 28);
	USER1.swapper("{30DAYSAGO}", var().date() - 30);
	iodat_.swapper("{60DAYSAGO}", var().date() - 60);
	USER1.swapper("{90DAYSAGO}", var().date() - 90);
	iodat_.swapper("{YESTERDAY}", var().date() - 1);
	USER1.swapper("{TOMORROW}", var().date() + 1);
	if (iodat_.index("{2WORKINGDAYSAGO}")) {
		daysago = 2;
		gosub getdaysago();
		USER1.swapper("{2WORKINGDAYSAGO}", xdate);
	}
	if (iodat_.index("{3WORKINGDAYSAGO}")) {
		daysago = 3;
		gosub getdaysago();
		USER1.swapper("{3WORKINGDAYSAGO}", xdate);
	}
	var closedperiod = sys.company.a(37);
	if (closedperiod) {
		opendate = iconv(closedperiod, sys.company.a(6)) + 1;
	} else {
		opendate = 11689;
	}
	iodat_.swapper("{OPERATIONS_OPEN_DATE}", opendate);

	//convert {TODAY-99} to today minus 99
	//and {TODAY+999} to today+999
	var sign = "-";
nextsign:
	tt = USER1.index("{TODAY" ^ sign);
	if (tt) {
		var t2 = (iodat_.substr(tt + 1, 999999)).field("}", 1);
		USER1.swapper("{" ^ t2 ^ "}", var().date() + t2.substr(6, 999999));
	}
	if (sign eq "-") {
		sign = "+";
		goto nextsign;
	}

	//run the report
	gosub exec();

	//docexit:
	////////

	if (not suppressemail) {

		//email only if there is an outputfile
		//ANALTIME2 emails everything out and returns 'OK ... ' in response
		//if dir(printfilename)<1> else goto nextdoc

		var subject = "EXODUS";
		//if repeatable then include report number to allow filtering
		if (sys.document.a(27) eq "") {
			subject ^= " " ^ docid;
		}
		subject ^= ": %RESULT%" ^ sys.document.a(2);

		//email it
		if (response_.substr(1, 2) ne "OK" or printfilename.osfile().a(1) lt 10) {

			//plain "OK" with no file means nothing to email
			if (USER3 eq "OK") {
				goto nextdoc;
			}

			body = "";
			body(-1) = response_;
			if (USER3.substr(1, 6) eq "Error:") {
				response_.splicer(1, 6, "Result:");
			}
			if (USER3.index("Error")) {
				subject ^= " ERROR";
				var(response_).oswrite("xyz.xyz");
			}
			//swap 'Error:' with 'Result:' in body
			body(-1) = ("Document: " ^ sys.document.a(2) ^ " (" ^ docid ^ ")").trim();
			body(-1) = "Database: " ^ SYSTEM.a(23) ^ " (" ^ SYSTEM.a(17) ^ ")";
			//swap '%RESULT%' with '* ' in subject
			subject.swapper("%RESULT%", "");

			//treat all errors as system errors for now
			//since autorun doesnt really know a user to send them to
			//NB programs should return OK+message if no report is required (eg "OK no ads found")
			if (USER3.substr(1, 2) eq "OK") {
				response_ = USER3.substr(3, 999999).trimf();
			} else {
				call sysmsg(subject ^ FM ^ body);
				goto nextdoc;
			}

			attachfilename = "";

		} else {

			var timetext = elapsedtimetext(fromdate, fromtime);

			//if ucase(printfilename[-4,4])='.XLS' then
			//locate ucase(field2(printfilename,'.',-1)) in 'XLS,CSV' using ',' setting xx then
			tt = (field2(printfilename, ".", -1)).lcase();
			if (tt.index("htm") and sys.document.a(33) ne "2") {
				//insert body from file
				body = "@" ^ printfilename;
				subject ^= " in " ^ timetext;
			} else {
				attachfilename = oscwd();
				if (not(VOLUMES)) {
					attachfilename ^= OSSLASH;
				}
				attachfilename ^= printfilename;
				body = timetext;
			}

			subject.swapper("%RESULT%", "");
		}
		body.swapper(FM, var().chr(13));

		// Option to force the actual email recipient
		var system117 = SYSTEM.a(117);
		if (forceemail)
			SYSTEM(117) = forceemail;

		print(" Emailing ", toaddress, ":");
		call sendmail(toaddress, ccaddress, subject, body, attachfilename, "", errormsg);
		print("done");

		// Restore original forced email or lack thereof
		SYSTEM(117) = system117;

		if (errormsg and errormsg ne "OK") {
			//call msg(errormsg)
			call sysmsg(errormsg);
			printl(errormsg);
		}

	}

	printl();
	goto nextdoc;
}

subroutine exec() {

	voccmd = USER0.a(2);

	tracing = 1;

	//generate a unique random output file
	while (true) {
		//linkfilename2=inpath:str(rnd(10^15),8)[1,8]
		linkfilename2 = inpath ^ ("00000000" ^ var(99999999).rnd()).substr(-8, 8);
		///BREAK;
		if (not(oslistf(linkfilename2 ^ ".*"))) break;
	}//loop;

	linkfilename2 ^= ".2";
	call oswrite("", linkfilename2);

	//turn interactive off in case running from command line
	//to avoid any reports prompting for input here
	var s33 = SYSTEM.a(33);
	SYSTEM(33) = 1;

	gosub exec2();

	//restore interactivity status
	SYSTEM(33) = s33;

	return;
}

subroutine exec2() {
	requeststarttime = ostime();
	//system<25>=requeststarttime
	//allow autorun processes to run for ever
	SYSTEM(25) = "";
	request_ = USER0.field(FM, 3, 99999);

	//localtime=mod(time()+@sw<1>,86400)
	//print @(0):@(-4):localtime 'MTS':' AUTORUN ':docid:
	//similar in LISTEN and AUTORUN
	printl();
	print(var().time().oconv("MTS"), " AUTORUN ", docid, " ", USERNAME, " ", request_.convert(FM, " "), " ", sys.document.a(2), ":");

	//print 'link',linkfilename2
	//print 'request',request
	//print 'iodat',iodat

	//following simulates LISTEN's 'EXECUTE'

	//provide an output file for the program to be executed
	//NB response file name for detaching processes
	//will be obtained from the output file name LISTEN2 RESPOND
	//this could be improved to work
	printfilename = linkfilename2;
	tt = oscwd();
	tt.splicer(-7, 7, "");
	if (printfilename.substr(1, tt.length()) eq tt) {
		printfilename.splicer(1, tt.length(), "../");
	}
	printfilename.converter("/", OSSLASH);

	//tt=printfilename[-1,'B.']
	tt = field2(printfilename, ".", -1);
	printfilename.splicer(-tt.length(), tt.length(), "htm");
	SYSTEM(2) = printfilename;
	//if tracing then
	// print datasetcode:' Waiting for output:':
	// end

	//execute the program
	//request, iodat and response are now passed and returned in @user0,1 and 3
	//other messages are passed back in @user4
	//execute instead of call prevents program crashes from crashing LISTEN
	response_ = "OK";
	win.valid = 1;
	USER4 = "";

	//pass the output file in linkfilename2
	//not good method, pass in system?
	if (var("LIST,SELECTJOURNALS").locateusing(",", USER0.a(1), xx)) {
		iodat_ = linkfilename2;
	}

	SYSTEM(117) = forceemail;

	execute(voccmd);

	SYSTEM(117) = "";

	//discard any stored input
	DATA = "";

	//detect memory corruption?
	//@user4='R18.6'
	if (msg_.index("R18.6")) {
		var halt = 1;
		USER4(-1) = "Corrupt temporary file. Restart Needed.";
		msg_(-1) = "EXODUS.NET TERMINATED";
	}

	//convert error message
	if (USER4.index(" IN INDEX.REDUCER AT ") or msg_.index(" IN RTP21 AT ")) {
		//@user4='Please select fewer records and/or simplify your request'
		call listen4(17, USER4);
	}

	//no records are not system errors
	if (USER3.substr(1, 9) eq "No record" or response_.substr(1, 7) eq "No item") {
		USER3.splicer(1, 0, "OK ");
		msg_ = "";
	}

	//send errors to exodus
	if (USER4.index("An internal error") or msg_.index("Error:")) {
		USER4.transfer(response_);
		goto sysmsgit;
	}

	//send errors to exodus
	if (USER3 eq "" or response_.substr(1, 2) ne "OK") {
		if (not USER3) {
			response_ = "No response from " ^ voccmd;
		}
sysmsgit:
		call sysmsg("AUTORUN " ^ docid ^ " " ^ sys.document.a(2) ^ FM ^ USER3);
	}

	call cropper(msg_);
	call cropper(response_);

	if (USER4) {
		USER1 = "";
		USER3 = "Error: " ^ msg_;
		gosub fmtresp();
	}

	if (response_ eq "") {
		//response='Error: No OK from ':voccmd:' ':request
		call listen4(18, USER3, voccmd);
		gosub fmtresp();
	}

	var rawresponse = response_;
	rawresponse.converter("\r\n", "|");

	//get the printfilename in case the print program changed it
	printfilename = SYSTEM.a(2);
	//and close it in case print program didnt (try to avoid sendmail attach errors)
	printfilename.osclose();
	if (tt.osopen(printfilename)) {
		tt.osclose();
	}
	var().osflush();

	//trace responded
	requeststoptime = ostime();
	if (tracing) {
		//print @(0):@(-4):'Responded in ':(requeststoptime-requeststarttime) 'MD20P':' SECS ':rawresponse
		print(" ", ((requeststoptime - requeststarttime).mod(86400)).oconv("MD20P"), "secs ", rawresponse);
		//do after "emailing" message
		//print str('-',79)
		//print linkfilename1
	}

	//make sure that the output file is closed
	if (printfile.osopen(printfilename)) {
		printfile.osclose();
	}

	return;
}

subroutine exit(in lockfilename,io lockfile,in lockkey) {

	call unlockrecord(lockfilename, lockfile, lockkey);

	return;
}

subroutine fmtresp() {

	//trim everything after <ESC> (why?)
	tt = USER3.index("<ESC>");
	if (tt) {
		response_ = USER3.substr(1, tt - 1);
	}

	//cannot remove since these may be proper codepage letters
	response_.converter("|", FM);
	USER3.converter(VM, FM);
	if (response_[1] eq FM) {
		USER3.splicer(1, 1, "");
	}
	response_.swapper(FM, "\r\n");

	return;
}

subroutine getdaysago() {
	var weekend = "67";
	marketcode = sys.company.a(30, 1);
	if (marketcode) {
		tt = marketcode.xlate("MARKETS", 9, "X");
		if (tt) {
			weekend = tt;
		}
	}

	xdate = var().date();
	while (true) {
		xdate -= 1;
		if (not(weekend.index((xdate - 1).mod(7) + 1))) {
			daysago -= 1;
		}
		///BREAK;
		if (not daysago) break;
	}//loop;

	return;
}

libraryexit()
