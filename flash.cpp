#include <iostream>
#include <vector>
#include <string>
#include <map>
#include <unordered_map>
#include "SSD_parameter.h"
#include "buffer.h"
#include "mapping.h"
#include "flash.h"

using std::cout;		using std::cerr;
using std::endl;		using std::string;
using std::ifstream;	using std::vector;
using std::map;			using std::ofstream;
using std::stoi;		using std::unordered_map;

enum STATUS { EMPTY, FREE, ACTIVE, VALID, INVALID, PARTIAL_INVALID, FULL };
enum sector_TYPE { READ, WRITE, sEMPTY };

//flash memory��l��
void Flash_memory::configuration()
{
	invalid_block_count = 0;
	active_block_pointer = 0;
	max_allocated_block_id = 0;	
	free_block_count = block_number;

	rmw_read_count = 0;
	read_count = 0;
	write_count = 0;
	erase_count = 0;
	migration_count = 0;

	gc_threshold = GC_threshold;
}

void Flash_memory::allocate_a_block()
{
	//�t�m�@�ӪŪ�PPN
	Physical_page empty_PPN;
	for (int i = 0; i < sectors_in_a_page; ++i)
	{		
		empty_PPN.sector_number[i] = -1;
		empty_PPN.valid_sector[i] = false;
	}	
	empty_PPN.invalid_count = 0;
	empty_PPN.free_count = sectors_in_a_page; //4
	empty_PPN.sector_count = 0;
	empty_PPN.status = FREE;

	//�t�m�@�ӪŪ�block
	Block empty_block;
	for (int i = 0; i < pages_in_a_block; ++i)
	{
		empty_block.PPN[i] = empty_PPN;
	}
	empty_block.free_page_pointer = 0;
	empty_block.invalid_count = 0;
	empty_block.status = EMPTY;

	//���t�m�Ū�block��used_block_list(active_block_list)
	//�N��block���ثe���b�g�J��block
	used_block_list[active_block_pointer] = empty_block;

	//�`�@�i�ϥΪ�block�Ӽƴ�1
	--free_block_count;
}

//�C��buffer��X�Ӫ�page�n�g�Jflash memory��
//�N�|�I�s��function�A�̫�^�ǭn�g�J������block_id
long long Flash_memory::allocation()
{
	//�p�G�O�̭�}�l�Ĥ@���g�Jflash
	//�N�N��S������blcok�Qallocate�L
	if (used_block_list.size() == 0)
	{
		//�t�m�@�ӷs��block
		allocate_a_block();

		//�^��free_block_id
		return active_block_pointer;
	}


	//�qused_block_list��ثe���b�g�J��block(active block)��X��
	unordered_map<long long, Block>::iterator used_block_list__active_block_pointer = used_block_list.find(active_block_pointer);

	//�p�G�{�b���b�g�J��block���Ŷ��A���N�����^�Ǧ�block��id
	if (used_block_list__active_block_pointer->second.free_page_pointer <= pages_in_a_block - 1)
	{
		return active_block_pointer;
	}
	//�p�G�{�b���b�g�J��block�W�@���w�g�Q�g��
	else
	{
		//�p�Grecycled_block_list���Ū�block
		//�N�����LGC�^���Lblock
		if (recycled_block_list.size() > 0)
		{
			//��recycled_block_list�̪��Ĥ@��block��@���@�U�n�^�Ǫ�block_id
			active_block_pointer = recycled_block_list.begin()->first;

			//��qrecycled_block_list��쪺block�ಾ��used_block_list
			used_block_list[active_block_pointer] = recycled_block_list.begin()->second;

			//�M��A��recycled_block_list�̭���診��block�M��
			recycled_block_list.erase(active_block_pointer);

			//�i�Ϊ�free_block�ƶq����
			--free_block_count;

			//�^�ǰt�m��block_id
			return active_block_pointer;
		}
		//�p�Grecycled_block_list�S�F��A�ӥB�{�b�W�@���g�J��block�S��n�Q�g��
		//�N�N��n�t�m�@�ӷs��block
		else
		{
			//�t�m�s��block�N�O��max_allocated_block_id���W
			//�]��block�O�̧ǥѤp��j�t�m
			//�Ҧp�G���]�쥻���b�gblock_id=5��block
			//��block_id=5��block�g���F����A�N�|�t�mblock_id=6��blcok
			++max_allocated_block_id;

			//�A��active_block_pointer�]�w�����t�m��block_id
			active_block_pointer = max_allocated_block_id;

			//�t�m�@���s���Ū�block
			allocate_a_block();

			//�^�ǭ��t�m��block_id
			return active_block_pointer;
		}
	}
}

//�P�_flash memory�O�_�i�ΪŶ�����
bool Flash_memory::full()
{
	/*if (free_block_count<=1)
	{
		return true;
	}
	else
	{
		return false;
	}*/

	//return false;

	//�����٥i�Q�t�m��block�ƶq���`�@�i�t�mblock�ƶq����v
	//�M��A��1�h��o�Ӥ�v�A�o��block�ϥΪ�����
	//�̫�h�P�_�O�_�W�LGC_threshold�H�W
	if ((1 - (double)free_block_count / block_number) >= GC_threshold)
	{
		return true;
	}
	else
	{
		return false;
	}
}

//��t�m����block�g�Jvictim_page
void Flash_memory::write(long long free_block_id, const Logical_page& victim_page)
{
	//����{�b�n�g��block
	unordered_map<long long, Block>::iterator free_block = used_block_list.find(free_block_id);

	//�����block���U�@�ӪŪ�PPN(free_PPN_id)
	long long free_PPN_id = free_block->second.free_page_pointer;

	//�M��A�����PPN
	Physical_page& free_PPN = free_block->second.PPN[free_PPN_id];
	
	//�q��PPN��offset 0�}�l�̧Ǽg�J
	for (int i = 0; i < victim_page.sector_count; ++i)
	{		
		//����p��victim_page��offset i�g�J��free_PPN��offset i
		free_PPN.sector_number[i] = victim_page.sector[i].sector_number;

		//�⦹PPN��offset i��sector���A�аO��valid
		free_PPN.valid_sector[i] = true;

		//�⦹PPN���Ū�sector�Ӽƻ���
		--free_PPN.free_count;
	}

	//�⦹PPN�����A�令valid
	free_PPN.status = VALID;

	//�⦹block�̭���free_page_pointer���W
	++free_block->second.free_page_pointer;

	//�p�G��block��PPN��n�����Q�g��(���׬Ovalid�άOinvalid)
	//���N�⦹block���invalid_block_list
	//�N��block���ᰵGC�|�Q�����n�^����block
	if (free_block->second.free_page_pointer >= pages_in_a_block)
	{
		//�p�⦹block������invalid��PPN�ƶq
		int invalid_count = free_block->second.invalid_count;

		//�̾ڦ�block��invalid��PPN�ƶq�A�⦹block��������invalid_block_list
		invalid_block_list[invalid_count][free_block->first] = free_block;

		//�⦹block�����A�令FULL
		//�N��block�w�Q�g��
		free_block->second.status = FULL;
	}
	//�p�G��block��PPN�S���Q�g��
	//�]�N�O����block�̭��٦��Ū�page
	else
	{
		//�⦹block�����A�令ACTIVE
		//�N��block�w�Q�ثe�٥��b�Q�g�J��
		free_block->second.status = ACTIVE;
	}

	//��flash memory�Qwrite�����ƻ��W
	++flash_memory.write_count;
}

//�d�ݭ��g�Jflash memory��victim page�O�_���ݭn��update��sector
void Flash_memory::update(const Logical_page& victim_page)
{
	//�q��victim_page��offset 0�}�l�̧ǧP�_�ϧ_��sector�n���ª����invalid��
	for (int i = 0; i < victim_page.sector_count; ++i)
	{
		//�p�⦹victim_page��offset i�O����LPN
		long long LPN_number = victim_page.sector[i].sector_number / (page_size / sector_size);

		//�p��victim_page��offset i�O��LPN���ĴX��sector
		int offset = victim_page.sector[i].sector_number % (page_size / sector_size);

		//�dmapping table�̭���LPN����T
		unordered_map<long long, L2P_entry>::iterator  L2P_LPN = mapping_table.L2P.find(LPN_number);

		//�p�Gmapping table�̭��䤣�즹LPN
		//�N��LPN�٨S�Q�W�@���S�Q�g��flash memory
		//�ҥH�]���ݭn��update
		if (L2P_LPN == mapping_table.L2P.end())
		{
			continue;
		}

		//�ھڦ�LPN��offset�qmapping table�W�����offset�Q�g�쪺PPN
		long long PPN_number = L2P_LPN->second.PPN[offset];

		//�p�G��LPN��offset���Q�g�J�Lflash memoty�̭�
		//�N�i�H�h���ª���Ƶ�invalid��
		if (PPN_number != -1)
		{
			//�ھڭ��o�쪺PPN�p��X�Q�g�쪺block�H��block�̭��ĴX��page
			long long block_id = PPN_number / pages_in_a_block;
			long long page_id = PPN_number % pages_in_a_block;

			//�Q�έ��o�쪺block_id�h�����block
			unordered_map<long long, Block>::iterator used_block_list__block_id = used_block_list.find(block_id);
			Block& used_block = used_block_list__block_id->second;

			//�����LPN��offset�Q�g��PPN�����Ӧ�m(bitmap)
			//�Ҧp�G�p�Gbitmap��0�A�N�N��Q�g��PPN��offset 0�A�̦�����
			int PPN_offset = L2P_LPN->second.PPN_bitmap[offset];

			//�⦹block��PPN��offset��invlalid��
			used_block.PPN[page_id].valid_sector[PPN_offset] = false;

			//�⦹PPN��invalid sector�ƶq���W
			++used_block.PPN[page_id].invalid_count;

			//�p�G�o����s�A��blcok��PPN���ܦ�invalid
			//�]�N�ݦ�block��invalid sector�ӼƬO�_����L�`�@�Q�g�쪺sector�Ӽ�
			//�N��i��ݭn�⦹block��s��s��invalid_block_list(��GC�Ϊ�list)
			if (used_block.PPN[page_id].invalid_count == sectors_in_a_page - used_block.PPN[page_id].free_count)
			{
				//����惡block��PPN���A��invalid
				used_block.PPN[page_id].status = INVALID;

				//�A�⦹blcok��invlalid page�ƶq���W
				++used_block.invalid_count;

				//�p�G��block��������PPN���ܦ�invlalid
				if (used_block.invalid_count >= pages_in_a_block)
				{
					///��惡block���A��invalid
					used_block.status = INVALID;
				}

				//�p�G��block���O���b�Q�g�oblock
				//�]�N�O��block�w�g�S��free page�F
				//���N�N��n��s��block��O��invlalid_block_list
				if (used_block.status != ACTIVE)
				{
					//���p�⦹block��invalid PPN�ƶq
					int invalid_count = used_block.invalid_count;					

					//�b�W�@��invalid_block_list�����block
					//�Ҧp�G���]����update����block��invlid PPN�ƶq�q5���W�ܦ�6
					//���N�|�⦹block�qinvalid_block_list[5]����
					//�M��A�⦹block��Jinvalid_block_list[6]�̭�
					unordered_map <long long, unordered_map <long long, Block>::iterator>::iterator block_find = invalid_block_list[invalid_count - 1].find(block_id);

					//�o����T�{��block���S���s�binvalid_block_list[invalid_count - 1]�̭�
					//�p�G���N���⥦������
					if (block_find != invalid_block_list[invalid_count - 1].end())
					{
						invalid_block_list[invalid_count - 1].erase(block_find);
					}

					//�̫�A�⦹block��J��s��invalid_block_list�̭�
					invalid_block_list[invalid_count][block_id] = used_block_list__block_id;
				}
			}
			//�p�G�o����s��blcok��PPN�S���ܦ�invalid
			//�N���ݭn��s��block��O��invlalid_block_list
			else
			{
				//��惡block��PPN���A��partial invalid
				//�o�Ӫ��A���Τ���
				used_block.PPN[page_id].status = PARTIAL_INVALID;
			}
		}
	}
}

//�B�zflasg memoey��read
void Flash_memory::read(long long LPN, int offsetd)
{
	//flasg memoey��read��Ū�����ƻ��W
	++flash_memory.read_count;
}

//erase flash memory�̭����Y��block�A�t�XGC�ϥ�
void Flash_memory::erase(unordered_map<long long, Block>::iterator& victim_block)
{
	//���o�즹block��id
	long long block_id = victim_block->first;

	//�M��⦹block�qused_block_list�̭�������
	used_block_list.erase(victim_block);

	//�t�m�@�ӪŪ�block
	Block empty_block;
	create_empty_block(empty_block);

	//����R������block_id�A���recycled_block_list
	//�N��block�w�g�Q�M�šA�ӥB����i�H�Qallocate�ϥ�
	recycled_block_list[block_id] = empty_block;

	//free block�Ӽƻ��W
	++free_block_count;

	//flash memory��erase���ƻ��W
	++flash_memory.erase_count;
}

//��falsh memory�Ŷ������ɡA�N�|�ϥΦ��禡�Ӱ�GC
void Flash_memory::gc()
{
	//�q�@��block�̭��̦h�i�ಣ�ͪ�invalid PPN�ƶq�}�l�M��nerase��block
	//�Ҧp�G���]1��block�i�H��128��PPN�A���N�|�qinvalid_block_list[128]�}�l�M��
	//�p�Ginvalid_block_list[128]�̭����S��block�A���N���U��invalid_block_list[127]
	//�̦������A�p�G�e�����䤣��A���̫�|���invalid_block_list[0]
	for (int invalid_region = pages_in_a_block; invalid_region >= 0; --invalid_region)
	{
		//�p�G��invalid_block_list[invalid_region]��block�s�b
		//�N��i�H�i�H�⦹block erase���A�M�X�Ŷ�
		if (invalid_block_list[invalid_region].size() > 0)
		{
			//�]��invalid_block_list[invalid_region]�̭��i��|���ܦh��block�s�b���ϰ�
			//�ҥH�ڭ̪���������ϰ쪺�Ĥ@��block�Ӱ�erase
			unordered_map<long long, Block>::iterator victim_block = invalid_block_list[invalid_region].begin()->second;

			//�T�{��block�̭����C��PPN
			//�p�G���Oinvalid���A���N�ݭn���h��(migration)			
			for (int i = 0; i < pages_in_a_block; ++i)
			{
				//�p�G��block�O�̭�������PPN���Oinvalid
				//�N�N���ΰ�����migration
				if (victim_block->second.status == INVALID)
				{
					break;
				}

				//�����block����i��PPN									
				Physical_page& victim_page = victim_block->second.PPN[i];

				//�p�G��PPN�����A���Oinvalid
				//�N�N��PPN�ݭn��migration
				if (victim_page.status != INVALID)
				{
					//�Ыؤ@�ӷs��page
					//�D�n�O�n�����쪺PPN
					//�ƻs�즹�s��page
					int index = 0;
					Logical_page new_page;
					new_page.sector_count = 0;

					//�q��page��offset 0�}�l�̧Ǽg�J
					for (int offset = 0; offset < sectors_in_a_page; ++offset)
					{
						//�p�G�nmigration��PPN����offset��secotr�Ovalid��
						//���N�i�H�⦹sector����T���ƻs��輲����page�̭�
						if (victim_page.valid_sector[offset] == true)
						{
							new_page.sector[new_page.sector_count].sector_number = victim_page.sector_number[offset];
							new_page.sector_flag[new_page.sector_count] = victim_page.valid_sector[offset];
							++new_page.sector_count;
							++index;
						}
					}

					//�o��O�ݦp�G��Ыت�page�٦��Ū�sector��
					//�A��Ѿl��sector����l��
					while (index < sectors_in_a_page)
					{
						new_page.sector[index].sector_number = -1;
						new_page.sector_flag[index] = false;
						++index;
					}

					//�̫�⦹��Ыت�page�����A�令dirty
					new_page.status = WRITE;

					//�e���Ыت�page���U�ӭn�⥦�g��flash memory�̭�
					//�ҥH�N����allocate�o��n�g�J��block_id�Mpage_id
					long long free_block_id = allocation();
					long long free_page_id = used_block_list[free_block_id].free_page_pointer;

					//�M��}��Ыت�page�g�J��۹�����flash memory��block�̭�
					write(free_block_id, new_page);

					//�̫�A��smapping table
					mapping_table.write(free_block_id, free_page_id, new_page);

					//�M���migration���ƻ��W
					//1��migration�N����1��write�[�W1��read
					++migration_count;

					//�o��n��flash memory��write���ƻ���
					//�]��write(free_block_id, new_page)�|��flash memory��write���ƻ��W
					//���O�o�����g�J�n�k����migration�̭�
					--write_count;
				}
			}

			//��invalid_block_list[invalid_region]�̭�����block��������
			invalid_block_list[invalid_region].erase(victim_block->first);

			//�̫�A�⦹block��erase��
			erase(victim_block);

			//�M����X�j��
			break;
		}
	}
}

//�Ыؤ@�ӪŪ�block�A�|�Aerase�ɳQ�I�s��
void create_empty_block(Block& empty_block)
{
	Physical_page empty_PPN;
	for (int i = 0; i < sectors_in_a_page; ++i)
	{		
		empty_PPN.sector_number[i] = -1;
		empty_PPN.valid_sector[i] = false;
	}	
	empty_PPN.invalid_count = 0;
	empty_PPN.free_count = sectors_in_a_page;
	empty_PPN.sector_count = 0;
	empty_PPN.status = FREE;

	//�t�m�@�ӪŪ�block	
	for (int i = 0; i < pages_in_a_block; ++i)
	{
		empty_block.PPN[i] = empty_PPN;
	}
	empty_block.free_page_pointer = 0;
	empty_block.invalid_count = 0;
	empty_block.status = EMPTY;
}