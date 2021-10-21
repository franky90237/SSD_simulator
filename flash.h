#pragma once
#ifndef FLASH_H
#define FLASH_H

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

//定義flash memory裡面PPN的結構
class Physical_page
{
private:

public:	
	//紀錄此PPN的每個offset存的sector資訊
	long long sector_number[sectors_in_a_page];

	//記錄此PPN的每個offset是否是valid
	bool valid_sector[sectors_in_a_page];

	//記錄此PPN的invalid sector個數
	int invalid_count;

	//記錄此PPN的free sector個數
	//free sector就會空的sector沒被寫過(不是invalid sector)
	int free_count;

	//紀錄此PPN的總共被寫過的sector個數	
	int sector_count;

	//記錄此PPN的狀態(invalid, valid)
	int status;
};

//定義flash memory裡面block的結構
class Block
{
private:

public:
	//記錄此block裡面每個PPN的資訊
	Physical_page PPN[pages_in_a_block];

	//記錄此block目前free PPN到哪一個
	//用來給下一次寫入用
	int free_page_pointer;

	//記錄此block的invalid PPN數量
	int invalid_count;

	//記錄此block的狀態(invalid, valid, active)
	//active代表此block還沒被寫滿，還有free PPN
	int status;
};

//定義flash memory的結構
class Flash_memory
{
private:

public:
	//flash memory裡面只要被寫過的block都會放到used_block_list裡面
	unordered_map<long long, Block> used_block_list;

	//flash memory在used_block_list裡面的block被做GC後會放到recycled_block_list裡面
	//以便給下一次allocate用
	unordered_map<long long, Block> recycled_block_list;

	//flash memory裡面只要有block被寫滿就會放到invalid_block_list裡面，方便之後做GC
	//依據一個block有多個PPN，invalid_block_list就有有幾區
	unordered_map <long long, unordered_map <long long, Block>::iterator> invalid_block_list[pages_in_a_block + 1];

	//紀錄flash memory裡面invalid block的數量
	long long invalid_block_count;

	//紀錄flash memory裡面目前正在寫的block
	long long active_block_pointer;

	//紀錄flash memory裡面最多allocate到的block_id是多少
	long long max_allocated_block_id;	

	//紀錄flash memory裡面free block的數量	
	long long free_block_count;

	//紀錄flash memory總共做幾次compact LPN的額外讀取
	long long rmw_read_count;

	//紀錄flash memory總共被read的次數
	long long read_count;

	//紀錄flash memory總共被write的次數
	long long write_count;

	//紀錄flash memory總共被erase的次數
	long long erase_count;

	//紀錄flash memory總共做幾次migration的次數
	//1個magration就是等於1個wrtie加上1個read
	long long migration_count;	

	//紀錄flash memory的GC門檻值
	double gc_threshold;

	void configuration();
	void allocate_a_block();
	long long allocation();
	bool full();
	void write(long long free_block_id, const Logical_page& victim_page);
	void update(const Logical_page& victim_page);
	void read(long long LPN, int offset);
	void erase(unordered_map<long long, Block>::iterator& victim_block);
	void gc();
};

extern Flash_memory flash_memory;
extern long long block_number;
void create_empty_block(Block& empty_block);

#endif // !FLASH_H