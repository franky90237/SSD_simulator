#pragma once
#ifndef MAPPING_H
#define MAPPING_H

#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include "SSD_parameter.h"
#include "buffer.h"

using std::cout;		using std::cerr;
using std::endl;		using std::string;
using std::ifstream;	using std::vector;
using std::map;			using std::ofstream;
using std::stoi;		using std::unordered_map;

//定義每個LPN在mapping table上的entry結構
class L2P_entry
{
private:

public:
	//紀錄LPN每個sector被寫到哪個PPN
	long long PPN[sectors_in_a_page];

	//紀錄LPN每個sector是否有被寫過
	bool PPN_flag[sectors_in_a_page];

	//紀錄LPN每個sector被寫到PPN的哪個offset
	int PPN_bitmap[sectors_in_a_page];

	//紀錄LPN的entry size
	int size;

	//紀錄LPN的sector的個數
	int sector_count;
};

//定義mapping table結構
class Mapping_table
{
private:

public:
	//利用hash map來儲存每個LPN相對應的entry	
	unordered_map<long long, L2P_entry> L2P;

	//目前整個mapping table的大小
	long long current_table_size;	

	void write(long long free_block_id, long long free_page_id, const Logical_page& page);
	long long read(long long LPN, int offset);
};

extern Mapping_table mapping_table;

#endif // !MAPPING_H