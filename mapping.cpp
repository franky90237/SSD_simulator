#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include <math.h>
#include "SSD_parameter.h"
#include "buffer.h"
#include "mapping.h"
#include "flash.h"

using std::cout;		using std::cerr;
using std::endl;		using std::string;
using std::ifstream;	using std::vector;
using std::map;			using std::ofstream;
using std::stoi;		using std::unordered_map;

//�ھڭ��Q�g�J��page�A�٦��o��page���Q�g�J��block_id�H��page_id�A�ӧ�smapping table
void Mapping_table::write(long long free_block_id, long long free_page_id, const Logical_page& page)
{
	//��hash map�ӧ�O�_���ƨϥιL�ۦP��LPN
	//�o��mapping table�u����LPN�Q��s�L
	//�N�⦹LPN��entry��iLPN_map�̭�
	unordered_map<long long, L2P_entry*> LPN_map;

	//�q��page��sector 0�}�l�̧ǧP�_
	for (int i = 0; i < page.sector_count; ++i)
	{
		//���p�⦹page��sector i��LPN�Moffset
		long long LPN = page.sector[i].sector_number / (page_size / sector_size);
		int offset = page.sector[i].sector_number % (page_size / sector_size);

		//�����LPN�bmapping table�W����T		
		unordered_map<long long, L2P_entry>::iterator  L2P_LPN = L2P.find(LPN);

		//�p�G�bmapping table�W�䤣�즹LPN
		//�N��LPN�O�Ĥ@���Q�g��flash memory�̭�
		if (L2P_LPN == L2P.end())
		{
			//�Ыؤ@��LPN�bmapping table��entry
			L2P_entry l2p_entry;

			//�q��entry��sector 0�}�l�̧Ǫ�l��
			for (int j = 0; j < sectors_in_a_page; ++j)
			{
				l2p_entry.PPN[j] = -1;
				l2p_entry.PPN_flag[j] = false;
				l2p_entry.PPN_bitmap[offset] = -1;
			}

			//�]�w��LPN����offset��sector�O�Q�g�����PPN
			l2p_entry.PPN[offset] = free_block_id * pages_in_a_block + free_page_id;

			//�]�w��LPN����offset��sector�O���Q�g�J�L��
			l2p_entry.PPN_flag[offset] = true;

			//�]�w��LPN����offset��sector�O�Q�g��PPN����i��sector
			l2p_entry.PPN_bitmap[offset] = i;

			//���]�w��LPN����entry size��0
			//�̫�|�A�p��u����entry size
			l2p_entry.size = 0;

			//�]�w��LPN����sector�ƶq�u��1��
			l2p_entry.sector_count = 1;

			//�⦹LPN��entry���mapping table�̭�
			L2P[LPN] = l2p_entry;

			//�dhash map�ݦ�����smapping table
			//��LPN��観�S���Q��s�L
			//�p�G�S���N��o��entry����m���s�_��
			//�o�ӳ̫�|�Ψӭp�⦹LPN��entry size���h��
			if (LPN_map.find(LPN) == LPN_map.end())
			{
				LPN_map[LPN] = &L2P[LPN];				
			}
		}
		//�p�G�bmapping table�W�䪺��LPN
		//�N��LPN�O���e�w�g���Q�g��flash memory�̭��L
		else
		{
			//�p�G��LPN��offset��sector�٨S���Q�g�L���A���N�i�H�⦹LPN����sector�ƶq���W
			if (L2P_LPN->second.PPN[offset] != -1) ++L2P_LPN->second.sector_count;

			//�]�w��LPN����offset��sector�O�Q�g�����PPN
			L2P_LPN->second.PPN[offset] = free_block_id * pages_in_a_block + free_page_id;

			//�]�w��LPN����offset��sector�O���Q�g�J�L��
			L2P_LPN->second.PPN_flag[offset] = true;

			//�]�w��LPN����offset��sector�O�Q�g��PPN����i��sector
			L2P_LPN->second.PPN_bitmap[offset] = i;

			//�dhash map�ݦ�����smapping table
			//��LPN��観�S���Q��s�L
			//�p�G�S���N��o��entry����m���s�_��
			//�o�ӳ̫�|�Ψӭp�⦹LPN��entry size���h��
			if (LPN_map.find(LPN) == LPN_map.end())
			{
				LPN_map[LPN] = &(L2P_LPN->second);
			}
		}
	}

	//�H�W���O�b�̾�LPN�Moffset�h��smapping table
	//���U�ӤU���|�}�l�p�⦹����s�L��LPN�L�̪�entry�bmapping table�W���j�p
	//�M��|��s�ثemapping table���`�@size���h��
	
	//�Q�Ϊ�LPN_map�A�}�l�@�Ӥ@�ӥh��̭�LPN��entry
	for (auto it = LPN_map.begin(); it != LPN_map.end(); ++it)
	{
		//sum�Ψӭp�⦹LPN��entry���`�@size
		int sum = 0;

		//�ΨӬݬO���O��LPN�̭��۾F��sector�O���O�Q�g��P��PPN
		//�p�G�O���ܡA�N�u�n�s�@��PPN�N�n
		long long pre_PPN = it->second->PPN[0];

		//�p�G��LPN����0��sector���Q�g��flash memory�L
		if (pre_PPN != -1)
		{
			//sum�N�[�WPPN��size�Mbitmap��size
			//�@��PPN��32��bit�A�@��bitmap��LPN��s�h�֭�sector
			//�H16KB page, 4KB sector�ӻ��A�N�O32bit+4bit
			sum += (32 + sectors_in_a_page);
		}
		//�p�G��LPN����0��sector�S�Q�g��flash memory�L
		else
		{
			//�N��@�L��PPN�u�ݭn1��bit�Ӫ��
			sum += 1;
		}

		//���ݧ�����LPN����0��sector
		//���ӤU�q��1��sector�}�l�̧Ǭd��
		for (int offset = 1; offset < sectors_in_a_page; ++offset)
		{
			//�ΨӰO���{�b��sector�Q�g�����PPN
			long long cur_PPN = it->second->PPN[offset];

			//�p�G��LPN����offset��sector���Q�g��flash memory�L
			if (cur_PPN != -1)
			{
				//�o��P�_�p�G�W��offset��sector�Q�g�쪺PPN��{�boffset��sector�Q�g�쪺PPN�O�_���P
				//�p�G���P�~�n�A�O���s��PPN�Mbitmap
				if (pre_PPN != cur_PPN)
				{
					sum += (32 + sectors_in_a_page);
				}
			}
			//�p�G��LPN����offset��sector�S�Q�g��flash memory�L
			else
			{
				//�N��@�L��PPN�u�ݭn1��bit�Ӫ��
				sum += 1;
			}
		}

		//�p�G�o�{��LPN������sector���Q�g��ۦP��PPN
		//���N���ݭn����bitmap�F
		if (sum == 32 + sectors_in_a_page)
		{
			sum = 32;
		}

		//��ثemappng table��size����{�b��LPN���e��entry size
		current_table_size -= it->second->size;

		//�M���s��LPN��entry size�����W���p��n��sum
		it->second->size = sum;

		//�̫�A��ثemappng table��size�[�W�{�b��LPN�s��entry size
		current_table_size += it->second->size;
	}
}

//Ū��mapping table�T�{��LPN�O�_���Q�g��flash memory�L
long long  Mapping_table::read(long long LPN, int offset)
{
	//�����LPN�Amapping table�W��entry
	unordered_map<long long, L2P_entry>::iterator  L2P_LPN = L2P.find(LPN);

	//�p�G��LPN�bmapping table�W�䤣��
	if (L2P_LPN == L2P.end())
	{
		//�^��-1��@�䤣��
		return -1;
	}
	//�p�G��LPN�bmapping table�W�����
	else
	{
		//�^�Ǧ�LPN����offset��sector�Q�g�쪺PPN
		return L2P_LPN->second.PPN[offset];
	}
}