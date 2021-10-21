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

//flash memory初始化
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
	//配置一個空的PPN
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

	//配置一個空的block
	Block empty_block;
	for (int i = 0; i < pages_in_a_block; ++i)
	{
		empty_block.PPN[i] = empty_PPN;
	}
	empty_block.free_page_pointer = 0;
	empty_block.invalid_count = 0;
	empty_block.status = EMPTY;

	//把剛配置空的block給used_block_list(active_block_list)
	//代表此block為目前正在寫入的block
	used_block_list[active_block_pointer] = empty_block;

	//總共可使用的block個數減1
	--free_block_count;
}

//每次buffer丟出來的page要寫入flash memory時
//就會呼叫此function，最後回傳要寫入的實體block_id
long long Flash_memory::allocation()
{
	//如果是最剛開始第一次寫入flash
	//就代表沒有任何blcok被allocate過
	if (used_block_list.size() == 0)
	{
		//配置一個新的block
		allocate_a_block();

		//回傳free_block_id
		return active_block_pointer;
	}


	//從used_block_list把目前正在寫入的block(active block)抓出來
	unordered_map<long long, Block>::iterator used_block_list__active_block_pointer = used_block_list.find(active_block_pointer);

	//如果現在正在寫入的block有空間，那就直接回傳此block的id
	if (used_block_list__active_block_pointer->second.free_page_pointer <= pages_in_a_block - 1)
	{
		return active_block_pointer;
	}
	//如果現在正在寫入的block上一次已經被寫滿
	else
	{
		//如果recycled_block_list有空的block
		//代表有做過GC回收過block
		if (recycled_block_list.size() > 0)
		{
			//把recycled_block_list裡的第一個block當作等一下要回傳的block_id
			active_block_pointer = recycled_block_list.begin()->first;

			//把從recycled_block_list抓到的block轉移給used_block_list
			used_block_list[active_block_pointer] = recycled_block_list.begin()->second;

			//然後再把recycled_block_list裡面剛剛那個block清掉
			recycled_block_list.erase(active_block_pointer);

			//可用的free_block數量遞減
			--free_block_count;

			//回傳配置的block_id
			return active_block_pointer;
		}
		//如果recycled_block_list沒東西，而且現在上一次寫入的block又剛好被寫滿
		//就代表要配置一個新的block
		else
		{
			//配置新的block就是把max_allocated_block_id遞增
			//因為block是依序由小到大配置
			//例如：假設原本正在寫block_id=5的block
			//當block_id=5的block寫滿了之後，就會配置block_id=6的blcok
			++max_allocated_block_id;

			//再把active_block_pointer設定成剛剛配置的block_id
			active_block_pointer = max_allocated_block_id;

			//配置一塊新的空的block
			allocate_a_block();

			//回傳剛剛配置個block_id
			return active_block_pointer;
		}
	}
}

//判斷flash memory是否可用空間不足
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

	//先算還可被配置的block數量佔總共可配置block數量的比率
	//然後再用1去減這個比率，得到block使用的佔比
	//最後去判斷是否超過GC_threshold以上
	if ((1 - (double)free_block_count / block_number) >= GC_threshold)
	{
		return true;
	}
	else
	{
		return false;
	}
}

//對配置完的block寫入victim_page
void Flash_memory::write(long long free_block_id, const Logical_page& victim_page)
{
	//抓取現在要寫的block
	unordered_map<long long, Block>::iterator free_block = used_block_list.find(free_block_id);

	//抓取此block的下一個空的PPN(free_PPN_id)
	long long free_PPN_id = free_block->second.free_page_pointer;

	//然後再抓取此PPN
	Physical_page& free_PPN = free_block->second.PPN[free_PPN_id];
	
	//從此PPN的offset 0開始依序寫入
	for (int i = 0; i < victim_page.sector_count; ++i)
	{		
		//把先計算victim_page的offset i寫入到free_PPN的offset i
		free_PPN.sector_number[i] = victim_page.sector[i].sector_number;

		//把此PPN的offset i的sector狀態標記為valid
		free_PPN.valid_sector[i] = true;

		//把此PPN的空的sector個數遞減
		--free_PPN.free_count;
	}

	//把此PPN的狀態改成valid
	free_PPN.status = VALID;

	//把此block裡面的free_page_pointer遞增
	++free_block->second.free_page_pointer;

	//如果此block的PPN剛好全部被寫滿(不論是valid或是invalid)
	//那就把此block放到invalid_block_list
	//代表此block之後做GC會被視為要回收的block
	if (free_block->second.free_page_pointer >= pages_in_a_block)
	{
		//計算此block的完全invalid的PPN數量
		int invalid_count = free_block->second.invalid_count;

		//依據此block的invalid的PPN數量，把此block放到相應的invalid_block_list
		invalid_block_list[invalid_count][free_block->first] = free_block;

		//把此block的狀態改成FULL
		//代表此block已被寫滿
		free_block->second.status = FULL;
	}
	//如果此block的PPN沒有被寫滿
	//也就是說此block裡面還有空的page
	else
	{
		//把此block的狀態改成ACTIVE
		//代表此block已被目前還正在被寫入中
		free_block->second.status = ACTIVE;
	}

	//把flash memory被write的次數遞增
	++flash_memory.write_count;
}

//查看剛剛寫入flash memory的victim page是否有需要做update的sector
void Flash_memory::update(const Logical_page& victim_page)
{
	//從此victim_page的offset 0開始依序判斷使否此sector要把舊的資料invalid掉
	for (int i = 0; i < victim_page.sector_count; ++i)
	{
		//計算此victim_page的offset i是哪個LPN
		long long LPN_number = victim_page.sector[i].sector_number / (page_size / sector_size);

		//計算victim_page的offset i是此LPN的第幾個sector
		int offset = victim_page.sector[i].sector_number % (page_size / sector_size);

		//查mapping table裡面此LPN的資訊
		unordered_map<long long, L2P_entry>::iterator  L2P_LPN = mapping_table.L2P.find(LPN_number);

		//如果mapping table裡面找不到此LPN
		//代表此LPN還沒被上一次沒被寫到flash memory
		//所以也不需要做update
		if (L2P_LPN == mapping_table.L2P.end())
		{
			continue;
		}

		//根據此LPN的offset從mapping table上抓取此offset被寫到的PPN
		long long PPN_number = L2P_LPN->second.PPN[offset];

		//如果此LPN的offset有被寫入過flash memoty裡面
		//就可以去把舊的資料給invalid掉
		if (PPN_number != -1)
		{
			//根據剛剛得到的PPN計算出被寫到的block以及block裡面第幾個page
			long long block_id = PPN_number / pages_in_a_block;
			long long page_id = PPN_number % pages_in_a_block;

			//利用剛剛得到的block_id去抓取此block
			unordered_map<long long, Block>::iterator used_block_list__block_id = used_block_list.find(block_id);
			Block& used_block = used_block_list__block_id->second;

			//抓取此LPN的offset被寫到PPN的哪個位置(bitmap)
			//例如：如果bitmap為0，就代表被寫到PPN的offset 0，依此類推
			int PPN_offset = L2P_LPN->second.PPN_bitmap[offset];

			//把此block的PPN的offset給invlalid掉
			used_block.PPN[page_id].valid_sector[PPN_offset] = false;

			//把此PPN的invalid sector數量遞增
			++used_block.PPN[page_id].invalid_count;

			//如果這次更新，此blcok的PPN有變成invalid
			//也就看此block的invalid sector個數是否等於他總共被寫到的sector個數
			//代表可能需要把此block更新到新的invalid_block_list(做GC用的list)
			if (used_block.PPN[page_id].invalid_count == sectors_in_a_page - used_block.PPN[page_id].free_count)
			{
				//先更改此block的PPN狀態為invalid
				used_block.PPN[page_id].status = INVALID;

				//再把此blcok的invlalid page數量遞增
				++used_block.invalid_count;

				//如果此block的全部的PPN都變成invlalid
				if (used_block.invalid_count >= pages_in_a_block)
				{
					///更改此block狀態為invalid
					used_block.status = INVALID;
				}

				//如果此block不是正在被寫得block
				//也就是此block已經沒有free page了
				//那就代表要更新此block到別的invlalid_block_list
				if (used_block.status != ACTIVE)
				{
					//先計算此block的invalid PPN數量
					int invalid_count = used_block.invalid_count;					

					//在上一個invalid_block_list抓取此block
					//例如：假設此次update讓此block的invlid PPN數量從5遞增變成6
					//那就會把此block從invalid_block_list[5]移除
					//然後再把此block放入invalid_block_list[6]裡面
					unordered_map <long long, unordered_map <long long, Block>::iterator>::iterator block_find = invalid_block_list[invalid_count - 1].find(block_id);

					//這邊先確認此block有沒有存在invalid_block_list[invalid_count - 1]裡面
					//如果有就先把它移除掉
					if (block_find != invalid_block_list[invalid_count - 1].end())
					{
						invalid_block_list[invalid_count - 1].erase(block_find);
					}

					//最後再把此block放入到新的invalid_block_list裡面
					invalid_block_list[invalid_count][block_id] = used_block_list__block_id;
				}
			}
			//如果這次更新此blcok的PPN沒有變成invalid
			//代表不需要更新此block到別的invlalid_block_list
			else
			{
				//更改此block的PPN狀態為partial invalid
				//這個狀態其實用不到
				used_block.PPN[page_id].status = PARTIAL_INVALID;
			}
		}
	}
}

//處理flasg memoey的read
void Flash_memory::read(long long LPN, int offsetd)
{
	//flasg memoey的read的讀取次數遞增
	++flash_memory.read_count;
}

//erase flash memory裡面的某個block，配合GC使用
void Flash_memory::erase(unordered_map<long long, Block>::iterator& victim_block)
{
	//先得到此block的id
	long long block_id = victim_block->first;

	//然後把此block從used_block_list裡面移除掉
	used_block_list.erase(victim_block);

	//配置一個空的block
	Block empty_block;
	create_empty_block(empty_block);

	//把剛剛刪除掉的block_id，放到recycled_block_list
	//代表此block已經被清空，而且之後可以被allocate使用
	recycled_block_list[block_id] = empty_block;

	//free block個數遞增
	++free_block_count;

	//flash memory的erase次數遞增
	++flash_memory.erase_count;
}

//當falsh memory空間不足時，就會使用此函式來做GC
void Flash_memory::gc()
{
	//從一個block裡面最多可能產生的invalid PPN數量開始尋找要erase的block
	//例如：假設1個block可以有128個PPN，那就會從invalid_block_list[128]開始尋找
	//如果invalid_block_list[128]裡面都沒有block，那就往下找invalid_block_list[127]
	//依此類推，如果前面都找不到，那最後會找到invalid_block_list[0]
	for (int invalid_region = pages_in_a_block; invalid_region >= 0; --invalid_region)
	{
		//如果此invalid_block_list[invalid_region]有block存在
		//代表可以可以把此block erase掉，清出空間
		if (invalid_block_list[invalid_region].size() > 0)
		{
			//因為invalid_block_list[invalid_region]裡面可能會有很多個block存在此區域
			//所以我們直接抓取此區域的第一個block來做erase
			unordered_map<long long, Block>::iterator victim_block = invalid_block_list[invalid_region].begin()->second;

			//確認此block裡面的每個PPN
			//如果不是invalid狀態的就需要做搬移(migration)			
			for (int i = 0; i < pages_in_a_block; ++i)
			{
				//如果此block是裡面全部的PPN都是invalid
				//就代表不用做任何的migration
				if (victim_block->second.status == INVALID)
				{
					break;
				}

				//抓取此block的第i個PPN									
				Physical_page& victim_page = victim_block->second.PPN[i];

				//如果此PPN的狀態不是invalid
				//就代表此PPN需要做migration
				if (victim_page.status != INVALID)
				{
					//創建一個新的page
					//主要是要把剛剛抓到的PPN
					//複製到此新的page
					int index = 0;
					Logical_page new_page;
					new_page.sector_count = 0;

					//從此page的offset 0開始依序寫入
					for (int offset = 0; offset < sectors_in_a_page; ++offset)
					{
						//如果要migration的PPN的第offset個secotr是valid的
						//那就可以把此sector的資訊給複製到剛撞見的page裡面
						if (victim_page.valid_sector[offset] == true)
						{
							new_page.sector[new_page.sector_count].sector_number = victim_page.sector_number[offset];
							new_page.sector_flag[new_page.sector_count] = victim_page.valid_sector[offset];
							++new_page.sector_count;
							++index;
						}
					}

					//這邊是看如果剛創建的page還有空的sector時
					//再把剩餘的sector給初始化
					while (index < sectors_in_a_page)
					{
						new_page.sector[index].sector_number = -1;
						new_page.sector_flag[index] = false;
						++index;
					}

					//最後把此剛創建的page的狀態改成dirty
					new_page.status = WRITE;

					//前面創建的page接下來要把它寫到flash memory裡面
					//所以就先做allocate得到要寫入的block_id和page_id
					long long free_block_id = allocation();
					long long free_page_id = used_block_list[free_block_id].free_page_pointer;

					//然後開剛創建的page寫入到相對應的flash memory的block裡面
					write(free_block_id, new_page);

					//最後再更新mapping table
					mapping_table.write(free_block_id, free_page_id, new_page);

					//然後把migration次數遞增
					//1個migration就等於1個write加上1個read
					++migration_count;

					//這邊要把flash memory的write次數遞減
					//因為write(free_block_id, new_page)會讓flash memory的write次數遞增
					//但是這次的寫入要歸類到migration裡面
					--write_count;
				}
			}

			//把invalid_block_list[invalid_region]裡面的此block給移除掉
			invalid_block_list[invalid_region].erase(victim_block->first);

			//最後再把此block給erase掉
			erase(victim_block);

			//然後跳出迴圈
			break;
		}
	}
}

//創建一個空的block，會再erase時被呼叫到
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

	//配置一個空的block	
	for (int i = 0; i < pages_in_a_block; ++i)
	{
		empty_block.PPN[i] = empty_PPN;
	}
	empty_block.free_page_pointer = 0;
	empty_block.invalid_count = 0;
	empty_block.status = EMPTY;
}