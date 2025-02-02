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

//#include <algorithm>  //for count in osrename
#include <fstream>
#include <iostream>
#include <locale>
#include <mutex>
#include <filesystem>
#include <fnmatch.h> //for fnmatch() globbing
#include <chrono>
#include <unistd.h> //for close()

//used to convert to and from utf8 in osread and oswrite
#include <boost/locale.hpp>

//#include <boost/thread/thread.hpp> // for posix_time
#include <boost/date_time/posix_time/posix_time.hpp>

#include <exodus/mv.h>
#include "mvhandles.h"

std::once_flag locale_once_flag;
void locale_once_func() {
	// so wcscoll() sorts a<B etc instead of character number wise where B<a
	// wcscoll currently only used on non-pc/non-mac ... see var::localeAwareCompare
	// so has no effect on those platform for < <= etc
	// only en_US.utf-8 seems to be widely available on all platforms
	//.utf8 causes a utf-8 to wchar conversion when converting string to wstring by iterators
	//.begin() .end() and we want to be able to control conversion more accurately than an
	// automatic conversion en_US is missing from Ubuntu. en_IN is available on Ubuntu and
	// Centos but not MacOSX if (!setlocale (LC_COLLATE, "en_US.utf8"))
	// en_US.utf8 
	//std::cout << std::cout.getloc().name() << std::endl;
	if (!setlocale(LC_ALL, "en_US.utf8")) {
		if (!setlocale(LC_ALL, "C.UTF-8"))
			std::cout << "Cannot setlocale LC_COLLATE to en_US.utf8" << std::endl;
	};
	//std::cout << std::cout.getloc().name() << std::endl;
}
class LocaleOnce {
   public:
	LocaleOnce() { std::call_once(locale_once_flag, locale_once_func); }
};
static LocaleOnce locale_once_static;

namespace exodus {

// this object caches fstream * pointers, to avoid multiple reopening files
// extern MvHandlesCache mv_handles_cache;
// Lifecircle of fstream object:
//	- created (by new) and opened in osbread()/osbwrite();
//	- pointer to created object stored in h_cache;
//  - when user calls osclose(), the stream is closed and the object is deleted, and removed from
//  h_cache;
//	- if user forgets to call osclose(), the stream remains opened (alive) until
//		~MvHandlesCache for h_cache closes/deletes all registered objects.

std::locale get_locale(CVR locale_name)	// throw (MVError)
{
	// assume is checked prior to calling since this is an internal exodus function
	// THISIS("std::locale get_locale(CVR locale_name)")
	// ISSTRING(locale_name)

	if (not locale_name.length() || locale_name == "utf8") {
		std::locale old_locale;

		//https://exceptionshub.com/read-unicode-utf-8-file-into-wstring.html
		//wif.imbue(std::locale(std::locale::empty(), new std::codecvt_utf8<wchar_t>));

		std::locale utf8_locale(old_locale,
								new std::codecvt_utf8<wchar_t>);

		return utf8_locale;
	} else {
		try {
			if (locale_name.length()) {
				std::locale mylocale(locale_name.toString().c_str());
				return mylocale;
			} else {
				// dont trust default locale since on osx 10.5.6 it fails!
				std::locale mylocale("C");
				return mylocale;
			}
		} catch (std::runtime_error& re) {
			throw MVError("get_locale cannot create locale for " ^ locale_name);
		}
	}
}

bool checknotabsoluterootfolder(std::string dirname) {
	// safety - will not rename/remove top level folders
	// cwd to top level and delete relative
	// top level folder has only one slash either at the beginning or, on windows, like x:\ .
	// NB copy/same code in osrmdir and osrename
	//if (!dirname.ends_with(OSSLASH_))
	//	return true;
	if ((!SLASH_IS_BACKSLASH && dirname[0] == OSSLASH_ &&
		 //std::count(dirname.begin(), dirname.end(), OSSLASH_) < 3) ||
		 var(dirname).count(OSSLASH_) < 3) ||
		(SLASH_IS_BACKSLASH && (dirname[1] == ':') && (dirname[2] == OSSLASH_))) {

		std::cerr
			<< "Forced removal/renaming of top two level directories by absolute path is "
			   "not supported for safety but you can use cwd() and relative path."
			<< dirname << std::endl;

		var().lasterror(var(dirname).quote() ^ " Cannot be deleted or moved (absolute) because it is a top level dir");

		return false;
	}
	return true;
}

// Returns with trailing OSSLASH
var var::ostempdirname() const {
	std::error_code error_code;
	//TODO handle error code specifically
	std::string dirname = std::string(std::filesystem::temp_directory_path(error_code));
	if (dirname.back() != OSSLASH_)
		dirname.push_back(OSSLASH_);
	return dirname;
}

// This function in Ubuntu/g++ returns *sequential* temporary file names which is perhaps not good
// A temporary file is actually created and deleted just to get a name that can be created by other processes.
// Any files created by the caller must be deleted or will hang around in /tmp unless reboot etc.
//
// PROBLEM:
// The file/filename returned could also be created by othet process because we are closing/deleting the file immediately.
//
// SOLUTION:
// TODO 1. Remove this function from exodus
//         REPLACE it with some kind of osopen(anonymoustempfile) that must be osclosed and will exist but can never be referred to by name
//         You could create ANOTHER file with the same name but that would be a different inode/file in the file system.
//      2. Provide osopen without filename to get a filehandle without name that will be automatically deleted when osclose is called.
//
// Linux's temporary file philosophy is to unlink temporary files immediately (remove dir entry) so they cannot be opened by other processes
// and will automatically be fully deleted when the file handle is closed/process terminates.
//

//var var::ostempfilename() const {
//
//	// Linux immediately unlinks the temporary file so it exists without a name.
//	// the actual file is automatically deleted from the file system when the file handle is closed/process is closed.
//	std::FILE* tempfile = std::tmpfile();
//
//	// Linux-specific method to acquire the tmpfile name
//  // BUT FILE WILL NOT EXIST SINCE IT IS UNLINKED IMMEDIATELY AND FULLY DELETED ON FCLOSE BELOW
//	var tempfilename;
//	namespace fs = std::filesystem;
//	tempfilename.var_str = fs::read_symlink(fs::path("/proc/self/fd") / std::to_string(fileno(tempfile)));
//	tempfilename.var_typ = VARTYP_STR;
//	if (auto pos = tempfilename.var_str.find(' '); pos != std::string::npos)
//		tempfilename.var_str.resize(pos);
//
//	// We close the file handle because this function returns temporary file names, not handles
//	// and would leak file handles if we did not.
//	fclose(tempfile);
//
//	return tempfilename;
//
//}

// This function actually creates a file which must be cleaned up by the caller
// The filenames are random names
// TODO is this threadsafe?
var var::ostempfilename() const {

	//https://cpp.hotexamples.com/examples/-/-/mkstemp/cpp-mkstemp-function-examples.html

	// Create a char* template of the temporary file name desired
	// (2*26+10)^6 possible filenames = 56,800,235,584
	// Note that 64 bit hash = 1.84467440737e+19 combinations
	var rvo_tempfilename = this->ostempdirname() ^ "~exoXXXXXX";
	std::vector<char> buffer(rvo_tempfilename.var_str.begin(), rvo_tempfilename.var_str.end());
	buffer.push_back('\0');

	// Linux only function to create a temporary file
	int fd_or_error;
	if ((fd_or_error = mkstemp(&buffer[0])) == -1)
		throw MVError(var(__PRETTY_FUNCTION__) ^ " - Cannot create tempfilename " ^ rvo_tempfilename.quote());

	// Must close or we will leak file handles because this badly designed function returns a name not a handle
	::close(fd_or_error);

	// Get the actual generated temporary file name
	rvo_tempfilename.var_str = buffer.data();

	return rvo_tempfilename;
}

bool var::suspend() const {
	return this->osshell();
}

bool var::osshell() const {

	THISIS("var var::osshell() const")
	// will be checked again by toString()
	// but put it here so any unassigned error shows in osshell
	assertString(function_sig);

	// breakoff();
	int shellresult = system(this->to_cmd_string().c_str());
	// breakon();

	return !shellresult;
}

bool var::osshellread(CVR oscmd) {

	THISIS("var var::osshellread() const")
	// will be checked again by toString()
	// but put it here so any unassigned error shows in osshell
	ISSTRING(oscmd)

	// default is to return empty string in any case
	var_str.clear();
	var_typ = VARTYP_STR;

	// fflush?

	//"r" means read
	std::FILE* pfile = popen(oscmd.to_cmd_string().c_str(), "r");
	// return a code to indicate program failure. but can this ever happen?
	if (pfile == nullptr) {
		this->lasterror(oscmd.quote() ^ " osshell failed.");
		return false;
	}
	// TODO buffer overflow check
	char cstr1[4096] = {0x0};
	while (std::fgets(cstr1, sizeof(cstr1), pfile) != nullptr) {
		// std::printf("%s\n", result);
		// cannot trust that standard input is convertable from utf8
		// output.var_str+=wstringfromUTF8((const UTF8*)result,strlen(result));
		//std::string str1 = cstr1;
		//output.var_str += std::string(str1.begin(), str1.end());
		//readstr.var_str += std::string(cstr1);
		var_str += cstr1;
	}

	//return true if no error code
	return !pclose(pfile);
}

bool var::osshellwrite(CVR writestr) const {

	THISIS("var var::osshellwrite(CVR writestr) const")
	// will be checked again by toString()
	// but put it here so any unassigned error shows in osshell
	assertString(function_sig);
	ISSTRING(writestr)

	//"w" means read
	std::FILE* cmd = popen(this->to_cmd_string().c_str(), "w");
	// return a code to indicate program failure. but can this ever happen?
	if (cmd == nullptr)
		return 1;

	// decided not to convert slashes here .. may be the wrong decision
	fputs(writestr.toString().c_str(), cmd);

	// return the process termination status
	return !pclose(cmd);
}

void var::osflush() const {
	std::cout << std::flush;
	return;
}

bool var::osopen() const {

	THISIS("bool var::osopen()")
	assertString(function_sig);

	// if reopening an osfile that is already opened then close and reopen
	// dont call if not necessary
	if (THIS_IS_OSFILE())
		osclose();

	return this->osopenx(*this, "") != 0;
}

bool var::osopen(CVR osfilename, CVR locale) const {

	THISIS("bool var::osopen(CVR osfilename, CVR locale)")
	assertDefined(function_sig);
	ISSTRING(osfilename)

	// if reopening an osfile that is already opened then close and reopen
	// dont call if not necessary
	if (THIS_IS_OSFILE())
		osclose();

	return this->osopenx(osfilename, locale) != 0;
}

static void del_fstream(void* handle) {
	delete (std::fstream*)handle;
}

std::fstream* var::osopenx(CVR osfilename, CVR locale) const {

	// IDENTICAL code in osbread and osbwrite
	// Try to get the cached file handle. the usual case is that you osopen a file before doing
	// osbwrite/osbread Using fstream instead of ofstream so that we can mix reads and writes on
	// the same filehandle
	std::fstream* pmyfile = 0;
	if (THIS_IS_OSFILE()) {
		pmyfile =
			(std::fstream*)mv_handles_cache.get_handle((int)var_int, var_str);
		if (pmyfile == 0)  // nonvalid handle
		{
			var_int = 0;
			//			var_typ ^= VARTYP_OSFILE;	// clear bit
			var_typ ^= VARTYP_OSFILE | VARTYP_INT;	// only STR bit should remains
		}
	}

	// if the file has NOT already been opened then open it now with the current default locale
	// and add it to the cache. but normally the filehandle will have been previously opened
	// with osopen and perhaps a specific locale.
	if (pmyfile == 0) {

		// delay checking until necessary
		THISIS("bool var::osopenx(CVR osfilename, CVR locale)")
		ISSTRING(osfilename)
		ISSTRING(locale)

		pmyfile = new std::fstream;

		pmyfile->imbue(get_locale(locale));

		// open the file for i/o (fail if the file doesnt exist and do NOT delete any
		// existing file) binary and in/out to allow reading and writing from same file
		// handle
		pmyfile->open(osfilename.to_path_string().c_str(),
					  std::ios::out | std::ios::in | std::ios::binary);
		if (!(*pmyfile)) {
			//delete pmyfile;

			//try to open read-only
			pmyfile->open(osfilename.to_path_string().c_str(),
						  std::ios::in | std::ios::binary);
			if (!(*pmyfile)) {
				delete pmyfile;
				return 0;
			}
		}

		// cache the file handle (we use the int to store the "file number"
		// and NAN to prevent isnum trashing mvint in the possible case that the osfilename
		// is an integer can addhandle fail?
		var_int =
			mv_handles_cache.add_handle(pmyfile, del_fstream, osfilename.var_str);
		var_typ = VARTYP_NANSTR_OSFILE;
		var_str = osfilename.var_str;
	}

	return pmyfile;
}

// on unix or with iconv we could convert to or any character format
// for sequential output we could use popen to progressively read and write with no seek facility
// popen("iconv -f utf-16 -t utf-8 >targetfile","w",filehandle) followed by progressive writes
// popen("iconv -f utf-32 -t utf-16 <sourcefile","r",filehandle) followed by progressive reads
// use utf-16 or utf-32 depending on windows or unix

/* typical code pages on Linux (see "iconv -l")
ISO-8859-2
ISO-8859-3
ISO-8859-5
ISO-8859-6
ISO-8859-7
ISO-8859-8
ISO-8859-9
EUC-JP
EUC-KR
ISO-8859-13
ISO-8859-15
GBK
GB18030
UTF-8
GB2312
BIG5
KOI8-R
KOI8-U
CP1251
TIS-620
WINDOWS-31J
WINDOWS-936
WINDOWS-1250
WINDOWS-1251
WINDOWS-1252
WINDOWS-1253
WINDOWS-1254
WINDOWS-1255
WINDOWS-1256
WINDOWS-1257
WINDOWS-1258
*/

// no binary conversion is performed on input unless
// codepage is provided then exodus converts from the
// specified codepage (not locale) on input to utf-8 internally
bool var::osread(CVR osfilename, CVR codepage) {

	THISIS("bool var::osread(CVR osfilename, CVR codepage")
	ISSTRING(osfilename)
	return osread(osfilename.to_path_string().c_str(), codepage);
}

bool var::osread(const char* osfilename, CVR codepage) {


	THISIS("bool var::osread(const char* osfilename, CVR codepage")
	assertDefined(function_sig);

	// osread returns empty string in any case
	var_str.clear();
	var_typ = VARTYP_STR;

	// get a file structure
	std::ifstream myfile;

	// open in binary (and position "at end" to find the file size with tellg)
	myfile.open(osfilename, std::ios::binary | std::ios::in | std::ios::ate);
	if (!myfile) {
		this->lasterror(var(osfilename) ^ " cannot be osread (1)");
		return false;
	}

	// determine the file size since we are going to read it all
	// NB tellg and seekp goes by bytes regardless of normal/wide stream
	// max file size 4GB?
	unsigned int bytesize;
	//	#pragma warning( disable: 4244 )
	// warning C4244: '=' : conversion from 'std::streamoff' to 'unsigned int', possible loss of
	// data
	bytesize = (unsigned int)myfile.tellg();

	// if empty file then done ok
	if (bytesize == 0) {
		myfile.close();
		return true;
	}

	// reserve memory - now reading directly into var_str
	// get file size * wchar memory to load the file or fail
	// boost::scoped_array<char> memblock(new char [bytesize]);
	// std::unique_ptr<char[]> memblock(new char[bytesize]);
	// if (memblock==0)
	try {
		// emergency memory - will be deleted at } - useful if OOM
		std::unique_ptr<char[]> emergencymemory(new char[16384]);
		//std::unique_ptr emergencymemory(new char[16384]);

		// resize the string to receive the whole file
		var_str.resize(bytesize);
	} catch (std::bad_alloc& ex) {
		throw MVOutOfMemory("Could not obtain " ^ var(int(bytesize * sizeof(char))) ^
							" bytes of memory to read " ^ var(osfilename));
		// myfile.close();
		// return false;
	}

	// read the file into the reserved memory block
	myfile.seekg(0, std::ios::beg);
	// myfile.read (memblock.get(), (unsigned int) bytesize);
	//myfile.read(&var_str[0], (unsigned int)bytesize);
	//c++17 provides non-const access to data() :)
	myfile.read(var_str.data(), (unsigned int)bytesize);

	bool failed = myfile.fail();

	// in case we didnt read the whole file for some reason, remove garbage in the end of the
	// string 	#pragma warning( disable: 4244 ) warning C4244: '=' : conversion from
	// 'std::streamoff' to 'unsigned int', possible loss of data
	bytesize = (unsigned int)myfile.gcount();
	var_str.resize(bytesize);
	myfile.close();

	// failure can indicate that we didnt get as many characters as requested
	if (failed && !bytesize) {
		this->lasterror(var(osfilename).quote() ^ " cannot be osread (2)");
		return false;
	}

	// ALN:JFI: actually we could use std::string 'tempstr' in place of 'memblock' by hacking
	//	.data member and reserve() or resize(), thus avoiding buffer-to-buffer-copying
	// var_str=std::string(memblock.get(), (unsigned int) bytesize);
	// SJB Done 20190604

	if (codepage)
		// var_str=boost::locale::conv::to_utf<char>(var_str,"ISO-8859-5")};
		var_str = boost::locale::conv::to_utf<char>(var_str, codepage.var_str);

	return true;
}

var var::to_codepage(CVR codepage) const {

	THISIS(
		"bool var::to_codepage(CVR codepage="
		") const")
	assertString(function_sig);
	ISSTRING(codepage)

	//from utf8 to codepage
	return boost::locale::conv::from_utf<char>(var_str, codepage.var_str);
}

var var::from_codepage(CVR codepage) const {

	THISIS(
		"bool var::from_codepage(CVR codepage="
		") const")
	assertString(function_sig);
	ISSTRING(codepage)

	//to utf from codepage
	return boost::locale::conv::to_utf<char>(var_str, codepage.var_str);
}

// no binary conversion is performed on output unless
// codepage is provided (not locale) then exodus assumes internally
// utf-8 and converts all output to the specified codepage
bool var::oswrite(CVR osfilename, CVR codepage) const {

	THISIS(
		"bool var::oswrite(CVR osfilename, CVR codepage="
		") const")
	assertString(function_sig);
	ISSTRING(osfilename)

	// get a file structure
	std::ofstream myfile;

	// delete any previous file,
	myfile.open(osfilename.to_path_string().c_str(),
				std::ios::trunc | std::ios::out | std::ios::binary);
	if (!myfile) {
		this->lasterror(osfilename.quote() ^ " osopen failed.");
		return false;
	}

	// write out the full string or fail
	if (codepage) {
		std::string tempstr = boost::locale::conv::from_utf<char>(var_str, codepage.var_str);
		myfile.write(tempstr.data(), int(tempstr.length()));
	} else {
		myfile.write(var_str.data(), int(var_str.size()));
	}
	bool failed = myfile.fail();
	myfile.close();
	return !failed;
}

#ifdef VAR_OSBREADWRITE_CONST_OFFSET
// a version that accepts a const offset ie ignores return value
//bool var::osbwrite(CVR osfilevar, CVR offset, const bool adjust) const
bool var::osbwrite(CVR osfilevar, CVR offset) const {
	return this->osbwrite(osfilevar, const_cast<VARREF>(offset));
}
#endif

//NOTE: unlike osread/oswrite which rely on iconv codepages to do any conversion
//osbread and osbwrite rely on the locale being passed in on the osopen stage

//bool var::osbwrite(CVR osfilevar, VARREF offset, const bool adjust) const
bool var::osbwrite(CVR osfilevar, VARREF offset) const {
	// osfilehandle is just the filename but buffers the "file number" in the mvint too


	THISIS(
		"bool var::osbwrite(CVR osfilevar, VARREF offset) "
		"const")
	assertString(function_sig);
	ISNUMERIC(offset)

	// test the following only if necessary in osopenx
	// ISSTRING(osfilename)

	// get the buffered file handle/open on the fly
	std::fstream* pmyfile = osfilevar.osopenx(osfilevar, "");
	if (pmyfile == 0) {
		this->lasterror(osfilevar.quote() ^ " osbwrite failed (1)");
		return false;
	}

	// std::cout << pmyfile->getloc().name();

	// NB seekp goes by bytes regardless of the fact that it is a wide stream
	// myfile.seekp (offset*sizeof(char));
	// offset should be in bytes except for fixed multibyte code pages like UTF16 and UTF32
	if (offset < 0)
		pmyfile->seekp(std::ios_base::end);
	else
		pmyfile->seekp(
			static_cast<long>(offset.toInt()));	 // avoid warning, see comments to seekg()

	// NB but write length goes by number of wide characters (not bytes)
	pmyfile->write(var_str.data(), int(var_str.size()));

	// on windows, fstream will try to convert to current locale codepage so
	// if you are trying to write an exodus string containing a GREEK CAPITAL GAMMA
	// unicode \x0393 and current codepage is *NOT* CP1253 (Greek)
	// then c++ wiofstream cannot convert \x0393 to a single byte (in CP1253)
	if (pmyfile->fail()) {
		// saved in cache, DO NOT CLOSE!
		// myfile.close();
		this->lasterror(osfilevar.quote() ^ " osbwrite failed (2)");
		return false;
	}

	// pass back the file pointer offset
	offset = (int)pmyfile->tellp();

	// although slow, ensure immediate visibility of osbwrites
	pmyfile->flush();

	// saved in cache, DO NOT CLOSE!
	// myfile.close();

	return true;
}

#ifdef VAR_OSBREADWRITE_CONST_OFFSET
// a version that ignores output of offset
//VARREF var::osbread(CVR osfilevar, CVR offset, const int bytesize,
//		  const bool adjust)
bool var::osbread(CVR osfilevar, CVR offset, const int bytesize) {
	// var offset_nonconst;
	// if (offset.assigned())
	//	offset_nonconst=offset;
	return this->osbread(osfilevar, const_cast<VARREF>(offset), bytesize);
}
#endif

ssize_t count_excess_UTF8_bytes(std::string& str) {

	// Scans backward from the end of string.
	const char* cptr = &str.back();
	int num = 1;
	int numBytesToTruncate = 0;

	for (int i = 0; 6 > i; ++i) {
		numBytesToTruncate += 1;
		if ((*cptr & 0x80) == 0x80) {
			// If char bit starts with 1xxxxxxx
			// It's a part of unicode character!
			// Find the first byte in the unicode character!

			// if ((*cptr & 0xFC) == 0xFC) { if (num == 6) { return 0; } break; }
			// if ((*cptr & 0xF8) == 0xF8) { if (num == 5) { return 0; } break; }

			// If char binary is 11110000, it means it's a 4 bytes long unicode.
			if ((*cptr & 0xF0) == 0xF0) {
				if (num == 4) {
					return 0;
				}
				break;
			}

			// If char binary is 11100000, it means it's a 3 bytes long unicode.
			if ((*cptr & 0xE0) == 0xE0) {
				if (num == 3) {
					return 0;
				}
				break;
			}

			if ((*cptr & 0xC0) == 0xC0) {
				if (num == 2) {
					return 0;
				}
				break;
			}

			num += 1;
		} else {
			// If char bit does not start with 1, nothing to truncate!
			return 0;
		}

		cptr -= 1;

	}  // next char

	return numBytesToTruncate;
}

//NOTE if the locale is not C then any partial non-utf-8 bytes at the end (due to bytesize
//not being an exact number of valid utf-8 code units) are trimmed off the return value
//The new offset is changed to reflect the above and is simply increased by bytesize

//VARREF var::osbread(CVR osfilevar, VARREF offset, const int bytesize, const bool adjust)
bool var::osbread(CVR osfilevar, VARREF offset, const int bytesize) {

	THISIS("bool var::osbread(CVR osfilevar, VARREF offset, const int bytesize")
	assertDefined(function_sig);
	ISNUMERIC(offset)

	// will be done if necessary in osopenx()
	// ISSTRING(osfilename)

	// default is to return empty string in any case
	var_str.clear();
	var_typ = VARTYP_STR;

	// strange case request to read 0 bytes
	if (bytesize <= 0)
		return true;

	// get the buffered file handle/open on the fly
	std::fstream* pmyfile = osfilevar.osopenx(osfilevar, "");
	if (pmyfile == 0) {
		this->lasterror(osfilevar.quote() ^ " osopen failed.");
		return false;
	}

	/*
		//NB all file sizes are in bytes NOT characters despite this being a wide character
	fstream
		// Position get pointer at the end of file, as expected it to be if we open file
	anew pmyfile->seekg(0, std::ios::end); unsigned int maxsize = pmyfile->tellg();

	var(int(maxsize)).outputl("maxsize=");
		//return "" if start reading past end of file
		if ((unsigned long)(int)offset>=maxsize)	// past EOF
			return *this;

	*/
	// seek to the offset
	// if (pmyfile->tellg() != static_cast<long> (offset.var_int))
	{
		if (pmyfile->fail())
			pmyfile->clear();
		// pmyfile->seekg (static_cast<long> (offset.var_int), std::ios::beg);	//
		// 'std::streampos' usually 'long' seekg always seems to result in tellg being -1 in
		// linux (Ubunut 10.04 64bit)
		pmyfile->rdbuf()->pubseekpos(static_cast<long>(offset.var_int));
	}
	// var((int) pmyfile->tellg()).outputl("2 tellg=");

	// get a memory block to read into
	// boost::scoped_array<char> memblock(new char [bytesize]);
	std::unique_ptr<char[]> memblock(new char[bytesize]);
	//std::unique_ptr memblock(new char[bytesize]);
	if (memblock == 0)
		throw MVOutOfMemory("Could not obtain " ^ var(int(bytesize * sizeof(char))) ^
							" bytes of memory to read " ^ osfilevar);
	// return *this;

	// read the data (converting characters on the fly)
	pmyfile->read(memblock.get(), bytesize);

	// bool failed = pmyfile.fail();
	if (pmyfile->fail()) {
		pmyfile->clear();
		pmyfile->seekg(0, std::ios::end);
	}

	// update the offset function argument
	// if (readsize > 0)
	offset = (int)pmyfile->tellg();

	// transfer the memory block to this variable's string
	//(is is possible to read directly into string data() avoiding a memory copy?
	// get the number of CHARACTERS read - utf8 bytes (except ASCII) convert to fewer wchar
	// characters. int readsize = pmyfile->gcount(); 	#pragma warning( disable: 4244 )
	// warning C4244: '=' : conversion from 'std::streamoff' to 'unsigned int', possible loss of
	// data
	var_str.assign(memblock.get(), (unsigned int)pmyfile->gcount());

	// trim off any excess utf8 bytes if utf8
	//	var(pmyfile->getloc().name()).outputl(L"loc name=");;
	if (pmyfile->getloc().name() != "C") {
		int nextrabytes = count_excess_UTF8_bytes(var_str);
		if (nextrabytes) {
			offset -= nextrabytes;
			var_str.resize(var_str.size() - nextrabytes);
		}
	}

	return true;
}

void var::osclose() const {
	// THISIS("void var::osclose() const")
	// assertString(function_sig);
	if (THIS_IS_OSFILE()) {
		mv_handles_cache.del_handle((int)var_int);
		var_int = 0L;
		var_typ ^= VARTYP_OSFILE | VARTYP_INT;	// only STR bit should remains
	}
	// in all other cases, the files should be closed.
}

bool var::osrename(CVR newosdir_or_filename) const {

	THISIS("bool var::osrename(CVR newosdir_or_filename) const")
	assertString(function_sig);
	ISSTRING(newosdir_or_filename)

	// prevent overwrite of existing file
	// ACQUIRE
	std::ifstream myfile;
	// binary?
	myfile.open(newosdir_or_filename.to_path_string().c_str(), std::ios::binary);
	if (myfile) {
		// RELEASE
		myfile.close();
		this->lasterror(newosdir_or_filename.quote() ^ " osrename failed to open new file.");
		return false;
	}

	// safety
	if (!checknotabsoluterootfolder(toString()))
		return false;

	return !std::rename(this->to_path_string().c_str(),
						newosdir_or_filename.to_path_string().c_str());
}

bool var::oscopy(CVR to_osfile_or_dirname) const {

	THISIS("bool var::oscopy(CVR to_osfile_or_dirname) const")
	assertString(function_sig);
	ISSTRING(to_osfile_or_dirname)

	std::filesystem::path frompathx(this->to_path_string().c_str());
	std::filesystem::path topathx(to_osfile_or_dirname.to_path_string().c_str());
	/*
	try
	{
		// will not overwrite so this is redundant
		// option to overwrite is not in initial versions of boost copy_file
		if (std::filesystem::exists(topathx))
			return false;

		std::filesystem::copy_file(frompathx, topathx);
	}
	catch (...)
	{
		return false;
	}
	*/
	//https://en.cppreference.com/w/cpp/filesystem/copy
	std::error_code error_code;
	copy(frompathx, topathx,
		 std::filesystem::copy_options::overwrite_existing | std::filesystem::copy_options::recursive | std::filesystem::copy_options::copy_symlinks,
		 error_code);

	return !error_code;
}

bool var::osmove(CVR newosdir_or_filename) const {

	THISIS("bool var::osmove(CVR newosdir_or_filename) const")
	assertString(function_sig);
	ISSTRING(newosdir_or_filename)

	// prevent overwrite of existing file
	// ACQUIRE
	std::ifstream myfile;
	// binary?
	myfile.open(newosdir_or_filename.to_path_string().c_str(), std::ios::binary);
	if (myfile) {
		// RELEASE
		myfile.close();
		this->lasterror(newosdir_or_filename.quote() ^ " already exists. Cannot osmove " ^ this->quote());
		return false;
	}

	// safety
	if (!checknotabsoluterootfolder(this->toString()))
		return false;

	// try to rename but will fail to move across file systems
	if (!this->osrename(newosdir_or_filename)) {

		//try to copy and delete instead
		if (!this->oscopy(newosdir_or_filename)) {
			this->lasterror(this->quote() ^ " failed to oscopy (1) to " ^ newosdir_or_filename.quote());
			return false;
		}

		//then try to delete original
		if (this->osdelete())
			return true;
		else {

			//otherwise delete the target too
			newosdir_or_filename.osdelete();
			this->lasterror(this->quote() ^ " failed to oscopy (2) to " ^ newosdir_or_filename.quote());
			return false;
		}

	}
	return true;
}

bool var::osdelete() const {
	return osdelete(*this);
}

bool var::osdelete(CVR osfilename) const {

	THISIS("bool var::osdelete(CVR osfilename) const")
	assertDefined(function_sig);
	ISSTRING(osfilename)
	osfilename.osclose();  // in case this is cached opened file handle
	if (std::remove(osfilename.to_path_string().c_str())) {
		this->lasterror(this->quote() ^ " failed to osdelete");
		return false;
	}
	return true;
}

const std::string var::to_path_string() const {
	var part2 = this->field(" ", 2, 999999);
	if (part2.length()) {

#if defined WIN32 || defined _WIN32
		var part = this->field(" ", 1).convert("/", OSSLASH);
#else
		// printf("path=%s\n",this->convert("\\",OSSLASH).toString().c_str());
		var part = this->field(" ", 1).convert("\\", OSSLASH);

		// very similar code below
		// standardise on ALL AND ONLY lower case os file and path names
		// in order to allow uppercase, will have to find and remove all uppercase in the
		// old source code
		/*
		var lcpart = part.lcase();
		if (lcpart != part && part.substr(-3,3) != ".LK" && part.substr(-3,3) != ".OV" && part.substr(-9) != "/DATA.CFG" && !part.index("./data/"))
		{
			part.errputl("WARNING - UPPERCASE OS=");
//			part = lcpart;
		}
		*/
#endif
		return part.toString() + " " + part2.toString();
	} else {

#if defined WIN32 || defined _WIN32
		return this->convert("/", OSSLASH).toString();
#else
		// very similar code above
		// standardise on ALL AND ONLY lower case os file and path names
		// in order to allow uppercase, will have to find and remove all uppercase in the
		// old source code
		/*
		var lcthis = this->lcase();
		if (lcthis != (*this) && this->substr(-3,3) != ".LK" && this->substr(-3,3) != ".OV" && this->substr(-9) != "/DATA.CFG")
		{
			(*this).errputl("WARNING - UPPERCASE OS=");
//			return lcthis.convert("\\", OSSLASH).toString();
		}
		*/
		return this->convert("\\", OSSLASH).toString();
#endif
	}
}

const std::string var::to_cmd_string() const {

	// while converting from DOS convert all backslashes in first word to forward slashes on
	// linux or leave as if exodus on windows

	// warning if any backslashes unless at least one of them is followed by $ which indicates
	// valid usage as an escape character aiming to recode all old windows-only code
	if (this->index("\\") && !this->index("\\$"))
		this->errputl("WARNING BACKSLASHES IN OS COMMAND:");

	return this->field(" ", 1).to_path_string() + " " + this->field(" ", 2, 999999).toString();
}

//convert some clock to time_t (for osfile() and osdir()
template <typename TP>
std::time_t to_time_t(TP tp) {
	using namespace std::chrono;
	auto sctp = time_point_cast<system_clock::duration>(tp - TP::clock::now() + system_clock::now());
	return system_clock::to_time_t(sctp);
}

// in MVdatetime.cpp
//void ptime2mvdatetime(const boost::posix_time::ptime& ptimex, int& mvdate, int& mvtime);
void ptime2mvdatetime(const boost::posix_time::ptime& ptimex, int& mvdate, int& mvtime);
//WARNING ACTUALLY DEFINED WITH BOOST POSIX TIME BUT IT IS THE SAME
//void ptime2mvdatetime(const std::filesystem::file_time_type& ptimex, int& mvdate, int& mvtime);

var var::osfile() const {

	THISIS("var var::osfile() const")
	assertString(function_sig);

	// get a handle and return "" if doesnt exist or isnt a regular file
	try {
		// boost 1.33 throws an error with files containing ~ or $ chars but 1.38 doesnt
		std::filesystem::path pathx(this->to_path_string().c_str());

		if (!std::filesystem::exists(pathx))
			return "";
		// is_regular is only in boost > 1.34
		// if (! std::filesystem::is_regular(pathx)) return "";
		if (std::filesystem::is_directory(pathx))
			return "";

		//not using low level interface
		//https://stackoverflow.com/questions/21159047/get-the-creation-date-of-file-or-folder-in-c#21159305

		// SIMILAR CODE IN OSFILE AND OSDIR

		// get last write datetime
		//std::time_t last_write_time = std::chrono::system_clock::to_time_t(std::filesystem::last_write_time(pathx));
		std::filesystem::file_time_type file_time = std::filesystem::last_write_time(pathx);
		std::time_t last_write_time = to_time_t(file_time);

		// convert to posix time
		boost::posix_time::ptime ptimex = boost::posix_time::from_time_t(last_write_time);
		//std::filesystem::file_time_type ptimex = std::filesystem::last_write_time(pathx);

		// convert posix time to mv date and time
		//TODO Currently returns local date/time. Should it not be UTC?
		int mvdate, mvtime;
		ptime2mvdatetime(ptimex, mvdate, mvtime);

		//file_size() is only available for files not directories
		return int(std::filesystem::file_size(pathx)) ^ FM ^ mvdate ^ FM ^ int(mvtime);
	} catch (...) {
		return "";
	};
}

bool var::osmkdir() const {

	THISIS("bool var::osmkdir() const")
	assertString(function_sig);

	try {

		std::filesystem::path pathx(this->to_path_string().c_str());

		if (std::filesystem::exists(pathx))	{
			this->lasterror(this->quote() ^ " osmkdir failed - already exists.");
			return false;
		}

		std::filesystem::create_directories(pathx);
	} catch (...) {
		this->lasterror(this->quote() ^ " osmkdir failed.");
		return false;
	}
	return true;
}

bool var::osrmdir(bool evenifnotempty) const {

	THISIS("bool var::osrmdir(bool evenifnotempty) const")
	assertString(function_sig);

	// get a handle and return "" if doesnt exist or is NOT a directory
	try {

		// boost 1.33 throws an error with files containing ~ or $ chars but 1.38 doesnt
		std::filesystem::path pathx(this->to_path_string().c_str());

		if (!std::filesystem::exists(pathx)) {
		this->lasterror(this->quote() ^ " osrmdir failed - does not exist.");
			return false;
		}

		if (!std::filesystem::is_directory(pathx)) {
			this->lasterror(this->quote() ^ " osrmdir failed - is not a directory.");
			return false;
		}

		if (evenifnotempty) {

			// safety .. simply REFUSE to rm top level folders if not empty
			// find some other way e.g. shell command if you must
			if (!checknotabsoluterootfolder(toString()))
				return false;

			std::filesystem::remove_all(pathx);
		} else
			std::filesystem::remove(pathx);
	} catch (...) {
		this->lasterror(this->quote() ^ " osrmdir failed - unknown cause.");
		return false;
	}

	return true;
}

var var::osdir() const {

	THISIS("var var::osdir() const")
	assertString(function_sig);

	// get a handle and return "" if doesnt exist or is NOT a directory
	// std::filesystem::wpath pathx(toTstring((*this)).c_str());
	try {

		// boost 1.33 throws an error with files containing ~ or $ chars but 1.38 doesnt
		std::filesystem::path pathx(this->to_path_string().c_str());

		if (!std::filesystem::exists(pathx))
			return "";
		if (!std::filesystem::is_directory(pathx))
			return "";

		//not using low level interface
		//https://stackoverflow.com/questions/21159047/get-the-creation-date-of-file-or-folder-in-c#21159305

		// SIMILAR CODE IN OSFILE AND OSDIR

		// get last write datetime
		//std::time_t last_write_time = std::chrono::system_clock::to_time_t(std::filesystem::last_write_time(pathx));
		std::filesystem::file_time_type file_time = std::filesystem::last_write_time(pathx);
		std::time_t last_write_time = to_time_t(file_time);

		// convert to posix time
		boost::posix_time::ptime ptimex = boost::posix_time::from_time_t(last_write_time);
		//std::filesystem::file_time_type ptimex = std::filesystem::last_write_time(pathx);

		// convert posix time to mv date and time
		int mvdate, mvtime;
		ptime2mvdatetime(ptimex, mvdate, mvtime);

		//file_size() is only available for files not directories
		//return int(std::filesystem::file_size(pathx)) ^ FM ^ mvdate ^ FM ^ int(mvtime);
		return FM ^ mvdate ^ FM ^ int(mvtime);

	} catch (...) {
		return "";
	};
}

var var::oslistf(CVR path, CVR spec) const {
	return this->oslist(path, spec, 1);
}

var var::oslistd(CVR path, CVR spec) const {
	return this->oslist(path, spec, 2);
}

/*
*@param	path	A directory or blank for current working directory. If spec if empty then the last part (after any slashes) is used as spec.
*@param	spec	Optional glob like "*",  "*.*", "*.???" etc. (CASE INSENSITIVE)
*@param	mode	1=files only, 2=directories only, otherwise both.
*@returns		List of directory and/or filenames depending on mode. fm separator
*/
var var::oslist(CVR path0, CVR spec0, const int mode) const {

	THISIS("var var::oslist(CVR path, CVR spec, const int mode) const")
	assertDefined(function_sig);
	ISSTRING(path0)
	ISSTRING(spec0)

	// returns an fm separated list of files and/or folders

	// http://www.boost.org/libs/filesystem/example/simple_ls.cpp

	var path = path0.to_path_string();
	var spec;
	if (spec0.length()) {
		spec = spec0;
	}
	// file globbing can and must be passed as tail end of path
	// perhaps could use <glob.h> in linux instead of regex
	else {
		spec = path.field2(OSSLASH, -1);
		path = path.substr(1, path.length() - spec.length());

//		// escape all the regex special characters that are found in the strint
//		// except the * ? which are glob special characters
//		// regex concept here is ([specialchars]) replace with \$1 where $1 will be any of
//		// the special chars note inside brackets, only  ^ - and ] are special characters
//		// inside [] char alternative to include a ] in the list of characters inside [] it
//		// must be the first character like []otherchars] of course all back slashes must be
//		// doubled up in c++ source code
//		spec.replacer("([][\\\\(){}|^$.+])", "\\\\$1");
//
//		// glob * becomes .* in regex matching any number of any characters
//		spec.swapper("*", ".*");
//
//		// glob ? becomes . in regex matching any one character
//		spec.swapper("?", ".");
	}

//	bool filter = false;
//	// std_boost::reregex re;
//	std::regex re;
//	if (spec) {
//		try {
//			// Set up the regular expression for case-insensitivity
//			// re.assign(toTstring(spec).c_str(), std_boost::regex_constants::icase);
//			re.assign(spec.toString().c_str(), std::regex_constants::icase);
//			filter = true;
//		} catch (std::regex_error& e) {
//			std::cout << spec.var_str << " is not a valid regular expression: \""
//					  << e.what() << "\"" << std::endl;
//			return "";
//		}
//	}

	bool getfiles = true;
	bool getfolders = true;
	if (mode == 1)
		getfolders = false;
	else if (mode == 2)
		getfiles = false;

	var filelist = "";
	//#if BOOST_FILESYSTEM_VERSION >= 3 or defined(C17)
	//#define LEAForFILENAME path().filename().string()
	//#define COMMAstd::filesystemNATIVE
	//#else
	//#define LEAForFILENAME leaf()
	//#define COMMAstd::filesystemNATIVE ,std::filesystem::native
	//#endif
	// get handle to folder
	// wpath or path before boost 1.34
	// std::filesystem::wpath full_path(std::filesystem::initial_path<std::filesystem::wpath>());
	// full_path = std::filesystem::system_complete(std::filesystem::wpath(toTstring(path), std::filesystem::native ));
	// std::filesystem::path full_path(std::filesystem::initial_path());
	// initial_path always return the cwd at the time it is first called which is almost useless
	std::filesystem::path full_path(std::filesystem::current_path());
	//full_path =
	//    std::filesystem::system_complete(std::filesystem::path(path.to_path_string().c_str() COMMAstd::filesystemNATIVE));
	//full_path =
	//    std::filesystem::absolute(std::filesystem::path(path.to_path_string().c_str()));
	if (path.length()) {
		//full_path = std::filesystem::absolute(std::filesystem::path(path.to_path_string().c_str()));
		std::error_code error_code;
		std::filesystem::path pathx = std::filesystem::path(path.to_path_string().c_str());
		full_path = std::filesystem::absolute(pathx, error_code);
		if (error_code) {
			std::cerr << "'" << path.to_path_string() << "' path : " << error_code.message() << std::endl;
			return filelist;
		}

	}
	if (spec == "*")
		spec = "";

	//std::clog << "fullpath='" << full_path << "'" <<std::endl;
	// quit if it isnt a folder
	if (!std::filesystem::is_directory(full_path))
		return filelist;

	// errno=0;
	// std::filesystem::wdirectory_iterator end_iter;
	// for (std::filesystem::wdirectory_iterator dir_itr(full_path );
	std::filesystem::directory_iterator end_iter;
	for (std::filesystem::directory_iterator dir_itr(full_path); dir_itr != end_iter; ++dir_itr) {
		try {

			// skip unwanted items
			// dir_itr->path().leaf()  changed to dir_itr->leaf() in three places
			// also is_directory(dir_itr->status()) changed to is_directory(*dir_itr)
			// to avoid compile errors on boost 1.33
			// http://www.boost.org/doc/libs/1_33_1/libs/filesystem/doc/index.htm
			//if (filter && !std::regex_match(dir_itr->path().filename().string(), re))
			//TRACE(dir_itr->path().filename().string())
			//TRACE(spec)
			//if (spec && !var(dir_itr->path().filename().string()).match(spec, "w"))
			//TRACE(dir_itr->path().filename().string())
			//TRACE(spec)
			if (spec && fnmatch(spec.var_str.c_str(), dir_itr->path().filename().c_str(), 0) != 0)
				continue;

			// work/efficiently if (std::filesystem::is_directory(dir_itr->status() ) )
			if (std::filesystem::is_directory(*dir_itr)) {
				if (getfolders)
					filelist ^= dir_itr->path().filename().string() ^ FM;
			}
			// is_regular is only in boost > 1.34
			// else if (std::filesystem::is_regular(dir_itr->status() ) )
			else  // if (std::filesystem::is_regular(dir_itr->status() ) )
			{
				if (getfiles)
					filelist ^= dir_itr->path().filename().string() ^ FM;
			}
			// else
			//{
			//  //++other_count;
			//  //std::cout << dir_itr->path().leaf() << " [other]\n";
			//}
		} catch (const std::exception& ex) {
			// evade warning: unused variable
			if (false)
				if (ex.what()) {
				};

			//++err_count;
			// std::cout << dir_itr->path().leaf() << " " << ex.what() << std::endl;
		}
	}

	// delete last separator
	filelist.popper();

	return filelist;
}

bool var::oscwd(const char* newpath) const {

	THISIS("var var::oscwd(const char* newpath) const")
	// doesnt use *this - should syntax be changed to setcwd? and getcwd()?
	assertDefined(function_sig);	 // not needed if *this not used
	//ISSTRING(newpath)

	// http://www.boost.org/doc/libs/1_38_0/libs/filesystem/doc/reference.html#Attribute-functions
	// wont compile on boost 1.33 so comment it out and make non-functional
	// until we have a reliable way to detect boost version
	// boost::filesystem::current_path(newpath.toString());

	// return oscwd();

	try {
		//std::filesystem::current_path(newpath.toString());
		std::filesystem::current_path(newpath);
	} catch (...) {
		// filesystem error: cannot set current path: No such file or directory
		// ignore all errors
		this->lasterror(var(newpath).quote() ^ " oscwd failed - unknown cause.");
		return false;
	}

	return true;
}

var var::oscwd() const {

	THISIS("var var::oscwd() const")
	// doesnt use *this - should syntax be changed to ossetcwd? and osgetcwd()?
	assertDefined(function_sig);	 // not needed if *this not used

	// TODO consider using complete() or system_complete()
	//"[Note: When portable behavior is required, use complete(). When operating system
	// dependent behavior is required, use system_complete()."

	// http://www.boost.org/doc/libs/1_38_0/libs/filesystem/doc/reference.html#Attribute-functions
	// std::string currentpath=boost::filesystem::current_path().string();
	std::string currentpath = std::filesystem::current_path().string();

	return var(currentpath).convert("/", OSSLASH);
}

}  // namespace exodus
