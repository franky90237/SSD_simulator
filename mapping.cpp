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

//根據剛剛被寫入的page，還有這個page剛剛被寫入的block_id以及page_id，來更新mapping table
void Mapping_table::write(long long free_block_id, long long free_page_id, const Logical_page& page)
{
	//用hash map來找是否重複使用過相同的LPN
	//這次mapping table只有有LPN被更新過
	//就把此LPN的entry放進LPN_map裡面
	unordered_map<long long, L2P_entry*> LPN_map;

	//從此page的sector 0開始依序判斷
	for (int i = 0; i < page.sector_count; ++i)
	{
		//先計算此page的sector i的LPN和offset
		long long LPN = page.sector[i].sector_number / (page_size / sector_size);
		int offset = page.sector[i].sector_number % (page_size / sector_size);

		//抓取此LPN在mapping table上的資訊		
		unordered_map<long long, L2P_entry>::iterator  L2P_LPN = L2P.find(LPN);

		//如果在mapping table上找不到此LPN
		//代表此LPN是第一次被寫到flash memory裡面
		if (L2P_LPN == L2P.end())
		{
			//創建一個LPN在mapping table的entry
			L2P_entry l2p_entry;

			//從此entry的sector 0開始依序初始化
			for (int j = 0; j < sectors_in_a_page; ++j)
			{
				l2p_entry.PPN[j] = -1;
				l2p_entry.PPN_flag[j] = false;
				l2p_entry.PPN_bitmap[offset] = -1;
			}

			//設定此LPN的第offset個sector是被寫到哪個PPN
			l2p_entry.PPN[offset] = free_block_id * pages_in_a_block + free_page_id;

			//設定此LPN的第offset個sector是有被寫入過的
			l2p_entry.PPN_flag[offset] = true;

			//設定此LPN的第offset個sector是被寫到PPN的第i個sector
			l2p_entry.PPN_bitmap[offset] = i;

			//先設定此LPN的的entry size為0
			//最後會再計算真正的entry size
			l2p_entry.size = 0;

			//設定此LPN的的sector數量只有1個
			l2p_entry.sector_count = 1;

			//把此LPN的entry放到mapping table裡面
			L2P[LPN] = l2p_entry;

			//查hash map看此次更新mapping table
			//此LPN剛剛有沒有被更新過
			//如果沒有就把這個entry的位置先存起來
			//這個最後會用來計算此LPN的entry size為多少
			if (LPN_map.find(LPN) == LPN_map.end())
			{
				LPN_map[LPN] = &L2P[LPN];				
			}
		}
		//如果在mapping table上找的此LPN
		//代表此LPN是之前已經有被寫到flash memory裡面過
		else
		{
			//如果此LPN第offset個sector還沒有被寫過的，那就可以把此LPN的的sector數量遞增
			if (L2P_LPN->second.PPN[offset] != -1) ++L2P_LPN->second.sector_count;

			//設定此LPN的第offset個sector是被寫到哪個PPN
			L2P_LPN->second.PPN[offset] = free_block_id * pages_in_a_block + free_page_id;

			//設定此LPN的第offset個sector是有被寫入過的
			L2P_LPN->second.PPN_flag[offset] = true;

			//設定此LPN的第offset個sector是被寫到PPN的第i個sector
			L2P_LPN->second.PPN_bitmap[offset] = i;

			//查hash map看此次更新mapping table
			//此LPN剛剛有沒有被更新過
			//如果沒有就把這個entry的位置先存起來
			//這個最後會用來計算此LPN的entry size為多少
			if (LPN_map.find(LPN) == LPN_map.end())
			{
				LPN_map[LPN] = &(L2P_LPN->second);
			}
		}
	}

	//以上都是在依據LPN和offset去更新mapping table
	//接下來下面會開始計算此次更新過的LPN他們的entry在mapping table上的大小
	//然後會更新目前mapping table的總共size為多少
	
	//利用的LPN_map，開始一個一個去抓裡面LPN的entry
	for (auto it = LPN_map.begin(); it != LPN_map.end(); ++it)
	{
		//sum用來計算此LPN的entry的總共size
		int sum = 0;

		//用來看是不是此LPN裡面相鄰的sector是不是被寫到同個PPN
		//如果是的話，就只要存一個PPN就好
		long long pre_PPN = it->second->PPN[0];

		//如果此LPN的第0個sector有被寫到flash memory過
		if (pre_PPN != -1)
		{
			//sum就加上PPN的size和bitmap的size
			//一個PPN為32個bit，一個bitmap看LPN能存多少個sector
			//以16KB page, 4KB sector來說，就是32bit+4bit
			sum += (32 + sectors_in_a_page);
		}
		//如果此LPN的第0個sector沒被寫到flash memory過
		else
		{
			//就當作他的PPN只需要1個bit來表示
			sum += 1;
		}

		//剛剛看完此此LPN的第0個sector
		//接來下從第1個sector開始依序查看
		for (int offset = 1; offset < sectors_in_a_page; ++offset)
		{
			//用來記錄現在的sector被寫到哪個PPN
			long long cur_PPN = it->second->PPN[offset];

			//如果此LPN的第offset個sector有被寫到flash memory過
			if (cur_PPN != -1)
			{
				//這邊判斷如果上個offset的sector被寫到的PPN跟現在offset的sector被寫到的PPN是否不同
				//如果不同才要再記錄新的PPN和bitmap
				if (pre_PPN != cur_PPN)
				{
					sum += (32 + sectors_in_a_page);
				}
			}
			//如果此LPN的第offset個sector沒被寫到flash memory過
			else
			{
				//就當作他的PPN只需要1個bit來表示
				sum += 1;
			}
		}

		//如果發現此LPN的全部sector都被寫到相同的PPN
		//那就不需要紀錄bitmap了
		if (sum == 32 + sectors_in_a_page)
		{
			sum = 32;
		}

		//把目前mappng table的size先減掉現在此LPN之前的entry size
		current_table_size -= it->second->size;

		//然後更新此LPN的entry size為剛剛上面計算好的sum
		it->second->size = sum;

		//最後再把目前mappng table的size加上現在此LPN新的entry size
		current_table_size += it->second->size;
	}
}

//讀取mapping table確認此LPN是否有被寫到flash memory過
long long  Mapping_table::read(long long LPN, int offset)
{
	//抓取此LPN再mapping table上的entry
	unordered_map<long long, L2P_entry>::iterator  L2P_LPN = L2P.find(LPN);

	//如果此LPN在mapping table上找不到
	if (L2P_LPN == L2P.end())
	{
		//回傳-1當作找不到
		return -1;
	}
	//如果此LPN在mapping table上有找到
	else
	{
		//回傳此LPN的第offset個sector被寫到的PPN
		return L2P_LPN->second.PPN[offset];
	}
}