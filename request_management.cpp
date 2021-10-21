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

enum request_item { tASU, tLBA, tSIZE, tTYPE, tTIME };
enum sector_TYPE { READ, WRITE, EMPTY, NOT_EXIST_READ };

int SIZE_less_zero_count = 0;

void Request_management(string token[])
{        
    long long LSN_start, LSN_end;
    int sector512_count;
    long long LBA = stoll(token[tLBA]);
    int SIZE = stoi(token[tSIZE]);
    int TYPE = (token[tTYPE] == "r") ? READ : WRITE;        

    if (SIZE <= 0)
    {
        ++SIZE_less_zero_count;
        return;
    }	
    
    //計算起始LSN
    //因為這邊request是以512B為單位，然後我們sector是以4KB為單位
    //所以把LBA除以8得到對應的LSN
    LSN_start = LBA / (sector_size / 512);        

    //計算此request的size要寫幾個LBA，這邊是以512B為單位計算
    //計算完要減1，沒減的話會多寫一個LSN
    sector512_count = ceil(SIZE / 512.0) - 1;

    //計算結束LSN    
    LSN_end = (LBA + sector512_count) / (sector_size / 512);     

    //計算request的SIZE：總共寫幾個LSN再乘上sector的size
    SIZE = ((LSN_end - LSN_start) + 1) * sector_size;

    //如果發現起始LSN大於結束LSN，那就代表有問題
    if (LSN_start > LSN_end)
    {
        printf("error : LSN_start > LSN_end \n");
        system("pause");
        exit(0);
    }

    long long LPN_start = LSN_start / (page_size / sector_size);
    long long LPN_end = LSN_end / (page_size / sector_size);
    
    /*for (long long LSN_now = LSN_start; LSN_now <= LSN_end; ++LSN_now)
    {
        long long LPN = LSN_now / (page_size / sector_size);
        int offset = LSN_now % (page_size / sector_size);        

        //如果此LPN存在buffer裡面
        unordered_map<long long, Logical_page>::iterator buffer_LPN = buffer.buffer.find(LPN);
        if (buffer_LPN != buffer.buffer.end())
        {
            //如果此LPN的sector存在buffer裡面
            if (buffer_LPN->second.sector_flag[offset] == true)
            {
                SIZE = SIZE - sector_size;
                ++SIZE_lost_count;
                continue;
            }
        }
    }*/

    //判斷buffer是否full   
    if (buffer.full(SIZE))
    {
        buffer_full_handler(SIZE);
    }
    
    //處理read request
    if (TYPE == READ)
    {
        buffer.read(LSN_start, LSN_end);
    }
    //處理write request
    else
    {
        buffer.write(LSN_start, LSN_end);
    }
}

void victim_page_list_write(vector<Logical_page>& victim_page_list)
{
    for (const auto& victim_page : victim_page_list)
    {
        //確認是否要做gc
        if (flash_memory.full())
        {
            flash_memory.gc();
        }

        long long free_block_id = flash_memory.allocation();
        long long free_page_id = flash_memory.used_block_list[free_block_id].free_page_pointer;		

        flash_memory.write(free_block_id, victim_page);		
        flash_memory.update(victim_page);
        mapping_table.write(free_block_id, free_page_id, victim_page);
    }
}

void buffer_full_handler(int SIZE)
{   
    //當buffer full時，儲存從buffer選出來的victim LPNs
    vector<Logical_page> victim_LPN_list;

    //把victim LPNs分成四種類型
    vector<Logical_page> hotRead_hotWrite_list, hotRead_coldWrite_list, coldRead_hotWrite_list, coldRead_coldWrite_list;

    //如果vicrim LPNs是hot read，就要對他們做compact LPN
    vector<Logical_page> rmw_hotRead_hotWrite_list, rmw_hotRead_coldWrite_list;

    //先讓四種類型各自做mixed PPN
    vector<Logical_page> mixed_hotRead_hotWrite_list, mixed_hotRead_coldWrite_list, mixed_coldRead_hotWrite_list, mixed_coldRead_coldWrite_list;    
    
    //最後再看同為hot write或者同為cold write類型的是否可以再進一步做mixed PPN
    vector<Logical_page> mixed_hot_list, mixed_cold_list;

    //從buffer挑選出victim LPNs，然後存入victim_LPN_list
    buffer.evict(victim_LPN_list, SIZE);

    //對victim_LPN_list坐四種類型的分類
    buffer.dynamic_adaption(victim_LPN_list, hotRead_hotWrite_list, hotRead_coldWrite_list, coldRead_hotWrite_list, coldRead_coldWrite_list);    

    //如果是hot read的LPNs，對他們先做compact LPN
    buffer.rmw(hotRead_hotWrite_list, rmw_hotRead_hotWrite_list);
    buffer.rmw(hotRead_coldWrite_list, rmw_hotRead_coldWrite_list);

    //如果是cold read的LPNs，因為clean sector不需要一起寫，所以先丟掉
    buffer.clean_sector_discard(coldRead_hotWrite_list);
    buffer.clean_sector_discard(coldRead_coldWrite_list);

    //先對四種類型的vimtim LPNs各自做mixed PPN，確保read和write都是相同類型的都先mix在一起
    buffer.mix(rmw_hotRead_hotWrite_list, mixed_hotRead_hotWrite_list);
    buffer.mix(rmw_hotRead_coldWrite_list, mixed_hotRead_coldWrite_list);
    buffer.mix(coldRead_hotWrite_list, mixed_coldRead_hotWrite_list);
    buffer.mix(coldRead_coldWrite_list, mixed_coldRead_coldWrite_list);

    //然後把同為hot write和同為cold write的集中到同一個list裡面
    for (auto it = mixed_coldRead_hotWrite_list.begin(); it != mixed_coldRead_hotWrite_list.end(); ++it)  mixed_hotRead_hotWrite_list.push_back(*it);
    for (auto it = mixed_coldRead_coldWrite_list.begin(); it != mixed_coldRead_coldWrite_list.end(); ++it)  mixed_hotRead_coldWrite_list.push_back(*it);
    
    //最後再把同為hot write和同為cold write的LPNs再做mixed PPN
    buffer.mix(mixed_hotRead_hotWrite_list, mixed_hot_list);
    buffer.mix(mixed_hotRead_coldWrite_list, mixed_hot_list);    

    //把mix完的page寫入flash裡面
    victim_page_list_write(mixed_hot_list);
    victim_page_list_write(mixed_cold_list);
}