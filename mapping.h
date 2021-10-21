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

//�w�q�C��LPN�bmapping table�W��entry���c
class L2P_entry
{
private:

public:
	//����LPN�C��sector�Q�g�����PPN
	long long PPN[sectors_in_a_page];

	//����LPN�C��sector�O�_���Q�g�L
	bool PPN_flag[sectors_in_a_page];

	//����LPN�C��sector�Q�g��PPN������offset
	int PPN_bitmap[sectors_in_a_page];

	//����LPN��entry size
	int size;

	//����LPN��sector���Ӽ�
	int sector_count;
};

//�w�qmapping table���c
class Mapping_table
{
private:

public:
	//�Q��hash map���x�s�C��LPN�۹�����entry	
	unordered_map<long long, L2P_entry> L2P;

	//�ثe���mapping table���j�p
	long long current_table_size;	

	void write(long long free_block_id, long long free_page_id, const Logical_page& page);
	long long read(long long LPN, int offset);
};

extern Mapping_table mapping_table;

#endif // !MAPPING_H