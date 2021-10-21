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

//�w�qflash memory�̭�PPN�����c
class Physical_page
{
private:

public:	
	//������PPN���C��offset�s��sector��T
	long long sector_number[sectors_in_a_page];

	//�O����PPN���C��offset�O�_�Ovalid
	bool valid_sector[sectors_in_a_page];

	//�O����PPN��invalid sector�Ӽ�
	int invalid_count;

	//�O����PPN��free sector�Ӽ�
	//free sector�N�|�Ū�sector�S�Q�g�L(���Oinvalid sector)
	int free_count;

	//������PPN���`�@�Q�g�L��sector�Ӽ�	
	int sector_count;

	//�O����PPN�����A(invalid, valid)
	int status;
};

//�w�qflash memory�̭�block�����c
class Block
{
private:

public:
	//�O����block�̭��C��PPN����T
	Physical_page PPN[pages_in_a_block];

	//�O����block�ثefree PPN����@��
	//�Ψӵ��U�@���g�J��
	int free_page_pointer;

	//�O����block��invalid PPN�ƶq
	int invalid_count;

	//�O����block�����A(invalid, valid, active)
	//active�N��block�٨S�Q�g���A�٦�free PPN
	int status;
};

//�w�qflash memory�����c
class Flash_memory
{
private:

public:
	//flash memory�̭��u�n�Q�g�L��block���|���used_block_list�̭�
	unordered_map<long long, Block> used_block_list;

	//flash memory�bused_block_list�̭���block�Q��GC��|���recycled_block_list�̭�
	//�H�K���U�@��allocate��
	unordered_map<long long, Block> recycled_block_list;

	//flash memory�̭��u�n��block�Q�g���N�|���invalid_block_list�̭��A��K���ᰵGC
	//�̾ڤ@��block���h��PPN�Ainvalid_block_list�N�����X��
	unordered_map <long long, unordered_map <long long, Block>::iterator> invalid_block_list[pages_in_a_block + 1];

	//����flash memory�̭�invalid block���ƶq
	long long invalid_block_count;

	//����flash memory�̭��ثe���b�g��block
	long long active_block_pointer;

	//����flash memory�̭��̦hallocate�쪺block_id�O�h��
	long long max_allocated_block_id;	

	//����flash memory�̭�free block���ƶq	
	long long free_block_count;

	//����flash memory�`�@���X��compact LPN���B�~Ū��
	long long rmw_read_count;

	//����flash memory�`�@�Qread������
	long long read_count;

	//����flash memory�`�@�Qwrite������
	long long write_count;

	//����flash memory�`�@�Qerase������
	long long erase_count;

	//����flash memory�`�@���X��migration������
	//1��magration�N�O����1��wrtie�[�W1��read
	long long migration_count;	

	//����flash memory��GC���e��
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