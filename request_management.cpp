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
    
    //�p��_�lLSN
    //�]���o��request�O�H512B�����A�M��ڭ�sector�O�H4KB�����
    //�ҥH��LBA���H8�o�������LSN
    LSN_start = LBA / (sector_size / 512);        

    //�p�⦹request��size�n�g�X��LBA�A�o��O�H512B�����p��
    //�p�⧹�n��1�A�S��ܷ|�h�g�@��LSN
    sector512_count = ceil(SIZE / 512.0) - 1;

    //�p�⵲��LSN    
    LSN_end = (LBA + sector512_count) / (sector_size / 512);     

    //�p��request��SIZE�G�`�@�g�X��LSN�A���Wsector��size
    SIZE = ((LSN_end - LSN_start) + 1) * sector_size;

    //�p�G�o�{�_�lLSN�j�󵲧�LSN�A���N�N�����D
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

        //�p�G��LPN�s�bbuffer�̭�
        unordered_map<long long, Logical_page>::iterator buffer_LPN = buffer.buffer.find(LPN);
        if (buffer_LPN != buffer.buffer.end())
        {
            //�p�G��LPN��sector�s�bbuffer�̭�
            if (buffer_LPN->second.sector_flag[offset] == true)
            {
                SIZE = SIZE - sector_size;
                ++SIZE_lost_count;
                continue;
            }
        }
    }*/

    //�P�_buffer�O�_full   
    if (buffer.full(SIZE))
    {
        buffer_full_handler(SIZE);
    }
    
    //�B�zread request
    if (TYPE == READ)
    {
        buffer.read(LSN_start, LSN_end);
    }
    //�B�zwrite request
    else
    {
        buffer.write(LSN_start, LSN_end);
    }
}

void victim_page_list_write(vector<Logical_page>& victim_page_list)
{
    for (const auto& victim_page : victim_page_list)
    {
        //�T�{�O�_�n��gc
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
    //��buffer full�ɡA�x�s�qbuffer��X�Ӫ�victim LPNs
    vector<Logical_page> victim_LPN_list;

    //��victim LPNs�����|������
    vector<Logical_page> hotRead_hotWrite_list, hotRead_coldWrite_list, coldRead_hotWrite_list, coldRead_coldWrite_list;

    //�p�Gvicrim LPNs�Ohot read�A�N�n��L�̰�compact LPN
    vector<Logical_page> rmw_hotRead_hotWrite_list, rmw_hotRead_coldWrite_list;

    //�����|�������U�۰�mixed PPN
    vector<Logical_page> mixed_hotRead_hotWrite_list, mixed_hotRead_coldWrite_list, mixed_coldRead_hotWrite_list, mixed_coldRead_coldWrite_list;    
    
    //�̫�A�ݦP��hot write�Ϊ̦P��cold write�������O�_�i�H�A�i�@�B��mixed PPN
    vector<Logical_page> mixed_hot_list, mixed_cold_list;

    //�qbuffer�D��Xvictim LPNs�A�M��s�Jvictim_LPN_list
    buffer.evict(victim_LPN_list, SIZE);

    //��victim_LPN_list���|������������
    buffer.dynamic_adaption(victim_LPN_list, hotRead_hotWrite_list, hotRead_coldWrite_list, coldRead_hotWrite_list, coldRead_coldWrite_list);    

    //�p�G�Ohot read��LPNs�A��L�̥���compact LPN
    buffer.rmw(hotRead_hotWrite_list, rmw_hotRead_hotWrite_list);
    buffer.rmw(hotRead_coldWrite_list, rmw_hotRead_coldWrite_list);

    //�p�G�Ocold read��LPNs�A�]��clean sector���ݭn�@�_�g�A�ҥH���ᱼ
    buffer.clean_sector_discard(coldRead_hotWrite_list);
    buffer.clean_sector_discard(coldRead_coldWrite_list);

    //����|��������vimtim LPNs�U�۰�mixed PPN�A�T�Oread�Mwrite���O�ۦP����������mix�b�@�_
    buffer.mix(rmw_hotRead_hotWrite_list, mixed_hotRead_hotWrite_list);
    buffer.mix(rmw_hotRead_coldWrite_list, mixed_hotRead_coldWrite_list);
    buffer.mix(coldRead_hotWrite_list, mixed_coldRead_hotWrite_list);
    buffer.mix(coldRead_coldWrite_list, mixed_coldRead_coldWrite_list);

    //�M���P��hot write�M�P��cold write��������P�@��list�̭�
    for (auto it = mixed_coldRead_hotWrite_list.begin(); it != mixed_coldRead_hotWrite_list.end(); ++it)  mixed_hotRead_hotWrite_list.push_back(*it);
    for (auto it = mixed_coldRead_coldWrite_list.begin(); it != mixed_coldRead_coldWrite_list.end(); ++it)  mixed_hotRead_coldWrite_list.push_back(*it);
    
    //�̫�A��P��hot write�M�P��cold write��LPNs�A��mixed PPN
    buffer.mix(mixed_hotRead_hotWrite_list, mixed_hot_list);
    buffer.mix(mixed_hotRead_coldWrite_list, mixed_hot_list);    

    //��mix����page�g�Jflash�̭�
    victim_page_list_write(mixed_hot_list);
    victim_page_list_write(mixed_cold_list);
}