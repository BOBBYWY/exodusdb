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

/* UTF-8 bytewise encoding
Binary    Hex          Comments
0xxxxxxx  0x00..0x7F   Only byte of a 1-byte character encoding
10xxxxxx  0x80..0xBF   Continuation bytes (1-3 continuation bytes)
110xxxxx  0xC0..0xDF   First byte of a 2-byte character encoding
1110xxxx  0xE0..0xEF   First byte of a 3-byte character encoding
11110xxx  0xF0..0xF4   First byte of a 4-byte character encoding
*/

#include <mutex> //for lock_guard

#if __has_include(<signal.h>)
#include <signal.h>	 //for raise(SIGTRAP)
#endif

/* using fastfloat instead of ryu because it is > 3 time faster
	but it doesnt prevent excessively long decimal ASCII input
	and unconfirmed if it does supports round trip ascii->double->ascii
	//from test_perf.cpp
    var v("1234.5678");  //23ns
    //v.isnum();         //+ 56ns ( 79ns using fastfloat)
    //v.isnum();         //+184ns (207ns using ryu)
    //v.isnum();         //+194ns (217ns using std::stod)
    //v.isnum();         //+???ns (???ns using <charconv> from_chars)
*/

//fastfloat  "1234.5678" -> 1234.5678  56ns (but does it round trip?)
//ryu        "1234.5678" -> 1234.5678 184ns
//std::stod  "1234.5678" -> 1234.5678 192ns (but does it round trip?)
//from_chars "1234.5678" -> 1234.5678 ???ns (but does it round trip?)

//Eisel-Lemire algorithm seems to be fastest "parse chars to double" according to Lavavej at Microsoft
//Feb 2021 https://reviews.llvm.org/D70631 "There have been a couple of algorithmic developments very recently
// - for parsing (from_chars), the Eisel-Lemire algorithm is much faster than the technique used here.
// I would have used it if it were available back then!"
//
//double only available in gcc 11 onwards. msvc has it from 2017 or 19
//https://github.com/fastfloat/fast_float
#if __has_include(<fast_float/fast_float.h>)
#define HAS_FASTFLOAT
#include <fast_float/fast_float.h>
#define STD_OR_FASTFLOAT fast_float

#elif __GNUC__ >= 11
#define USE_CHARCONV
#define STD_OR_FASTFLOAT std

//#elif __has_include(<ryu/ryu.h>)
//#define HAS_RYU
//#include <ryu/ryu.h>
#endif

//gcc 10 doesnt include conv from and to floating point
#include <charconv>	 // for from_chars and to_chars

//#include <cmath>      //for stod()
//#include <sstream>
//#include <iomanip>    //for setprecision

//#include <cmath>    //for abs(double)
//#include <cstdlib>  //for exit
//#include <iostream> //cin and cout
//#include <memory>   //for unique_ptr

#include <boost/utility/string_view.hpp>

#include <exodus/mv.h>
//#include <exodus/mvutf.h>
#include <exodus/mvlocale.h>

// Keep doxygen happy
#undef DEFAULT_SPACE
#define DEFAULT_SPACE

// std::ios::sync_with_stdio(false);
bool desynced_with_stdio = false;

// TODO check that all string increase operations dont crash the system

namespace exodus {

static std::mutex global_mutex_threadstream;
#define LOCKIOSTREAM std::lock_guard guard(global_mutex_threadstream);

int var::localeAwareCompare(const std::string& str1, const std::string& str2) {
	// https://www.boost.org/doc/libs/1_70_0/libs/locale/doc/html/collation.html
	// eg ensure lower case sorts before uppercase (despite "A" \x41 is less than "a" \x61)
	init_boost_locale1();

	// Level
	// 0 = primary – ignore accents and character case, comparing base letters only. For example "facade"
	// and "Façade" are the same.
	// 1 = secondary – ignore character case but consider accents. "facade" and
	// "façade" are different but "Façade" and "façade" are the same.
	// 3 = tertiary – consider both case and
	// accents: "Façade" and "façade" are different. Ignore punctuation.
	// 4 = quaternary – consider all case,
	// accents, and punctuation. The words must be identical in terms of Unicode representation.
	// 5 = identical – as quaternary, but compare code points as well.
	//#define COMP_LEVEL identical

#define COMP_LEVEL identical

//	boost::string_view str1b(str1.data(), str1.size());
//	boost::string_view str2b(str2.data(), str2.size());

	int result = std::use_facet<boost::locale::collator<char>>(tls_boost_locale1)
					 .compare(boost::locale::collator_base::COMP_LEVEL, str1, str2);

	//var(str1).outputl("str1=");
	//var(str2).outputl("str2=");
	//var(result).outputl("comp=");

	return result;
}

var var::version() const {
	return var(__DATE__).iconv("D").oconv("D") ^ " " ^ var(__TIME__);
}

bool var::eof() const {
	// THISIS("bool var::eof() const")
	// assertDefined(function_sig);

	return (std::cin.eof());
}

bool var::hasinput(int milliseconds) {
	//declare in haskey.cpp
	bool haskey(int milliseconds);

	LOCKIOSTREAM

	return haskey(milliseconds);
}

//binary safe version (except nl/eof?) with little or no editing except backspace and no default
VARREF var::input() {

	THISIS("bool var::input()")
	assertDefined(function_sig);

	var_str.clear();
	var_typ = VARTYP_STR;

	//LOCKIOSTREAM

	// pressing crtl+d indicates eof on unix or ctrl+Z on dos/windows?
	if (!std::cin.eof())
		std::getline(std::cin, var_str);

	return *this;
}

// input with prompt allows default value and editing
// but is not binary safe because we allow line editing when there is a prompt (even if empty)
VARREF var::input(CVR prompt) {

	THISIS("bool var::input(CVR prompt")
	assertDefined(function_sig);
	ISSTRING(prompt)

	//LOCKIOSTREAM

	var default_input = this->assigned() ? (*this) : "";

	var_str.clear();
	var_typ = VARTYP_STR;

	//output any prompt and flush
	if (prompt.length())
		prompt.output().osflush();

	//windows currently doesnt allow line editing
	if (SLASH_IS_BACKSLASH) {
		this->input();
	}

	//linux terminal input line editing
	else {
		//swap double quotes with \"
		default_input.swapper("\"", "\\\"");
		var cmd = "bash -c 'read -i " ^ default_input.quote() ^ " -e EXO_TEMP_READ && printf \"%s\" \"$EXO_TEMP_READ\"'";
		//cmd.outputl("cmd=");
		this->osshellread(cmd);
		if ((*this) == "")
			std::cout << std::endl;
	}

	return *this;
}

// for nchars, use int instead of var to trigger error at point of calling not here
// not binary safe if nchars = 0 because we allow line editing assuming terminal console
VARREF var::inputn(const int nchars) {

	THISIS("bool var::inputn(const int nchars")
	assertDefined(function_sig);

	//LOCKIOSTREAM

	var_str.clear();
	var_typ = VARTYP_STR;

	//declare function in getkey.cpp
	int getkey(void);

	//input whatever characters are available into this var a return true if more than none
	// quit if error or EOF
	if (nchars < 0) {

		for (;;) {
			int int1;
			{
				//LOCKIOSTREAM
				int1 = getkey();
			}

			//quit if no (more) characters available
			if (int1 < 0)
				break;

			//var_str += int1;
			var_str.push_back(int1);
		}
	}

	//input a certain number of characters input this var and return true if more than none
	else if (nchars > 0) {

		for (;;) {
			int int1;
			{
				//LOCKIOSTREAM
				int1 = getkey();
			}

			// try again after a short delay if no key and not enough characters yet
			// TODO implement as poll/epoll/select
			if (int1 < 0) {
				this->ossleep(100);
				continue;
			}

			// Enter/Return key always returns whatever has been entered so far
			//if (int1 < 0 || int1 == 0x0d)
			//	break;

			// add the character to the output
			//var_str += int1;
			var_str.push_back(int1);

			// quit if got the desired number of characters
			//nchars cannot be negative at this point
			if (var_str.size() >= uint(nchars))
				break;
		}

	} else {
		this->input();
	}

	return *this;
}

void var::stop(CVR text) const {

	THISIS("void var::stop(CVR text) const")
	ISSTRING(text)

	// exit(0);
	throw MVStop(text);
}

/*
//swig for perl wants it on windows!void var::win32_abort(CVR text) const
{
	abort(text);
}
*/

void var::abort(CVR text) const {

	THISIS("void var::abort(CVR text) const")
	ISSTRING(text)

	// exit(1);
	throw MVAbort(text);
}

void var::abortall(CVR text) const {

	THISIS("void var::abortall(CVR text) const")
	ISSTRING(text)

	// exit(1);
	throw MVAbort(text);
}

bool var::assigned() const {
	// THISIS("bool var::assigned() const")

	// treat undefined as unassigned
	// undefined is a state where we are USING the variable before its contructor has been
	// called! which is possible (in syntax like var xx.osread()?) and also when passing default
	// variables to functions in the functors on ISDEFINED(gcc)

	if (var_typ & VARTYP_MASK)
		return false;

	return var_typ != VARTYP_UNA;
}

bool var::unassigned() const {
	// see explanation above in assigned
	// THISIS("bool var::unassigned() const")
	// assertDefined(function_sig);

	if (var_typ & VARTYP_MASK)
		return true;

	return !var_typ;
}

VARREF var::unassigned(CVR defaultvalue) {

	// see explanation above in assigned
	// assertDefined(function_sig);


	THISIS("VARREF var::unassigned(CVR defaultvalue) const")
	ISASSIGNED(defaultvalue)

	//?allow undefined usage like var xyz=xyz.readnext();
	// if (var_typ & VARTYP_MASK)

	if (this->unassigned()) {
		// throw MVUndefined("unassigned( ^ defaultvalue ^")");
		// var_str="";
		// var_typ=VARTYP_STR;
		*this = defaultvalue;
	}
	return *this;
}

char var::toChar() const {

	THISIS("char var::toChar() const")
	assertString(function_sig);

	if (var_str.empty())
		return '\0';

	return var_str[0];
}

// temporary var can return move its string into the output
std::string var::toString() && {

	THISIS("std::string var::toString() &&")
	assertString(function_sig);

	return std::move(var_str);
}

// non-temporary var can return a const ref to its string
const std::string& var::toString() const& {

	THISIS("std::string var::toString() const&")
	assertString(function_sig);

	return var_str;
}

var var::length() const {

	THISIS("var var::length() const")
	assertString(function_sig);

	return int(var_str.size());
}

// synonym for length for compatibility with pick's len()
var var::len() const {

	THISIS("var var::len() const")
	assertString(function_sig);

	return int(var_str.size());
}

const char* var::data() const {

	THISIS("const char* var::data() const")
	assertString(function_sig);

	return var_str.data();
}

std::u32string var::to_u32string() const {

	 THISIS("std::u32string var::to_u32string() const")
	 assertString(function_sig);

	// 1.4 secs per 10,000,000 var=var copies of 3 byte ASCII strings
	// simple var=var copy of the following data

	// 14.9 secs round trips u8->u32->u8 per 10,000,000 on vm7
	// SKIPS/TRIMS OUT any bad utf8
	return boost::locale::conv::utf_to_utf<char32_t>(var_str);
}

void var::from_u32string(std::u32string u32str) const {
	// for speed, dont validate
	// THISIS("void var::from_u32tring() const")
	// assertDefined(function_sig);
	var_typ = VARTYP_STR;

	var_str = boost::locale::conv::utf_to_utf<char>(u32str);
}

// CONSTRUCTOR from const std::u32string
var::var(const std::wstring& wstr1) {
	var_typ = VARTYP_STR;
	var_str = boost::locale::conv::utf_to_utf<char>(wstr1);
}

// trim leading trimchars from a given string
inline void trimmerf_helper(std::string& instr, SV trimchars) {

	auto start_pos = instr.find_first_not_of(trimchars);

	// Early exit
	if (start_pos == std::string::npos) {
		instr.clear();
		return;
	}

	// return var(var_str.substr(start_pos));
	instr.erase(0, start_pos);

	return;
}

inline void trimmerb_helper(std::string& instr, SV trimchars) {

	std::size_t end_pos = instr.find_last_not_of(trimchars);

	// Early exit
	if (end_pos == std::string::npos) {
		instr.clear();
		return;
	}

	// return var(var_str.substr(0,end_pos+1));
	instr.erase(end_pos + 1);

	return;
}

inline void trimmerm_helper(std::string& instr, SV trimchars) {

	// ONLY works after trimming leading (F)ront and trailing (B)ack spaces

	// find the starting position of any embedded trimchars
	auto start_pos = std::string::npos;
	while (true) {

		// find a trimchars
		start_pos = instr.find_last_of(trimchars, start_pos);

		// Early exit
		// if no (more) trimchars then return the string
		if (start_pos == std::string::npos || start_pos <= 0)
			return;

		// find the first non-trimchar thereafter
		auto end_pos = instr.find_last_not_of(trimchars, start_pos - 1);

		// if first non trimchars character is not one before the trimchars
		if (end_pos < start_pos - 1) {
			instr.erase(end_pos + 1, start_pos - end_pos - 1);
		}

		if (end_pos <= 0)
			break;

		start_pos = end_pos - 1;
	}

	return;
}

var var::trim(SV trimchars, SV options) const& {

	THISIS("var var::trim(SV trimchars, SV options) const&")
	assertStringMutator(function_sig);

	return var(*this).trimmer(trimchars, options);
}

VARREF var::trimmer(SV trimchars, SV options) {

	THISIS("var var::trimmer(SV trimchars, SV options) const")
	assertStringMutator(function_sig);

	// Front only
	if (options == "F") {
		trimmerf_helper(var_str, trimchars);
	}

	// Back only
	else if (options == "B") {
		trimmerb_helper(var_str, trimchars);

	}

	// Front and Back, no Middle
	else if (options == "FB") {
		// back and front
		trimmerb_helper(var_str, trimchars);
		trimmerf_helper(var_str, trimchars);

	}

	// Front, Back and Middle
	else {
		// b, m, f for speed
		trimmerb_helper(var_str, trimchars);
		//trimmerm_helper(var_str, trimchars);
		trimmerf_helper(var_str, trimchars);
		// Sadly m must be last
		trimmerm_helper(var_str, trimchars);
	}

	return *this;
}

//trimf() - trim leading spaces/character
var var::trimf(SV trimchars DEFAULT_SPACE) const& {

	THISIS("var var::trimf(SV trimchars) const&")
	assertStringMutator(function_sig);

	var rvo = *this;

	trimmerf_helper(rvo.var_str, trimchars);

	return rvo;
}

// in-place
VARREF var::trimmerf(SV trimchars DEFAULT_SPACE) {

	THISIS("VARREF var::trimmerf(SV trimchars)")
	assertStringMutator(function_sig);

	trimmerf_helper(var_str, trimchars);

	return *this;
}

// trimb() - trim backward (trailing) spaces/character
var var::trimb(SV trimchars DEFAULT_SPACE) const& {

	THISIS("var var::trimb(SV trimchars) const&")
	assertStringMutator(function_sig);

	var rvo = *this;

	trimmerb_helper(rvo.var_str, trimchars);

	return rvo;
}

// in-place
VARREF var::trimmerb(SV trimchars DEFAULT_SPACE) {

	THISIS("VARREF var::trimmerb(SV trimchars)")
	assertStringMutator(function_sig);

	trimmerb_helper(var_str, trimchars);

	return *this;
}

//trim() - remove leading, trailing and excess internal spaces/character
var var::trim(SV trimchars DEFAULT_SPACE) const& {

	THISIS("var var::trim(SV trimchars) const&")
	assertStringMutator(function_sig);

	var rvo = *this;

	// Similar code in various places
	trimmerb_helper(rvo.var_str, trimchars);
	//trimmerm_helper(rvo.var_str, trimchars);
	trimmerf_helper(rvo.var_str, trimchars);
	//trimmerm only works after f and b trimchars are removed
	trimmerm_helper(rvo.var_str, trimchars);

	return rvo;
}

// in-place
VARREF var::trimmer(SV trimchars DEFAULT_SPACE) {

	// reimplement with boost string trim_if algorithm
	// http://www.boost.org/doc/libs/1_39_0/doc/html/string_algo/reference.html

	THISIS("VARREF var::trimmer(SV trimchars)")
	assertStringMutator(function_sig);

	// Similar code in various places
	trimmerb_helper(var_str, trimchars);
	//trimmerm_helper(var_str, trimchars);
	trimmerf_helper(var_str, trimchars);
	//trimmerm only works after f and b trimchars are removed
	trimmerm_helper(var_str, trimchars);

	return *this;
}

// invert() - inverts lower 8 bits of UTF8 codepoints (not bytes)
var var::invert() const& {
	var tt = *this;
	tt.inverter();
	return tt;
}

// on temporary
VARREF var::invert() && {
	return this->inverter();
}

// in-place
VARREF var::inverter() {

	THISIS("VARREF var::inverter()")
	assertStringMutator(function_sig);

	// xor each unicode code point, with the bits we want to toggle ... ie the bottom 8
	// since we will keep inversion within the same 256 byte pages of unicode codepoints
	// TODO invert directly in the UTF8 bytes - requires some cleverness

	// convert to char32.t string - four bytes per code point
	//std::u32string u32string1 = this->to_u32string();
	std::u32string u32str1(*this);

	// invert only the lower 8 bits to keep the resultant code points within the same unicode
	// 256 byte page
	for (auto& c : u32str1)
		c ^= char32_t(255);
	;

	// convert back to utf8
	//this->from_u32string(u32str1);
	*this = var(u32str1);

	return *this;
}

// ucase() - upper case
var var::ucase() const& {
	return var(*this).ucaser();
}

// on temporary
VARREF var::ucase() && {
	return this->ucaser();
}

// in-place
VARREF var::ucaser() {

	THISIS("VARREF var::ucaser()")
	assertStringMutator(function_sig);
	//assertString(function_sig);

	// optimise for ASCII
	// try ASCII uppercase to start with for speed
	// this may not be correct for all locales. eg Turkish I i İ ı mixing Latin and Turkish
	// letters.
	bool allASCII = false;
	for (char& c : var_str) {
		allASCII = (c & ~0x7f) == 0;
		if (!allASCII)
			break;
		c = std::toupper(c);
	}
	if (allASCII)
		return *this;

	init_boost_locale1();

	var_str = boost::locale::to_upper(var_str, tls_boost_locale1);

	return *this;

	/*
	int32_t ucasemap_utf8ToLower (
			const UCaseMap *  	csm,
			char *  	dest,
			int32_t  	destCapacity,
			const char *  	src,
			int32_t  	srcLength,
			UErrorCode *  	pErrorCode
		)
	*/
}

// lcase() - lower case
var var::lcase() const& {
	return var(*this).lcaser();
}

// on temporary
VARREF var::lcase() && {
	return this->lcaser();
}

// in-place
VARREF var::lcaser() {

	THISIS("VARREF var::lcaser()")
	assertStringMutator(function_sig);
	//assertString(function_sig);

	// return localeAwareChangeCase(1);

	// optimise for ASCII
	// try ASCII uppercase to start with for speed
	// this may not be correct for all locales. eg Turkish I i İ ı mixing Latin and Turkish
	// letters.
	bool allASCII = false;
	for (char& c : var_str) {
		allASCII = (c & ~0x7f) == 0;
		if (!allASCII)
			break;
		c = std::tolower(c);
	}
	if (allASCII)
		return *this;

	init_boost_locale1();

	var_str = boost::locale::to_lower(var_str, tls_boost_locale1);

	return *this;
}

// tcase() - title case
var var::tcase() const& {
	return var(*this).tcaser();
}

// on temporary
VARREF var::tcase() && {
	return this->tcaser();
}

// in-place
VARREF var::tcaser() {

	THISIS("VARREF var::tcaser()")
	assertStringMutator(function_sig);
	//assertString(function_sig);

	init_boost_locale1();

	// should not title 1 2 or 3 letter words or perhaps just a list of exceptions as follows:
	// a abaft about above afore after along amid among an apud as aside at atop below but by
	// circa down for from given in into lest like mid midst minus near next of off on onto out
	// over pace past per plus pro qua round sans save since than thru till times to under until
	// unto up upon via vice with worth the and nor or yet so or a an as at but by circa for
	// from in into like mid of on onto out over per pro qua sans than thru to until unto up
	// upon via vice with the and nor or yet so

	var_str = boost::locale::to_title(var_str, tls_boost_locale1);

	return *this;
}

// fcase()
// fold case - prepare for indexing/searching
// https://www.w3.org/International/wiki/Case_folding
// Note that accents are sometimes significant and sometime not. e.g. in French
//  cote (rating)
//  coté (highly regarded)
//  côte (coast)
//  côté (side)
var var::fcase() const& {
	return var(*this).fcaser();
}

// on temporary
VARREF var::fcase() && {
	return this->fcaser();
}

// in-place
VARREF var::fcaser() {

	THISIS("VARREF var::fcaser()")
	assertStringMutator(function_sig);
	//assertString(function_sig);

	init_boost_locale1();

	var_str = boost::locale::fold_case(var_str, tls_boost_locale1);

	return *this;
}

inline bool is_ascii(std::string_view str1) {
	// optimise for ASCII
	// try ASCII uppercase to start with for speed
	// this may not be correct for all locales. eg Turkish I i İ ı mixing Latin and Turkish
	// letters.
	//for (const char& c : str1) {
	for (const char c : str1) {
		if ((c & ~0x7f) != 0)
			return false;
	}
	return true;
}

// *** USED TO NORMALISE ALL KEYS BEFORE READING OR WRITING ***
// *** otherwise can have two record with similar keys á and á
// postgres gives FALSE for the following:
// SELECT 'á' = 'á';

// normalise()
// normalise (unicode NFC, C=Compact ... norm_nfc)
// see "Unicode Normalization Forms" https://unicode.org/reports/tr15/
//"It is crucial that Normalization Forms remain stable over time. That is, if a string that does
// not
// have any unassigned characters is normalized under one version of Unicode,
// it must remain normalized under all future versions of Unicode."
var var::normalize() const& {
	return var(*this).normalizer();
}

// on temporary
VARREF var::normalize() && {
	return this->normalizer();
}

// in-place
VARREF var::normalizer() {

	THISIS("VARREF var::normalizer()")
	assertStringMutator(function_sig);
	//assertString(function_sig);

	// optimise for ASCII which needs no normalisation
	if (is_ascii(var_str))
		return (*this);

	init_boost_locale1();

	// TODO see if the quick way to check if already in NFC format
	// is available in some library or if it is already built into boost locale normalize
	// because checking for being in normal form is very fast according to unicode docs

	// norm_nfc
	var_str = boost::locale::normalize(var_str, boost::locale::norm_nfc, tls_boost_locale1);

	return *this;
}

var var::unique() const {

	THISIS("var var::unique()")
	assertString(function_sig);

	// linemark
	var result = "";
	var start = 0;
	var bit;
	var delimiter;
	var sepchar = VM;
	int RMseq_plus1 = RM.seq() + 1;
	// bool founddelimiter = false;
	while (true) {

		// bit=this->remove(start, delimiter);
		bit = this->substr2(start, delimiter);

		// if (!founddelimiter && delimiter)
		if (delimiter)
			// sepchar=RM_-int(delimiter)+1;
			sepchar = var().chr(RMseq_plus1 - delimiter);

		if (bit.length()) {
			if (not(result.locateusing(sepchar, bit))) {
				//if (delimiter)
				result ^= bit ^ sepchar;
			}
		}
		if (not delimiter)
			break;
	}  // loop;
	//result.popper();
	if (not result.var_str.empty())
		result.var_str.pop_back();

	return result;
}

// BINARY - 1st byte
var var::seq() const {

	THISIS("var var::seq() const")
	assertString(function_sig);

	if (var_str.empty())
		return 0;

	int byteno = var_str[0];
	if (byteno >= 0)
		return byteno;
	else
		return byteno + 256;
}

// UTF8 - 1st UTF code point
var var::textseq() const {

	THISIS("var var::textseq() const")
	assertString(function_sig);
	/*
		if (var_str.empty())
			return 0;

		int byteno=var_str[0];
		if (byteno>=0)
			return byteno;
		else
			return byteno+256;
	*/
	// get four bytes from input string since in UTF8 a unicode code point may occupy up to 4
	// bytes
	std::u32string str1 = boost::locale::conv::utf_to_utf<char32_t>(var_str.substr(0, 4));

	return int(uint32_t(str1[0]));
}

// only returns BINARY bytes 0-255 (128-255) cannot be stored in the database unless with other
// bytes making up UTF8
var var::chr(const int charno) const {
	return char(charno % 256);
}

// returns unicode 1-4 byte sequences (in utf8)
// returns empty string for some invalid unicode points like 0xD800 to 0xDFFF which is reserved for
// UTF16 0x110000 ... is invalid too
var var::textchr(const int utf_codepoint) const {
	// doesnt use *this at all (do we need a version that does?)

	// return var((char) int1);

	if (!utf_codepoint)
		return std::string("\0", 1);

	std::wstring wstr1;
	wstr1.push_back(wchar_t(uint32_t(utf_codepoint)));
	return boost::locale::conv::utf_to_utf<char>(wstr1);
}

// quote() - wrap with double quotes
var var::quote() const& {
	return var(*this).quoter();
}

// on temporary
VARREF var::quote() && {
	return this->quoter();
}

// in-place
VARREF var::quoter() {

	THISIS("VARREF var::quoter()")
	assertStringMutator(function_sig);

	// NB this is std::string "replace" not var field replace
	var_str.replace(0, 0, "\"");
	var_str.push_back('"');
	return *this;
}

// squoter() - wrap with single quotes
var var::squote() const& {
	return var(*this).squoter();
}

// on temporary
VARREF var::squote() && {
	return this->squoter();
}

// in-place
VARREF var::squoter() {

	THISIS("VARREF var::squoter()")
	assertStringMutator(function_sig);

	// NB this is std::string "replace" not var field replace
	var_str.replace(0, 0, "'");
	var_str.push_back('\'');
	return *this;
}

//unquote() - remove outer double or single quotes
var var::unquote() const& {
	return var(*this).unquoter();
}

// on temporary
VARREF var::unquote() && {
	return this->unquoter();
}

// in-place
VARREF var::unquoter() {

	THISIS("VARREF var::unquoter()")
	assertStringMutator(function_sig);

	// removes MATCHING beginning and terminating " or ' characters
	// also removes a SINGLE " or ' on the grounds that you probably want to eliminate all such
	// characters

	// no change if no length
	size_t len = var_str.size();
	if (len < 2)
		return *this;

	char char0 = var_str[0];

	// no change if not starting " or '
	if (char0 != '\"' && char0 != '\'')
		return *this;

	// no change if terminating character ne starting character
	if (var_str[len - 1] != char0)
		return *this;

	// erase first (and last character if more than one)
	var_str.erase(0, 1);
	if (len)
		var_str.erase(len - 2, 1);

	return *this;
}

//splice() remove/replace/insert part of a string with another string
var var::splice(const int start1, const int length, SV insertstr) const& {
	return var(*this).splicer(start1, length, insertstr);
}

// on temporary
VARREF var::splice(const int start1, const int length, SV insertstr) && {
	return this->splicer(start1, length, insertstr);
}

// splice() remove/replace/insert part of a string (up to the end) with another string
var var::splice(const int start1, SV insertstr) const& {
	return var(*this).splicer(start1, insertstr);
}

// on temporary
VARREF var::splice(const int start1, SV insertstr) && {
	return this->splicer(start1, insertstr);
}

// in-place
VARREF var::splicer(const int start1, const int length, SV insertstr) {

	THISIS("VARREF var::splicer(const int start1,const int length, SV insertstr)")
	assertStringMutator(function_sig);
	//ISSTRING(insertstr)

	int start0;
	int lengthb;

	// First work out start index from start position
	// Depends on length of string, if position is negative
	if (start1 < 0)
		// abcdef[-3,2] -> abcdef[4,2] ie de
		start0 = var_str.size() + start1;
	else
		start0 = start1 - 1;

	// Negative start index means 0
	// abcdef[-8,2] -> abcdef[1,2] ie ab
	if (start0 < 0)
		start0 = 0;

	// Negative length simply moves the start char backwards
	// and the length is up to and including the start char
	if (length < 0) {

		// abcdef[4,-2]  -> abcdef[3,2]
		// abcdef[4,-99] -> abcdef[1,4]

		//int start0_save = start0;
		lengthb = start0 + 1;

		start0 = start0 + length + 1;
		if (start0 < 0) {
			start0 = 0;
			//lengthb = start0_save + 1;
		} else
			lengthb = -length;

		//std::cerr << "start0  = " << start0  << std::endl;
		//std::cerr << "lengthb = " << lengthb << std::endl;
	} else
		lengthb = length;

	if (uint(start0) >= var_str.size()) {
		//if (newstr.var_str.size())
			var_str += insertstr;
	} else {
		//if (insertstr.var_str.size())
			var_str.replace(start0, lengthb, insertstr);
		//else
		//	var_str.erase(start0,lengthb);
	}

	return *this;
}

// in-place
VARREF var::splicer(const int start1, SV insertstr) {

	THISIS("VARREF var::splicer(const int start1, SV insertstr)")
	assertStringMutator(function_sig);
	//ISSTRING(insertstr)

	// TODO make sure start and length work like pickos and HANDLE NEGATIVE LENGTH!
	int start1b;
	if (start1 > 0)
		start1b = start1;
	else if (start1 < 0) {
		start1b = int(var_str.size()) + start1 + 1;
		if (start1b < 1)
			start1b = 1;
	} else
		start1b = 1;

	if (uint(start1b) > var_str.size())
		var_str += insertstr;
	else
		var_str.replace(start1b - 1, var_str.size(), insertstr);

	return *this;
}

// pop() remove last byte of string
var var::pop() const& {
	return var(*this).popper();
}

// on temporary do in place
VARREF var::pop() && {
	return this->popper();
}

// in-place
VARREF var::popper() {

	THISIS("VARREF var::popper()")
	assertStringMutator(function_sig);

	if (!var_str.empty())
		var_str.pop_back();

	return *this;
}


/* Failed attempt to get compiler to call different functions depending on specific arguments

template<class T1, class T2, class T3>
VARREF splicerx(T1 start1, T2 length, const T3 str) {
	return this->splice(start1, length, var(str));
};

// Specialise splicer(-1, 1, "") to call popper()
// Sadly compiler never chooses this one over the main template
template<const int = -1, const int = 1, const char* = "">
VARREF splicerx(const int start1, const int length, const char* c) {
       this->outputl("testing");
       return this->popper();
};
*/

VARREF var::transfer(VARREF destinationvar) {

	THISIS("VARREF var::transfer(VARREF destinationvar)")
	// transfer even unassigned vars (but not uninitialised ones)
	//assertDefined(function_sig);
	assertAssigned(function_sig);
	ISDEFINED(destinationvar)

	destinationvar.var_str.swap(var_str);
	destinationvar.var_typ = var_typ;
	destinationvar.var_int = var_int;
	destinationvar.var_dbl = var_dbl;

	var_str.clear();
	var_typ = VARTYP_STR;

	return destinationvar;
}

// kind of const needed in calculatex
CVR var::exchange(CVR var2) const {

	THISIS("VARREF var::exchange(VARREF var2)")
	assertAssigned(function_sig);
	ISDEFINED(var2)

	// intermediary copies of var2
	auto mvtypex = var2.var_typ;
	auto mvintx = var2.var_int;
	auto mvdblx = var2.var_dbl;

	// do string first since it is the largest and most likely to fail
	var_str.swap(var2.var_str);

	// copy mv to var2
	var2.var_typ = var_typ;
	var2.var_int = var_int;
	var2.var_dbl = var_dbl;

	// copy intermediaries to mv
	var_typ = mvtypex;
	var_int = mvintx;
	var_dbl = mvdblx;

	return var2;
}

var var::str(const int num) const {

	THISIS("var var::str(const int num) const")
	assertString(function_sig);

	var newstr = "";
	if (num < 0)
		return newstr;

	int basestrlen = int(var_str.size());
	if (basestrlen == 1)
		newstr.var_str.resize(num, var_str.at(0));
	else if (basestrlen)
		for (int ii = num; ii > 0; --ii)
			newstr ^= var_str;

	return newstr;
}

var var::space() const {

	THISIS("var var::space() const")
	assertNumeric(function_sig);

	var newstr = "";
	int nspaces = this->round().toInt();
	if (nspaces > 0)
		newstr.var_str.resize(nspaces, ' ');

	return newstr;
}

//crop() - remove superfluous FM, VM etc.
var var::crop() const& {
	return var(*this).cropper();
}

// on temporary
VARREF var::crop() && {
	return this->cropper();
}

// in-place
VARREF var::cropper() {

	THISIS("VARREF var::cropper()")
	assertStringMutator(function_sig);

	std::string newstr;

	std::string::iterator iter = var_str.begin();
	std::string::iterator iterend = var_str.end();

	while (iter != iterend) {

		char charx = (*iter);
		++iter;

		// simply append ordinary characters
		if (charx < STM_ || charx > RM_) {
			newstr.push_back(charx);
			continue;
		}

		// found a separator

		// remove any lower separators from the end of the string
		while (!newstr.empty()) {
			char lastchar = newstr.back();
			if (lastchar >= STM_ && lastchar < charx)
				newstr.pop_back();
			else
				break;
		}

		// append the separator
		newstr.push_back(charx);
	}

	// remove any trailing separators
	while (!newstr.empty() && newstr.back() >= STM_ && newstr.back() <= RM_) {
		newstr.pop_back();
	}

	var_str = newstr;
	// swap(var_str,newstr);

	return *this;
}

// lower() drops FM to VM, VM to SM etc.
var var::lower() const& {
	return var(*this).lowerer();
}

// on temporary
VARREF var::lower() && {
	return this->lowerer();
}

// in-place
VARREF var::lowerer() {

	THISIS("VARREF var::lowerer()")
	assertStringMutator(function_sig);
	//assertString(function_sig);

	// note: rotate lowest sep to highest
	//this->converter(_RM_ _FM_ _VM_ _SM_ _TM_ _STM_, _FM_ _VM_ _SM_ _TM_ _STM_ _RM_);

	//bottom marks get crushed together but STM is infrequently used
	// reversible by raiser only if no STM chars are present - which are not common
	this->converter(_RM_ _FM_ _VM_ _SM_ _TM_, _FM_ _VM_ _SM_ _TM_ _STM_);

	return *this;
}

// raise() lifts VM to FM, SM to VM etc.
var var::raise() const& {
	return var(*this).raiser();
}

// on temporary
VARREF var::raise() && {
	return this->raiser();
}

// in-place
VARREF var::raiser() {

	THISIS("VARREF var::raiser()")
	assertStringMutator(function_sig);
	//assertString(function_sig);

	// note: rotate highest sep to lowest
	// advantage is it is reversible by lowerer but the problem is that the smallest delimiter becomes the largest
	//this->converter(_FM_ _VM_ _SM_ _TM_ _STM_ _RM_, _RM_ _FM_ _VM_ _SM_ _TM_ _STM_);

	// top two marks get crushed together but RM is rarely used
	// reversible by lowerer only if no RM are present - which are rare
	this->converter(_FM_ _VM_ _SM_ _TM_ _STM_, _RM_ _FM_ _VM_ _SM_ _TM_);

	return *this;
}

//generic helper to handle char and u32_char wise conversion (mapping)
template <class T1, class T2, class T3>
void string_converter(T1& var_str, const T2 oldchars, const T3 newchars) {
	typename T1::size_type pos = T1::npos;

	while (true) {
		// locate (backwards) any of the from characters
		// because we might be removing characters
		// and it is faster to remove last character first
		pos = var_str.find_last_of(oldchars, pos);

		if (pos == T1::npos)
			break;

		// find which from character we have found
		int fromcharn = int(oldchars.find(var_str[pos]));

		if (fromcharn < int(newchars.length()))
			var_str.replace(pos, 1, newchars.substr(fromcharn, 1));
			//var_str.replace(pos, 1, newchars[fromcharn]);
		else
			var_str.erase(pos, 1);

		if (pos == 0)
			break;

		pos--;
	}
	return;
}

// convert() - replaces one by one in string, a list of characters with another list of characters
// if the target list is shorter than the source list of characters then characters are deleted
//var var::convert(CVR oldchars, CVR newchars) const& {
var var::convert(SV oldchars, SV newchars) const& {

//	THISIS("var var::convert(SV oldchars,SV newchars) const")
//	assertString(function_sig);

	// return var(*this).converter(oldchars,newchars);
	var temp = var(*this).converter(oldchars, newchars);
	return temp;
}

// on temporary
//VARREF var::convert(CVR oldchars, CVR newchars) && {
VARREF var::convert(SV oldchars, SV newchars) && {
	//dont check if defined/assigned since temporaries very unlikely to be so

	return this->converter(oldchars, newchars);
}

// in-place
//VARREF var::converter(CVR oldchars, CVR newchars) {
VARREF var::converter(SV oldchars, SV newchars) {

	THISIS("VARREF var::converter(SV oldchars,SV newchars)")
	assertStringMutator(function_sig);

	//string_converter(var_str, oldchars.var_str, newchars.var_str);
	string_converter(var_str, oldchars, newchars);

	return *this;
}

//// in-place for const char*
//VARREF var::converter(const char* oldchars, const char* newchars) {
//
//	THISIS("VARREF var::converter(const char* oldchars, const char* newchars)")
//	assertStringMutator(function_sig);
//
//	string_converter(var_str, std::string(oldchars), std::string(newchars));
//
//	return *this;
//}

// textconvert() - replaces one by one in string, a list of characters with another list of characters
// if the target list is shorter than the source list of characters then characters are deleted
var var::textconvert(SV oldchars, SV newchars) const& {
	return var(*this).textconverter(oldchars, newchars);
}

// on temporary
VARREF var::textconvert(SV oldchars, SV newchars) && {
	return this->textconverter(oldchars, newchars);
}

// in-place
VARREF var::textconverter(SV oldchars, SV newchars) {

	THISIS("VARREF var::converter(CVR oldchars,CVR newchars)")
	assertStringMutator(function_sig);

	// all ASCII -> bytewise conversion for speed
	if (is_ascii(oldchars) && is_ascii(newchars)) {
		string_converter(var_str, oldchars, newchars);
	}

	// any non-ASCI -> convert to wide before conversion, then back again
	else {

		// convert everything to from UTF8 to wide string
		std::u32string u32str1 = this->to_u32string();
		//std::u32string u32_oldchars = var(oldchars).to_u32string();
		//std::u32string u32_newchars = var(newchars).to_u32string();
		//std::u32string u32_oldchars = boost::locale::conv::utf_to_utf<char32_t>(oldchars);
		//std::u32string u32_newchars = boost::locale::conv::utf_to_utf<char32_t>(newchars);
		std::u32string u32_oldchars = boost::locale::conv::utf_to_utf<char32_t>(std::string(oldchars));
		std::u32string u32_newchars = boost::locale::conv::utf_to_utf<char32_t>(std::string(newchars));

		// convert the wide characters
		string_converter(u32str1, u32_oldchars, u32_newchars);

		// convert the string back to UTF8 from wide string
		//this->from_u32string(u32str1);
		*this = var(u32str1);
	}
	return *this;
}

/////////
// output
/////////

//warning put() is not threadsafe whereas output(), errput() and logput() are threadsafe
CVR var::put(std::ostream& ostream1) const {

	THISIS("CVR var::put(std::ostream& ostream1) const")
	assertString(function_sig);

	// prevent output to cout suppressing output to cout (by non-exodus routines)
	// http://gcc.gnu.org/ml/gcc-bugs/2006-05/msg01196.html
	// TODO optimise by calling once instead of every call to output()
	if (!desynced_with_stdio) {
		std::ios::sync_with_stdio(false);
		desynced_with_stdio = true;
	}

	// verify conversion to UTF8
	// std::string tempstr=(*this).toString();

	ostream1.write(var_str.data(), (std::streamsize)var_str.size());
	return *this;
}

// output -> cout which is buffered standard output
///////////////////////////////////////////////////

// output() buffered threadsafe output to standard output
CVR var::output() const {
	LOCKIOSTREAM
	return this->put(std::cout);
}

// outputl() flushed threadsafe output to standard output
// adds \n and flushes so is slower than output("\n")
CVR var::outputl() const {
	LOCKIOSTREAM
	this->put(std::cout);
	std::cout << std::endl;
	return *this;
}

// outputt() buffered threadsafe output to standard output
// adds \t
CVR var::outputt() const {
	LOCKIOSTREAM
	this->put(std::cout);
	std::cout << '\t';
	return *this;
}

// overloaded output() outputs a prefix str
CVR var::output(CVR str) const {
	LOCKIOSTREAM
	str.put(std::cout);
	return this->put(std::cout);
}

// oveloaded outputl() outputs a prefix str
CVR var::outputl(CVR str) const {
	LOCKIOSTREAM
	str.put(std::cout);
	this->put(std::cout);
	std::cout << std::endl;
	return *this;
}

// overloaded outputt() outputs a prefix str
CVR var::outputt(CVR str) const {
	LOCKIOSTREAM
	std::cout << "\t";
	str.put(std::cout);
	std::cout << "\t";
	this->put(std::cout);
	return *this;
}

// errput -> cerr which is unbuffered standard error
////////////////////////////////////////////////////

// errput() unbuffered threadsafe output to standard error
CVR var::errput() const {
	LOCKIOSTREAM
	//return put(std::cerr);
	std::cerr << *this;
	return *this;
}

// errputl() unbuffered threadsafe output to standard error
// adds "\n"
CVR var::errputl() const {
	LOCKIOSTREAM
	//this->put(std::cerr);
	std::cerr << *this;
	std::cerr << std::endl;
	return *this;
}

// overloaded errput outputs a prefix str
CVR var::errput(CVR str) const {
	LOCKIOSTREAM
	//str.put(std::cerr);
	//return this->put(std::cerr);
	std::cerr << str;
	std::cerr << *this;
	return *this;
}

// overloaded errputl outputs a prefix str
CVR var::errputl(CVR str) const {
	LOCKIOSTREAM
	//str.put(std::cerr);
	//this->put(std::cerr);
	std::cerr << str;
	std::cerr << *this;
	std::cerr << std::endl;
	return *this;
}

// logput -> clog which is a buffered version of cerr standard error output
///////////////////////////////////////////////////////////////////////////

// logput() buffered threadsafe output to standard log
CVR var::logput() const {
	LOCKIOSTREAM
	//this->put(std::clog);
	std::clog << *this;
	//std::clog.flush();
	return *this;
}

// logput() flushed threadsafe output to standard log
CVR var::logputl() const {
	LOCKIOSTREAM
	//this->put(std::clog);
	std::clog << *this;
	std::clog << std::endl;
	return *this;
}

// overloaded logput with a prefix str
CVR var::logput(CVR str) const {
	LOCKIOSTREAM
	//str.put(std::clog);
	std::clog << str;
	std::clog << *this;
	return *this;
}

// overloaded logputl with a prefix str
CVR var::logputl(CVR str) const {
	LOCKIOSTREAM
	//str.put(std::clog);
	//this->put(std::clog);
	std::clog << str;
	std::clog << *this;
	std::clog << std::endl;
	return *this;
}

////////
// DCOUNT
////////
// TODO make a char and char version for speed
var var::dcount(CVR substrx) const {

	THISIS("var var::dcount(CVR substrx) const")
	assertString(function_sig);
	ISSTRING(substrx)

	if (var_str.empty())
		return 0;

	return count(substrx) + 1;
}

///////
// COUNT
///////

var var::count(CVR substrx) const {

	THISIS("var var::count(CVR substrx) const")
	assertString(function_sig);
	ISSTRING(substrx)

	if (substrx.var_str == "")
		return 0;

	std::string::size_type substr_len = substrx.var_str.size();

	// find the starting position of the field or return ""
	std::string::size_type start_pos = 0;
	int fieldno = 0;
	while (true) {
		start_pos = var_str.find(substrx.var_str, start_pos);
		// past of of string?
		if (start_pos == std::string::npos)
			return fieldno;
		// start_pos++;
		start_pos += substr_len;
		fieldno++;
	}
}

var var::count(const char charx) const {

	THISIS("var var::count(const char charx) const")
	assertString(function_sig);

	// find the starting position of the field or return ""
	std::string::size_type start_pos = 0;
	int fieldno = 0;
	while (true) {
		start_pos = var_str.find_first_of(charx, start_pos);
		// past of of string?
		if (start_pos == std::string::npos)
			return fieldno;
		start_pos++;
		fieldno++;
	}
}

var var::index2(CVR substrx, const int startchar1) const {

	THISIS("var var::index2(CVR substrx,const int startchar1) const")
	assertString(function_sig);
	ISSTRING(substrx)

	if (substrx.var_str == "")
		return var(0);

	// find the starting position of the field or return ""
	std::string::size_type start_pos = startchar1 - 1;
	start_pos = var_str.find(substrx.var_str, start_pos);

	// past of of string?
	if (start_pos == std::string::npos)
		return var(0);

	return var((int)start_pos + 1);
}

var var::index(CVR substrx, const int occurrenceno) const {

	THISIS("var var::index(CVR substrx,const int occurrenceno) const")
	assertString(function_sig);
	ISSTRING(substrx)

	//TODO implement negative occurenceno as meaning backwards from the end
	//eg -1 means the last occurrence

	if (substrx.var_str == "")
		return var(0);

	std::string::size_type start_pos = 0;
	std::string::size_type substr_len = substrx.var_str.size();

	// negative and 0th occurrence mean the first
	int countdown = occurrenceno >= 1 ? occurrenceno : 1;

	for (;;) {

		// find the starting position of the field or return ""
		start_pos = var_str.find(substrx.var_str, start_pos);

		// past of of string?
		if (start_pos == std::string::npos)
			return 0;

		--countdown;

		// found the right occurrence
		if (countdown == 0)
			return ((int)start_pos + 1);

		// skip to character after substr (not just next character)
		// start_pos++;
		start_pos += substr_len;
	}

	// should never get here
	return 0;
}

var var::debug(CVR var1) const {
	// THISIS("var var::debug() const")

	std::clog << "var::debug(" << var1 << ")" << std::endl;
	//flush to ensure all stdout is visible
	std::cout << std::flush;

	//"good way to break into the debugger by causing a seg fault"
	// *(int *)0 = 0;
	// throw var("Debug Statement");

#if __has_include(<signal.h>)
	::raise(SIGTRAP);
#elif 1
	__asm__("int3");
	//__asm__("int3");
	//__asm__("int3");

#elif defined(_MSC_VER)
	// throw var("Debug Statement");
	throw MVDebug(var1);

// another way to break into the debugger by causing a seg fault
#elif 0
	*(int*)0 = 0;
#endif

	return "";
}

var var::logoff() const {
	// THISIS("var var::logoff() const")
	// THIS"var.logoff()".assertString(function_sig);

	//LOCKIOSTREAM
	//std::cout << "var::logoff not implemented yet " << std::endl;
	//return "";
	throw MVLogoff();
}

var var::xlate(CVR filename, CVR fieldno, CVR mode) const {

	THISIS("var var::xlate(CVR filename,CVR fieldno, CVR mode) const")
	ISSTRING(mode)

	return xlate(filename, fieldno, mode.var_str.c_str());
}

// TODO provide a version with int fieldno to handle the most frequent case
// although may also support dictid (of target file) instead of fieldno

var var::xlate(CVR filename, CVR fieldno, const char* mode) const {

	THISIS("var var::xlate(CVR filename,CVR fieldno, const char* mode) const")
	assertString(function_sig);
	ISSTRING(filename)
	// fieldnames are supported as mvprogram::xlate
	// but not here in var::xlate which only supports field numbers since it has no
	// access to dictionaries
	ISNUMERIC(fieldno)

	// open the file (skip this for now since sql doesnt need "open"
	var file;
	// if (!file.open(filename))
	//{
	//	_STATUS=filename^" does not exist";
	//	record="";
	//	return record;
	//}
	//file MUST be lower case in order to detect "dict."
	file = filename.lcase();

	char sep = fieldno.length() ? VM_ : RM_;

	var response = "";
	int nmv = this->dcount(_VM_);
	for (int vn = 1; vn <= nmv; ++vn) {

		//test every time instead of always appending and removing at the end
		//because the vast majority of xlate are single valued so it is faster
		if (vn > 1)
			response ^= sep;

		// read the record
		var key = this->a(1, vn);
		var record;
		if (!record.reado(file, key)) {
			// if record doesnt exist then "", or original key if mode is "C"

			// no record and mode C returns the key
			// gcc warning: comparison with string literal results in unspecified
			// behaviour if (mode=="C")
			if (*mode == *"C")
				response ^= key;

			// no record and mode X or anything else returns ""
			continue;
		}

		// extract the field or field 0 means return the whole record
		if (fieldno) {

			// numeric fieldno not zero return field
			// if (fieldno.isnum())

			// throw non-numeric error if fieldno not numeric
			response ^= record.a(fieldno);

			// non-numeric fieldno - cannot call calculate from here
			// return calculate(fieldno,filename,mode);
			continue;
		}

		// fieldno "" returns whole record
		if (!fieldno.length()) {
			response ^= record;
			continue;
		}

		// field no 0 returns key
		response ^= key;
	}
	//response.convert(FM^VM,"^]").outputl("RESPONSE=");
	return response;
}

}  // namespace exodus
