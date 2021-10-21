#include <vector>
#include <string>
#include <map>
#include <queue>
#include "buffer.h"
#include "mapping.h"
#include "flash.h"
#include "SSD_parameter.h"

using std::cout;        using std::cerr;
using std::endl;        using std::string;
using std::ifstream;    using std::vector;
using std::map;         using std::ofstream;
using std::stoi;        using std::pair;
using std::priority_queue;

enum sector_TYPE { READ, WRITE, EMPTY, NOT_EXIST_READ };
enum LPN_TYPE { hotRead_hotWrite, hotRead_coldWrite, coldRead_hotWrite, coldRead_coldWrite };

//buffer的初始化
void Buffer::configuration()
{
    //如果buffer是16MB，那最少要丟的LPN個數設定為14個
    if (max_buffer_size == (16 * 1024 * 1024))
    {
        least_buffer_evict_count = 14;
    }
    //如果buffer是32MB，那最少要丟的LPN個數設定為6個
    else if (max_buffer_size == (32 * 1024 * 1024))
    {
        least_buffer_evict_count = 6;
    }
    //如果buffer是64MB，那最少要丟的LPN個數設定為8個
    else if (max_buffer_size == (64 * 1024 * 1024))
    {
        least_buffer_evict_count = 8;
    }
    //其他配置的話，那最少要丟的LPN個數也設定為8個
    else
    {
        least_buffer_evict_count = 8;
    }

    //目前buffer的size設為0
    current_buffer_size = 0;

    //這邊只是計算hit ratio用的
    //目前用不太到
    write_hit_count = 0;
    write_miss_count = 0;
    read_hit_count = 0;
    read_miss_count = 0;
}

//從LSN_start到LSN_end依序寫入buffer
void Buffer::write(long long LSN_start, long long LSN_end)
{
    //用來判斷這次LSN相對應的LPN是否在這次write中有被用過
    bool flag_duplicate = false;

    //用hash map來找是否重複使用過相同的LPN
    //然後就可以給flag_duplicate相對應的數值
    unordered_map<long long, int> LPN_map;

    //遞增整體系統時間
    ++time_current;

    //從起始LSN到結束LSN，一個一個慢慢寫入buffer裡面
    for (long long LSN_now = LSN_start; LSN_now <= LSN_end; ++LSN_now)
    {
        //計算現在LSN的LPN和offset
        //如果page size為16KB，sector size為4KB
        //這樣就是把LSN除以4
        long long LPN = LSN_now / (page_size / sector_size);
        int offset = LSN_now % (page_size / sector_size);

        //判斷此LPN是否還沒被寫入過
        if (LPN_map.find(LPN) == LPN_map.end())
        {
            LPN_map[LPN] = 1;
            flag_duplicate = false;
        }
        //判斷此LPN是否剛剛被寫入過
        else
        {            
            flag_duplicate = true;
        }

        //找buffer裡面是否有現在LSN對應的LPN
        unordered_map<long long, Logical_page>::iterator buffer_LPN = buffer.find(LPN);

        //創建一個sector(LSN)
        //這邊其實可以包成一個函數去call
        sector trace_sector;
        trace_sector.sector_number = LSN_now;
        trace_sector.write_count = 1;
        trace_sector.read_count = 0;
        trace_sector.status = WRITE;

        //如果buffer上找不到此LPN
        if (buffer_LPN == buffer.end())
        {
            //創建一個page(LPN)
            Logical_page trace_page;
            Initialize_Page(trace_page, trace_sector);

            //把此page放入buffer裡面
            buffer[LPN] = trace_page;

            //更新當前的buffer大小
            current_buffer_size += sector_size;
            ++write_miss_count;
        }
        //如果buffer上找的到此LPN
        else
        {
            //如果此LPN的sector不在buffer裡面，那就把此LPN相對應的sector寫入
            if (buffer_LPN->second.sector_flag[offset] == false)
            {
                buffer_LPN->second.sector[offset] = trace_sector;
                buffer_LPN->second.sector_flag[offset] = true;
                if (flag_duplicate == false)  ++(buffer_LPN->second.write_count);
                ++(buffer_LPN->second.sector_count);                

                buffer_LPN->second.time_access = time_current;
                buffer_LPN->second.status = WRITE;

                //更新當前的buffer大小
                current_buffer_size += sector_size;
                ++write_miss_count;
            }
            //如果此LPN的sector存在buffer裡面，那就更新此LPN的一些資訊
            else
            {
                if (buffer_LPN->second.sector[offset].status == READ)
                {
                    ++buffer_LPN->second.write_sector_count;
                }

                //把此LPN的sector的狀態改成dirty
                buffer_LPN->second.sector[offset].status = WRITE;
                ++(buffer_LPN->second.sector[offset].write_count);
                if (flag_duplicate == false)  ++buffer_LPN->second.write_count;
                buffer_LPN->second.time_access = time_current;

                //如果原本此LPN的sector全部都是clean sector
                //那就把LPN的狀態改成dirty LPN
                buffer_LPN->second.status = WRITE;

                ++write_hit_count;
            }
        }
    }
}

//從LSN_start到LSN_end依序讀入buffer
void Buffer::read(long long LSN_start, long long LSN_end)
{            
    //用來判斷這次LSN相對應的LPN是否在這次read中有被用過
    bool flag_duplicate = false;

    //用hash map來找是否重複使用過相同的LPN
    //然後就可以給flag_duplicate相對應的數值
    unordered_map<long long, int> LPN_map;

    //因為讀PPN會把全部LSN都讀上來
    //所以先用來暫存多讀上來的LSN
    //如果此次起始LSN到結束LSN裡面有相對應的LSN
    //就可以當作有一起讀到
    unordered_map<long long, int> LSN_map;

    //遞增整體系統時間
    ++time_current;

    //從起始LSN到結束LSN，一個一個慢慢寫入buffer裡面
    for (long long LSN_now = LSN_start; LSN_now <= LSN_end; ++LSN_now)
    {
        //計算現在LSN的LPN和offset
        //如果page size為16KB，sector size為4KB
        //這樣就是把LSN除以4
        long long LPN = LSN_now / (page_size / sector_size);
        int offset = LSN_now % (page_size / sector_size);

        //判斷此LPN是否還沒被read過
        if (LPN_map.find(LPN) == LPN_map.end())
        {
            LPN_map[LPN] = 1;
            flag_duplicate = false;
        }
        //判斷此LPN是否剛剛被read過
        else
        {            
            flag_duplicate = true;
        }

        //找buffer裡面是否有現在LSN對應的LPN
        unordered_map<long long, Logical_page>::iterator buffer_LPN = buffer.find(LPN);

        //創建一個sector(LSN)	
        sector trace_sector;
        trace_sector.sector_number = LSN_now;
        trace_sector.write_count = 0;
        trace_sector.read_count = 1;
        trace_sector.status = READ;

        //如果此LPN不存在buffer裡面
        if (buffer_LPN == buffer.end())
        {
            //因為要讀LPN不存在buffer裡面
            //所以去mapping table找看看是否存在flash memory裡面			
            long long PPN_number = mapping_table.read(LPN, offset);

            //雖然此LPN不存在buffer裡面
            //但是此LPN存在flash裡面，那就去flash memory做讀取
            if (PPN_number != -1)
            {
                //如果現在要讀的LSN沒有因為剛剛讀取別的LSN時，被一起暫存到LSN_map裡面
                //那就依然要去flash memory裡面讀取
                if (LSN_map.find(LSN_now) == LSN_map.end())
                {
                    //去flash memory讀取
                    flash_memory.read(LPN, offset);
                    ++read_miss_count;
                }
                //如果現在要讀的LSN剛剛已經被讀過
                else
                {
                    ++read_hit_count;
                }

                //創建一個page(LPN)
                Logical_page trace_page;
                Initialize_Page(trace_page, trace_sector);

                //把此page放入buffer裡面
                buffer[LPN] = trace_page;

                //更新當前的buffer大小
                current_buffer_size += sector_size;

                //計算此PPN的block_id，以及block裡面的page_id
                long long block_id = PPN_number / pages_in_a_block;
                int page_id = PPN_number % pages_in_a_block;

                //這個是bebug用
                //如果沒找到此block那就代表可能有問題
                unordered_map<long long, Block>::iterator used_block = flash_memory.used_block_list.find(block_id);
                if (used_block == flash_memory.used_block_list.end())
                {
                    printf("Buffer::read : used_block == flash_memory.used_block_list.end() \n");
                    system("pause");
                    exit(0);
                }

                //找到flasm memory裡面，要讀的block裡面的真正的PPN
                const Physical_page& PPN_read = used_block->second.PPN[page_id];

                //確認要讀的PPN裡面的每個sector
                for (int i = 0; i < sectors_in_a_page; ++i)
                {
                    //如果要讀的PPN的第i個sector是被寫入過的，而且此sector不是我現在正要讀的sector
                    //那就先把這些PPN裡面的其他sector暫存到LSN_map上面
                    if (PPN_read.valid_sector[i] == true && PPN_read.sector_number[i] != LSN_now)
                    {
                        //計算要讀的PPN的第i個sector是哪個LSN
                        long long LSN_temp = PPN_read.sector_number[i];

                        //查找LSN_map看是否之前有之前就有先暫存起來
                        //如果沒有就放到LSN_map
                        unordered_map<long long, int>::iterator LSN_map_find = LSN_map.find(LSN_temp);
                        if (LSN_map_find == LSN_map.end())
                        {
                            LSN_map[LSN_temp] = 1;
                        }
                    }
                }
            }
            //如果此LPN都不存在buffer和flash裡面
            //查找mapping table得到的PPN如果是-1，代表不存在flash memory裡面
            //處理這種不存在的LSN讀取，採用當作有讀取到buffer裡面，但是之後這個LSN會被寫入到flash memory裡面
            else
            {
                //如果現在要讀的LSN沒有因為剛剛讀取別的LSN時，被一起暫存到LSN_map裡面
                //那就依然要去flash memory裡面讀取
                if (LSN_map.find(LSN_now) == LSN_map.end())
                {
                    //去flash memory讀取
                    flash_memory.read(LPN, offset);
                    ++read_miss_count;                    
                }
                //如果現在要讀的LSN剛剛已經被讀過
                else
                {
                    ++read_hit_count;
                }

                //創建一個page(LPN)
                Logical_page trace_page;

                //page(LPN)的sector狀態設定為「不存在的讀取sector」
                //代表說這個要讀取sector他不存在buffer和flash memory裡面
                //但是這個sector之後要被寫到flash memory裡面
                trace_sector.status = NOT_EXIST_READ;

                //初始化page(LPN)，把sector放入LPN裡面
                Initialize_Page(trace_page, trace_sector);

                //把LPN放入buffer裡面
                buffer[LPN] = trace_page;

                //更新當前的buffer大小
                current_buffer_size += sector_size;

                //++skip_read_count;                
            }
        }
        //如果此LPN存在buffer裡面，但還不能確定相對應的sector是否存在buffer裡面
        else
        {
            //如果此LPN的sector不存在buffer裡面
            if (buffer_LPN->second.sector_flag[offset] == false)
            {
                //因為要讀LPN不存在buffer裡面
                //所以去mapping table找看看是否存在flash memory裡面	
                long long PPN_number = mapping_table.read(LPN, offset);

                //雖然此LPN不存在buffer裡面
                //但是此LPN存在flash裡面，那就去flash memory做讀取
                if (PPN_number != -1)
                {
                    //如果現在要讀的LSN沒有因為剛剛讀取別的LSN時，被一起暫存到LSN_map裡面
                    //那就依然要去flash memory裡面讀取
                    if (LSN_map.find(LSN_now) == LSN_map.end())
                    {
                        //去flash memory讀取
                        flash_memory.read(LPN, offset);
                        ++read_miss_count;                        
                    }
                    //如果現在要讀的LSN剛剛已經被讀過
                    else
                    {
                        ++read_hit_count;
                    }

                    //更新此LPN的資訊
                    buffer_LPN->second.sector[offset] = trace_sector;
                    buffer_LPN->second.sector[offset].status = READ;
                    buffer_LPN->second.sector_flag[offset] = true;
                    if (flag_duplicate == false) ++(buffer_LPN->second.read_count);
                    ++(buffer_LPN->second.sector_count);
                    buffer_LPN->second.time_access = time_current;

                    //更新當前的buffer大小
                    current_buffer_size += sector_size;

                    //計算此PPN的block_id，以及block裡面的page_id
                    long long block_id = PPN_number / pages_in_a_block;
                    int page_id = PPN_number % pages_in_a_block;

                    //這個是bebug用
                    //如果沒找到此block那就代表可能有問題
                    unordered_map<long long, Block>::iterator used_block = flash_memory.used_block_list.find(block_id);
                    if (used_block == flash_memory.used_block_list.end())
                    {
                        printf("Buffer::read : used_block == flash_memory.used_block_list.end() \n");
                        system("pause");
                        exit(0);
                    }

                    //找到flasm memory裡面，要讀的block裡面的真正的PPN
                    const Physical_page& PPN_read = used_block->second.PPN[page_id];

                    //確認要讀的PPN裡面的每個sector
                    for (int i = 0; i < sectors_in_a_page; ++i)
                    {
                        //如果要讀的PPN的第i個sector是被寫入過的，而且此sector不是我現在正要讀的sector
                        //那就先把這些PPN裡面的其他sector暫存到LSN_map上面
                        if (PPN_read.valid_sector[i] == true && PPN_read.sector_number[i] != LSN_now)
                        {
                            //計算要讀的PPN的第i個sector是哪個LSN
                            long long LSN_temp = PPN_read.sector_number[i];

                            //查找LSN_map看是否之前有之前就有先暫存起來
                            //如果沒有就放到LSN_map
                            unordered_map<long long, int>::iterator LSN_map_find = LSN_map.find(LSN_temp);
                            if (LSN_map_find == LSN_map.end())
                            {
                                LSN_map[LSN_temp] = 1;
                            }
                        }
                    }
                }
                //如果此LPN都不存在buffer和flash裡面
                //查找mapping table得到的PPN如果是-1，代表不存在flash memory裡面
                //處理這種不存在的LSN讀取，採用當作有讀取到buffer裡面，但是之後這個LSN會被寫入到flash memory裡面
                else
                {
                    if (LSN_map.find(LSN_now) == LSN_map.end())
                    {
                        ++flash_memory.read_count;
                        ++read_miss_count;
                    }
                    else
                    {
                        ++read_hit_count;
                    }

                    //更新此LPN的資訊
                    buffer_LPN->second.sector[offset] = trace_sector;

                    //LPN的sector狀態設定為「不存在的讀取sector」
                    //代表說這個要讀取sector他不存在buffer和flash memory裡面
                    //但是這個sector之後要被寫到flash memory裡面
                    buffer_LPN->second.sector[offset].status = NOT_EXIST_READ;

                    buffer_LPN->second.sector_flag[offset] = true;
                    if (flag_duplicate == false) ++(buffer_LPN->second.read_count);
                    ++(buffer_LPN->second.sector_count);
                    buffer_LPN->second.time_access = time_current;

                    //更新當前的buffer大小
                    current_buffer_size += sector_size;

                    //++skip_read_count;                    
                }
            }
            //如果此LPN的sector存在buffer裡面
            else
            {
                //更新此LPN的資訊
                ++(buffer_LPN->second.sector[offset].read_count);
                if (flag_duplicate == false) ++(buffer_LPN->second.read_count);
                buffer_LPN->second.time_access = time_current;

                ++read_hit_count;                         
            }
        }
    }   
}

//判斷buffer是否空間還夠
bool Buffer::full(int request_size)
{
    //如果當前的buffer size加上request size比最多可容納的buffer size還大
    //那就代表這次的request會導致buffer被寫滿
    if (current_buffer_size + request_size > max_buffer_size)
    {
        return true;
    }
    else
    {
        return false;
    }
}

//
void Buffer::evict(vector<Logical_page>& victim_LPN_list, int request_size)
{
    //計算需要在buffer清出多少size
    int required_size = request_size - (max_buffer_size - current_buffer_size);

    //挑選出victim LPNs
    victim_selection(victim_LPN_list, required_size);
}

void Buffer::victim_selection(vector<Logical_page>& victim_LPN_list, int required_size)
{
    //Logical_page victim_LPN;

    //計算目前丟了幾個victim LPNs
    int evict_count = 0;

    //用需要清出多少size來計算最多要丟幾個LPN
    int required_size_max_count = ceil((double)required_size / sector_size);

    //比較最多要丟幾個LPN和buffer初始設定最少要丟幾個LPN
    //比較大的那個，當作最多要丟的LPN個數
    int max_evict_count = (required_size_max_count > least_buffer_evict_count) ? required_size_max_count : least_buffer_evict_count;

    //利用max_heap來存分數前幾小的LPN
    priority_queue <Logical_page, vector<Logical_page>, Page_compare > max_heap;

    //先把當前時間加1，避免等一下計算time_current-time_access變成0
    ++time_current;

    //把buffer裡面的LPN一個一個計算分數
    //然後判斷是否要放進max_heap裡面
    int heap_count = 0;
    for (auto& LPN : buffer)
    {        
        //計算每個LPN的weight(score)
        double score = (a * LPN.second.write_count + (1 - a) * LPN.second.read_count) / ((LPN.second.sector_count) * (time_current - LPN.second.time_access));
        LPN.second.score = score;

        //如果max_heap裡面的LPN個數，還不到最多要丟的LPN個數
        //那就把此LPN放進max_heap裡面
        //這個情況只會發生在一開始剛放入max_heap的時候
        if (heap_count < max_evict_count)
        {
            Logical_page temp = LPN.second;
            max_heap.push(temp);
        }
        //如果max_heap裡面的LPN個數已經足夠了
        //所以要開始判斷是否要把此LPN放入max_heap裡面
        else
        {
            //如果此LPN的weight(score)，比max_heap裡面最大的LPN的weight還要小
            //那就代表此LPN應該被放入max_heap，因為weight(score)越低越應該被挑選為victim LPN
            if (max_heap.top().score > score)
            {
                Logical_page temp = LPN.second;
                max_heap.push(temp);
                max_heap.pop();
            }
            //如果此LPN的weight(score)，跟max_heap裡面最大的LPN的weight一樣大
            //而且此LPN，比max_heap裡面最大的weight的LPN，近期被存取過
            //那就代表此LPN應該被放入max_heap，替換掉max_heap的top的LPN
            else if (max_heap.top().score == score && max_heap.top().time_access < LPN.second.time_access)
            {
                Logical_page temp = LPN.second;
                max_heap.push(temp);
                max_heap.pop();
            }
        }

        ++heap_count;
    }

    //如果出現front() called on empty vector的錯誤，代表heap沒東西可以拿了，但又想去拿導致的。

    //把max heap裡面的LPN從top端依序丟出到minimum_LPN裡面
    //因為top端都是最大的，所以從minimum_LPN最後一個位置開始放入
    vector<Logical_page> minimum_LPN(max_heap.size());
    for (int i = minimum_LPN.size() - 1; max_heap.size() > 0; --i)
    {
        minimum_LPN.at(i) = max_heap.top();
        max_heap.pop();
    }

    //上面做完之後就會得到一個array以LPN的weight由小到大排序
    //然後就依序從array的從開始丟LPN
    //直到需要清出的size小於0 或者 已經丟掉的個數達到了至少要丟的buffer個數
    int evict_index = 0;
    while (required_size > 0 || evict_count < least_buffer_evict_count)
    {
        //fin_1的第4663814個request的size為16.32MB
        //所以如果實驗是跑16MB buffer size的設定
        //會把buffer的所有東西都丟掉
        if (evict_index >= minimum_LPN.size()) break;

        Logical_page& temp_LPN = minimum_LPN.at(evict_index);

        required_size = required_size - temp_LPN.sector_count * sector_size;
        current_buffer_size = current_buffer_size - temp_LPN.sector_count * sector_size;
        victim_LPN_list.push_back(temp_LPN);
        buffer.erase(temp_LPN.LPN);
        
        ++evict_index;
        ++evict_count;
    }    
}

//把victim_LPN_list裡面的每個LPN的clean sector給丟掉
void Buffer::clean_sector_discard(vector<Logical_page>& victim_LPN_list)
{
    for (int i = 0; i < victim_LPN_list.size(); ++i)
    {
        Logical_page& victim_LPN_now = victim_LPN_list.at(i);

        //如果此LPN有被讀取過，才代表他有clean sector
        if (victim_LPN_now.read_count > 0)
        {
            for (int offset = 0; offset < sectors_in_a_page; ++offset)
            {
                //如果是clean sector，就把此sector資訊都初始化
                if (victim_LPN_now.sector[offset].status == READ)
                {
                    victim_LPN_now.sector[offset].sector_number = -1;
                    victim_LPN_now.sector[offset].status = EMPTY;
                    victim_LPN_now.sector_flag[offset] = false;
                    --victim_LPN_now.sector_count;

                    if (victim_LPN_now.sector_count == 0)
                    {
                        victim_LPN_now.status = EMPTY;
                    }
                }
            }
        }       
    }
}

//對LPN做分類(hotRead_hotWrite, hotRead_coldWrite, coldRead_hotWrite, coldRead_coldWrite)
int Buffer::LPN_classification(Logical_page& victim_LPN)
{
    //return false;   

    //如果此LPN平均每個sector被read至少2次以上，就當作此LPN為hot read LPN
    if (victim_LPN.read_count / victim_LPN.sector_count >= 2)
    {
        //如果此LPN平均每個sector被write至少2次以上，就當作此LPN為hot write LPN
        if (victim_LPN.write_count / victim_LPN.sector_count >= 2)
        {
            return hotRead_hotWrite;
        }
        else
        {
            return hotRead_coldWrite;
        }
    }
    //如果此LPN平均每個sector被read1次以下，就當作此LPN為cold read LPN
    else
    {
        //如果此LPN平均每個sector被write至少2次以上，就當作此LPN為hot write LPN
        if (victim_LPN.write_count / victim_LPN.sector_count >= 2)
        {
            return coldRead_hotWrite;
        }
        else
        {
            return coldRead_coldWrite;
        }
    }

    /*if (victim_LPN.read_count >= 2)
    {
        if (victim_LPN.write_count >= 2)
        {
            return hotRead_hotWrite;
        }
        else
        {
            return hotRead_coldWrite;
        }
    }
    else
    {
        if (victim_LPN.write_count >= 2)
        {
            return coldRead_hotWrite;
        }
        else
        {
            return coldRead_coldWrite;
        }
    }*/
}

//對victim_LPN_list裡面全部的LPN做分類再依序放入對應的類別list裡面
void Buffer::dynamic_adaption(vector<Logical_page>& victim_LPN_list, vector<Logical_page>& hotRead_hotWrite_list,
    vector<Logical_page>& hotRead_coldWrite_list, vector<Logical_page>& coldRead_hotWrite_list, vector<Logical_page>& coldRead_coldWrite_list)
{
    for (int i = 0; i < victim_LPN_list.size(); ++i)
    {
        int status = LPN_classification(victim_LPN_list.at(i));

        if (status == hotRead_hotWrite)
        {
            hotRead_hotWrite_list.push_back(victim_LPN_list.at(i));
        }
        else if (status == hotRead_coldWrite)
        {
            hotRead_coldWrite_list.push_back(victim_LPN_list.at(i));
        }
        else if (status == coldRead_hotWrite)
        {
            coldRead_hotWrite_list.push_back(victim_LPN_list.at(i));
        }
        else if (status == coldRead_coldWrite)
        {
            coldRead_coldWrite_list.push_back(victim_LPN_list.at(i));
        }
        else
        {
            printf("Buffer::dynamic_adaption : LPN_classification() returns wrong number \n");
            system("pause");
            exit(0);
        }
    }
}

//對hot_list裡面的LPN做mix，然後結果存放在mixed_PPN裡面
void Buffer::mix(vector<Logical_page>& hot_list, vector<Logical_page>& mixed_PPN)
{
    //把hot_list換成victim_LPN_list這個名字
    vector<Logical_page>& victim_LPN_list = hot_list;
    if (victim_LPN_list.size() <= 0)
    {
        return;
    }

    //mix就類似bin-packing問題
    //所以假設現在是第0個bin(從0開始)
    int bin = 0;

    //先做排序由大到小，依照每個LPN的sector個數
    quick_sort(victim_LPN_list,0, victim_LPN_list.size()-1);

    //因為剛有排序過，所以就從頭LPN開始做bin-packing
    //這邊是用first-fit來做
    for (int i = 0; i < victim_LPN_list.size(); ++i)
    {
        Logical_page& victim_LPN_now = victim_LPN_list.at(i);

        //從第0個bin開始查看每個已使用過的bin
        //如果有bin可以容納，等一下就可以把此LPN放入那個bin
        for (bin = 0; bin < mixed_PPN.size(); ++bin)
        {
            //判斷bin的sector數量加上LPN的sector數量
            //如果在總共可容納的sector數量以下，就代表此bin可以使用
            if (mixed_PPN.at(bin).sector_count + victim_LPN_now.sector_count <= sectors_in_a_page)
            {
                break;
            }
        }

        //如果之前已使用過的bin都放不下
        //那就必須再開一個新的bin了
        if (bin >= mixed_PPN.size())
        {
            //創建一個新的bin
            Logical_page empty_PPN;
            empty_PPN.sector_count = 0;

            //初始化bin的一些資訊
            int index = 0;
            for (int offset = 0; offset < sectors_in_a_page; ++offset)
            {
                if (victim_LPN_now.sector_flag[offset] == true)
                {
                    empty_PPN.sector[empty_PPN.sector_count] = victim_LPN_now.sector[offset];
                    empty_PPN.sector_flag[empty_PPN.sector_count] = true;
                    ++empty_PPN.sector_count;
                    ++index;
                }

                if (index == victim_LPN_now.sector_count) break;
            }

            while (index < sectors_in_a_page)
            {
                empty_PPN.sector[index].sector_number = -1;
                empty_PPN.sector_flag[index] = false;
                ++index;
            }

            //把bin放入mixed_PPN list裡面，之後一起寫到flash memory裡面
            mixed_PPN.push_back(empty_PPN);
        }
        //如果之前已使用過的某個bin放得下 
        else
        {
            //把mixed_PPN.at(bin)用另一個名子，比較方便命名
            Logical_page& mixed_PPN_now = mixed_PPN.at(bin);

            //更新此bin的資訊
            //把此LPN給放進這個bin裡面
            int count = 0;
            for (int offset = 0; offset < sectors_in_a_page; ++offset)
            {
                if (victim_LPN_now.sector_flag[offset] == true)
                {
                    mixed_PPN_now.sector[mixed_PPN_now.sector_count] = victim_LPN_now.sector[offset];
                    mixed_PPN_now.sector_flag[mixed_PPN_now.sector_count] = true;
                    ++mixed_PPN_now.sector_count;
                    ++count;
                }

                if (count == victim_LPN_now.sector_count) break;
            }
        }
    }
}

//rmw就是compact LPN
void Buffer::rmw(vector<Logical_page>& cold_list, vector<Logical_page>& non_mixed_PPN)
{
    //把cold_list換成victim_LPN_list這個名字
    vector<Logical_page>& victim_LPN_list = cold_list;
    if (victim_LPN_list.size() <= 0)
    {
        return;
    }

    //查看此LPN在flash的sector是否會蓋過buffer的sector
    bool flag_cover = false;   

    //每個LPN依序判斷是否該做compact LPN
    for (int i = 0; i < victim_LPN_list.size(); ++i)
    {
        Logical_page& victim_LPN_now = victim_LPN_list.at(i);
        long long LPN = victim_LPN_now.LPN;

        //查看此LPN的mapping table
        unordered_map<long long, L2P_entry>::iterator L2P_LPN = mapping_table.L2P.find(LPN);

        //創建hash map，用來計算此LPN總共被write到幾個不同的實體位置(PPN)
        unordered_map<long long, int> flash_PPN_map;

        //創建hash map，用來計算此LPN如果做compact LPN
        //總共要read幾個不同的實體位置(PPN)
        unordered_map<long long, int> rmw_PPN_map;

        if (victim_LPN_now.status == READ)
        {
            continue;
        }

        //如果mapping table上找的到此LPN，代表此LPN之前有被寫到flash裡面
        if (L2P_LPN != mapping_table.L2P.end())
        {
            //從此LPN的第0個sector一直察看到最後一個sector
            for (int offset = 0; offset < sectors_in_a_page; ++offset)
            {
                //查看此LPN的第offset個sector的實體位置(PPN)
                long long PPN_number = L2P_LPN->second.PPN[offset];

                //如果此sector有被寫到flash memory過
                //代表說他不會是-1
                if (PPN_number != -1)
                {
                    //把此LPN的sector存在的PPN紀錄到hash map裡面
                    //確保不會重複紀錄
                    flash_PPN_map[PPN_number] = 1;
                }                

                //如果此LPN在flash memory裡面有buffer沒有的sector
                //代表一定需要做額外讀取
                if (victim_LPN_now.sector_flag[offset] == false && L2P_LPN->second.PPN_flag[offset] == true)
                {
                    //紀錄要read的PPN
                    //利用hash map能避免重複紀錄
                    rmw_PPN_map[PPN_number] = 1;

                    //把flag設為true，代表此LPN要做額外的read來達成compact LPN
                    flag_cover = true;                    
                }                
            }
        }

        //創建一個compact LPN
        Logical_page temp_PPN;
        temp_PPN.sector_count = 0;

        //初始化compcat LPN的資訊
        int index = 0;
        for (int offset = 0; offset < sectors_in_a_page; ++offset)
        {
            if (victim_LPN_now.sector_flag[offset] == true)
            {
                temp_PPN.sector[temp_PPN.sector_count] = victim_LPN_now.sector[offset];
                temp_PPN.sector[temp_PPN.sector_count].status = WRITE;
                temp_PPN.sector_flag[temp_PPN.sector_count] = true;
                ++temp_PPN.sector_count;
                ++index;
            }

            if (index == victim_LPN_now.sector_count) break;
        }

        while (index < sectors_in_a_page)
        {
            temp_PPN.sector[index].sector_number = -1;
            temp_PPN.sector[temp_PPN.sector_count].status = EMPTY;
            temp_PPN.sector_flag[index] = false;
            ++index;
        }

        //如果此LPN被write到5至少兩個PPN以上，或者flash memory裡面有buffer沒有的sector
        //就代表此LPN做compact LPN的話，需要額外的read
        if (flash_PPN_map.size() >= 2 || flag_cover == true)
        {
            for (int offset = 0; offset < sectors_in_a_page; ++offset)
            {
                long long PPN_number = L2P_LPN->second.PPN[offset];

                if (PPN_number != -1 && victim_LPN_now.sector_flag[offset] == false)
                {
                    temp_PPN.sector[temp_PPN.sector_count].sector_number = L2P_LPN->first * sectors_in_a_page + offset;
                    temp_PPN.sector[temp_PPN.sector_count].status = WRITE;
                    temp_PPN.sector_flag[temp_PPN.sector_count] = true;
                    ++temp_PPN.sector_count;
                }
            }

            flag_cover = false;
            flash_memory.rmw_read_count += rmw_PPN_map.size();                       
        }                   

        //把做完compact LPN的LPN放入list裡面
        non_mixed_PPN.push_back(temp_PPN);
    }
}

void quick_sort(vector<Logical_page>& victim_LPN_list, int front, int end)
{
    if (front < end)
    {
        Logical_page temp = victim_LPN_list[end];
        victim_LPN_list[end] = victim_LPN_list[(front + end) / 2];
        victim_LPN_list[end] = temp;

        int pivot = partition(victim_LPN_list, front, end);
        quick_sort(victim_LPN_list, front, pivot - 1);
        quick_sort(victim_LPN_list, pivot + 1, end);
    }
}

int partition(vector<Logical_page>& victim_LPN_list, int front, int end)
{
    int i = front - 1, j = front;

    while (j < end)
    {
        //這邊遠本寫成.score = =大錯特錯，難怪結果變不好
        if (victim_LPN_list[j].sector_count > victim_LPN_list[end].sector_count)
        {
            ++i;

            Logical_page temp = victim_LPN_list[i];
            victim_LPN_list[i] = victim_LPN_list[j];
            victim_LPN_list[j] = temp;
        }

        ++j;
    }

    Logical_page temp = victim_LPN_list[i + 1];
    victim_LPN_list[i + 1] = victim_LPN_list[end];
    victim_LPN_list[end] = temp;

    return i + 1;
}

void Initialize_Page(Logical_page& trace_page, sector& trace_sector)
{
    long long LPN = trace_sector.sector_number / (page_size / sector_size);
    int offset = trace_sector.sector_number % (page_size / sector_size);

    trace_page.LPN = LPN;
    for (int i = 0; i < sectors_in_a_page; ++i)
    {
        trace_page.sector[i].sector_number = -1;
        trace_page.sector[i].status = EMPTY;
        trace_page.sector_flag[i] = false;        
    }

    trace_page.sector[offset] = trace_sector;    
    trace_page.sector_flag[offset] = true;
    trace_page.write_count = trace_sector.write_count;
    trace_page.read_count = trace_sector.read_count;
    trace_page.time_access = buffer.time_current;
    trace_page.sector_count = 1;   

    trace_page.status = trace_sector.status;
}

bool Page_compare::operator()(const Logical_page& p1, const Logical_page& p2)
{
    if (p1.score < p2.score)
    {
        return true;
    }
    else if (p1.score == p2.score && p1.time_access > p2.time_access)
    {
        return true;
    }

    return false;
}