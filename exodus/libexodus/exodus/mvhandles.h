//
// Copyright (c) 2010 steve.bush@neosys.com
//
// Desigion making
//	1. According to exodus design, file variable stores file name, but cannot store pointer to
// opened file.
//  2. Every osbread/osbwrite operation reopens a file -> low speed
//  3. We could save opened file pointer into table and keep index of table in file variable.
//	4. The same true for DB connections.
//	5. Interface type to handle all kinds of opened things is 'void *'.
//  6. Handle is added to cache table with destructor functor, 3-4 lines of additional code to
//  write,
//		but significantly simplifies object design.
//
#ifndef MVHANDLES_H
#define MVHANDLES_H

#include <string>
#include <vector>

namespace exodus {

using CACHED_HANDLE = void*;
using DELETER_AND_DESTROYER = void (*)(CACHED_HANDLE);

class MvHandleEntry {
   public:
	MvHandleEntry();
	DELETER_AND_DESTROYER deleter;	// =0 means that slot is empty
	CACHED_HANDLE handle;
	std::string extra;
};

class MvHandlesCache {
   public:
	MvHandlesCache();
	int add_handle(CACHED_HANDLE handle_to_opened_file, DELETER_AND_DESTROYER del, std::string name);
	// MvHandleEntry & operator [] (int idx)
	//{
	//	return conntbl[ idx];
	//}
	CACHED_HANDLE get_handle(int index, std::string name);
	void del_handle(int index);
	virtual ~MvHandlesCache();

   private:
	std::vector<MvHandleEntry> conntbl;
};

#ifndef INSIDE_MVHANDLES_CPP
extern
#endif								  // INSIDE_MVHANDLES_CPP
	MvHandlesCache mv_handles_cache;  // global table (intended usage: mvos.cpp and mvdbpostgres.cpp)

}  // namespace exodus
#endif	// MVHANDLES_H
