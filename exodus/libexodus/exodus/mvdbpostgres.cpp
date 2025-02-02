//https://wiki.postgresql.org/wiki/Don't_Do_This

/*
Copyright (c) 2009 steve.bush@neosys.com

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

// in case of migration to db2 or mysql here is a very detailed comparison in 2006
// http://www.ibm.com/developerworks/data/library/techarticle/dm-0606khatri/

// http://www.postgresql.org/docs/8.3/static/libpq-envars.html
// PGHOST/PGHOSTADDR
// PGPORT
// PGDATABASE
// PGUSER
// PGPASSWORD
// PGPASSFILE defaults to ~/.pgpass
// PGSERVICE in pg_service.conf in PGSYSCONFDIR

// TODO sql quoting of all parameters like dbname filename etc to prevent sql injection.

// 0=silent, 1=errors, 2=warnings, 3=results, 4=tracing, 5=debugging
// 0=silent, 1=errors, 2=warnings, 3=failures, 4=successes, 5=debugging ?

// MSVC requires exception handling (eg compile with /EHsc or EHa?) for delayed dll loading
// detection

////ALN:TODO: REFACTORING NOTES
// Proposed to split content of mvdbpostres.cpp into 3 layers (classic approach):
//	mvdbfuncs.cpp - api things, like mvglobalfuncs.cpp
//	mvdbdrv.cpp - base abstract class mv_db_drv or MvDbDrv to define db access operations (db
// driver interface); 	mvdbdrvpostgres.cpp - subclass of mv_db_drv, specific to PostgreSQL things
// like
// PGconn and PQfinish; 	mvdbdrvmsde.cpp - possible subclass of mv_db_drv, specific to MSDE (AKA
// MSSQL Express); 	mvdblogic.cpp - intermediate processing (most of group 2) functions.
// Proposed refactoring would:
//		- improve modularity of the Exodus platform;
//		- allow easy expanding to other DB engines.

#include <unordered_map>
#include <map>

#if defined _MSC_VER  // || defined __CYGWIN__ || defined __MINGW32__
#define WIN32_LEAN_AND_MEAN
#include <DelayImp.h>
#include <windows.h>
#else
#endif

#include <cstring>	//for strcmp strlen
#include <iostream>

// see exports.txt for all PQ functions
#include <libpq-fe.h>  //in postgres/include

//#include <arpa/inet.h>//for ntohl()

#include "MurmurHash2_64.h"	 // it has included in mvdbconns.h (uint64_t defined)

#include <exodus/mv.h>
#include <exodus/mvdbconns.h>  // placed as last include, causes boost header compiler errors
//#include <exodus/mvenvironment.h>
//#include <exodus/mvutf.h>

namespace exodus {

// the idea is for exodus to have access to one standard database without secret password
static var defaultconninfo =
	"host=127.0.0.1 port=5432 dbname=exodus user=exodus "
	"password=somesillysecret connect_timeout=10";

// Deleter function to close connection and connection cache object
// this is also called in the destructor of MVConnectionsCache
// MAKE POSTGRES CONNECTIONS ARE CLOSED PROPERLY OTHERWISE MIGHT RUN OUT OF CONNECTIONS!!!
// TODO so make sure that we dont use exit(n) anywhere in the programs!
static void connection_DELETER_AND_DESTROYER(PGconn* pgconn) {
	auto pgconn2 = pgconn;
    // at this point we have good new connection to database
    if (DBTRACE) {
        var("").logput("DBTR PQFinish");
		std::clog << pgconn << std::endl;
	}
	//var("========================== deleting connection ==============================").errputl();
	PQfinish(pgconn2);	// AFAIK, it destroys the object by pointer
					//	delete pgp;
}

#define DEBUG_LOG_SQL if (DBTRACE) sql.squote().logputl("SQL0 ");

#define DEBUG_LOG_SQL1 if (DBTRACE) {((this->assigned() ? (this->substr(1,50)) : "") ^ " | " ^ sql.swap("$1", var(paramValues[0]).substr(1,50).squote())).logputl("SQL1 ");}

// this front end C interface is based on postgres
// http://www.postgresql.org/docs/8.2/static/libpq-exec.html
// doc/postgres/libpq-example.html
// src/test/examples/testlibpq.c

//(backend pg functions extract and dateextract are based
// on samples in src/tutorial/funcs_new.c)

// SQL EXAMPLES
// create or replace view testview as select exodus_extract_text(data,1,0,0) as field1 from test;
// create index testfield1 on test (exodus_extract_text(data,1,0,0));
// select * from testview where field1  > 'aaaaaaaaa';
// analyse;
// explain select * from testview where field1  > 'aaaaaaaaa';
// explain analyse select * from testview where field1  > 'aaaaaaaaa';e

thread_local int thread_default_data_mvconn_no = 0;
thread_local int thread_default_dict_mvconn_no = 0;
//thread_local var thread_connparams = "";
thread_local var thread_dblasterror = "";
thread_local MVConnections thread_connections(connection_DELETER_AND_DESTROYER);
thread_local std::unordered_map<std::string, std::string> thread_file_handles;

//std::string getpgresultcell(PGresult* pgresult, int rown, int coln) {
//	return std::string(PQgetvalue(pgresult, rown, coln), PQgetlength(pgresult, rown, coln));
//}

var getpgresultcell(PGresult* pgresult, int rown, int coln) {
	return var(PQgetvalue(pgresult, rown, coln), PQgetlength(pgresult, rown, coln));
}

//void dump_pgresult(PGresult* pgresult) {
//
//	/* first, print out the attribute names */
//	printf("----------");
//	printf("%p", pgresult);
//	printf("----------\n");
//	int nFields = PQnfields(pgresult);
//	int i,j;
//	for (i = 0; i < nFields; i++)
//		printf("%-15s", PQfname(pgresult, i));
//	printf("\n");
//	printf("----------\n");
//
//	/* next, print out the rows and columns */
//	for (i = 0; i < PQntuples(pgresult); i++)
//	{
//		for (j = 0; j < nFields; j++)
//			printf("%-15s", PQgetvalue(pgresult, i, j));
//		printf("\n");
//	}
//	printf("==========\n");
//}

// Given a file name or handle, extract filename, standardize utf8, lowercase and change . to _
var get_normal_filename(CVR filename_or_handle) {
	return filename_or_handle.a(1).normalize().lcase().convert(".", "_").swap("dict_","dict.");
}

// Detect sselect command words that are values like quoted words or plain numbers.
// eg "xxx" 'xxx' 123 .123 +123 -123
static const var valuechars("\"'.0123456789-+");

// Get a unique lock number per filename & key to communicate to other connections
uint64_t mvdbpostgres_hash_filename_and_key(CVR filehandle, CVR key) {

	std::string fileandkey = get_normal_filename(filehandle);
	fileandkey.push_back(' ');
	fileandkey.append(key.normalize().toString());

	// TODO .. provide endian identical version
	// required if and when exodus processes connect to postgres on a DIFFERENT host
	// although currently (Sep2010) use of symbolic dictionaries requires exodus to be on the
	// SAME host
	return MurmurHash64((char*)fileandkey.data(), int(fileandkey.size() * sizeof(char)), 0);

}

// MVresult is a RAII/SBRM container of a PGresult pointer and calls PQclear on destruction
// Safe and automatic clearing of heap data generated by the postgresql C language PQlib api
class MVresult {

   public:

	// Owns the PGresult object on the stack. Initially none.
	PGresult* pgresult_ = nullptr;

	// Pointer into pgresult if there are many rows
	// e.g. in readnextx after FETCH nn
	int rown_ = 0;

	// Default constructor results in a nullptr
	MVresult() = default;

	// Allow construction from a PGresult*
	MVresult(PGresult* pgresult)
	:
	pgresult_(pgresult) {

		if (DBTRACE) {
			var("MVresult (c) own ").logput();
			std::clog << pgresult_ << std::endl;
		}
	}

	// Prevent copy
	MVresult(const MVresult&) = delete;

	// Move contructor transfers ownership of the PGresult
	MVresult(MVresult&& mvresult)
	:
	rown_(mvresult.rown_) {

		//if already assigned to a different PGresult then clear the old one first
		if (pgresult_ && pgresult_ != mvresult.pgresult_) {
			if (DBTRACE) {
				var("MVresult (m) PQC ").logput();
				std::clog << pgresult_ << std::endl;
			}
			PQclear(pgresult_);
			pgresult_ = nullptr;
		}

		pgresult_=mvresult.pgresult_;
		mvresult.pgresult_ = nullptr;

		if (DBTRACE) {
			var("MVresult (m) own ").logput();
			std::clog << pgresult_ << std::endl;
		}
	}

	// Allow assignment from a PGresult*
	void operator=(PGresult* pgresult) {

		rown_ = 0;

		//if already assigned to a different PGresult then clear the old one first
		if (pgresult_ && pgresult_ != pgresult) {
			if (DBTRACE) {
				var("MVresult (=) PQC ").logput();
				std::clog << pgresult_ << std::endl;
			}
			PQclear(pgresult_);
		}

		pgresult_ = pgresult;
		if (DBTRACE) {
			var("MVresult (=) own ").logput();
			std::clog << pgresult_ << std::endl;
		}
	}

	// Allow implicit conversion to a PGresult*
	// No tiresome .get() required as in unique_ptr
	operator PGresult*() const {
		return pgresult_;
	}

	// Destructor calls PQClear
	// This the whole point of having MVresult.
	~MVresult() {
		if (pgresult_) {
			if (DBTRACE) {
				var("MVresult (~) PQC ").logput();
				std::clog << pgresult_ << std::endl;
			}
			PQclear(pgresult_);
		}
	}
};

//very few entries so map will be much faster than unordered_map
//thread_local std::unordered_map<std::string, MVresult> thread_mvresults;
thread_local std::map<std::string, MVresult> thread_mvresults;

int get_mvconn_no(CVR dbhandle) {

	if (!dbhandle.assigned()) {
		// var("get_mvconn_no() returning 0 - unassigned").logputl();
		return 0;
	}
	var mvconn_no = dbhandle.a(2);
	if (mvconn_no.isnum()) {
		/// var("get_mvconn_no() returning " ^ mvconn_no).logputl();
		return mvconn_no;
	}

	// var("get_mvconn_no() returning 0 - not numeric").logputl();

	return 0;
}

int get_mvconn_no_or_default(CVR dbhandle) {

	int mvconn_no = get_mvconn_no(dbhandle);
	if (mvconn_no)
		return mvconn_no;

	// otherwise get the default connection
	if (!mvconn_no) {

		//if (DBTRACE and dbhandle.assigned()) {
		//	dbhandle.logputl("DBTR get_mvconn_no_or_default=");
		//}

		//dbhandle MUST always arrive in lower case to detect if "dict."
		bool isdict = dbhandle.unassigned() ? false : dbhandle.starts("dict.");
		//bool isdict = dbhandle.unassigned() ? false : (dbhandle.starts("dict.") || dbhandle.starts("DICT."));
		//bool isdict = false;

		if (isdict)
			mvconn_no = thread_default_dict_mvconn_no;
		else
			mvconn_no = thread_default_data_mvconn_no;

		// var(mvconn_no).logputl("mvconn_no2=");

		// otherwise try the default connection
		if (!mvconn_no) {

			var defaultdb;

			//look for dicts in the following order
			//1. $EXO_DICT if defines
			//2. db "dict" if present
			//3. the default db connection
			if (isdict) {
				defaultdb.osgetenv("EXO_DICT");
				if (!defaultdb)
					//must be the same in mvdbpostgres.cpp and dict2sql
					defaultdb="exodus";
			} else {
				defaultdb = "";
			}
			//TRACE(defaultdb)

			//try to connect
			if (defaultdb.connect())
				mvconn_no = get_mvconn_no(defaultdb);

			//if cannot connect then for dictionaries look on default connection
			else if (isdict) {

				//attempt a default connection if not already done
				if (!thread_default_data_mvconn_no) {
					defaultdb = "";
					defaultdb.connect();
				}

				mvconn_no = thread_default_data_mvconn_no;
			}

			//save default dict/data connections
			if (isdict) {
				thread_default_dict_mvconn_no = mvconn_no;
				if (DBTRACE) {
					var(mvconn_no).logputl("DBTR NEW DEFAULT DICT CONN ");
				}
			}
			else {
				thread_default_data_mvconn_no = mvconn_no;
				if (DBTRACE) {
					var(mvconn_no).logputl("DBTR NEW DEFAULT DATA CONN ");
				}
			}
		}

		//save the connection number in the dbhandle
		//if (mvconn_no) {
		//	dbhandle.unassigned("");
		//	dbhandle.r(2, mvconn_no);
		//}

	}

	return mvconn_no;
}

// var::get_pgconnection()
// 1. return the associated db connection
// this could be a previously opened filevar, a previous connected connectionvar
// or any variable previously used for a default connection
// OR
// 2. return the thread-default connection
// OR
// 3. do a default connect if necessary
//
// NB in case 2 and 3 the connection id is recorded in the var
// use void pointer to avoid need for including postgres headers in mv.h or any fancy class
// hierarchy (assumes accurate programming by system programmers in exodus mvdb routines)
PGconn* get_pgconnection(CVR dbhandle) {

	// var("--- connection ---").logputl();
	// get the connection associated with *this
	int mvconn_no = get_mvconn_no_or_default(dbhandle);
	// var(mvconn_no).logputl("mvconn_no1=");

	// otherwise error
	if (!mvconn_no)
		throw MVDBException("pgconnection() requested when not connected.");

	if (DBTRACE) {
		std::cout << std::endl;
		PGconn* pgconn=thread_connections.get_pgconnection(mvconn_no);
		std::clog << "CONN " << mvconn_no << " " << pgconn << std::endl;
	}

	// return the relevent pg_connection structure
	auto pgconn = thread_connections.get_pgconnection(mvconn_no);
	//TODO error abort if zero
	return pgconn;

}

// gets lock_table, associated with connection, associated with this object
MVConnection* get_mvconnection(CVR dbhandle) {
	int mvconn_no = get_mvconn_no_or_default(dbhandle);
	if (!mvconn_no)
		throw MVDBException("get_mvconnection() attempted when not connected");

	return thread_connections.get_mvconnection(mvconn_no);
}

void var::lasterror(CVR msg) const {
	// no checking for speed
	// THISIS("void var::lasterror(CVR msg")
	// ISSTRING(msg)

	// tcache_get (tc_idx=12) at malloc.c:2943
	// 2943    malloc.c: No such file or directory.
	// You have heap corruption somewhere -- someone is running off the end of an array or
	// dereferencing an invalid pointer or using some object after it has been freed. EVADE
	// error for now by commenting next line

	thread_dblasterror = msg;
}

var var::lasterror() const {
	return thread_dblasterror ?: "";
}

static bool get_mvresult(CVR sql, MVresult& mvresult, PGconn* pgconn);

#if defined _MSC_VER  //|| defined __CYGWIN__ || defined __MINGW32__
LONG WINAPI DelayLoadDllExceptionFilter(PEXCEPTION_POINTERS pExcPointers) {
	LONG lDisposition = EXCEPTION_EXECUTE_HANDLER;

	PDelayLoadInfo pDelayLoadInfo =
		PDelayLoadInfo(pExcPointers->ExceptionRecord->ExceptionInformation[0]);

	switch (pExcPointers->ExceptionRecord->ExceptionCode) {
		case VcppException(ERROR_SEVERITY_ERROR, ERROR_MOD_NOT_FOUND):
			printf("mvdbpostgres: %s was not found\n", pDelayLoadInfo->szDll);
			break;
			/*
		   case VcppException(ERROR_SEVERITY_ERROR, ERROR_PROC_NOT_FOUND):
			  if (pdli->dlp.fImportByName) {
					  printf("Function %s was not found in %sn",
					  pDelayLoadInfo->dlp.szProcName, pDelayLoadInfo->szDll);
			  } else {
			  printf("Function ordinal %d was not found in %sn",
					  pDelayLoadInfo->dlp.dwOrdinal, pDelayLoadInfo->szDll);
			  }
			  break;
		*/
		default:
			// Exception is not related to delay loading
			printf("Unknown problem %s\n", pDelayLoadInfo->szDll);
			lDisposition = EXCEPTION_CONTINUE_SEARCH;
			break;
	}

	return (lDisposition);
}

// msvc uses a special mode to catch failure to load a delay loaded dll that is incompatible with
// the normal try/catch and needs to be put in simple global function with no complex objects (that
// require standard c++ try/catch stack unwinding?) maybe it would be easier to manually load it
// using dlopen/dlsym implemented in var as var::load and var::call
// http://msdn.microsoft.com/en-us/library/5skw957f(vs.80).aspx
bool msvc_PQconnectdb(PGconn** pgconn, const std::string& conninfo) {
	// connect or fail
	__try {
		*pgconn = PQconnectdb(conninfo.c_str());
	} __except (DelayLoadDllExceptionFilter(GetExceptionInformation())) {
		return false;
	}
	return true;
}

#endif

var var::build_conn_info(CVR conninfo) const {
	// priority is
	// 1) given parameters //or last connection parameters
	// 2) individual environment parameters
	// 3) environment connection string
	// 4) config file parameters
	// 5) hard coded default parameters

	var result(conninfo);
	// if no conninfo details provided then use last connection details if any
	//if (!conninfo)
	//	result = thread_connparams;

	// otherwise search for details from exodus config file
	// if incomplete connection parameters provided
	if (not result.index("host=") or not result.index("port=") or not result.index("dbname=") or
		not result.index("user=") or not result.index("password=")) {

		// discover any configuration file parameters
		// TODO parse config properly instead of just changing \n\r to spaces!
		var configfilename = "";
		var home = "";
		if (home.osgetenv("HOME"))
			configfilename = home ^ OSSLASH ^ ".config/exodus/exodus.cfg";
		else if (home.osgetenv("USERPROFILE"))
			configfilename ^= home ^ OSSLASH ^ "Exodus\\.exodus";
		var configconn = "";
		if (!configconn.osread(configfilename))
			configconn.osread("exodus.cfg");
		//postgres ignores after \n?
		configconn.converter("\r\n","  ");

		// discover any configuration in the environment
		var envconn = "";
		var temp;
		if (temp.osgetenv("EXO_CONN") && temp)
			envconn ^= " " ^ temp;

		// specific variable are appended ie override
		if (temp.osgetenv("EXO_HOST") && temp)
			envconn ^= " host=" ^ temp;

		if (temp.osgetenv("EXO_PORT") && temp)
			envconn ^= " port=" ^ temp;

		if (temp.osgetenv("EXO_USER") && temp)
			envconn ^= " user=" ^ temp;

		if (temp.osgetenv("EXO_DATA") && temp) {
			envconn.replacer(R"(dbname\s*=\s*\w*)", "");
			envconn ^= " dbname=" ^ temp;
		}

		if (temp.osgetenv("EXO_PASS") && temp)
			envconn ^= " password=" ^ temp;

		if (temp.osgetenv("EXO_TIME") && temp)
			envconn ^= " connect_timeout=" ^ temp;

		result = defaultconninfo ^ " " ^ configconn ^ " " ^ envconn ^ " " ^ result;
	}
	return result;
}

// var connection;
// connection.connect2("dbname=exodusbase");
bool var::connect(CVR conninfo) {

	THISIS("bool var::connect(CVR conninfo")
	// nb dont log/trace or otherwise output the full connection info without HIDING the
	// password
	assertDefined(function_sig);
	ISSTRING(conninfo)

	var fullconninfo = conninfo.trimf().trimb();

	//use *this if conninfo not specified;
	bool isdefault = false;
	if (!fullconninfo) {
		if (this->assigned())
			fullconninfo = *this;
		isdefault = !fullconninfo;
	}

	//add dbname= if missing
	if (fullconninfo && !fullconninfo.index("="))
		fullconninfo = "dbname=" ^ fullconninfo.lcase();

	fullconninfo = build_conn_info(fullconninfo);

	if (DBTRACE) {
		//fullconninfo.replace(R"(password\s*=\s*\w*)", "password=**********").logputl("DBTR var::connect( ) ");
		conninfo.replace(R"(password\s*=\s*\w*)", "password=**********").logputl("DBTR var::connect( ) ");
	}

	PGconn* pgconn;
	for (;;) {

#if defined _MSC_VER  //|| defined __CYGWIN__ || defined __MINGW32__
		if (not msvc_PQconnectdb(&pgconn, fullconninfo.var_str)) {
			var libname = "libpq.dl";
			// var libname="libpq.so";
			var errmsg="ERROR: mvdbpostgres connect() Cannot load shared library " ^ libname ^
				". Verify configuration PATH contains postgres's \\bin.";
			this->lasterror(errmsg);
			return false;
		};
#else
		//connect
		pgconn = PQconnectdb(fullconninfo.var_str.c_str());
#endif

		//connected OK
		if (PQstatus(pgconn) == CONNECTION_OK || fullconninfo)
			break;

		// required even if connect fails according to docs
		PQfinish(pgconn);

		// try again with default conninfo
		fullconninfo = defaultconninfo;

	}

	// failed to connect so return false
	if (PQstatus(pgconn) != CONNECTION_OK) {

		var errmsg = "ERROR: mvdbpostgres connect() Connection to database failed: " ^ var(PQerrorMessage(pgconn));

		this->lasterror(errmsg);

		// required even if connect fails according to docs
		PQfinish(pgconn);

		return false;
	}

// abort if multithreading and it is not supported
#ifdef PQisthreadsafe
	if (!PQisthreadsafe()) {
		// TODO only abort if environmentn>0
		throw MVDBException("connect(): Postgres PQ library is not threadsafe");

	}
#endif

	// at this point we have good new connection to database

	// cache the new connection handle
	int mvconn_no = thread_connections.add_connection(pgconn, fullconninfo.var_str);
	//(*this) = conninfo ^ FM ^ conn_no;
	if (!this->assigned())
		(*this) = "";
	if (not this->a(1))
		//this->r(1,fullconninfo.field(" ",1));
		this->r(1,fullconninfo.field2("dbname=",-1).field(" ",1));
	this->r(2, mvconn_no);
	this->r(3, mvconn_no);

	if (DBTRACE) {
		fullconninfo.replace(R"(password\s*=\s*\w*)", "password=**********").logputl("DBTR var::connect() OK ");
		this->logput("DBTR var::connect() OK ");
		std::clog << " " << pgconn << std::endl;
	}

	// this->outputl("new connection=");

	// set default connection - ONLY IF THERE ISNT ONE ALREADY
	if (isdefault && !thread_default_data_mvconn_no) {
		thread_default_data_mvconn_no = mvconn_no;
		if (DBTRACE) {
			this->logputl("DBTR NEW DEFAULT DATA CONN " ^ var(mvconn_no) ^ " on ");
		}

	}

	// save last connection string (used in startipc())
	//thread_connparams = fullconninfo;

	// doesnt work
	// need to set PQnoticeReceiver to suppress NOTICES like when creating files
	// PQsetErrorVerbosity(pgconn, PQERRORS_TERSE);
	// but this does
	// this turns off the notice when creating tables with a primary key
	// DEBUG5, DEBUG4, DEBUG3, DEBUG2, DEBUG1, LOG, NOTICE, WARNING, ERROR, FATAL, and PANIC
	//this->sqlexec(var("SET client_min_messages = ") ^ (DBTRACE ? "LOG" : "WARNING"));
	this->sqlexec(var("SET client_min_messages = ") ^ (DBTRACE ? "LOG" : "WARNING"));

	return true;
}

// conn1.attach("filename1^filename2...");
bool var::attach(CVR filenames) {

	THISIS("bool var::attach(CVR filenames")
	assertDefined(function_sig);
	ISSTRING(filenames)

	//option to attach all dict files
	var filenames2;
	if (filenames == "dict") {
		filenames2 = "";
		var allfilenames = this->listfiles();
		for (var filename : allfilenames) {
			if (filename.substr(1, 5) == "dict.") {
				filenames2 ^= filename ^ FM;
			}
		}
		filenames2.popper();
	}
	else {
		filenames2 = filenames;
	}

	// cache file handles in thread_file_handles
	var notattached_filenames = "";
	for (var filename : filenames2) {
		var filename2 = get_normal_filename(filename);
		var file;
		if (file.open(filename2,*this)) {
			// Similar code in dbattach and open
			thread_file_handles[filename2] = file.var_str;
			if (DBTRACE)
				file.logputl("DBTR var::attach() ");
		}
		else {
			notattached_filenames ^= filename2 ^ " ";
		}
	}

	//fail if anything not attached
	if (notattached_filenames) {
		var errmsg = "ERROR: mvdbpostgres/attach: " ^ notattached_filenames ^ "cannot be attached on connection " ^ (*this).a(1).quote();
		this->lasterror(errmsg);
		return false;
	}

	return true;
}

// conn1.detach("filename1^filename2...");
void var::detach(CVR filenames) {

	THISIS("bool var::detach(CVR filenames")
	assertDefined(function_sig);
	ISSTRING(filenames)

	for (var filename : filenames) {
		// Similar code in detach and deletefile
		thread_file_handles.erase(get_normal_filename(filename));
	}
	return;
}

// if this->obj contains connection_id, then such connection is disconnected with this-> becomes UNA
// Otherwise, default connection is disconnected
void var::disconnect() {

	THISIS("bool var::disconnect()")
	assertDefined(function_sig);

	if (DBTRACE)
		(this->assigned() ? *this : var("")).logputl("DBTR var::disconnect() ");

	var mvconn_no = get_mvconn_no(*this);
	if (!mvconn_no)
		mvconn_no = thread_default_data_mvconn_no;

	if (mvconn_no) {

		//disconnect
		thread_connections.del_connection((int)mvconn_no);
		var_typ = VARTYP_UNA;

		// if we happen to be disconnecting the same connection as the default connection
		// then reset the default connection so that it will be reconnected to the next
		// connect this is rather too smart but will probably do what people expect
		if (mvconn_no == thread_default_data_mvconn_no) {
			thread_default_data_mvconn_no = 0;
			if (DBTRACE) {
				var(mvconn_no).logputl("DBTR var::disconnect() DEFAULT CONN FOR DATA ");
			}
		}

		// if we happen to be disconnecting the same connection as the default connection FOR DICT
		// then reset the default connection so that it will be reconnected to the next
		// connect this is rather too smart but will probably do what people expect
		if (mvconn_no == thread_default_dict_mvconn_no) {
			thread_default_dict_mvconn_no = 0;
			if (DBTRACE) {
				var(mvconn_no).logputl("DBTR var::disconnect() DEFAULT CONN FOR DICT ");
			}
		}
	}
	return;
}

void var::disconnectall() {

	THISIS("bool var::disconnectall()")
	assertDefined(function_sig);

	var mvconn_no = get_mvconn_no(*this);
	if (!mvconn_no)
		mvconn_no = 2;

	if (DBTRACE)
		mvconn_no.logputl("DBTR var::disconnectall() >= ");

	thread_connections.del_connections(mvconn_no);

	if (thread_default_data_mvconn_no >= mvconn_no) {
		thread_default_data_mvconn_no = 0;
		if (DBTRACE) {
			var(mvconn_no).logputl("DBTR var::disconnectall() DEFAULT CONN FOR DATA ");
		}
	}

	if (thread_default_dict_mvconn_no >= mvconn_no) {
		thread_default_dict_mvconn_no = 0;
		if (DBTRACE) {
			var(mvconn_no).logputl("DBTR var::disconnectall() DEFAULT CONN FOR DICT ");
		}
	}

	return;
}

// open filehandle given a filename on current thread-default connection
// we are using strict filename/filehandle syntax (even though we could use one variable for both!)
// we store the filename in the filehandle since that is what we need to generate read/write sql
// later usage example:
// var file;
// var filename="customers";
// if (not file.open(filename)) ...

// connection is optional and default connection may be used instead
bool var::open(CVR filename, CVR connection /*DEFAULTNULL*/) {

	THISIS("bool var::open(CVR filename, CVR connection)")
	assertDefined(function_sig);
	ISSTRING(filename)

	var filename2 = get_normal_filename(filename);

	// filename dos or DOS  means osread/oswrite/osdelete
	if (filename2.var_str.size() == 3 && filename2.var_str == "dos") {
		//(*this) = "dos";
		var_str = "dos";
		var_typ = VARTYP_NANSTR;
		return true;
	}

	// Either use connection provided
	var connection2;
	if (connection) {
		connection2 = connection;
	}
	else {

		// Or use any preopened or preattached file handle if available
	    auto entry = thread_file_handles.find(filename2);
    	if (entry != thread_file_handles.end()) {
			//(*this) = thread_file_handles.at(filename2);
			auto cached_file_handle = entry->second;

			// Make sure the connection is still valid otherwise redo the open
			auto pgconn = get_pgconnection(cached_file_handle);
			if (! pgconn) {
				thread_file_handles.erase(filename2);
				//var(cached_file_handle).errputl("==== Connection cache INVALID = ");
			} else {
				//var(cached_file_handle).errputl("==== Connection cache VALID   = ");
				var_str = cached_file_handle;
				var_typ = VARTYP_NANSTR;
				if (DBTRACE)
					this->logputl("DBTR open() cached or attached ");
				return true;
			}
		}

		// Or determine connection from filename in case filename is a handle
		//use default data or dict connection
		connection2 = filename2;

	}

	//if (DBTRACE) {
	//	connection2.logputl("DBTR var::open-1 ");
	//}

	var tablename;
	var schema;
	var and_schema_clause;
	if (filename2.index(".")) {
		schema = filename2.field(".",1);
		tablename = filename2.field(".",2,999);
		and_schema_clause = " AND table_schema = " ^ schema.squote();
	} else {
		schema = "public";
		tablename = filename2;
		//no schema filter allows opening temporary files with exist in various pg_temp_xxxx schemata
		//and_schema_clause = "";
		and_schema_clause = " AND table_schema != 'dict'";
	}
	// 1. look in information_schema.tables
	var sql =
		"\
		SELECT\
		EXISTS	(\
    		SELECT 	table_name\
    		FROM 	information_schema.tables\
    		WHERE   table_name = " ^ tablename.squote() ^ and_schema_clause ^ "\
				)";
	var result;
	connection2.sqlexec(sql, result);
	//result.convert(RM,"|").logputl("result=");

	//if (DBTRACE) {
	//	connection2.logputl("DBTR var::open-2 ");
	//}

	// 2. look in materialised views
	// select matviewname from pg_matviews where matviewname = '...';
	if (result[-1] != "t") {
		sql =
			"\
			SELECT\
			EXISTS	(\
	    		SELECT 	matviewname as table_name\
	    		FROM 	pg_matviews\
	    		WHERE\
						matviewname = '" ^
			filename2 ^
			"'\
					)\
		";
		connection2.sqlexec(sql, result);
	}

	//failure if not found
	if (result[-1] != "t") {
		var errmsg = "ERROR: mvdbpostgres 2 open(" ^ filename.quote() ^
					 ") file does not exist.";
		this->lasterror(errmsg);
		return false;
	}
	// */

	//this->lasterror();

	// var becomes a filehandle containing the filename and connection no
	(*this) = filename2 ^ FM ^ get_mvconn_no_or_default(connection2);
	filename2.var_str.push_back(FM_);
	filename2.var_str.append(std::to_string(get_mvconn_no_or_default(connection2)));
	var_str = filename2.var_str;
	var_typ = VARTYP_NANSTR;

	// Cache the filehandle so future opens return the same
	// Similar code in dbattach and open
	thread_file_handles[filename2] = var_str;

	if (DBTRACE)
		this->logputl("DBTR var::open-3 ");

	return true;
}

void var::close() {

	THISIS("void var::close()")
	assertString(function_sig);
	/*TODO
		if (var_typ!=VARTYP_UNA) QMClose(var_int);
	*/
}

bool var::readv(CVR filehandle, CVR key, const int fieldno) {
	//THISIS("bool var::readv(CVR filehandle,CVR key,const int fieldno)")
	//assertDefined(function_sig);
	//ISSTRING(filehandle)
	//ISSTRING(key)

	if (!this->read(filehandle, key))
		return false;

	var_str = this->a(fieldno).var_str;
	var_typ = VARTYP_STR;

	return true;
}

bool var::reado(CVR filehandle, CVR key) {

	THISIS("bool var::reado(CVR filehandle,CVR key)")
	assertDefined(function_sig);
	//ISSTRING(filehandle)
	//ISSTRING(key)
	ISSTRING(filehandle)
	ISSTRING(key)

	// check cache first, and return any cached record
	int mvconn_no = get_mvconn_no_or_default(filehandle);
	if (!mvconn_no)
		throw MVDBException("get_mvconn_no() failed");

	// Attempt to get record from the cache
	// TODO cache non-existent records as empty
	std::string cachedrecord =
		thread_connections.getrecord(mvconn_no, filehandle.a(1).var_str, key.var_str);
	if (!cachedrecord.empty()) {

		//(*this) = cachedrecord;
		var_str = std::move(cachedrecord);
		var_typ = VARTYP_STR;

		//this->lasterror();

		return true;

	}

	// ordinary read
	bool result = this->read(filehandle, key);

	//cache the ordinary read if successful
	if (result)
		this->writeo(filehandle, key);

	return result;
}

bool var::writeo(CVR filehandle, CVR key) const {

	THISIS("bool var::writeo(CVR filehandle,CVR key)")
	assertString(function_sig);
	ISSTRING(filehandle)
	ISSTRING(key)

	// update cache
	// virtually identical code in read and write/update/insert/delete
	int mvconn_no = get_mvconn_no_or_default(filehandle);
	if (!mvconn_no)
		throw MVDBException("get_mvconn_no() failed");

	thread_connections.putrecord(mvconn_no, filehandle.a(1).var_str, key.var_str, var_str);

	return true;
}

bool var::deleteo(CVR key) const {

	THISIS("bool var::deleteo(CVR key)")
	assertString(function_sig);
	ISSTRING(key)

	// update cache
	// virtually identical code in read and write/update/insert/delete
	int mvconn_no = get_mvconn_no_or_default(*this);
	if (!mvconn_no)
		throw MVDBException("get_mvconn_no() failed");

	thread_connections.delrecord(mvconn_no, this->a(1).var_str, key.var_str);

	return true;
}

bool var::read(CVR filehandle, CVR key) {

	THISIS("bool var::read(CVR filehandle,CVR key)")
	assertDefined(function_sig);
	ISSTRING(filehandle)
	ISSTRING(key)

	//amending var_str invalidates all flags
	//var_typ = VARTYP_STR;
	//var_typ = VARTYP_UNA;
	//var_str.resize(0);

	// LEAVE RECORD UNTOUCHED UNLESS RECORD IS SUCCESSFULLY READ
	// initialise the record to unassigned (actually empty string at the moment)
	// unless record and key are the same variable
	// in which case allow failure to read to leave the record (key) untouched
	//if (this != &key) {
	//	var_typ = VARTYP_UNA;
	//	//var_typ = VARTYP_STR;
	//	var_str.resize(0);
	//}

	// lower case key if reading from dictionary
	// std::string key2;
	// if (filehandle.substr(1,5).lcase()=="dict.")
	//	key2=key.lcase().var_str;
	// else
	//	key2=key.var_str;
	std::string key2 = key.normalize().var_str;

	// filehandle dos or DOS means osread/oswrite/osdelete
	if (filehandle.var_str.size() == 3 && (filehandle.var_str == "dos" || filehandle.var_str == "DOS")) {
		//return this->osread(key2);  //.convert("\\",OSSLASH));
		//use osfilenames unnormalised so we can read and write as is
		return this->osread(key);  //.convert("\\",OSSLASH));
	}

	// asking to read DOS file! do osread using key as osfilename!
	if (filehandle == "") {
		var errmsg = "read(...) filename not specified, probably not opened.";
		this->lasterror(errmsg);
		throw MVDBException(errmsg);
	}

	// reading a magic special key returns all keys in the file in natural order
	if (key == "%RECORDS%") {
		var sql = "SELECT key from " ^ get_normal_filename(filehandle) ^ ";";

		auto pgconn = get_pgconnection(filehandle);
		if (! pgconn)
			return false;

		MVresult mvresult;
		if (!get_mvresult(sql, mvresult, pgconn))
			return false;

		// *this = "";
		var_str.clear();
		var_typ = VARTYP_STR;

		var keyn;
		int ntuples = PQntuples(mvresult);
		for (int tuplen = 0; tuplen < ntuples; tuplen++) {
			if (!PQgetisnull(mvresult, tuplen, 0)) {
				var key = getpgresultcell(mvresult, tuplen, 0);
				//skip metadata keys starting and ending in % eg "%RECORDS%"
				if (key[1] != "%" && key[-1] != "%") {
					if (this->length() <= 65535) {
						if (!this->locatebyusing("AR", _FM_, key, keyn))
							this->inserter(keyn, key);
					} else {
						var_str.append(key.var_str);
						var_str.push_back(FM_);
					}
				}
			}
		}

		//remove any trailing FM
		if (var_str.back() == FM_)
			var_str.pop_back();

		//this->lasterror();

		return true;
	}

	// get filehandle specific connection or fail
	auto pgconn = get_pgconnection(filehandle);
	if (!pgconn)
		return false;

	// Parameter array
	const char* paramValues[] = {key2.data()};
	int paramLengths[] = {int(key2.size())};

	var sql = "SELECT data FROM " ^ get_normal_filename(filehandle) ^ " WHERE key = $1";

	DEBUG_LOG_SQL1
	MVresult mvresult = PQexecParams(pgconn,
											// TODO: parameterise filename
											sql.var_str.c_str(), 1, /* one param */
											nullptr,					/* let the backend deduce param type */
											paramValues, paramLengths,
											0,	 // text arguments
											0);	 // text results

	// Handle serious errors
	if (PQresultStatus(mvresult) != PGRES_TUPLES_OK) {
		var sqlstate = var(PQresultErrorField(mvresult, PG_DIAG_SQLSTATE));
		var errmsg =
			"read(" ^ filehandle.convert("." _FM_, "_^").swap("dict_","dict.").quote() ^ ", " ^ key.quote() ^ ")";
		if (sqlstate == "42P01")
			errmsg ^= " File doesnt exist";
		else
			errmsg ^= var(PQerrorMessage(pgconn)) ^ " sqlstate:" ^ sqlstate;
		;
		this->lasterror(errmsg);
		throw MVDBException(errmsg);
	}

	if (PQntuples(mvresult) < 1) {
		//leave record (this) untouched if record cannot be read
		// *this = "";

		this->lasterror("ERROR: mvdbpostgres read() record does not exist " ^
						   key.quote());
		return false;
	}

	// A serious error
	if (PQntuples(mvresult) > 1) {
		var errmsg = "ERROR: mvdbpostgres read() SELECT returned more than one record";
		throw MVDBException(errmsg);
	}

	*this = getpgresultcell(mvresult, 0, 0);

	return true;
}

var var::hash(const uint64_t modulus) const {

	THISIS("var var::hash() const")
	assertDefined(function_sig);
	assertString(function_sig);
	// ISNUMERIC(modulus)

	// https://softwareengineering.stackexchange.com/questions/49550/which-hashing-algorithm-is-best-for-uniqueness-and-speed

	// not normalizing for speed
	// std::string tempstr=this->normalize();

	// uint64_t
	// hash64=MurmurHash64((wchar_t*)fileandkey.data(),int(fileandkey.size()*sizeof(wchar_t)),0);
	uint64_t hash64 =
		MurmurHash64((char*)var_str.data(), int(var_str.size() * sizeof(char)), 0);
	if (modulus)
		return var_int = hash64 % modulus;
	else
		return var_int = hash64;
}

// file doesnt not have to exist since all locks are actually numerical hashes
//
// Returns
// 0  - Failure
// 1  - Success
// "" - Failure - already locked and not in a transaction
// 2  - Success - already locked and in a transaction
var var::lock(CVR key) const {

	// on postgres, repeated locks for the same thing (from the same connection) succeed and
	// stack up they need the same number of unlocks (from the same connection) before other
	// connections can take the lock unlock returns true if a lock (your lock) was released and
	// false if you dont have the lock NB return "" if ALREADY locked on this connection


	THISIS("var var::lock(CVR key) const")
	assertDefined(function_sig);
	ISSTRING(key)

	PGconn* pgconn = get_pgconnection(*this);
	if (!pgconn)
		return false;

	auto mvconnection = get_mvconnection(*this);

	// TODO consider preventing begintrans if lock cache not empty
	auto hash64 = mvdbpostgres_hash_filename_and_key(*this, key);

	// if already in lock cache
	//
	// then OUTSIDE transaction then FAIL with "" to indicate already locked
	//
	// then INSIDE transaction then SUCCEED with 2 to indicate already locked
	//
	// postgres allows and stacks up multiple locks whereas multivalue databases dont
	if (mvconnection->connection_locks.contains(hash64)) {
		if (mvconnection->in_transaction)
			return 2; //SUCCESS TYPE ALREADY CACHED
		else
			return ""; //FAILURE TYPE ALREADY CACHED
	}

	// Parameter array
	const char* paramValues[] = {(char*)&hash64};
	int paramLengths[] = {sizeof(uint64_t)};
	int paramFormats[] = {1};  // binary

	// Locks outside transactions remain for the duration of the connection and can be unlocked/relocked or unlockall'd
	// Locks inside transactions are unlockable and automatically unlocked on commit/rollback
	const char* sql = (mvconnection->in_transaction) ? "SELECT PG_TRY_ADVISORY_XACT_LOCK($1)" : "SELECT PG_TRY_ADVISORY_LOCK($1)";

	// Debugging
	if (DBTRACE)
		((this->assigned() ? *this : "") ^ " | " ^ var(sql).swap("$1)", var(hash64) ^ ") file:" ^ (*this) ^ " key:" ^ key)).logputl("SQLL ");

	// Call postgres
	MVresult mvresult = PQexecParams(pgconn,
											// TODO: parameterise filename
											sql, 1,										 /* one param */
											nullptr,										 /* let the backend deduce param type */
											paramValues, paramLengths, paramFormats, 1); /* ask for binary mvresults */

	// Handle serious errors
	if (PQresultStatus(mvresult) != PGRES_TUPLES_OK || PQntuples(mvresult) != 1) {
		var errmsg = "lock(" ^ (*this) ^ ", " ^ key ^ ")\n" ^
					 var(PQerrorMessage(pgconn)) ^ "\n" ^ "PQresultStatus=" ^
					 var(PQresultStatus(mvresult)) ^ ", PQntuples=" ^
					 var(PQntuples(mvresult));
		throw MVDBException(errmsg);
	}

	// Add to lock cache if successful
	if (*PQgetvalue(mvresult, 0, 0) != 0) {
		std::pair<const uint64_t, int> lock(hash64, 0);
		mvconnection->connection_locks.insert(lock);
		return 1;
	}

	// Otherwise indicate failure
	return 0;

}

bool var::unlock(CVR key) const {


	THISIS("void var::unlock(CVR key) const")
	assertDefined(function_sig);
	ISSTRING(key)

	auto hash64 = mvdbpostgres_hash_filename_and_key(*this, key);

	auto pgconn = get_pgconnection(*this);
	if (!pgconn)
		return false;

	auto mvconnection = get_mvconnection(*this);

	// Unlock inside transaction has no effect
	if (mvconnection->in_transaction)
		return false;

	// If not in lock cache then return false
	if (!mvconnection->connection_locks.contains(hash64))
		return false;

	// Remove from lock cache
	mvconnection->connection_locks.erase(hash64);

	// Parameter array
	const char* paramValues[] = {(char*)&hash64};
	int paramLengths[] = {sizeof(uint64_t)};
	int paramFormats[] = {1};//binary

	// $1=hashed filename and key
	const char* sql = "SELECT PG_ADVISORY_UNLOCK($1)";

	if (DBTRACE)
		((this->assigned() ? *this : "") ^ " | " ^ var(sql).swap("$1)", var(hash64) ^ ") file:" ^ (*this) ^ " key:" ^ key)).logputl("SQLL ");

	// Call postgres
	MVresult mvresult = PQexecParams(pgconn,
											// TODO: parameterise filename
											sql, 1,										 /* one param */
											nullptr,										 /* let the backend deduce param type */
											paramValues, paramLengths, paramFormats, 1); /* ask for binary results */

	// Handle serious errors
	if (PQresultStatus(mvresult) != PGRES_TUPLES_OK) {
		var errmsg = "unlock(" ^ this->convert(_FM_, "^") ^ ", " ^ key ^ ")\n" ^
					 var(PQerrorMessage(pgconn)) ^ "\n" ^ "PQresultStatus=" ^
					 var(PQresultStatus(mvresult)) ^ ", PQntuples=" ^
					 var(PQntuples(mvresult));
		throw MVDBException(errmsg);
	}

	// Should return true
	return PQntuples(mvresult) == 1;
}

bool var::unlockall() const {

	THISIS("void var::unlockall() const")
	assertDefined(function_sig);

	auto pgconn = get_pgconnection(*this);
	if (!pgconn)
		return false;

	auto mvconnection = get_mvconnection(*this);

	// Locks in transactions cannot be cleared
	if (mvconnection->in_transaction)
		return false;

	// If lock cache is empty already then return true
	if (mvconnection->connection_locks.empty())
		return true;

	// Clear the lock cache
	mvconnection->connection_locks.clear();

	// Should return true
	return this->sqlexec("SELECT PG_ADVISORY_UNLOCK_ALL()");

}

// returns only success or failure so any response is logged and saved for future lasterror() call
bool var::sqlexec(CVR sql) const {
	var response = -1;	//no response required
	if (!this->sqlexec(sql, response)) {
		this->lasterror(response);
		//skip table does not exist because it is very normal to check if table exists
		//if ((true && !response.index("sqlstate:42P01")) || response.index("syntax") || DBTRACE)
		//	response.logputl();

		// For now, all sqlexec calls that do not accept a response have any error response sent to cerr
		response.errputl();
		return false;
	}
	return true;
}

// returns success or failure, and response = data or errmsg (response can be preset to max number of tuples)
bool var::sqlexec(CVR sqlcmd, VARREF response) const {

	THISIS("bool var::sqlexec(CVR sqlcmd, VARREF response) const")
	ISSTRING(sqlcmd)

	auto pgconn = get_pgconnection(*this);
	if (!pgconn) {
		response = "Error: sqlexec cannot find thread database connection";
		return false;
	}

	// log the sql command
	if (DBTRACE)
		((this->assigned() ? *this : "") ^ " | " ^ sqlcmd.convert("\t"," ").trim()).logputl("SQLE ");

	// will contain any mvresult IF successful

	// NB PQexec cannot be told to return binary results
	// but it can execute multiple commands
	// whereas PQexecParams is the opposite
	MVresult mvresult = PQexec(pgconn, sqlcmd.var_str.c_str());

	if (PQresultStatus(mvresult) != PGRES_COMMAND_OK &&
		PQresultStatus(mvresult) != PGRES_TUPLES_OK) {
		//int xx = PQresultStatus(mvresult);
		var sqlstate = var(PQresultErrorField(mvresult, PG_DIAG_SQLSTATE));
		// sql state 42P03 = duplicate_cursor
		response = var(PQerrorMessage(pgconn)) ^ " sqlstate:" ^ sqlstate ^ " " ^ sqlcmd;
		return false;
	}

	//errmsg = var(PQntuples(mvresult));

	//quit if no rows/columns provided or no response required (integer<=0)
	int nrows = PQntuples(mvresult);
	int ncols = PQnfields(mvresult);
	if (nrows == 0 or ncols == 0 || (response.assigned() && ((response.var_typ & VARTYP_INT) && response <= 0))) {
		response = "";
		return true;
	}

	//option to limit number of rows returned
	if (response.assigned() && response.isnum() && response < nrows && response)
		nrows = response;

	response = "";

	//first row is the column names
	for (int coln = 0; coln < ncols; ++coln) {
		response.var_str.append(PQfname(mvresult, coln));
		response.var_str.push_back(FM_);
	}
	response.var_str.pop_back();

	//output the rows
	for (int rown = 0; rown < nrows; rown++) {
		response.var_str.push_back(RM_);
		for (int coln = 0; coln < ncols; ++coln) {
			response.var_str.append(PQgetvalue(mvresult, rown, coln));
			response.var_str.push_back(FM_);
		}
		response.var_str.pop_back();
	}

	return true;
}

// writev writes a specific field number in a record
//(why it is "writev" instead of "writef" isnt known!
bool var::writev(CVR filehandle, CVR key, const int fieldno) const {
	if (fieldno <= 0)
		return write(filehandle, key);


	THISIS("bool var::writev(CVR filehandle,CVR key,const int fieldno) const")
	// will be duplicated in read and write but do here to present the correct function name on
	// error
	assertString(function_sig);
	ISSTRING(filehandle)
	ISSTRING(key)

	// get the old record
	var record;
	if (!record.read(filehandle, key))
		record = "";

	// replace the field
	record.r(fieldno, var_str);

	// write it back
	record.write(filehandle, key);

	return true;
}

/* "prepared statement" version doesnt seem to make much difference approx -10% - possibly because
two field file is so simple bool var::write(CVR filehandle,CVR key) const {}
*/

//"update if present or insert if not" is handled in postgres using ON CONFLICT clause
bool var::write(CVR filehandle, CVR key) const {

	THISIS("bool var::write(CVR filehandle, CVR key) const")
	assertString(function_sig);
	ISSTRING(filehandle)
	ISSTRING(key)

	// std::string key2=key.var_str;
	// std::string data2=var_str;
	std::string key2 = key.normalize().var_str;
	std::string data2 = this->normalize().var_str;

	// clear any cache
	filehandle.deleteo(key2);

	// filehandle dos or DOS means osread/oswrite/osdelete
	if (filehandle.var_str.size() == 3 && (filehandle.var_str == "dos" || filehandle.var_str == "DOS")) {
		//this->oswrite(key2);	 //.convert("\\",OSSLASH));
		//use osfilenames unnormalised so we can read and write as is
		this->oswrite(key);	 //.convert("\\",OSSLASH));
		return true;
	}

	var sql;

	// Note cannot use postgres PREPARE/EXECUTE with parameterised filename
	// but performance gain is probably not great since the sql we use to read and write is
	// quite simple (could PREPARE once per file/table)

	sql = "INSERT INTO " ^ get_normal_filename(filehandle) ^ " (key,data) values( $1 , $2)";
	sql ^= " ON CONFLICT (key)";
	sql ^= " DO UPDATE SET data = $2";

	auto pgconn = get_pgconnection(filehandle);
	if (!pgconn)
		return false;

	// Parameter array
	const char* paramValues[] = {key2.data(), data2.data()};
	int paramLengths[] = {int(key2.size()), int(data2.size())};

	DEBUG_LOG_SQL1
	MVresult mvresult = PQexecParams(pgconn,
											// TODO: parameterise filename
											sql.var_str.c_str(),
											2,	   // two params (key and data)
											nullptr,  // let the backend deduce param type
											paramValues, paramLengths,
											0,	 // text arguments
											0);	 // text results

	// Handle serious errors
	if (PQresultStatus(mvresult) != PGRES_COMMAND_OK) {
		var errmsg = var("ERROR: mvdbpostgres write(" ^ filehandle.convert(_FM_, "^") ^
						 ", " ^ key ^ ") failed: PQresultStatus=" ^
						 var(PQresultStatus(mvresult)) ^ " " ^
						 var(PQerrorMessage(pgconn)));
		throw MVDBException(errmsg);
	}

	// if not updated 1 then fail
	//return strcmp(PQcmdTuples(mvresult), "1") != 0;
	return true;
}

//"updaterecord" is non-standard for pick - but allows "write only if already exists" logic

bool var::updaterecord(CVR filehandle, CVR key) const {

	THISIS("bool var::updaterecord(CVR filehandle,CVR key) const")
	assertString(function_sig);
	ISSTRING(filehandle)
	ISSTRING(key)

	// clear any cache
	filehandle.deleteo(key);

	// std::string key2=key.var_str;
	// std::string data2=var_str;
	std::string key2 = key.normalize().var_str;
	std::string data2 = this->normalize().var_str;

	// Parameter array
	const char* paramValues[] = {key2.data(), data2.data()};
	int paramLengths[] = {int(key2.size()), int(data2.size())};

	var sql = "UPDATE "  ^ get_normal_filename(filehandle) ^ " SET data = $2 WHERE key = $1";

	auto pgconn = get_pgconnection(filehandle);
	if (!pgconn)
		return false;

	DEBUG_LOG_SQL1
	MVresult mvresult = PQexecParams(pgconn,
											// TODO: parameterise filename
											sql.var_str.c_str(),
											2,	   // two params (key and data)
											nullptr,  // let the backend deduce param type
											paramValues, paramLengths,
											0,	 // text arguments
											0);	 // text results

	// Handle serious errors
	if (PQresultStatus(mvresult) != PGRES_COMMAND_OK) {
		var errmsg = "ERROR: mvdbpostgres update(" ^ filehandle.convert(_FM_, "^") ^
					 ", " ^ key ^ ") Failed: " ^ var(PQntuples(mvresult)) ^ " " ^
					 var(PQerrorMessage(pgconn));
		throw MVDBException(errmsg);
	}

	// if not updated 1 then fail
	if (strcmp(PQcmdTuples(mvresult), "1") != 0) {
		var("ERROR: mvdbpostgres update(" ^ filehandle.convert(_FM_, "^") ^
			", " ^ key ^ ") Failed: " ^ var(PQntuples(mvresult)) ^ " " ^
			var(PQerrorMessage(pgconn)))
			.errputl();
		return false;
	}

	return true;
}

//"insertrecord" is non-standard for pick - but allows faster writes under "write only if doesnt
// already exist" logic

bool var::insertrecord(CVR filehandle, CVR key) const {

	THISIS("bool var::insertrecord(CVR filehandle,CVR key) const")
	assertString(function_sig);
	ISSTRING(filehandle)
	ISSTRING(key)

	// clear any cache
	filehandle.deleteo(key);

	// std::string key2=key.var_str;
	// std::string data2=var_str;
	std::string key2 = key.normalize().var_str;
	std::string data2 = this->normalize().var_str;

	// Parameter array
	const char* paramValues[] = {key2.data(), data2.data()};
	int paramLengths[] = {int(key2.size()), int(data2.size())};

	var sql =
		"INSERT INTO " ^ get_normal_filename(filehandle) ^ " (key,data) values( $1 , $2)";

	auto pgconn = get_pgconnection(filehandle);
	if (!pgconn)
		return false;

	DEBUG_LOG_SQL1
	MVresult mvresult = PQexecParams(pgconn,
											// TODO: parameterise filename
											sql.var_str.c_str(),
											2,	   // two params (key and data)
											nullptr,  // let the backend deduce param type
											paramValues, paramLengths,
											0,	 // text arguments
											0);	 // text results

	// Handle serious errors
	if (PQresultStatus(mvresult) != PGRES_COMMAND_OK) {
		var errmsg = "ERROR: mvdbpostgres insertrecord(" ^
					 filehandle.convert(_FM_, "^") ^ ", " ^ key ^ ") Failed: " ^
					 var(PQntuples(mvresult)) ^ " " ^
					 var(PQerrorMessage(pgconn));
		throw MVDBException(errmsg);
	}

	// if not updated 1 then fail
	if (strcmp(PQcmdTuples(mvresult), "1") != 0) {
		var("ERROR: mvdbpostgres insertrecord(" ^ filehandle.convert(_FM_, "^") ^
			", " ^ key ^ ") Failed: " ^ var(PQntuples(mvresult)) ^ " " ^
			var(PQerrorMessage(pgconn)))
			.errputl();
		return false;
	}

	return true;
}

bool var::deleterecord(CVR key) const {

	THISIS("bool var::deleterecord(CVR key) const")
	assertString(function_sig);
	ISSTRING(key)

	// clear any cache
	this->deleteo(key);

	// std::string key2=key.var_str;
	std::string key2 = key.normalize().var_str;

	// filehandle dos or DOS means osread/oswrite/osdelete
	if (var_str.size() == 3 && (var_str == "dos" || var_str == "DOS")) {
		//return this->osdelete(key2);
		//use osfilenames unnormalised so we can read and write as is
		return this->osdelete(key);
	}

	// Parameter array
	const char* paramValues[] = {key2.data()};
	int paramLengths[] = {int(key2.size())};

	var sql = "DELETE FROM " ^ get_normal_filename(*this) ^ " WHERE KEY = $1";

	auto pgconn = get_pgconnection(*this);
	if (!pgconn)
		return false;

	DEBUG_LOG_SQL1
	MVresult mvresult = PQexecParams(pgconn, sql.var_str.c_str(), 1, /* two param */
											nullptr,								   /* let the backend deduce param type */
											paramValues, paramLengths,
											0,	 // text arguments
											0);	 // text results

	// Handle serious errors
	if (PQresultStatus(mvresult) != PGRES_COMMAND_OK) {
		var errmsg = "ERROR: mvdbpostgres deleterecord(" ^ this->convert(_FM_, "^") ^
					 ", " ^ key ^ ") Failed: " ^ var(PQntuples(mvresult)) ^ " " ^
					 var(PQerrorMessage(pgconn));
		throw MVDBException(errmsg);
	}

	// if not updated 1 then fail
	bool result;
	if (strcmp(PQcmdTuples(mvresult), "1") != 0) {
		if (DBTRACE)
			var("var::deleterecord(" ^ this->convert(_FM_, "^") ^ ", " ^ key ^
				") failed. Record does not exist")
				.errputl();
		result = false;
	} else
		result = true;

	return result;
}

void var::clearcache() const {

	THISIS("bool var::clearcache() const")
	assertDefined(function_sig);

	int mvconn_no = get_mvconn_no_or_default(*this);
	if (!mvconn_no)
		throw MVDBException("get_mvconn_no() failed in clearcache");

	thread_connections.clearrecordcache(mvconn_no);

	// Warning if any cursors have not been closed/cleaned up.
	for (auto& entry : thread_mvresults)
		var(entry.first).errputl("WARNING: Cursor not cleaned up.");

	// Clean up cursors - RAII/SBRM will call PQClear on the related PGresult* objects
	thread_mvresults.clear();

	return;
}

/* How do transactions work in EXODUS?

1. Visibilty of updates you make in a transaction

Before you commit, any and all updates you make will not be read by any other connection.

After you commit, any and all updates that you made will be immediately readable by all other connections regardless of whether they are in a transaction or not.

Other connections can choose to be in a transaction mode where they CANNOT read any updates made after the commencement of their transaction. This allows reports to be consistent. NO IMPLEMENTED YET

2. Visibility of other transactions while in a transaction.

During your transaction on a connection by default you can will read any and all committed updates from other connections immediately they occur.

Optionally you can be in a transaction mode where you will NOT read any updates made by other transactions after the commencement of your transaction.

3. Lock visibility

Locks are essentially independent of transactions and can be seen by other connections in real time.

However, locks placed during a transaction cannot be unlocked and are automatically released after you commit. Unlock commands are therefore ignored.

Within transactions, lock requests for locks that have already been obtained SUCCEED. This is the opposite of repeated locks outside of transactions which FAIL.

4. How to coordinate updates between asynchronous processes using locks.

	This process allows multiple asynchronous processes to update the database concurrently/in parallel as long as they are updating different data.

	a. Start a transaction

	b. Acquire an agreed lock appropriate for the data in c. and d.,
	   OR wait for a limited period of time,
	   OR or cancel the whole process i.e. rollback to to the state at a.

	c. Read the data
	d. Write the data

	e. Repeat from b. as required for multiple updates

	e. Commit the transaction (will also release all locks)

	Generally *LOCK BEFORE READ* and *WRITE BEFORE UNLOCK* so that all READ and WRITE operations are "protected" by locks.

	Locks form a barrier between changed state.

	In the above, WRITE encompasses INSERT, UPDATE and DELETE.

5. Postgres facilities used

	Transaction isolation level used is the default:

		READ COMMITTED (default)

		SERIALIZABLE or REPEATABLE READ - pending implementation to facilite consistent reports based on snapshots

		See https://www.postgresql.org/docs/12/sql-set-transaction.html

	Locks are obtained and released:

		Outside a transaction using pg_try_advisory_lock() and pg_try_advisory_unlock()

		Inside a transaction  using pg_try_advisory_xact_lock() and commit or rollback.

		See https://www.postgresql.org/docs/12/functions-admin.html#FUNCTIONS-ADVISORY-LOCKS

6. How Postgres handles versioning

	Summary explanation: https://vladmihalcea.com/how-does-mvcc-multi-version-concurrency-control-work/

	Detailed explanation: https://www.interdb.jp/pg/pgsql05.html

	PostgreSQL stores all row versions in the table data structure.

	This is called MVCC and postgres uses a version of it called "Snapshot Isolation".

	It never updates existing rows, only adds new rows and marks the old rows as "deleted".

	Automatic and manual VACUUM processes remove stale deleted rows at convenient times.

	Every row has two additional columns:

		t_xmin - the transaction id that inserted the record
		t_xmax - the transaction id that deleted the row, if deleted.

	Transaction id is an ever increasing 32 bit integer so it is possible to determine the
	state of the database "as at" any transaction id. Every query eg SELECT gets its
	own transaction id. Wrap around to zero after approx. 4.2 billion transactions is inevitable
	and the concept of before and after is not absolute.

	See SELECT txid_current();

	Every row also has the following control info.

		t_cid - holds the command id (cid), which means how many SQL commands were executed
		        before this command was executed within the current transaction beginning from 0.

		t_ctid - holds the tuple identifier (tid) that points to itself or a new tuple replacing it.

*/

// If this is opened SQL connection, pass connection ID to sqlexec
bool var::begintrans() const {

	THISIS("bool var::begintrans() const")
	assertDefined(function_sig);

	// Clear the record cache
	this->clearcache();

	// begin a transaction
	//if (!this->sqlexec("BEGIN"))
	if (!this->sqlexec("BEGIN TRANSACTION ISOLATION LEVEL READ COMMITTED"))
		return false;

	auto mvconnection = get_mvconnection(*this);

	// Change status
	// ESSENTIAL Used to change locking type to PER TRANSACTION
	// so all locks persist until after commit i.e. cannot be specifically unlocked
	mvconnection->in_transaction = true;

	return true;

}

bool var::rollbacktrans() const {

	THISIS("bool var::rollbacktrans() const")
	assertDefined(function_sig);

	// Clear the record cache
	this->clearcache();

	// Rollback a transaction
	if (!this->sqlexec("ROLLBACK"))
		return false;

	auto mvconnection = get_mvconnection(*this);

	// Change status
	mvconnection->in_transaction = false;

    // Clear the lock cache
    mvconnection->connection_locks.clear();

	return true;
}

bool var::committrans() const {

	THISIS("bool var::committrans() const")
	assertDefined(function_sig);

	// Clear the record cache
	this->clearcache();

	// end (commit) a transaction
	if (!this->sqlexec("END"))
		return false;

	auto mvconnection = get_mvconnection(*this);

	// Change status
	mvconnection->in_transaction = false;

    // Clear the lock cache
    mvconnection->connection_locks.clear();

	return true;

}

bool var::statustrans() const {

	THISIS("bool var::statustrans() const")
	assertDefined(function_sig);
//
//	auto pgconn = get_pgconnection(*this);
//	if (!pgconn) {
//		this->lasterror("db connection " ^ var(get_mvconn_no(*this)) ^ "not opened");
//		return false;
//	}
//
//	//this->lasterror();
//
//	// only idle is considered to be not in a transaction
//	return (PQtransactionStatus(pgconn) != PQTRANS_IDLE);

	auto mvconnection = get_mvconnection(*this);
	return mvconnection->in_transaction;

}

// sample code
// var().dbcreate("mynewdb");//create a new database on the current thread-default connection
// var file;
// file.open("myfile");
// file.dbcreate("mynewdb");//creates a new db on the same connection as a file was opened on
// var connectionhandle;
// connectionhandle.connect("connection string pars");
// connectionhandle.dbcreate("mynewdb");

bool var::dbcreate(CVR dbname) const {
	return this->dbcopy(var(""), dbname);
}

bool var::dbcopy(CVR from_dbname, CVR to_dbname) const {

	THISIS("bool var::dbcreate(CVR from_dbname, CVR to_dbname)")
	assertDefined(function_sig);
	ISSTRING(from_dbname)
	ISSTRING(to_dbname)

	//create a database
	var sql = "CREATE DATABASE " ^ to_dbname ^ " WITH";
	sql ^= " ENCODING='UTF8' ";
	if (from_dbname)
		sql ^= " TEMPLATE " ^ from_dbname;
	if (!this->sqlexec(sql)) {
		return false;
	}

	///connect to the new db
	var newdb;
	if (not newdb.connect(to_dbname)) {
		newdb.lasterror().errputl();
		return false;
	}

	//add dict schema to allow creation of dict files like dict.xxxxxxxx
	sql = "CREATE SCHEMA IF NOT EXISTS dict";
	//sql ^= "AUTHORIZATION exodus;"
	var result = true;
	if (!newdb.sqlexec(sql)) {
		newdb.lasterror().errputl();
		result = false;
	}

	//disconnec
	newdb.disconnect();

	return result;

}

bool var::dbdelete(CVR dbname) const {

	THISIS("bool var::dbdelete(CVR dbname)")
	assertDefined(function_sig);
	ISSTRING(dbname)

	return this->sqlexec("DROP DATABASE " ^ dbname);
}

bool var::createfile(CVR filename) const {

	THISIS("bool var::createfile(CVR filename)")
	assertDefined(function_sig);
	ISSTRING(filename)

	// var tablename = "TEMP" ^ var(100000000).rnd();
	// Postgres The ON COMMIT clause for temporary tables also resembles the SQL standard, but
	// has some differences. If the ON COMMIT clause is omitted, SQL specifies that the default
	// behavior is ON COMMIT DELETE ROWS. However, the default behavior in PostgreSQL is ON
	// COMMIT PRESERVE ROWS. The ON COMMIT DROP option does not exist in SQL.

	var sql = "CREATE";
	if (filename.substr(-5, 5) == "_temp")
		sql ^= " TEMPORARY ";
	sql ^= " TABLE " ^ get_normal_filename(filename);
	sql ^= " (key text primary key, data text)";

	if (this->assigned())
		return this->sqlexec(sql);
	else
		return filename.sqlexec(sql);
}

bool var::renamefile(CVR filename, CVR newfilename) const {

	THISIS("bool var::renamefile(CVR filename, CVR newfilename)")
	assertDefined(function_sig);
	ISSTRING(filename)
	ISSTRING(newfilename)

	var sql = "ALTER TABLE " ^ filename ^ " RENAME TO " ^ newfilename;

	if (this->assigned())
		return this->sqlexec(sql);
	else
		return filename.sqlexec(sql);
}

bool var::deletefile(CVR filename) const {

	THISIS("bool var::deletefile(CVR filename)")
	assertDefined(function_sig);
	ISSTRING(filename)

	// False if file does not exist
	// Avoid generating sql errors since they abort transations
	if (!var().open(filename, *this)) {
		//this->errputl(filename ^ " cannot be deleted because it does not exist. ");
		return false;
	}

	// Remove from filehandle cache regardless of success or failure to deletefile
	// Delete from cache AFTER the above open which will place it in the cache
	// Similar code in detach and deletefile
	if (thread_file_handles.erase(get_normal_filename(filename))) {
		//filename.errputl("::deletefile ==== Connection cache DELETED = ");
	} else {
		//filename.errputl("::deletefile ==== Connection cache NONE    = ");
	}

	var sql = "DROP TABLE " ^ filename.a(1) ^ " CASCADE";
	//var sql = "DROP TABLE IF EXISTS " ^ filename.a(1) ^ " CASCADE";

	if (this->assigned())
		return this->sqlexec(sql);
	else
		return filename.sqlexec(sql);
}

bool var::clearfile(CVR filename) const {

	THISIS("bool var::clearfile(CVR filename)")
	assertDefined(function_sig);
	ISSTRING(filename)

	var sql = "DELETE FROM " ^ filename.a(1);
	if (this->assigned())
		return this->sqlexec(sql);
	else
		return filename.sqlexec(sql);
}

inline void unquoter_inline(VARREF string) {
	// remove "", '' and {}
	static var quotecharacters("\"'{");
	if (quotecharacters.index(string[1]))
		string = string.substr(2, string.length() - 2);
}

inline void tosqlstring(VARREF string1) {

	// if double quoted then convert to sql style single quoted strings
	// double up any internal single quotes
	if (string1[1] == "\"") {
	//if (string1.var_str.front() == '"') {
		string1.swapper("'", "''");
		string1.splicer(1, 1, "'");
		string1.splicer(-1, 1, "'");
		//string1.var_str.front() = "'";
		//string1.var_str.back() = "'";
	}
}

inline var get_fileexpression(CVR mainfilename, CVR filename, CVR keyordata) {

	// evade warning: unused parameter mainfilename
	if (false && mainfilename) {
	}

	// if (filename == mainfilename)
	//	return keyordata;
	// else
	//return filename.convert(".", "_") ^ "." ^ keyordata;
	return get_normal_filename(filename) ^ "." ^ keyordata;

	// if you dont use STRICT in the postgres function declaration/definitions then nullptr
	// parameters do not abort functions

	// use COALESCE function in case this is a joined but missing record (and therefore null)
	// in MYSQL this is the ISNULL expression?
	// xlatekeyexpression="exodus_extract_text(coalesce(" ^ filename ^ ".data,''::text), " ^
	// xlatefromfieldname.substr(9); if (filename==mainfilename) return expression; return
	// "coalesce(" ^ expression ^", ''::text)";
}

var get_dictexpression(CVR cursor, CVR mainfilename, CVR filename, CVR dictfilename, CVR dictfile, CVR fieldname0, VARREF joins, VARREF unnests, VARREF selects, VARREF ismv, bool forsort) {

	//cursor is required to join any calculated fields in any second pass

	ismv = false;

	var fieldname = fieldname0.convert(".", "_");
	var actualdictfile = dictfile;

	// Open dict.xxxx or dict.voc on the default dict connection or throw an error
	if (!actualdictfile) {

		// The dictionary of all dict. files is dict.voc. Used when selecting any dict. file.
		var dictfilename;
		if (mainfilename.substr(1, 5).lcase() == "dict.")
			dictfilename = "dict.voc";
		else
			dictfilename = "dict." ^ mainfilename;

		// If dict .mainfile is not available, use dict.voc
		if (!actualdictfile.open(dictfilename)) {
			dictfilename = "dict.voc";
			if (!actualdictfile.open(dictfilename)) {

				throw MVDBException("get_dictexpression() cannot open " ^
									dictfilename.quote());
			}
		}
	}

	//if doing 2nd pass then calculated fields have been placed in a parallel temporary file
	//and their column names appended with a colon (:)
	var stage2_calculated = fieldname[-1] == ":";
	var stage2_filename = "SELECT_CURSOR_STAGE2_" ^ cursor.a(1);

	if (stage2_calculated) {
		fieldname.popper();
		//create a pseudo look up ... except that SELECT_CURSOR_STAGE2 has the fields stored in sql columns and not in the usual data column
		stage2_calculated = "@ANS=XLATE(\"" ^ stage2_filename ^ "\",@ID," ^ fieldname ^ "_calc,\"X\")";
		stage2_calculated.logputl("stage2_calculated simulation --------------------->");
	}

	// given a file and dictionary id
	// returns a postgres sql expression like (texta(filename.data,99,0,0))
	// using one of the exodus backend functions installed in postgres like textextract,
	// dateextract etc.
	var dictrec;
	if (!dictrec.read(actualdictfile, fieldname)) {
		// try lowercase
		fieldname.lcaser();
		if (!dictrec.read(actualdictfile, fieldname)) {
			// try uppercase
			fieldname.ucaser();
			if (!dictrec.read(actualdictfile, fieldname)) {
				// try in voc lowercase
				fieldname.lcaser();
				if (not dictrec.read("dict.voc", fieldname)) {
					// try in voc uppercase
					fieldname.ucaser();
					if (not dictrec.read("dict.voc", fieldname)) {
						if (fieldname == "@ID" || fieldname == "ID")
							dictrec = "F" ^ FM ^ "0" ^ FM ^ "Ref" ^ FM ^
									  FM ^ FM ^ FM ^ FM ^ FM ^ "" ^ FM ^
									  15;
						else {
							throw MVDBException(
								"get_dictexpression() cannot read " ^
								fieldname.quote() ^ " from " ^
								actualdictfile.convert(FM, "^")
									.quote() ^
								" or \"dict.voc\"");
							//					exodus::errputl("ERROR:
							// mvdbpostgres get_dictexpression() cannot
							// read " ^ fieldname.quote() ^ " from " ^
							// actualdictfile.quote());
							//return "";
						}
					}
				}
			}
		}
	}

	//create a pseudo look up. to trigger JOIN logic to the table that we stored
	//Note that SELECT_TEMP has the fields stored in sql columns and not in the usual data column
	if (stage2_calculated) {
		dictrec.r(8, stage2_calculated);
	}

	var dicttype = dictrec.a(1);
	var fieldno = dictrec.a(2);
	var conversion = dictrec.a(7);

	var isinteger = conversion == "[NUMBER,0]" || dictrec.a(11) == "0N" ||
					dictrec.a(11).substr(1, 3) == "0N_";
	var isdecimal = conversion.substr(1, 2) == "MD" || conversion.substr(1, 7) == "[NUMBER" ||
					dictrec.a(12) == "FLOAT" || dictrec.a(11).index("0N");
	//dont assume things that are R are numeric
	//eg period 1/19 is right justified but not numeric and sql select will crash if ::float8 is used
	//||dictrec.a(9) == "R";
	var isnumeric = isinteger || isdecimal || dictrec.a(9) == "R";
	var ismv1 = dictrec.a(4)[1] == "M";
	var fromjoin = false;

	var isdate = conversion[1] == "D" || conversion.substr(1, 5) == "[DATE";
	var istime = !isdate && (conversion.substr(1,2) == "MT" || conversion.substr(1, 5) == "[TIME");

	var sqlexpression;
	if (dicttype == "F") {
		// key field
		if (!fieldno) {

			if (forsort && !isdate && !istime)
				// sqlexpression="exodus_extract_sort(" ^
				// get_fileexpression(mainfilename, filename,"key") ^ ")";
				sqlexpression =
					"exodus_extract_sort(" ^ mainfilename ^ ".key,0,0,0)";
			else
				// sqlexpression="convert_from(" ^ get_fileexpression(mainfilename,
				// filename, "key") ^ ", 'UTF8')";
				sqlexpression = get_fileexpression(mainfilename, filename, "key");

			// multipart key
			var keypartn = dictrec.a(5);
			if (keypartn) {
				sqlexpression =
					"split_part(" ^ sqlexpression ^ ", '*', " ^ keypartn ^ ")";
			}

			// example of multipart key and date conversion
			// select date '1967-12-31' + split_part(convert_from(key, 'UTF8'),
			// '*',2)::integer from filename
			if (isdate)
				sqlexpression =
					//"date('1967-12-31') + " ^ sqlexpression ^ "::integer";
					// cannot seem to use + on dates in indexes
					//therefore
					"exodus_extract_date(" ^ sqlexpression ^ ",0,0,0)";
			else if (istime)
				sqlexpression =
					"exodus_extract_time(" ^ sqlexpression ^ ",0,0,0)";

			return sqlexpression;
		}

		var extractargs =
			get_fileexpression(mainfilename, filename, "data") ^ "," ^ fieldno ^ ", 0, 0)";

		if (conversion.substr(1, 9) == "[DATETIME")
			sqlexpression = "exodus_extract_datetime(" ^ extractargs;

		else if (isdate)
			sqlexpression = "exodus_extract_date(" ^ extractargs;

		else if (istime)
			sqlexpression = "exodus_extract_time(" ^ extractargs;

		// for now (until we have a extract_number/integer/float) that doesnt fail on
		// non-numeric like cast "as integer" and "as float" does note that we could use
		// exodus_extract_sort for EVERYTHING inc dates/time/numbers etc. but its large size
		// is perhaps a disadvantage
		else if (forsort)
			sqlexpression = "exodus_extract_sort(" ^ extractargs;

		else if (isnumeric)
			sqlexpression = "exodus_extract_number(" ^ extractargs;

		else
			sqlexpression = "exodus_extract_text(" ^ extractargs;
	} else if (dicttype == "S") {

		var functionx = dictrec.a(8).trim();

		// sql expression available
		sqlexpression = dictrec.a(17);
		if (sqlexpression) {
			// return sqlexpression;
		}

		// sql function available
		// eg dict_schedules_PROGRAM_POSITION(key text, data text)
		else if (functionx.index("/"
								 "*pgsql")) {

			// plsql function name assumed to be like "dictfilename_FIELDNAME()"
			sqlexpression = get_normal_filename(actualdictfile).swap("dict.", "dict_") ^ "_" ^ fieldname ^ "(";

			// function arguments are (key,data)
			sqlexpression ^= get_fileexpression(mainfilename, filename, "key");
			sqlexpression ^= ", ";
			sqlexpression ^= get_fileexpression(mainfilename, filename, "data");
			sqlexpression ^= ")";
		}

		//handle below.
		else if (stage2_calculated && ismv1) {
			sqlexpression = stage2_filename ^ "." ^ fieldname ^ "_calc";
		}

		// simple join or stage2 but not on multivalued
		// stage2_calculated="@ANS=XLATE(\"SELECT_CURSOR_STAGE2_" ^ this->a(1) ^ "\",@ID," ^ fieldname ^ "_calc,\"X\")";
		else if ((!ismv1 || stage2_calculated) && functionx.substr(1, 11).ucase() == "@ANS=XLATE(") {
			functionx = functionx.a(1, 1);

			functionx.splicer(1, 11, "");

			// allow for <1,@mv> in arg3 by replacing comma with |
			functionx.swapper(",@mv", "|@mv");

			//allow for field(@id,'*',x) in arg2 by replacing commas with |
			functionx.swapper(",'*',", "|'*'|");

			// arg1 filename
			var xlatetargetfilename = functionx.field(",", 1).trim().convert(".", "_");
			unquoter_inline(xlatetargetfilename);

			// arg2 key
			var xlatefromfieldname = functionx.field(",", 2).trim();

			// arg3 target field number/name
			var xlatetargetfieldname = functionx.field(",", 3).trim().unquoter();

			// arg4 mode X or C
			var xlatemode = functionx.field(",", 4).trim().convert("'\" )", "");

			// if the fourth field is 'X', "X", "C" or 'C' then
			// assume we have a good simple xlate functionx and can convert to a JOIN
			if (xlatemode == "X" || xlatemode == "C") {

				// determine the expression in the xlate target file
				// VARREF todictexpression=sqlexpression;
				if (xlatetargetfieldname.isnum()) {
					sqlexpression =
						"exodus_extract_text(" ^
						get_fileexpression(mainfilename, xlatetargetfilename,
									   "data") ^
						", " ^ xlatetargetfieldname ^ ", 0, 0)";
				} else if (stage2_calculated) {
					sqlexpression = xlatetargetfieldname;
				} else {
					// var dictxlatetofile=xlatetargetfilename;
					// if (!dictxlatetofile.open("DICT",xlatetargetfilename))
					//	throw MVDBException("get_dictexpression() DICT" ^
					// xlatetargetfilename ^ " file cannot be opened"); var
					// ismv;
					var xlatetargetdictfilename = "dict." ^ xlatetargetfilename;
					var xlatetargetdictfile;
					if (!xlatetargetdictfile.open(xlatetargetdictfilename))
						throw MVDBException(xlatetargetdictfilename ^ " cannot be opened for " ^ functionx);

					sqlexpression = get_dictexpression(cursor,
						filename, xlatetargetfilename, xlatetargetdictfilename,
						xlatetargetdictfile, xlatetargetfieldname, joins, unnests,
						selects, ismv, forsort);
				}

				// determine the join details
				var xlatekeyexpression = "";
				//xlatefromfieldname.logputl("xlatefromfieldname=");
				if (xlatefromfieldname.trim().substr(1, 8).lcase() == "@record<") {
					xlatekeyexpression = "exodus_extract_text(";
					xlatekeyexpression ^= filename ^ ".data";
					xlatekeyexpression ^= ", " ^ xlatefromfieldname.substr(9);
					xlatekeyexpression.popper();
					xlatekeyexpression ^=
						var(", 0").str(3 - xlatekeyexpression.count(',')) ^ ")";
				} else if (xlatefromfieldname.trim().substr(1, 10).lcase() == "field(@id|") {
					xlatekeyexpression = "split_part(";
					xlatekeyexpression ^= filename ^ ".key,'*',";
					xlatekeyexpression ^= xlatefromfieldname.field("|", 3).field(")", 1) ^ ")";
				}
				// TODO				if
				// (xlatefromfieldname.substr(1,8)=="FIELD(@ID)
				else if (xlatefromfieldname[1] == "{") {
					xlatefromfieldname =
						xlatefromfieldname.substr(2).popper();
					xlatekeyexpression = get_dictexpression(cursor,
						filename, filename, dictfilename, dictfile,
						xlatefromfieldname, joins, unnests, selects, ismv, forsort);
				} else if (xlatefromfieldname == "@ID") {
					xlatekeyexpression = filename ^ ".key";
				} else {
					// throw  MVDBException("get_dictexpression() " ^
					// filename.quote() ^ " " ^ fieldname.quote() ^ " - INVALID
					// DICTIONARY EXPRESSION - " ^ dictrec.a(8).quote());
					var("ERROR: mvdbpostgres get_dictexpression() " ^
						filename.quote() ^ " " ^ fieldname.quote() ^
						" - INVALID DICTIONARY EXPRESSION - " ^
						dictrec.a(8).quote())
						.errputl();
					return "";
				}

				//if the xlate key expression is stage2_calculated then
				//indicate that the whole dictid expression is stage2_calculated
				//and do not do any join
				if (xlatekeyexpression.index("exodus_call")) {
					sqlexpression = "exodus_call(";
					return sqlexpression;
				}

				fromjoin = true;

				// joins needs to follow "FROM mainfilename" clause
				// except for joins based on mv fields which need to follow the
				// unnest function
				var joinsectionn = ismv ? 2 : 1;

				// add the join
				///similar code above/below
				//main file is on the left
				//secondary file is on the right
				//normally we want all records on the left (main file) and any secondary file records that exist ... LEFT JOIN
				//if joining to stage2_calculated field file then we want only records that exist in the stage2_calculated fields file ... RIGHT JOIN (could be INNER JOIN)
				//RIGHT JOIN MUST BE IDENTICAL ELSE WHERE TO PREVENT DUPLICATION
				var join_part1 = stage2_calculated ? "RIGHT" : "LEFT";
				join_part1 ^= " JOIN " ^ xlatetargetfilename ^ " ON ";

				var join_part2 =
					xlatetargetfilename ^ ".key = " ^ xlatekeyexpression;
				// only allow one join per file for now.
				// TODO allow multiple joins to the same file via different keys
				if (!joins.a(joinsectionn).index(join_part1))
					joins.r(joinsectionn, -1, join_part1 ^ join_part2);

				return sqlexpression;
			} else {
				// not xlate X or C
				goto exodus_call;
			}
		}

		// FOLLOWING IS CURRENTLY DISABLED
		// if we get here then we were unable to work out any sql expression or function
		// so originally we instructed postgres to CALL EXODUS VIA IPC to run exodus
		// subroutines in the context of the calling program. exodus mvdbpostgres.cpp setup
		// a separate listening thread with a separate pgconnection before calling postgres.
		// BUT exodus subroutines cannot make request to the db while it is handling a
		// request FROM the db - unless it were to setup ANOTHER threada and pgconnection to
		// handle it. this is also perhaps SLOW since it has to copy the whole RECORD and ID
		// etc to exodus via IPC for every call!
		else {
exodus_call:
			sqlexpression = "'" ^ fieldname ^ "'";
			int environmentn = 1;  // getenvironmentn()
			sqlexpression = "exodus_call('exodusservice-" ^ getprocessn() ^ "." ^
							environmentn ^ "', '" ^ dictfilename.lcase() ^ "', '" ^
							fieldname.lcase() ^ "', " ^ filename ^ ".key, " ^ filename ^
							".data,0,0)";
			//sqlexpression.logputl("sqlexpression=");
			// TODO apply naturalorder conversion by passing forsort
			// option to exodus_call

			return sqlexpression;
		}
	} else {
		// throw  filename ^ " " ^ fieldname ^ " - INVALID DICTIONARY ITEM";
		// throw  MVDBException("get_dictexpression(" ^ filename.quote() ^ ", " ^
		// fieldname.quote() ^ ") invalid dictionary type " ^ dicttype.quote());
		var("ERROR: mvdbpostgres get_dictexpression(" ^ filename.quote() ^ ", " ^
			fieldname.quote() ^ ") invalid dictionary type " ^
			dicttype.quote())
			.errputl();
		return "";
	}

	// Multivalued or xref fields need special handling
	///////////////////////////////////////////////////

	ismv = ismv1;

	// vector (for GIN or indexing/filtering multivalue fields)
	//if ((ismv1 and !forsort) || fieldname.substr(-5).ucase() == "_XREF") {
	if ((ismv1 and !forsort) || fieldname.substr(-4).ucase() == "XREF") {
		//this is the sole creation of to_tsvector in mvdbpostgres.cpp
		//it will be used like to_tsvector(...) @@ to_tsquery(...)
		sqlexpression = "to_tsvector('simple'," ^ sqlexpression ^ ")";
		//sqlexpression = "to_tsvector('english'," ^ sqlexpression ^ ")";
		//sqlexpression = "string_to_array(" ^ sqlexpression ^ ",chr(29),'')";

		//multivalued prestage2_calculated field DUPLICATE CODE
		if (fieldname0[-1] == ":") {
			var joinsectionn = 1;
			var join = "RIGHT JOIN " ^ stage2_filename ^ " ON " ^ stage2_filename ^ ".key = " ^ filename ^ ".key";
			//if (!joins.a(joinsectionn).index(join))
			if (!joins.index(join))
				joins.r(joinsectionn, -1, join);
		}

	}

	// unnest multivalued fields into multiple output rows
	else if (ismv1) {

		//ismv = true;

		// var from="string_to_array(" ^ sqlexpression ^ ",'" ^ VM ^ "'";
		if (sqlexpression.substr(1, 20) == "exodus_extract_date(" || sqlexpression.substr(1, 20) == "exodus_extract_time(")
			sqlexpression.splicer(20, 0, "_array");
		else {
			sqlexpression.replacer("exodus_extract_sort\\(", "exodus_extract_text\\(");
			sqlexpression = "string_to_array(" ^ sqlexpression ^ ", chr(29),'')";

			// Note 3rd argument '' means convert empty multivalues to nullptr in the array
			// otherwise conversion to float will fail
			if (isnumeric)
				sqlexpression ^= "::float8[]";
		}

		//now do this for all fields including WHERE and ORDER BY
		//eg SELECT BOOKING_ORDERS WITH YEAR_PERIOD "21.02" AND WITH IS_LATEST AND WITH CLIENT_CODE "MIC" AND WITH @ID NOT "%]" BY ORDER_NO
		//if (forsort)
		{

			// use the fieldname as a sql column name

			// if a mv field requires a join then add it to the SELECT clause
			// since not known currently how to to do mv joins in the FROM clause
			// Note the join clause should already have been added to the JOINS for the FROM
			// clause
			if (fromjoin) {

				// this code should never execute as joined mv fields now return the plain
				// sql expression. we assume they are used in WHERE and ORDER BY clauses

				// unnest in select clause
				// from="unnest(" ^ from ^ ")";
				// as FIELDNAME
				sqlexpression ^= " as " ^ fieldname;

				// dont include more than once, in case order by and filter on the same
				// field
				if (!selects.a(1).index(sqlexpression))
					selects ^= ", " ^ sqlexpression;
			} else {

				//multivalued prestage2_calculated field DUPLICATE CODE
				if (fieldname0[-1] == ":") {
					var joinsectionn = 1;
					var join = "RIGHT JOIN " ^ stage2_filename ^ " ON " ^ stage2_filename ^ ".key = " ^ filename ^ ".key";
					//if (!joins.a(joinsectionn).index(join))
					if (!joins.index(join))
						joins.r(joinsectionn, -1, join);
				}

				// insert with SMs since expression can contain VMs
				if (!unnests.a(2).locate(fieldname)) {
					unnests.r(2, -1, fieldname);
					unnests.r(3, -1, sqlexpression);
				}
			}

			sqlexpression = fieldname;
		}
	}

	return sqlexpression;
}

// var getword(VARREF remainingwords, CVR joinvalues=false)
var getword(VARREF remainingwords, VARREF ucword) {

	// gets the next word
	// or a series of words separated by FM while they are numbers or quoted strings)
	// converts to sql quoted strings
	// and clips them from the input string

	bool joinvalues = true;

	var word1 = remainingwords.field(" ", 1);
	remainingwords = remainingwords.field(" ", 2, 99999);

	//separate out leading or trailing parens () but not both
	if (word1.length() > 1) {
		if (word1[1] == "(" && word1[-1] != ")") {
			//put remaining word back on the pending words
			remainingwords.splicer(1, 0, word1.substr(2) ^ " ");
			//return single leading paren (
			word1 = "(";
		} else if (word1[-1] == ")") {
			//put single closing paren back on the pending words
			remainingwords.splicer(1, 0, ") ");
			//return word without trailing paren )
			word1.popper();
		}
	}

	// join words within quote marks into one quoted phrase
	var char1 = word1[1];
	if ((char1 == DQ || char1 == SQ)) {
		while (word1[-1] != char1 || word1.length() <= 1) {
			if (remainingwords.length()) {
				word1 ^= " " ^ remainingwords.field(" ", 1);
				remainingwords = remainingwords.field(" ", 2, 99999);
			} else {
				word1 ^= char1;
				break;
			}
		}
	}

	tosqlstring(word1);

	// grab multiple values (numbers or quoted words) into one list, separated by FM
	//value chars are " ' 0-9 . + -
	if (remainingwords && joinvalues && valuechars.index(word1[1])) {
		word1 = SQ ^ word1.unquote().swap("'", "''") ^ SQ;

		var nextword = remainingwords.field(" ", 1);

		//'x' and 'y' and 'z' becomes 'x' 'y' 'z'
		// to cater for WITH fieldname NOT 'X' AND 'Y' AND 'Z'
		// duplicated above/below
		if (nextword == "and") {
			var nextword2 = remainingwords;
			if (valuechars.index(nextword2[1])) {
				nextword = nextword2;
				remainingwords = remainingwords.field(" ", 2, 99999);
			}
		}

		/*
		while (nextword && valuechars.index(nextword[1])) {
			tosqlstring(nextword);
			if (word1 != "")
				word1 ^= FM_;
			word1 ^= SQ ^ nextword.unquote() ^ SQ;

			remainingwords = remainingwords.field(" ", 2, 99999);
			nextword = remainingwords.field(" ", 1);

			//'x' and 'y' and 'z' becomes 'x' 'y' 'z'
			// to cater for WITH fieldname NOT 'X' AND 'Y' AND 'Z'
			// duplicated above/below
			if (nextword == "and") {
				var nextword2 = remainingwords;
				if (valuechars.index(nextword2[1])) {
					nextword = nextword2;
					remainingwords = remainingwords.field(" ", 2, 99999);
				}
			}
		}
		*/
		nextword = getword(remainingwords, ucword);
		if (nextword && valuechars.index(nextword[1])) {
			tosqlstring(nextword);
			if (word1 != "")
				word1 ^= FM_;
			word1 ^= SQ ^ nextword.unquote() ^ SQ;
		}
		else {
			//push the nextword back if not a value word
			remainingwords = nextword ^ " " ^ remainingwords;
		}

	} else {
		// word1.ucaser();
	}

	ucword = word1.ucase();
	return word1;
}

bool var::saveselect(CVR filename) {

	THISIS("bool var::saveselect(CVR filename) const")
	//?allow undefined usage like var xyz=xyz.select();
	// assertDefined(function_sig);
	ISSTRING(filename)

	if (DBTRACE)
		filename.logputl("DBTR var::saveselect() ");

	int recn = 0;
	var key;
	var mv;

	// save preselected keys into a file to be used with INNERJOIN on select()

	// this should not throw if the select does not exist
	this->deletefile(filename);

	// clear or create any existing saveselect file with the same name
	if (!this->createfile(filename))
		throw MVDBException("saveselect cannot create file " ^ filename);

	var file;
	if (!file.open(filename, (*this)))
		throw MVDBException("saveselect cannot open file " ^ filename);

	while (this->readnext(key, mv)) {
		recn++;

		// save a key
		(mv ^ FM ^ recn).write(file, key);
	}

	return recn > 0;
}

void to_extract_text(VARREF dictexpression) {
				dictexpression.replacer("^exodus_extract_number\\(", "exodus_extract_text\\(");
				dictexpression.replacer("^exodus_extract_sort\\(", "exodus_extract_text\\(");
				dictexpression.replacer("^exodus_extract_date\\(", "exodus_extract_text\\(");
				dictexpression.replacer("^exodus_extract_time\\(", "exodus_extract_text\\(");
				dictexpression.replacer("^exodus_extract_datetime\\(", "exodus_extract_text\\(");
}

bool var::select(CVR sortselectclause) {

	THISIS("bool var::select(CVR sortselectclause) const")
	//?allow undefined usage like var xyz=xyz.select();
	assertDefined(function_sig);
	ISSTRING(sortselectclause)

	if (!sortselectclause || sortselectclause.substr(-2, 2) == "R)")
		return this->selectx("key, mv::integer, data", sortselectclause);
	else
		return this->selectx("key, mv::integer", sortselectclause);
}

// currently only called from select, selectrecord and getlist
// TODO merge into plain select()
bool var::selectx(CVR fieldnames, CVR sortselectclause) {
	// private - and arguments are left unchecked for speed
	//?allow undefined usage like var xyz=xyz.select();
	if (var_typ & VARTYP_MASK) {
		// throw MVUndefined("selectx()");
		var_str.clear();
		var_typ = VARTYP_STR;
	}

	// fieldnames.logputl("fieldnames=");
	// sortselectclause.logputl("sortselectclause=");

	// default to ""
	if (!(var_typ & VARTYP_STR)) {
		if (!var_typ) {
			var_str.clear();
			var_typ = VARTYP_STR;
		} else
			this->createString();
	}

	if (DBTRACE)
		sortselectclause.logputl("sortselectclause=");

	var actualfilename = get_normal_filename(*this);
	// actualfilename.logputl("actualfilename=");
	var dictfilename = actualfilename;
	var actualfieldnames = fieldnames;
	var dictfile = "";
	var keycodes = "";
	bool bykey = false;
	var wordn;
	var distinctfieldnames = "";

	var whereclause = "";
	bool orwith = false;
	var orderclause = "";
	var joins = "";
	var unnests = "";
	var selects = "";
	var ismv = false;

	var maxnrecs = "";
	var xx;	 // throwaway return value

	//prepare to save calculated fields that cannot be calculated by postgresql for secondary processing
	var calc_fields = "";
	//var ncalc_fields=0;
	this->r(10, "");

	//catch bad FM character
	if (sortselectclause.var_str.find(FM_) != std::string::npos)
		throw MVDBException("Illegal FM character in " ^ sortselectclause);

	// sortselect clause can be a filehandle in which case we extract the filename from field1
	// omitted if filename.select() or filehandle.select()
	// cursor.select(...) where ...
	// SELECT (or SSELECT) nnn filename with .... and with ... by ... by
	// filename can be omitted if calling like filename.select(...) or filehandle.select(...)
	// nnn is optional limit to number of records returned
	// TODO only convert \t\r\n outside single and double quotes
	//var remaining = sortselectclause.a(1).convert("\t\r\n", "   ").trim();
	var remaining = sortselectclause.convert("\t\r\n", "   ").trim();

	// remaining.logputl("remaining=");

	// remove trailing options eg (S) or {S}
	var lastword = remaining.field2(" ", -1);
	if ((lastword[1] == "(" && lastword[-1] == ")") ||
		(lastword[1] == "{" && lastword[-1] == "}")) {
		remaining.splicer(-lastword.length() - 1, 999, "");
	}

	var firstucword = remaining.field(" ", 1).ucase();

	// sortselectclause may start with {SELECT|SSELECT {maxnrecs} filename}
	if (firstucword == "SELECT" || firstucword == "SSELECT") {
		if (firstucword == "SSELECT")
			bykey = true;

		// remove it
		var xx = getword(remaining, xx);

		firstucword = remaining.field(" ", 1).ucase();
	}

	// the second word can be a number to limit the number of records selected
	if (firstucword.length() and firstucword.isnum()) {
		maxnrecs = firstucword;

		// remove it
		var xx = getword(remaining, xx);

		firstucword = remaining.field(" ", 1).ucase();
	}

	// the next word can be the filename if not one of the select clause words
	// override any filename in the cursor variable
	if (firstucword && not var("BY BY-DSND WITH WITHOUT ( { USING DISTINCT").locateusing(" ", firstucword)) {
		actualfilename = firstucword;
		dictfilename = actualfilename;
		// remove it
		var xx = getword(remaining, firstucword);
	}

	// actualfilename.logputl("actualfilename=");
	if (!actualfilename) {
		// this->outputl("this=");
		throw MVDBException("filename missing from select statement:" ^ sortselectclause);
	}

	while (remaining.length()) {

		// remaining.logputl("remaining=");
		// whereclause.logputl("whereclause=");
		// orderclause.logputl("orderclause=");

		var ucword;
		var word1 = getword(remaining, ucword);

		// skip options (last word and surrounded by brackets)
		// (S) etc
		// options - last word enclosed in () or {}
		if (!remaining.length() &&
			((word1[1] == "(" && word1[-1] == ")") ||
			 (word1[1] == "{" && word1[-1] == "}"))) {
			// word1.logputl("skipping last word in () options ");
			continue;
		}

		// 1. numbers or strings without leading clauses like with ... mean record keys
		// 2. value chars are " ' 0-9 . + -
		// 3. values are ignored after any with/by statements to skip the following
		//    e.g. JUSTLEN "T#20" or HEADING "..."
		else if (valuechars.index(word1[1])) {
			if (!whereclause && !orderclause) {
				if (keycodes)
					keycodes ^= FM;
				keycodes ^= word1;
			}
			continue;
		}

		// using filename
		else if (ucword == "USING" && remaining) {
			dictfilename = getword(remaining, xx);
			if (!dictfile.open("dict." ^ dictfilename)) {
				throw MVDBException("select() dict_" ^ dictfilename ^
									" file cannot be opened");

			}
			continue;
		}

		// distinct fieldname (returns a field instead of the key)
		else if (ucword == "DISTINCT" && remaining) {

			var distinctfieldname = getword(remaining, xx);
			var distinctexpression = get_dictexpression(*this, 
				actualfilename, actualfilename, dictfilename, dictfile,
				distinctfieldname, joins, unnests, selects, ismv, false);
			var naturalsort_distinctexpression = get_dictexpression(*this, 
				actualfilename, actualfilename, dictfilename, dictfile,
				distinctfieldname, joins, unnests, selects, ismv, true);

			if (true) {
				// this produces the right values but in random order
				// it use any index on the distinct field so it works on large
				// indexed files select distinct is really only useful on INDEXED
				// fields unless the file is small
				distinctfieldnames = "DISTINCT " ^ (unnests ? distinctfieldname : distinctexpression);
			} else {
				// this produces the right results in the right order
				// BUT DOES IS USE INDEXES AND ACT VERY FAST??
				distinctfieldnames = "DISTINCT ON (" ^
									 naturalsort_distinctexpression ^ ") " ^
									 distinctexpression;
				orderclause ^= ", " ^ naturalsort_distinctexpression;
			}
			continue;
		}

		// by or by-dsnd
		else if (ucword == "BY" || ucword == "BY-DSND") {
			// next word must be dictid
			var dictid = getword(remaining, xx);
			var dictexpression =
				get_dictexpression(*this, actualfilename, actualfilename, dictfilename,
								  dictfile, dictid, joins, unnests, selects, ismv, true);

			// dictexpression.logputl("dictexpression=");
			// orderclause.logputl("orderclause=");

			// no filtering in database on calculated items
			//save then for secondary filtering
			if (dictexpression.index("exodus_call"))
			//if (dictexpression == "true")
			{
				if (!calc_fields.a(1).locate(dictid)) {
					//++ncalc_fields;
					calc_fields.r(1, -1, dictid);
				}
				continue;
			}

			//use postgres collation instead of exodus_extract_sort
			if (dictexpression.contains("exodus_extract_sort")) {
				dictexpression.swapper("exodus_extract_sort", "exodus_extract_text");
				dictexpression ^= " COLLATE exodus_natural";
			}

			orderclause ^= ",\n " ^ dictexpression;

			if (ucword == "BY-DSND")
				orderclause ^= " DESC";

			continue;
		}

		// subexpression combination
		else if (ucword == "AND" || ucword == "OR") {
			// dont start with AND or OR
			if (whereclause)
				whereclause ^= "\n " ^ ucword;
			if (ucword == "OR") {
				orwith = true;
			}
			continue;
		}

		// subexpression grouping
		else if (ucword == "(" || ucword == ")") {
			whereclause ^= "\n " ^ ucword;
			continue;
		}

		// with dictid eq/starting/ending/containing/like 1 2 3
		// with dictid 1 2 3
		// with dictid between x and y
		else if (ucword == "WITH" || ucword == "WITHOUT") {

			/////////////////////////////////////////////////////////
			// Filter Stage 1 - Decide if positive or negative filter
			/////////////////////////////////////////////////////////

			var negative = ucword == "WITHOUT";

			// next word must be the NOT/NO or the dictionary id
			word1 = getword(remaining, ucword);

			// can negate before (and after) dictionary word
			// eg WITH NOT/NO INVOICE_NO or WITH INVOICE_NO NOT
			if (ucword == "NOT" || ucword == "NO") {
				negative = !negative;
				// word1=getword(remaining,true);
				// remove NOT or NO
				word1 = getword(remaining, ucword);
			}

			//////////////////////////////////////////////////////////
			// Filter Stage 2 - Acquire column function to be filtered
			//////////////////////////////////////////////////////////

			// skip AUTHORISED for now since too complicated to calculate in database
			// ATM if (word1.ucase()=="AUTHORISED") { 	if
			//(whereclause.substr(-4,4).ucase() == " AND")
			//whereclause.splicer(-4,4,""); 	continue;
			//}

			// process the dictionary id
			var forsort =
				false;	// because indexes are NOT created sortable (exodus_sort()
			var dictexpression =
				get_dictexpression(*this, actualfilename, actualfilename, dictfilename,
								  dictfile, word1, joins, unnests, selects, ismv, forsort);
			var usingnaturalorder = dictexpression.index("exodus_extract_sort") or dictexpression.index("exodus_natural");
			var dictid = word1;

			//var dictexpression_isarray=dictexpression.index("string_to_array(");
			var dictexpression_isarray = dictexpression.index("_array(");
			var dictexpression_isvector = dictexpression.index("to_tsvector(");
			//var dictexpression_isfulltext = dictid.substr(-5).ucase() == "_XREF";
			var dictexpression_isfulltext = dictid.substr(-4).ucase() == "XREF";

			// add the dictid expression
			//if (dictexpression.index("exodus_call"))
			//	dictexpression = "true";

			//whereclause ^= " " ^ dictexpression;

			// the words after the dictid can be NOT/NO or values
			// word1=getword(remaining, true);
			word1 = getword(remaining, ucword);

			///////////////////////////////////////////////////////////////////////
			// Filter Stage 3 - 2nd chance to decide if positive or negative filter
			///////////////////////////////////////////////////////////////////////

			// can negate before (and after) dictionary word
			// eg WITH NOT/NO INVOICE_NO or WITH INVOICE_NO NOT
			if (ucword == "NOT" || ucword == "NO") {
				negative = !negative;
				// word1=getword(remaining,true);
				// remove NOT/NO and acquire any values
				word1 = getword(remaining, ucword);
			}

			/////////////////////////////////////////////////
			// Filter Stage 4 - SIMPLE BETWEEN or FROM clause
			/////////////////////////////////////////////////

			// BETWEEN x AND y
			// FROM x TO y

			if (ucword == "BETWEEN" || ucword == "FROM") {

				//prevent BETWEEN being used on fields
				if (dictexpression_isvector) {
					throw MVDBException(
						sortselectclause ^
						" 'BETWEEN x AND y' and 'FROM x TO y' ... are not currently supported for mv or xref columns");
				}

				// get and append first value
				word1 = getword(remaining, ucword);

				// get and append second value
				var word2 = getword(remaining, xx);

				// discard any optional intermediate "AND"
				if (word2.ucase() == "AND" || word2.ucase() == "TO") {
					word2 = getword(remaining, xx);
				}

				// check we have two values (in word1 and word2)
				if (!valuechars.index(word1[1]) || !valuechars.index(word2[1])) {
					throw MVDBException(
						sortselectclause ^
						"BETWEEN x AND y/FROM x TO y must be followed by two values (x AND/TO y)");
				}

				if (usingnaturalorder) {
					word1 = naturalorder(word1.var_str);
					word2 = naturalorder(word2.var_str);
				}

				// no filtering in database on calculated items
				//save then for secondary filtering
				if (dictexpression.index("exodus_call")) {
					var opid = negative ? ">!<" : "><";

					//almost identical code for exodus_call above/below
					var calc_fieldn;
					if (!calc_fields.locate(dictid, calc_fieldn, 1)) {
						//++ncalc_fields;
						calc_fields.r(1, calc_fieldn, dictid);
					}

					//prevent WITH XXX appearing twice in the same sort/select clause
					//unless and until implementeda
					if (calc_fields.a(2, calc_fieldn))
						throw MVDBException("WITH " ^ dictid ^ " must not appear twice in " ^ sortselectclause.quote());

					calc_fields.r(2, calc_fieldn, opid);
					calc_fields.r(3, calc_fieldn, word1.lowerer());
					calc_fields.r(4, calc_fieldn, word2);

					whereclause ^= " true";
					continue;
				}
				//select numrange(100,150,'[]')  @> any(string_to_array('1,2,150',',','')::numeric[]);
				if (dictexpression_isarray) {
					var date_time_numeric;
					if (dictexpression.index("date_array(")) {
						whereclause ^= " daterange(";
						date_time_numeric = "date";
					} else if (dictexpression.index("time_array(")) {
						whereclause ^= " tsrange(";
						//date_time_numeric = "time";
						date_time_numeric = "interval";
					} else {
						whereclause ^= " numrange(";
						date_time_numeric = "numeric";
					}
					whereclause ^= word1 ^ "," ^ word2 ^ ",'[]') ";
					whereclause ^= " @> ";
					whereclause ^= " any( " ^ dictexpression ^ "::" ^ date_time_numeric ^ "[])";
					continue;
				}

				whereclause ^= " " ^ dictexpression;

				if (negative)
					whereclause ^= " NOT ";

				if (whereclause)
					whereclause ^= " BETWEEN " ^ word1 ^ " AND " ^ word2;

				continue;
			}

			////////////////////////////////////
			// Filter Stage 5 - ACQUIRE OPERATOR
			////////////////////////////////////

			// 1. currently using regular expression instead of SQL LIKE
			// 2. use "BETWEEN X and XZZZZZZZ" instead of "LIKE X%"
			//    to ensure that any postgresql btree index is used if present

			var op = "";
			var prefix = "";
			var postfix = "";

			//using regular expression logic for CONTAINING/STARTING/ENDING
			//will be converted to tsvector logic if dictexpression_isvector
			if (ucword == "CONTAINING" or ucword == "[]") {
				prefix = ".*";
				postfix = ".*";
				op = "=";
				word1 = getword(remaining, ucword);
			} else if (ucword == "STARTING" or ucword == "]") {

				//identical code above/below
				if (dictexpression_isvector) {
					prefix = "^";
					postfix = ".*";
				}
				//NOT using regular expression logic for single valued fields and STARTING
				else {
					op = "]";
				}

				word1 = getword(remaining, ucword);
			} else if (ucword == "ENDING" or ucword == "[") {
				prefix = ".*";
				postfix = "$";
				op = "=";
				word1 = getword(remaining, ucword);
			}

			//"normal" comparative filtering
			// 1) Acquire operator - or empty if not present

			// convert PICK/AREV relational operators to standard SQL relational operators
			// IS/ISNT/NOT -> EQ/NE/NE
			var aliasno;
			if (var("IS EQ NE NOT ISNT GT LT GE LE").locateusing(" ", ucword, aliasno)) {
				word1 = var("= = <> <> <> > < >= <=").field(" ", aliasno);
				ucword = word1;
			}

			// capture operator is any
			if (var("= <> > < >= <= ~ ~* !~ !~*").locateusing(" ", ucword)) {
				// is an operator
				op = ucword;
				// get another word (or words)
				word1 = getword(remaining, ucword);
			}

			////////////////////////////////////
			// Filter Stage 6 - ACQUIRE VALUE(S)
			////////////////////////////////////
			//TRACE(word1)

			// determine Pick/AREV values like "[xxx" "xxx]" and "[xxx]"
			// TODO
			if (word1[1] == "'") {

				if (word1[2] == "[") {
					word1.splicer(2, 1, "");
					prefix = ".*";

					//CONTAINING
					if (word1[-2] == "]") {
						word1.splicer(-2, 1, "");
						postfix = ".*";
					}
					//ENDING
					else {
						postfix = "$";
					}

					//STARTING
				} else if (word1[-2] == "]") {
					word1.splicer(-2, 1, "");

					//identical code above/below
					if (dictexpression_isvector) {
						prefix = "^";
						postfix = ".*";
					}
					//NOT using regular expression logic for single valued fields and STARTING
					//this should trigger 'COLLATE "C" BETWEEN x AND y" below to ensure postgres indexes are used
					else {
						if (op == "<>")
							negative = !negative;
						op = "]";
					}
				}
				ucword = word1.ucase();
			}

			//select WITH ..._XREF uses postgres full text searching
			//which has its own prefix and postfix rules. see below
			if (dictexpression_isfulltext) {
				prefix = "";
				postfix = "";
			}

			/*implement using posix regular string matching
				~ 	Matches regular expression, case sensitive
					'thomas' ~ '.*thomas.*'
				~* 	Matches regular expression, case insensitive
					'thomas' ~* '.*Thomas.*'
				!~ 	Does not match regular expression, case sensitive
					'thomas' !~ '.*Thomas.*'
				!~* Does not match regular expression, case insensitive
					'thomas' !~* '.*vadim.*'
			*/

			else if (!dictexpression_isvector && (prefix || postfix)) {

				//postgres match matches anything in the string unless ^ and/or $ are present
				// so .* is not necessary in prefix and postfix
				if (prefix == ".*")
					prefix = "";
				if (postfix == ".*")
					postfix = "";

				// escape any posix special characters;
				// [\^$.|?*+()
				// if present in the search criteria, they need to be escaped with
				// TWO backslashes.
				word1.swapper("\\", "\\\\");
				var special = "[^$.|?*+()";
				for (int ii = special.length(); ii > 0; --ii) {
					if (special.index(word1[ii]))
						word1.splicer(ii, 0, "\\");
				}
				word1.swapper("'" _FM_ "'", postfix ^ "'" _FM_ "'" ^ prefix);
				word1.splicer(-1, 0, postfix);
				word1.splicer(2, 0, prefix);

				//only ops <> and != are supported when using the regular expression operator (starting/ending/containing)
				if (op == "<>")
					negative = !negative;
				else if (op != "=" and op != "")
					throw MVDBException("SELECT ... WITH " ^ op ^ " " ^ word1 ^ " is not supported. " ^ prefix.quote() ^ " " ^ postfix.quote());

				// use regular expression operator
				op = "~";
				ucword = word1;
			}

			// word1 at this point may be empty, contain a value or be the first word of an unrelated clause
			// if non-value word1 unrelated to current phrase
			if (ucword.length() && !valuechars.index(ucword[1])) {

				// push back and treat as missing value
				// remaining[1,0]=ucword:' '
				remaining.splicer(1, 0, ucword ^ " ");

				// simulate no given value .. so a boolean filter like "WITH APPROVED"
				word1 = "";
				ucword = "";
			}

			var value = word1;

			//change 'WITH SOMEMVFIELD = ""' to just 'WITH SOMEMVFIELD' to avoid ts_vector searching for nothing
			if (value == "''") {

				//remove multivalue handling - duplicate code elsewhere
				if (dictexpression.index("to_tsvector(")) {
					//dont create exodus_tobool(to_tsvector(...
					dictexpression.swapper("to_tsvector('simple',","");
					dictexpression.popper();
					dictexpression_isvector = false;
				}

			}

			/////////////////////////////////////////////////////////////////////
			// Filter Stage 7 - SAVE INFO FOR CALCULATED FIELDS IN STAGE 2 SELECT
			/////////////////////////////////////////////////////////////////////

			// "Calculated fields" are exodus/c++ functions that cannot be run by postgres
			// and are done in exodus in mvprogram.cpp two pass select

			// no filtering in database on calculated items
			//save then for secondary filtering
			if (dictexpression.index("exodus_call"))
			//if (dictexpression == "true")
			{
				//no op or value means test for Pick/AREV true (zero and '' are false)
				if (op == "" && value == "")
					op = "!!";

				//missing op presumed to be =
				else if (op == "")
					op = "=";

				// invert comparison if "without" or "not" for calculated fields
				if (negative &&
					var("= <> > < >= <= ~ ~* !~ !~* !! ! ]").locateusing(" ", op, aliasno)) {
					// op.logputl("op entered:");
					negative = false;
					op = var("<> = <= >= < > !~ !~* ~ ~* ! !! !]").field(" ", aliasno);
					// op.logputl("op reversed:");
				}

				//++ncalc_fields;
				//calc_fields.r(1,ncalc_fields,dictid);
				//calc_fields.r(2,ncalc_fields,op);
				//calc_fields.r(3,ncalc_fields,value);
				//dictid = calc_fields.a(1,n);
				//op     = calc_fields.a(2,n);
				//values  = calc_fields.a(3,n);

				//almost identical code for exodus_call above/below
				var calc_fieldn;
				if (!calc_fields.locate(dictid, calc_fieldn, 1)) {
					//++ncalc_fields;
					calc_fields.r(1, calc_fieldn, dictid);
				}
				if (calc_fields.a(2, calc_fieldn))
					throw MVDBException("WITH " ^ dictid ^ " must not appear twice in " ^ sortselectclause.quote());

				//save the op
				calc_fields.r(2, calc_fieldn, op);

				//save the value(s) after removing quotes and using SM to separate values instead of FM
				calc_fields.r(3, calc_fieldn, value.unquote().swap("'" _FM_ "'", FM).convert(FM, SM));

				//place holder to be removed before issuing actual sql command
				whereclause ^= " true";

				continue;
			}

			///////////////////////////////////////////////////////////
			// Filter Stage 8 - DUMMY OP AND VALUE SAVE IF NOT PROVIDED
			///////////////////////////////////////////////////////////

			// missing op and value means NOT '' or NOT 0 or NOT nullptr
			// WITH CLIENT_TYPE
			if (op == "" && value == "") {
				//op = "<>";
				//value = "''";

				//remove conversion to date/number etc
				to_extract_text(dictexpression);

				//remove conversion to array
				//eg string_to_array(exodus_extract_text(JOBS.data,6, 0, 0), chr(29),'')
				if (dictexpression.substr(1, 16) == "string_to_array(") {
					dictexpression.splicer(1, 16, "");
					dictexpression.splicer(-13, "");
				}

				//remove multivalue handling - duplicate code elsewhere
				if (dictexpression.index("to_tsvector(")) {
					//dont create exodus_tobool(to_tsvector(...
					dictexpression.swapper("to_tsvector('simple',","");
					dictexpression.popper();
					//TRACE(dictexpression)
					dictexpression_isvector = false;
				}

				//currently tobool requires only text input
				//TODO some way to detect DATE SYMBOLIC FIELDS and not hack special dict words!
				//doesnt work on multivalued fields - results in:
				//exodus_tobool(SELECT_CURSOR_STAGE2_19397_37442_012029.TOT_SUPPINV_AMOUNT_BASE_calc, chr(29),)
				//TODO work out better way of determining DATE/TIME that must be tested versus null
				if (dictexpression.index("FULLY_") || (!dictexpression.index("exodus_extract") && dictexpression.index("_DATE")))
					dictexpression ^= " is not null";
				else
					dictexpression = "exodus_tobool(" ^ dictexpression ^ ")";
			}

			// missing op means =
			// eg WITH CLIENT_TYPE "X" .. assume missing "=" sign
			else if (op == "") {
				op = "=";
			}

			///////////////////////////////////////////////////////////
			// Filter Stage 9 - PROCESS DICTEXPRESSION, OP AND VALUE(S)
			///////////////////////////////////////////////////////////

			// op and value(s) are now set

			// natural order value(s)
			if (usingnaturalorder)
				value = naturalorder(value.var_str);

			// without xxx = "abc"
			// with xxx not = "abc"

			// notword.logputl("notword=");
			// ucword.logputl("ucword=");

			//allow searching for text with * characters embedded
			//otherwise interpreted as glob character?
			if (dictexpression_isvector) {
				value.swapper("*", "\\*");
			}

			// STARTING
			// special processing for STARTING]
			// convert "STARTING 'ABC'"  to "BETWEEN 'X' AND 'XZZZZZZ'
			// so that any btree index if present will be used. "LIKE" or REGULAR EXPRESSIONS will not use indexes
			if (op == "]") {

				var expression = "";
				// value is a FM separated list here so "subvalue" is a field
				for (var subvalue : value) {
					/* ordinary UTF8 collation strangely doesnt sort single punctuation characters along with phrases starting with the same
					   so we will use C collation which does. All so that we can use BETWEEN instead of LIKE to support STARTING WITH syntax

					Example WITHOUT collation showing % sorted in different places

					test_test=# select * from test1 order by key;
					    key    |   data    
					-----------+---------==--
					 %         | %
					 +         | +
					 1         | 1
					 10        | 10
					 2         | 2
					 20        | 20
					 A         | A
					 B         | B
					 %RECORDS% | RECORDS
					 +RECORDS+ | +RECORDS+
					*/
					dictexpression.replacer("^exodus_extract_number\\(", "exodus_extract_text\\(");
					expression ^= dictexpression ^ " COLLATE \"C\"";
					expression ^= " BETWEEN " ^ subvalue ^ " AND " ^ subvalue.splice(-1, 0, "ZZZZZZ") ^ FM;
				}
				expression.popper();
				expression.swapper(FM, " OR ");
				value = expression;

				// indicate that the dictexpression is included in the value(s)
				op = "(";

			}

			// single value data with multiple values filter
			else if (value.index(FM) && !dictexpression_isvector) {

				//WARNING ", " is swapped in mvprogram.cpp ::select()
				//so change there if changed here
				value.swapper(_FM_, ", ");

				// no processing for arrays (why?)
				if (dictexpression_isarray) {
				}

				//lhs is an array ("multivalues" in postgres)
				//dont convert rhs to in() or any()
				else if (op == "=") {
					to_extract_text(dictexpression);
					op = "in";
					value = "( " ^ value ^ " )";
				}

				// not any of
				else if (op == "<>") {
					to_extract_text(dictexpression);
					op = "not in";
					value = "( " ^ value ^ " )";
				}

				//any of
				else {
					to_extract_text(dictexpression);
					value = "ANY(ARRAY[" ^ value ^ "])";
				}
			}

			//full text search OR mv data search
			if (dictexpression_isvector) {

				//see note on isxref in "multiple values" section above
				op = "@@";

				// & separates multiple required
				// | separates multiples alternatives
				// 'fat & (rat | cat)'::tsquery;
				//
				// :* means "ending in" to postgres tsquery. see:
				// https://www.postgresql.org/docs/10/datatype-textsearch.html
				// "lexemes in a tsquery can be labeled with * to specify prefix matching:"
				//
				// Spaces are NOT allowed in values unless quoted
				// Single quotes must be doubled '' (not ")
				// since the whole query string will be single quoted

				//in full text query on multiple words,
				//we implement that words all are required
				//all values and words separated by spaced are used as "word stems"

				//using to_tsquery to search multivalued data
				if (not dictexpression_isfulltext) {

					//double the single quotes so the whole thing can be wrapped in single quotes
					//and because to_tsquery generates a syntax error in case of spaces inside values unless quotedd
					value.swapper("'","''");

					//wrap everything in single quotes for sql
					value.squoter();

					//multiple with options become alternatives using to_tsquery | divider
					value.swapper(_FM_, "|");

				}

				// if full text search
				//if (dictid.substr(-5).ucase() == "_XREF") {
				if (dictexpression_isfulltext) {

					//https://www.postgresql.org/docs/current/textsearch-controls.html
					//and
					//https://www.postgresql.org/docs/current/datatype-textsearch.html#DATATYPE-TSQUERY

					//construct according to ts_query syntax using & | ( )
					//e.g. trying to find records containing either ADIDAS or KIA MOTORS where \036 is VM
					//value 'ADID\036KIA&MOT' -> '(ADID:*)|(KIA:*&MOT:*)

					// xxx:* searches for words starting with xxx

					//multivalues are searched using "OR" which is the | pipe character in ts_query syntax
					//words separated by spaces (or & characters) are searched for uing "AND" which is & in ts_query syntax
					var values="";
					value.unquoter().converter(VM,FM);
					for (var partvalue : value) {

						//remove all single quotes
						//partvalue.converter("'","");

						//swap all single quotes in search term for pairs of single quotes as per postgres syntax
						partvalue.swapper("'","''");

						//append postfix :* to every search word
						//so STEV:* also finds STEVE and STEVEN

						//spaces should have been converted to & before selection
						//spaces imply &
						//partvalue.swapper(" ", "&");
						//partvalue.splicer(-1, 0, ":*");

						//treat entered colons as &
						partvalue.swapper(":", "&");

						//respect any user entered AND or OR operators
						//search for all words STARTING with user defined words
						partvalue.swapper("&", ":*&");
						partvalue.swapper("|", ":*|");
						partvalue.swapper("!", ":*!");

						partvalue ^= ":*";

						values ^= "(" ^ partvalue ^ ")";
						values ^= FM;
					}
					values.popper();
					values.swapper(FM, "|");
					value = values.squote();
				}
				//select multivalues starting "XYZ" by selecting "XYZ]"
				else if (postfix) {
					value.swapper("]''", "'':*");
					value.swapper("]", ":*");
					//value.swapper("|", ":*|");
					value.splicer(-1, 0, ":*");
				}

				value.swapper("]''", "'':*");
				value.swapper("]", ":*");
				//value.splicer(-1, 0, ":*");

				//use "simple" dictionary (ie none) to allow searching for words starting with 'a'
				//use "english" dictionary for stemming (or "simple" dictionary for none)
				// MUST use the SAME in both to_tsvector AND to_tsquery
				//https://www.postgresql.org/docs/10/textsearch-dictionaries.html
				//this is the sole occurrence of to_tsquery in mvdbpostgres.cpp
				//it will be used like to_tsvector(...) @@ to_tsquery(...)
				if (value)
					value = "to_tsquery('simple'," ^ value ^ ")";
				//value = "to_tsquery('english'," ^ value ^ ")";

				/* creating a "none" stop list?
				printf "" > /usr/share/postgresql/10/tsearch_data/none.stop
				CREATE TEXT SEARCH DICTIONARY public.simple_dict (
				    TEMPLATE = pg_catalog.simple,
				    STOPWORDS = none
				);
				in psql default_text_search_config(pgcatalog.simple_dict)
				*/
			}

			// testing for "" may become testing for null
			// for date and time which are returned as null for empty string
			else if (value == "''") {
				if (dictexpression.index("extract_date") ||
					dictexpression.index("extract_datetime") ||
					dictexpression.index("extract_time")) {
					//if (op == "=")
					//	op = "is";
					//else
					//	op = "is not";
					//value = "null";
					dictexpression.swapper("extract_date(","extract_text(");
					dictexpression.swapper("extract_datetime(","extract_text(");
					dictexpression.swapper("extract_time(","extract_text(");
				}
				// currently number returns 0 for empty string
				//|| dictexpression.index("extract_number")
				else if (dictexpression.index("extract_number")) {
					//value = "'0'";
					dictexpression.swapper("extract_number(","extract_text(");
				}
				//horrible hack to allow filtering calculated date fields versus ""
				//TODO detect FULLY_BOOKED and FULLY_APPROVED as dates automatically
				else if (dictexpression.index("FULLY_")) {
					if (op == "=")
						op = "is";
					else
						op = "is not";
					value = "null";
				}
			}

			//if selecting a mv array then convert right hand side to array
			//(can only handle = operator at the moment)
			if (dictexpression_isarray && (op == "=" or op == "<>")) {
				if (op == "<>") {
					negative = !negative;
					op = "=";
				}

				if (value == "''") {
					value = "'{}'";
				} else {
					op = "&&";	//postgres array overlap operator
					value.swapper("'", "\"");
					//convert to postrgesql array syntax
					value = "'{" ^ value ^ "}'";
				}
			}

			//////////////////////////////////////////////
			// Filter Stage 10 - COMBINE INTO WHERE CLAUSE
			//////////////////////////////////////////////

			//negate
			if (negative)
				whereclause ^= " not";

			if (op == "(")
				whereclause ^= " ( " ^ value ^ " )";
			else
				whereclause ^= " " ^ dictexpression ^ " " ^ op ^ " " ^ value;
			// whereclause.logputl("whereclause=");

		}  //with/without

	}  // getword loop

	if (calc_fields && orwith) {
		//		throw MVDBException("OR not allowed with sort/select calculated fields");
	}

	// prefix specified keys into where clause
	if (keycodes) {
		//if (keycodes.count(FM))
		{
			keycodes = actualfilename ^ ".key IN ( " ^ keycodes.swap(FM, ", ") ^ " )";

			if (whereclause)
				//whereclause ^= "\n AND ( " ^ keycodes ^ " ) ";
				whereclause = keycodes ^ "\n AND " ^ whereclause;
			else
				whereclause = keycodes;
		}
	}
	//TRACE(actualfilename)
	// sselect add by key on the end of any specific order bys
	if (bykey)
		orderclause ^= ", " ^ actualfilename ^ ".key";

	//if calculated fields then secondary sort/select is going to use data column, so add the data column if missing
	if (calc_fields && actualfieldnames.substr(-6) != ", data")
		actualfieldnames ^= ", data";

	//remove mv::integer if no unnesting (sort on mv fields)
	if (!unnests) {
		// sql ^= ", 0 as mv";
		if (actualfieldnames.index("mv::integer, data")) {
			// replace the mv column with zero if selecting record
			actualfieldnames.swapper("mv::integer, data", "0::integer, data");
		} else
			actualfieldnames.swapper(", mv::integer", "");
	}

	// if any active select, convert to a file and use as an additional filter on key
	// or correctly named savelistfilename exists from getselect or makelist
	var listname = "";
	// see also listname below
	//	if (this->hasnext()) {
	//		listname=this->a(1) ^ "_" ^ getprocessn() ^ "_tempx";
	//		this->savelist(listname);
	//		var savelistfilename="savelist_" ^ listname;
	//		joins ^= " \nINNER JOIN\n " ^ savelistfilename ^ " ON " ^ actualfilename ^
	//".key = " ^ savelistfilename ^ ".key";
	//	}

	// disambiguate from any INNER JOIN key
	//actualfieldnames.logputl("actualfieldnames=");
	//actualfieldnames.swapper("key", actualfilename ^ ".key");
	//actualfieldnames.swapper("data", actualfilename ^ ".data");
	actualfieldnames.replacer("\\bkey\\b", actualfilename ^ ".key");
	actualfieldnames.replacer("\\bdata\\b", actualfilename ^ ".data");

	// DISTINCT has special fieldnames
	if (distinctfieldnames)
		actualfieldnames = distinctfieldnames;

	// remove redundant clauses
	whereclause.swapper("\n AND true", "");
	whereclause.swapper("true\n AND ", "");

	//determine the connection from the filename
	//could be an attached on a non-default connection
	//selecting dict files would trigger this
	//TRACE(*this)
	//TRACE(actualfilename)
	if (not this->a(2) || actualfilename.lcase().starts("dict.")) {
		var actualfile;
		if (actualfile.open(actualfilename))
			this->r(2, actualfile.a(2));
		//TRACE(actualfile)
	}
	//TRACE(*this)
	//save any active selection in a temporary table and INNER JOIN to it to avoid complete selection of primary file
	if (this->hasnext()) {

		//create a temporary sql table to hold the preselected keys
		var temptablename = "SELECT_CURSOR_" ^ this->a(1);
		//var createtablesql = "DROP TABLE IF EXISTS " ^ temptablename ^ ";\n";
		//createtablesql ^= "CREATE TABLE " ^ temptablename ^ "\n";
		var createtablesql = "CREATE TEMPORARY TABLE IF NOT EXISTS " ^ temptablename ^ "\n";
		createtablesql ^= " (KEY TEXT)\n";
		createtablesql ^= ";DELETE FROM " ^ temptablename ^ "\n";
		var errmsg;
		if (!this->sqlexec(createtablesql, errmsg)) {
			throw MVDBException(errmsg);
		}

		//readnext the keys into a temporary table
		var key;
		while (this->readnext(key)) {
			//std::cout<<key<<std::endl;
			this->sqlexec("INSERT INTO " ^ temptablename ^ "(KEY) VALUES('" ^ key.swap("'", "''") ^ "')");
		}

		if (this->a(3))
			debug();
		//must be empty!

		joins.inserter(1, 1, "\n RIGHT JOIN " ^ temptablename ^ " ON " ^ temptablename ^ ".key = " ^ actualfilename ^ ".key");
	}

	// assemble the full sql select statement:

	//DECLARE - cursor
	// WITH HOLD is a very significant addition
	// var sql="DECLARE cursor1_" ^ (*this) ^ " CURSOR WITH HOLD FOR SELECT " ^ actualfieldnames
	// ^ " FROM ";
	//TRACE(*this);
	var sql = "DECLARE\n cursor1_" ^ this->a(1).convert(".", "_") ^ " SCROLL CURSOR WITH HOLD FOR";

	//SELECT - field/column names
	sql ^= " \nSELECT\n " ^ actualfieldnames;
	if (selects)
		sql ^= selects;

	//FROM - filename and any specially related files
	sql ^= " \nFROM\n " ^ actualfilename;

	//JOIN - (1)?
	if (joins.a(1))
		sql ^= " " ^ joins.a(1).convert(VM, "\n");

	//UNNEST - mv fields
	//mv fields get added to the FROM clause like "unnest() as xyz" allowing the use of xyz in WHERE/ORDER BY
	//should only be one unnest (parallel mvs if more than one) since it is not clear how sselect by mv by mv2 should work if they are not in parallel
	if (unnests) {
		// unnest
		sql ^= ",\n unnest(\n  " ^ unnests.a(3).swap(VM, ",\n  ") ^ "\n )";
		// as fake tablename
		sql ^= " with ordinality as mvtable1";
		// brackets allow providing column names for use elsewhere
		// and renaming of automatic column "ORDINAL" to "mv" for use in SELECT key,mv ...
		// sql statement
		sql ^= "( " ^ unnests.a(2).swap(VM, ", ") ^ ", mv)";
	}

	//JOIN - related files
	if (joins.a(2))
		sql ^= " " ^ joins.a(2).convert(VM, "");

	//WHERE - excludes calculated fields if doing stage 1 of a two stage sort/select
	//TODO when doing stage2, skip "WITH/WITHOUT xxx" of stage1 fields
	if (whereclause)
		sql ^= " \nWHERE \n" ^ whereclause;

	//ORDER - suppressed if doing stage 1 of a two stage sort/select
	if (orderclause && !calc_fields)
		sql ^= " \nORDER BY \n" ^ orderclause.substr(3);

	//LIMIT - number of records returned
	// no limit initially if any calculated items - limit will be done in secondary sort/select
	if (maxnrecs && !calc_fields)
		sql ^= " \nLIMIT\n " ^ maxnrecs;

	// Final catch of obsolete function that was replaced by COLLATE keyword
	sql.replacer("exodus_extract_sort\\(", "exodus_extract_text\\(");

	//sql.logputl("sql=");

	// DEBUG_LOG_SQL
	// if (DBTRACE)
	//	exodus::logputl(sql);

	// first close any existing cursor with the same name, otherwise cannot create  new cursor
    // Avoid generating sql errors since they abort transations
	if (this->cursorexists()) {
		var sql = "";
		sql ^= "CLOSE cursor1_";

		if (this->assigned()) {
			var cursorcode = this->a(1).convert(".", "_");
			sql ^= cursorcode;
			var cursorid = this->a(2) ^ "_" ^ cursorcode;
			thread_mvresults.erase(cursorid);
		}

		var errmsg;
		if (!this->sqlexec(sql, errmsg)) {

			if (errmsg)
				errmsg.errputl("::selectx on handle(" ^ *this ^ ") " ^ sql ^ "\n");
			// return false;
		}
	}

	var errmsg;
	if (!this->sqlexec(sql, errmsg)) {

		if (listname)
			this->deletelist(listname);

		// TODO handle duplicate_cursor sqlstate 42P03
		sql.logputl("sql=");

		throw MVDBException(errmsg);

		// if (autotrans)
		//	rollbacktrans();
		return false;
	}

	//sort/select on calculated items may be done in mvprogram::calculate
	//which can call calculate() and has access to mv.RECORD, mv.ID etc
	if (calc_fields) {
		calc_fields.r(5, dictfilename.lower());
		calc_fields.r(6, maxnrecs);
		this->r(10, calc_fields.lower());
	}

	return true;
}

void var::clearselect() {

	// THISIS("void var::clearselect() const")
	// assertString(function_sig);

	// default cursor is ""
	this->unassigned("");

	/// if readnext through string
	//3/4/5/6 setup in makelist. cleared in clearselect
	//if (this->a(3) == "%MAKELIST%")
	{
		this->r(6, "");
		this->r(5, "");
		this->r(4, "");
		this->r(3, "");
		//		return;
	}

	var listname = (*this) ^ "_" ^ getprocessn() ^ "_tempx";

	// if (DBTRACE)
	//	exodus::logputl("DBTRACE: ::clearselect() for " ^ listname);

	// Dont close cursor unless it exists otherwise sql error aborts any transaction
    // Avoid generating sql errors since they abort transations
	// if (not this->cursorexists())
	if (not this->cursorexists())
		return;

	// clear any select list
	this->deletelist(listname);

	var errors;

	//delete any temporary sql table created to hold preselected keys
	//if (this->assigned())
	//{
	//	var temptablename="PRESELECT_CURSOR_" ^ this->a(1);
	//	var deletetablesql = "DROP TABLE IF EXISTS " ^ temptablename ^ ";\n";
	//	if (!this->sqlexec(deletetablesql, errors))
	//	{
	//		if (errors)
	//			errors.logputl("::clearselect " ^ errors);
	//		return;
	//	}
	//}

	//if (this->assigned())
	var cursorcode = this->a(1).convert(".", "_");
	var cursorid = this->a(2) ^ "_" ^ cursorcode;

	// Clean up cursor cache
	thread_mvresults.erase(cursorid);

	var sql = "";
	// sql^="DECLARE BEGIN ";
	sql ^= "CLOSE cursor1_" ^ cursorcode;
	// sql^="\nEXCEPTION WHEN\n invalid_cursor_name\n THEN";
	// sql^="\nEND";

	//sql.output();

	if (!this->sqlexec(sql, errors)) {
		if (errors)
			errors.errputl("::clearselect on handle(" ^ *this ^ ") ");
		return;
	}

	return;
}

// NB global not member function
//	To make it var:: privat member -> pollute mv.h with PGresultptr :(
// bool readnextx(const std::string& cursor, PGresultptr& mvresult)
// called by readnext (and perhaps hasnext/select to implement LISTACTIVE)
//bool readnextx(CVR cursor, PGresult* pgresult, PGconn* pgconn, int  nrows, int* rown) {
bool readnextx(CVR cursor, PGconn* pgconn, int  direction, PGresult*& pgresult, int* rown) {

	var cursorcode = cursor.a(1).convert(".", "_");
	var cursorid = cursor.a(2) ^ "_" ^ cursorcode;

	MVresult* mvresult = nullptr;
	auto entry = thread_mvresults.find(cursorid);
	if (entry != thread_mvresults.end()) {

		// Extract the current pgresult and rown of the cursor
		mvresult = &entry->second;
		*rown = mvresult->rown_;

		// If backwards
		// (should only be done after going forwards)
		if (direction < 0) {

			// rown is unlikely to be used after requesting backwards
			(*rown)--;

			mvresult->rown_ = *rown;

			return true;
		}

		//Increment the rown counter
		(*rown)++;

		// If forwards
		// Increment the current rown index into the pgresult
		// return success and the rown if within bounds
//TRACE(*rown)
//TRACE(PQntuples(*mvresult))
		if (*rown < PQntuples(*mvresult)) {

			// Save the rown for the next iteration
			mvresult->rown_ = *rown;

			// Return the pgresult array
			pgresult = mvresult->pgresult_;

//TRACE(*rown)
//TRACE(getpgresultcell(pgresult, *rown, 0))

			// Indicate success. true = found a new key/record
			return true;
		}

	}

	var fetch_nrecs=64;

	var sql;
	//if (direction >= 0)
	sql = "FETCH FORWARD " ^ fetch_nrecs ^ " in cursor1_" ^ cursorcode;
	//else
	//	sql = "FETCH BACKWARD " ^ var(-1).abs() ^ " in cursor1_" ^ cursorcode;

	// sql="BEGIN;" ^ sql ^ "; END";

	// execute the sql
	// cant use sqlexec here because it returns data
	// sqlexec();
	MVresult mvresult2;
	if (!get_mvresult(sql, mvresult2, pgconn)) {

		if (entry != thread_mvresults.end())
			thread_mvresults.erase(entry);

		var errmsg = var(PQresultErrorMessage(mvresult2));
		// errmsg.logputl("errmsg=");
		// var(mvresult2).logputl("mvresult2=");
		var sqlstate = "";
		if (PQresultErrorField(mvresult2, PG_DIAG_SQLSTATE)) {
			sqlstate = var(PQresultErrorField(mvresult2, PG_DIAG_SQLSTATE));
		}
		// mvresult2 is NULLPTR if if get_mvresult failed but since the mvresult is needed by
		// the caller, it will be cleared by called if not NULLPTR PQclear(mvresult2);

		// if cursor simply doesnt exist then see if a savelist one is available and enable
		// it 34000 - "ERROR:  cursor "cursor1_" does not exist"
		if (direction >= 0 && sqlstate == "34000") {
			return false;

			/**
			//if the standard select list file is available then select it, i.e. create
			a CURSOR, so FETCH has something to work on var listfilename="savelist_" ^
			cursor ^ "_" ^ getprocessn() ^ "_tempx"; if (not var().open(listfilename))
				return false;
			//TODO should add BY LISTITEMNO
			if (not cursor.select("select " ^ listfilename))
				return false;
			if (DBTRACE)
				exodus::logputl("DBTRACE: readnextx(...) found standard selectfile "
			^ listfilename);

			return readnextx(cursor, mvresult, pgconn, clearselect_onfail, forwards);
			**/
		}

		// any other error
		if (errmsg)
			throw MVDBException(errmsg ^ " sqlstate= " ^ sqlstate.quote() ^ " in SQL " ^
								sql);

		return false;
	}

	// If no rows returned
	//if (!PQntuples(*mvresult))
	//	return false;

	// 1. Do NOT clear the cursor even if forward since we may be testing it
	// 2. DO NOT clear since the mvresult2 is needed by the caller

	//Increment the rown counter from -1 to 0
	//mvresult2.rown_++;

	// Save rown for the next iteration
	*rown = mvresult2.rown_;

//TRACE(mvresult2.rown_)

	// Return a pointer to the pgresult
	pgresult = mvresult2;

	// Transfer the probably multi-row result into the thread_mvresults cache
	// for subsequent readnextx
	//thread_mvresults[cursorid] = pgresult;
	//thread_mvresults[cursorid] = mvresult2;
	if (entry != thread_mvresults.end())
		entry->second = pgresult;
	else
		thread_mvresults[cursorid] = pgresult;

	// Relinquish ownership of pgresult
	mvresult2.pgresult_ = nullptr;

	//dump_pgresult(pgresult);

	// Indicate success. true = found a new key/record
	return true;
}

bool var::deletelist(CVR listname) const {

	THISIS("bool var::deletelist(CVR listname) const")
	//?allow undefined usage like var xyz=xyz.select();
	// assertDefined(function_sig);
	ISSTRING(listname)

	if (DBTRACE)
		this->logputl("DBTR var::deletelist(" ^ listname ^ ") ");

	// open the lists file on the same connection
	var lists = *this;
	if (!lists.open("LISTS"))
		//skip this error for now because maybe no LISTS on some databases
		return false;
		//throw MVDBException("deletelist() LISTS file cannot be opened");

	// initial block of keys are stored with no suffix (i.e. no *1)
	lists.deleterecord(listname);

	// supplementary blocks of keys are stored with suffix *2, *3 etc)
	for (int listno = 2;; ++listno) {
		var xx;
		if (!xx.read(lists, listname ^ "*" ^ listno))
			break;
		lists.deleterecord(listname ^ "*" ^ listno);
	}

	return true;
}

bool var::savelist(CVR listname) {


	THISIS("bool var::savelist(CVR listname)")
	//?allow undefined usage like var xyz=xyz.select();
	// assertDefined(function_sig);
	ISSTRING(listname)

	if (DBTRACE)
		this->logputl("DBTR var::savelist(" ^ listname ^ ") ");

	// open the lists file on the same connection
	var lists = *this;
	if (!lists.open("LISTS"))
		throw MVDBException("savelist() LISTS file cannot be opened");

	// this should not throw if the list does not exist
	this->deletelist(listname);

	var listno = 1;
	var listkey = listname;
	var block = "";
	static int maxblocksize = 1024 * 1024;

	var key;
	var mv;
	while (this->readnext(key, mv)) {

		// append the key
		block.var_str.append(key.var_str);

		// append SM + mvno if mvno present
		if (mv) {
			block.var_str.push_back(VM_);
			block.var_str.append(mv.var_str);
		}

		// save a block of keys if more than a certain size (1MB)
		if (block.length() > maxblocksize) {
			// save the block
			block.write(lists, listkey);

			// prepare the next block
			listno++;
			listkey = listname ^ "*" ^ listno;
			block = "";
			continue;
		}

		// append a FM separator since lists use FM
		block.var_str.push_back(FM_);
	}

	// write the block
	if (block.length() > 1) {
		block.write(lists, listkey);
		listno++;
	}

	return listno > 0;
}

bool var::getlist(CVR listname) {

	THISIS("bool var::getlist(CVR listname) const")
	//?allow undefined usage like var xyz=xyz.select();
	// assertDefined(function_sig);
	ISSTRING(listname)

	if (DBTRACE)
		listname.logputl("DBTR var::getlist(" ^ listname ^ ") ");

	//int recn = 0;
	var key;
	var mv;
	var listfilename = "savelist_" ^ listname.field(" ", 1);
	listfilename.converter("-.*/", "____");
	// return this->selectx("key, mv::integer",listfilename);

	// open the lists file on the same connection
	var lists = *this;
	if (!lists.open("LISTS"))
		throw MVDBException("getlist() LISTS file cannot be opened");

	var keys;
	if (!keys.read(lists, listname))
		// throw MVDBException(listname.quote() ^ " list does not exist.");
		return false;

	// provide first block of keys for readnext
	this->r(3, listname);

	// list number for readnext to get next block of keys from lists file
	// suffix for first block is nothing (not *1) and then *2, *3 etc
	this->r(4, 1);

	// key pointer for readnext to remove next key from the block of keys
	this->r(5, 0);

	// keys separated by vm. each key may be followed by a sm and the mv no for readnext
	this->r(6, keys.lowerer());

	return true;
}

//TODO make it work for multiple keys or select list
bool var::formlist(CVR keys, CVR fieldno) {

	THISIS("bool var::formlist(CVR keys, CVR fieldno)")
	//?allow undefined usage like var xyz=xyz.select();
	assertString(function_sig);
	ISSTRING(keys)
	ISNUMERIC(fieldno)

	if (DBTRACE)
		keys.logputl("DBTR var::formlist() ");

	this->clearselect();

	var record;
	if (not record.read(*this, keys)) {
		keys.errputl("formlist() cannot read on handle(" ^ *this ^ ") ");
		return false;
	}

	//optional field extract
	if (fieldno)
		record = record.a(fieldno).converter(VM, FM);

	this->makelist("", record);

	return true;
}

// MAKELIST would be much better called MAKESELECT
// since the most common usage is to omit listname in which case the keys will be used to simulate a
// SELECT statement Making a list can be done simply by writing the keys into the list file without
// using this function
bool var::makelist(CVR listname, CVR keys) {

	THISIS("bool var::makelist(CVR listname)")
	//?allow undefined usage like var xyz=xyz.select();
	assertDefined(function_sig);
	ISSTRING(listname)
	ISSTRING(keys)

	if (DBTRACE)
		this->logputl("DBTR var::makelist(" ^ listname ^ ") ");

	// this is not often used since can be achieved by writing keys to lists file directly
	if (listname) {
		this->deletelist(listname);

		// open the lists file on the same connection
		var lists = *this;
		if (!lists.open("LISTS"))
			throw MVDBException("makelist() LISTS file cannot be opened");

		keys.write(lists, listname);
		return true;
	}

	// provide a block of keys for readnext
	//3/4/5/6 setup in makelist. cleared in clearselect

	// listid in the lists file must be set for readnext to work, but not exist in the file
	// readnext will look for %MAKELIST%*2 in the lists file when it reaches the end of the
	// block of keys provided and must not find it
	this->r(3, "%MAKELIST%");

	// list number for readnext to get next block of keys from lists file
	// suffix for first block is nothing (not *1) and then *2, *3 etc
	this->r(4, 1);

	// key pointer for readnext to find next key from the block of keys
	this->r(5, 0);

	// keys separated by vm. each key may be followed by a sm and the mv no for readnext
	this->r(6, keys.lower());

	return true;
}

//bool var::hasnext() const {
bool var::hasnext() {

	// var xx;
	// return this->readnext(xx);

	// THISIS("bool var::hasnext() const")
	// assertString(function_sig);

	// default cursor is ""
	this->unassigned("");

	// readnext through string of keys if provided
	// Note: code similarity between hasnext and readnext
	var listid = this->a(3);
	if (listid) {
		var keyno = this->a(5);
		keyno++;

		var key_and_mv = this->a(6, keyno);

		// true if we have another key
		if (key_and_mv.length())
			return true;

		if (DBTRACE)
			this->logputl("DBTR var::hasnext(" ^ listid ^ ") ");

		// otherwise try and get another block
		var lists = *this;
		if (!lists.open("LISTS"))
			throw MVDBException("hasnext() LISTS file cannot be opened");

		var listno = this->a(4);
		listno++;
		listid.fieldstorer("*", 2, 1, listno);

		// if no more blocks of keys then return false
		var block;
		if (!block.read(lists, listid)) {

			//clear the listid
			this->r(3, "");

			return false;
		}

		// might as well cache the next block for the next readnext
		this->r(4, listno);
		this->r(5, 0);
		this->r(6, block.lowerer());

		return true;
	}

	//TODO avoid this trip to the database somehow?
    // Avoid generating sql errors since they abort transations
	if (!this->cursorexists())
		return false;

	auto pgconn = get_pgconnection(*this);
	if (!pgconn) {
		// this->clearselect();
		return false;
	}

	// Try to move the cursor forward
	PGresult* pgresult = nullptr;
	int rown;
	if (!readnextx(*this, pgconn, /*direction=*/1, pgresult, &rown))
		return false;

	//////////////////////////////
	// restore the cursor back one
	//////////////////////////////

	readnextx(*this, pgconn, /*direction=*/-1, pgresult, &rown);

	return true;
}

bool var::readnext(VARREF key) {
	var valueno;
	return this->readnext(key, valueno);
}

bool var::readnext(VARREF key, VARREF valueno) {

	//?allow undefined usage like var xyz=xyz.readnext();
	if (var_typ & VARTYP_MASK) {
		// throw MVUndefined("readnext()");
		var_str.clear();
		var_typ = VARTYP_STR;
	}

	// default cursor is ""
	this->unassigned("");


	THISIS("bool var::readnext(VARREF key, VARREF valueno) const")
	assertString(function_sig);

	var record;
	return this->readnext(record, key, valueno);
}

bool var::readnext(VARREF record, VARREF key, VARREF valueno) {

	//?allow undefined usage like var xyz=xyz.readnext();
	if (var_typ & VARTYP_MASK || !var_typ) {
		// throw MVUndefined("readnext()");
		var_str.clear();
		var_typ = VARTYP_STR;
	}

	// default cursor is ""
	this->unassigned("");


	THISIS("bool var::readnext(VARREF record, VARREF key, VARREF valueno) const")
	assertString(function_sig);
	ISDEFINED(key)
	ISDEFINED(record)

	// readnext through string of keys if provided
	// Note: code similarity between hasnext and readnext
	var listid = this->a(3);
	if (listid) {

		if (DBTRACE)
			this->logputl("DBTR var::readnext() ");

		record = "";
		while (true) {
			var keyno = this->a(5);
			keyno++;

			var key_and_mv = this->a(6, keyno);

			// if no more keys, try to get next block of keys, otherwise return false
			if (key_and_mv.length() == 0) {

				// makelist provides one block of keys and nothing in the lists file
				if (listid == "%MAKELIST%") {
					this->r(3, "");
					this->r(4, "");
					this->r(5, "");
					this->r(6, "");
					return false;
				}

				var lists = *this;
				if (!lists.open("LISTS"))
					throw MVDBException("readnext() LISTS file cannot be opened");

				var listno = this->a(4);
				listno++;
				listid.fieldstorer("*", 2, 1, listno);

				var block;
				if (!block.read(lists, listid)) {

					//clear the listid
					this->r(3, "");

					return false;
				}

				this->r(4, listno);
				this->r(5, 0);
				this->r(6, block.lowerer());
				continue;
			}

			// bump up the key no pointer
			this->r(5, keyno);

			// extract and return the key (and mv if present)
			key = key_and_mv.a(1, 1, 1);
			valueno = key_and_mv.a(1, 1, 2);
			return true;
		}
	}

	auto pgconn = get_pgconnection(*this);
	if (! pgconn)
		return "";

	//TODO avoid this trip to the database somehow?
    // Avoid generating sql errors since they abort transations
	if (!this->cursorexists())
		return false;

	//MVresult mvresult;
	PGresult* pgresult = nullptr;
	int rown;
	if (!readnextx(*this, pgconn, /*direction=*/1, pgresult, &rown)) {
		this->clearselect();
		return false;
	}

	//dump_pgresult(pgresult);

	// key is first column
	// char* data = PQgetvalue(mvresult, 0, 0);
	// int datalen = PQgetlength(mvresult, 0, 0);
	// key=std::string(data,datalen);
	key = getpgresultcell(pgresult, rown, 0);
	// TODO return zero if no mv in select because no by mv column

	//recursive call to skip any meta data with keys starting and ending %
	//eg keys like "%RECORDS%" (without the quotes)
	//similar code in readnext()
	if (key[1] == "%" && key[-1] == "%") {
		return readnext(record, key, valueno);
	}

	// vn is second column
	// valueno=var((int)ntohl(*(uint32_t*)PQgetvalue(mvresult, 0, 1)));
	// vn is second column
	// record is third column
	if (PQnfields(pgresult) > 1)
		// valueno=var((int)ntohl(*(uint32_t*)PQgetvalue(mvresult, 0, 1)));
		valueno = getpgresultcell(pgresult, rown, 1);
	else
		valueno = 0;

	// record is third column
	if (PQnfields(pgresult) < 3) {
		//var errmsg = "readnext() must follow a select() clause with option (R)";
		//this->lasterror(errmsg);
		//throw MVDBException(errmsg);
		// return false;

		// Dont throw an error, just return empty record. why?
		record = "";
	} else {
		record = getpgresultcell(pgresult, rown, 2);
	}

	return true;
}

bool var::createindex(CVR fieldname0, CVR dictfile) const {

	THISIS("bool var::createindex(CVR fieldname, CVR dictfile) const")
	assertString(function_sig);
	ISSTRING(fieldname0)
	ISSTRING(dictfile)

	var filename = get_normal_filename(*this);
	var fieldname = fieldname0.convert(".", "_");

	// actual dictfile to use is either given or defaults to that of the filename
	var actualdictfile;
	if (dictfile.assigned() and dictfile != "")
		actualdictfile = dictfile;
	else
		actualdictfile = "dict." ^ filename;

	// example sql
	// create index ads__brand_code on ads (exodus_extract_text(data,3,0,0));

	// throws if cannot find dict file or record
	var joins = "";	   // throw away - cant index on joined fields at the moment
	var unnests = "";  // unnests are only created for ORDER BY, not indexing or selecting
	var selects = "";
	var ismv;
	var forsort = false;
	var dictexpression = get_dictexpression(*this, filename, filename, actualdictfile, actualdictfile,
										   fieldname, joins, unnests, selects, ismv, forsort);
	// dictexpression.logputl("dictexp=");stop();

	//mv fields return in unnests, not dictexpression
	//if (unnests)
	//{
	//	//dictexpression = unnests.a(3);
	//	unnests.convert(FM,"^").logputl("unnests=");
	//}

	var sql;

	// index on calculated columns causes an additional column to be created
	if (dictexpression.index("exodus_call")) {
		("ERROR: Cannot create index on " ^ filename ^ " for calculated field " ^ fieldname).errputl();
		return false;

		/*

		// add a manually calculated index field
		var index_fieldname = "index_" ^ fieldname;
		sql = "alter table " ^ filename ^ " add " ^ index_fieldname ^ " text";
		if (not var().sqlexec(sql))
		{
			sql.logputl("sql failed ");
			return false;
		}

		// update the new index field for all records
		sql = "update " ^ filename ^ " set " ^ index_fieldname ^ " = " ^ dictexpression;
		sql.logputl("sql=");
		if (not var().sqlexec(sql))
		{
			sql.logputl("sql failed ");
			return false;
		}
		dictexpression = index_fieldname;
		*/

	}

	// create postgres index
	sql = "CREATE INDEX index__" ^ filename ^ "__" ^ fieldname ^ " ON " ^ filename;
	//if (ismv || fieldname.substr(-5).lcase() == "_xref")
	if (dictexpression.index("to_tsvector("))
		sql ^= " USING GIN";
	sql ^= " (";
	sql ^= dictexpression;
	sql ^= ")";

	var response = "";
	if (!this->sqlexec(sql, response)) {
		//ERROR:  cannot create index on foreign table "clients"
		//sqlstate:42809 CREATE INDEX index__suppliers__SEQUENCE_XREF ON suppliers USING GIN (to_tsvector('simple',dict_suppliers_SEQUENCE_XREF(suppliers.key, suppliers.data)))
		if (!response.index("sqlstate:42809"))
			response.errputl();
		return false;
	}

	return true;
}

bool var::deleteindex(CVR fieldname0) const {

	THISIS("bool var::deleteindex(CVR fieldname) const")
	assertString(function_sig);
	ISSTRING(fieldname0)

	var filename = get_normal_filename(*this);
	var fieldname = fieldname0.convert(".", "_");

	// delete the index field (actually only present on calculated field indexes so ignore
	// result) deleting the index field automatically deletes the index
	var index_fieldname = "index_" ^ fieldname;
	if (var().sqlexec("alter table " ^ filename ^ " drop " ^ index_fieldname))
		return true;

	// delete the index.
	// var filename=*this;
	var sql = "drop index index__" ^ filename ^ "__" ^ fieldname;
	bool result = this->sqlexec(sql);
	if (!result)
		this->lasterror().errputl();
	return result;
}

var var::listfiles() const {

	THISIS("var var::listfiles() const")
	assertDefined(function_sig);

	// from http://www.alberton.info/postgresql_meta_info.html

	var schemafilter = " NOT IN ('pg_catalog', 'information_schema') ";

	var sql =
		"SELECT table_name, table_schema FROM information_schema.tables"
		//" WHERE table_type = 'BASE TABLE' AND";
		" WHERE";
	sql ^= " table_schema " ^ schemafilter;

	sql ^= " UNION SELECT matviewname as table_name, schemaname as table_schema from pg_matviews";
	sql ^= " WHERE schemaname " ^ schemafilter;

	auto pgconn = get_pgconnection(*this);
	if (! pgconn)
		return "";

	MVresult mvresult;
	if (!get_mvresult(sql, mvresult, pgconn))
		return "";

	var filenames = "";
	int nfiles = PQntuples(mvresult);
	for (int filen = 0; filen < nfiles; filen++) {
		if (!PQgetisnull(mvresult, filen, 0)) {
			var filename = getpgresultcell(mvresult, filen, 0);
			var schema = getpgresultcell(mvresult, filen, 1);
			if (schema == "public")
				filenames ^= filename;
			else
				filenames ^= schema ^ "." ^ filename;
			filenames.var_str.push_back(FM_);
		}
	}
	filenames.var_str.pop_back();

	return filenames;
}

var var::dblist() const {

	THISIS("var var::dblist() const")
	assertDefined(function_sig);

	var sql = "SELECT datname FROM pg_database";

	auto pgconn = get_pgconnection(*this);
	if (! pgconn)
		return "";

	MVresult mvresult;
	if (!get_mvresult(sql, mvresult, pgconn))
		return "";

	var dbnames = "";
	auto ndbs = PQntuples(mvresult);
	for (auto dbn = 0; dbn < ndbs; dbn++) {
		if (!PQgetisnull(mvresult, dbn, 0)) {
			std::string dbname = getpgresultcell(mvresult, dbn, 0);
			if (!dbname.starts_with("template") and !dbname.starts_with("postgres")) {
				dbnames.var_str.append(dbname);
				dbnames.var_str.push_back(FM_);
			}
		}
	}
	dbnames.var_str.pop_back();

	return dbnames.sort();
}

//TODO avoid round trip to server to check this somehow or avoid calling it all the time
bool var::cursorexists() {
	// THISIS("var var::cursorexists() const")
	// could allow undefined usage since *this isnt used?
	// assertString(function_sig);

    // Avoid generating sql errors since they abort transations

	// default cursor is ""
	this->unassigned("");

	var cursorcode = this->a(1).convert(".", "_");
	var cursorid = this->a(2) ^ "_" ^ cursorcode;

	// true if exists in cursor cache
	if (thread_mvresults.find(cursorid) != thread_mvresults.end())
		return true;

	auto pgconn = get_pgconnection(*this);
	if (! pgconn)
		return "";

	// from http://www.alberton.info/postgresql_meta_info.html
	var sql = "SELECT name from pg_cursors where name = 'cursor1_" ^ cursorcode ^ "'";

	MVresult mvresult;
	if (!get_mvresult(sql, mvresult, pgconn))
		return false;

	return PQntuples(mvresult) > 0;
}

var var::listindexes(CVR filename0, CVR fieldname0) const {

	THISIS("var var::listindexes(CVR filename) const")
	// could allow undefined usage since *this isnt used?
	assertDefined(function_sig);
	ISSTRING(filename0)
	ISSTRING(fieldname0)

	var filename = filename0.a(1);
	var fieldname = fieldname0.convert(".", "_");

	// TODO for some reason doesnt return the exodus index_file__fieldname records
	// perhaps you have to be connected with sufficient postgres rights
	var sql =
		"SELECT relname"
		" FROM pg_class"
		" WHERE oid IN ("
		" SELECT indexrelid"
		" FROM pg_index, pg_class"
		" WHERE";
	if (filename)
		sql ^= " relname = '" ^ filename.lcase() ^ "' AND ";
	// if (fieldname)
	//	sql^=" ???relname = '" ^ fieldname.lcase() ^  "' AND ";
	sql ^=
		" pg_class.oid=pg_index.indrelid"
		" AND indisunique != 't'"
		" AND indisprimary != 't'"
		");";

	auto pgconn = get_pgconnection(*this);
	if (! pgconn)
		return "";

	// execute command or return empty string
	MVresult mvresult;
	if (!get_mvresult(sql, mvresult, pgconn))
		return "";

	var tt;
	var indexname;
	var indexnames = "";
	int nindexes = PQntuples(mvresult);
	var lc_fieldname = fieldname.lcase();
	for (int indexn = 0; indexn < nindexes; indexn++) {
		if (!PQgetisnull(mvresult, indexn, 0)) {
			indexname = getpgresultcell(mvresult, indexn, 0);
			if (indexname.substr(1, 6) == "index_") {
				tt = indexname.index("__");
				if (tt) {
					indexname.substrer(8, 999999).swapper("__", VM);
					if (fieldname && indexname.a(1, 2) != lc_fieldname)
						continue;

					// indexnames^=FM^indexname;
					var fn;
					if (not indexnames.locateby("A", indexname, fn, 0))
						indexnames.inserter(fn, indexname);
				}
			}
		}
	}
	// indexnames.splicer(1,1,"");

	return indexnames;
}

var var::reccount(CVR filename0) const {

	THISIS("var var::reccount(CVR filename_or_handle_or_null) const")
	// could allow undefined usage since *this isnt used?
	assertDefined(function_sig);
	ISSTRING(filename0)

	var filename = filename0 ?: (*this);

	// vacuum otherwise unreliable
	if (!this->statustrans())
		this->flushindex(filename);

	var sql = "SELECT reltuples::integer FROM pg_class WHERE relname = '";
	sql ^= filename.a(1).lcase();
	sql ^= "';";

	auto pgconn = get_pgconnection(filename);
	if (! pgconn)
		return "";

	// execute command or return empty string
	MVresult mvresult;
	if (!get_mvresult(sql, mvresult, pgconn))
		return "";

	var reccount = "";
	if (PQntuples(mvresult) && PQnfields(mvresult) > 0 && !PQgetisnull(mvresult, 0, 0)) {
		// reccount=var((int)ntohl(*(uint32_t*)PQgetvalue(mvresult, 0, 0)));
		reccount = getpgresultcell(mvresult, 0, 0);
		reccount += 0;
	}

	return reccount;
}

var var::flushindex(CVR filename) const {

	THISIS(
		"var var::flushindex(CVR filename="
		") const")
	// could allow undefined usage since *this isnt used?
	assertDefined(function_sig);
	ISSTRING(filename)

	var sql = "VACUUM";
	if (filename)
		// attribute 1 in case passed a filehandle instead of just filename
		sql ^= " " ^ filename.a(1).lcase();
	sql ^= ";";
	// sql.logputl("sql=");

	// TODO perhaps should get connection from filehandle if passed a filehandle
	auto pgconn = get_pgconnection(*this);
	if (! pgconn)
		return "";

	// execute command or return empty string
	MVresult mvresult;
	if (!get_mvresult(sql, mvresult, pgconn))
		return "";

	var flushresult = "";
	if (PQntuples(mvresult)) {
		flushresult = true;
	}

	return flushresult;
}

// used for sql commands that require no parameters
// returns 1 for success
// returns 0 for failure
// mvresult is returned to caller to extract any data and call PQclear(mvresult) in destructor of MVresult
static bool get_mvresult(CVR sql, MVresult& mvresult, PGconn* pgconn) {
	DEBUG_LOG_SQL

	/* dont use PQexec because is cannot be told to return binary results
	 and use PQexecParams with zero parameters instead
	//execute the command
	mvresult = get_mvresult(pgconn, sql.var_str.c_str());
	mvresult = mvresult;
	*/

	// Parameter array with no parameters
	const char* paramValues[1];
	int paramLengths[1];

	// will contain any mvresult IF successful
	mvresult = PQexecParams(pgconn, sql.toString().c_str(), 0, /* zero params */
							nullptr,									  /* let the backend deduce param type */
							paramValues, paramLengths,
							0,	 // text arguments
							0);	 // text results

	// Handle serious error. Why not throw?
	if (!mvresult) {
		var("ERROR: mvdbpostgres PQexec command failed, no error code: ").errputl();
		return false;
	}

	switch (PQresultStatus(mvresult)) {

		case PGRES_COMMAND_OK:
			return true;

		case PGRES_TUPLES_OK:
			return PQntuples(mvresult) > 0;

		case PGRES_NONFATAL_ERROR:

			var("ERROR: mvdbpostgres SQL non-fatal error code " ^
				var(PQresStatus(PQresultStatus(mvresult))) ^ ", " ^
				var(PQresultErrorMessage(mvresult)))
				.errputl();

			return true;

		default:

			var("ERROR: mvdbpostgres pqexec " ^ var(sql)).errputl();
			var("ERROR: mvdbpostgres pqexec " ^
				var(PQresStatus(PQresultStatus(mvresult))) ^ ": " ^
				var(PQresultErrorMessage(mvresult)))
				.errputl();

			return false;
	}

	// should never get here
	return false;

}

}  // namespace exodus
