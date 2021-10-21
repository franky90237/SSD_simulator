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

//buffer����l��
void Buffer::configuration()
{
    //�p�Gbuffer�O16MB�A���̤֭n�᪺LPN�ӼƳ]�w��14��
    if (max_buffer_size == (16 * 1024 * 1024))
    {
        least_buffer_evict_count = 14;
    }
    //�p�Gbuffer�O32MB�A���̤֭n�᪺LPN�ӼƳ]�w��6��
    else if (max_buffer_size == (32 * 1024 * 1024))
    {
        least_buffer_evict_count = 6;
    }
    //�p�Gbuffer�O64MB�A���̤֭n�᪺LPN�ӼƳ]�w��8��
    else if (max_buffer_size == (64 * 1024 * 1024))
    {
        least_buffer_evict_count = 8;
    }
    //��L�t�m���ܡA���̤֭n�᪺LPN�ӼƤ]�]�w��8��
    else
    {
        least_buffer_evict_count = 8;
    }

    //�ثebuffer��size�]��0
    current_buffer_size = 0;

    //�o��u�O�p��hit ratio�Ϊ�
    //�ثe�Τ��Ө�
    write_hit_count = 0;
    write_miss_count = 0;
    read_hit_count = 0;
    read_miss_count = 0;
}

//�qLSN_start��LSN_end�̧Ǽg�Jbuffer
void Buffer::write(long long LSN_start, long long LSN_end)
{
    //�ΨӧP�_�o��LSN�۹�����LPN�O�_�b�o��write�����Q�ιL
    bool flag_duplicate = false;

    //��hash map�ӧ�O�_���ƨϥιL�ۦP��LPN
    //�M��N�i�H��flag_duplicate�۹������ƭ�
    unordered_map<long long, int> LPN_map;

    //���W����t�ήɶ�
    ++time_current;

    //�q�_�lLSN�쵲��LSN�A�@�Ӥ@�ӺC�C�g�Jbuffer�̭�
    for (long long LSN_now = LSN_start; LSN_now <= LSN_end; ++LSN_now)
    {
        //�p��{�bLSN��LPN�Moffset
        //�p�Gpage size��16KB�Asector size��4KB
        //�o�˴N�O��LSN���H4
        long long LPN = LSN_now / (page_size / sector_size);
        int offset = LSN_now % (page_size / sector_size);

        //�P�_��LPN�O�_�٨S�Q�g�J�L
        if (LPN_map.find(LPN) == LPN_map.end())
        {
            LPN_map[LPN] = 1;
            flag_duplicate = false;
        }
        //�P�_��LPN�O�_���Q�g�J�L
        else
        {            
            flag_duplicate = true;
        }

        //��buffer�̭��O�_���{�bLSN������LPN
        unordered_map<long long, Logical_page>::iterator buffer_LPN = buffer.find(LPN);

        //�Ыؤ@��sector(LSN)
        //�o����i�H�]���@�Ө�ƥhcall
        sector trace_sector;
        trace_sector.sector_number = LSN_now;
        trace_sector.write_count = 1;
        trace_sector.read_count = 0;
        trace_sector.status = WRITE;

        //�p�Gbuffer�W�䤣�즹LPN
        if (buffer_LPN == buffer.end())
        {
            //�Ыؤ@��page(LPN)
            Logical_page trace_page;
            Initialize_Page(trace_page, trace_sector);

            //�⦹page��Jbuffer�̭�
            buffer[LPN] = trace_page;

            //��s��e��buffer�j�p
            current_buffer_size += sector_size;
            ++write_miss_count;
        }
        //�p�Gbuffer�W�䪺�즹LPN
        else
        {
            //�p�G��LPN��sector���bbuffer�̭��A���N�⦹LPN�۹�����sector�g�J
            if (buffer_LPN->second.sector_flag[offset] == false)
            {
                buffer_LPN->second.sector[offset] = trace_sector;
                buffer_LPN->second.sector_flag[offset] = true;
                if (flag_duplicate == false)  ++(buffer_LPN->second.write_count);
                ++(buffer_LPN->second.sector_count);                

                buffer_LPN->second.time_access = time_current;
                buffer_LPN->second.status = WRITE;

                //��s��e��buffer�j�p
                current_buffer_size += sector_size;
                ++write_miss_count;
            }
            //�p�G��LPN��sector�s�bbuffer�̭��A���N��s��LPN���@�Ǹ�T
            else
            {
                if (buffer_LPN->second.sector[offset].status == READ)
                {
                    ++buffer_LPN->second.write_sector_count;
                }

                //�⦹LPN��sector�����A�令dirty
                buffer_LPN->second.sector[offset].status = WRITE;
                ++(buffer_LPN->second.sector[offset].write_count);
                if (flag_duplicate == false)  ++buffer_LPN->second.write_count;
                buffer_LPN->second.time_access = time_current;

                //�p�G�쥻��LPN��sector�������Oclean sector
                //���N��LPN�����A�令dirty LPN
                buffer_LPN->second.status = WRITE;

                ++write_hit_count;
            }
        }
    }
}

//�qLSN_start��LSN_end�̧�Ū�Jbuffer
void Buffer::read(long long LSN_start, long long LSN_end)
{            
    //�ΨӧP�_�o��LSN�۹�����LPN�O�_�b�o��read�����Q�ιL
    bool flag_duplicate = false;

    //��hash map�ӧ�O�_���ƨϥιL�ۦP��LPN
    //�M��N�i�H��flag_duplicate�۹������ƭ�
    unordered_map<long long, int> LPN_map;

    //�]��ŪPPN�|�����LSN��Ū�W��
    //�ҥH���ΨӼȦs�hŪ�W�Ӫ�LSN
    //�p�G�����_�lLSN�쵲��LSN�̭����۹�����LSN
    //�N�i�H��@���@�_Ū��
    unordered_map<long long, int> LSN_map;

    //���W����t�ήɶ�
    ++time_current;

    //�q�_�lLSN�쵲��LSN�A�@�Ӥ@�ӺC�C�g�Jbuffer�̭�
    for (long long LSN_now = LSN_start; LSN_now <= LSN_end; ++LSN_now)
    {
        //�p��{�bLSN��LPN�Moffset
        //�p�Gpage size��16KB�Asector size��4KB
        //�o�˴N�O��LSN���H4
        long long LPN = LSN_now / (page_size / sector_size);
        int offset = LSN_now % (page_size / sector_size);

        //�P�_��LPN�O�_�٨S�Qread�L
        if (LPN_map.find(LPN) == LPN_map.end())
        {
            LPN_map[LPN] = 1;
            flag_duplicate = false;
        }
        //�P�_��LPN�O�_���Qread�L
        else
        {            
            flag_duplicate = true;
        }

        //��buffer�̭��O�_���{�bLSN������LPN
        unordered_map<long long, Logical_page>::iterator buffer_LPN = buffer.find(LPN);

        //�Ыؤ@��sector(LSN)	
        sector trace_sector;
        trace_sector.sector_number = LSN_now;
        trace_sector.write_count = 0;
        trace_sector.read_count = 1;
        trace_sector.status = READ;

        //�p�G��LPN���s�bbuffer�̭�
        if (buffer_LPN == buffer.end())
        {
            //�]���nŪLPN���s�bbuffer�̭�
            //�ҥH�hmapping table��ݬݬO�_�s�bflash memory�̭�			
            long long PPN_number = mapping_table.read(LPN, offset);

            //���M��LPN���s�bbuffer�̭�
            //���O��LPN�s�bflash�̭��A���N�hflash memory��Ū��
            if (PPN_number != -1)
            {
                //�p�G�{�b�nŪ��LSN�S���]�����Ū���O��LSN�ɡA�Q�@�_�Ȧs��LSN_map�̭�
                //���N�̵M�n�hflash memory�̭�Ū��
                if (LSN_map.find(LSN_now) == LSN_map.end())
                {
                    //�hflash memoryŪ��
                    flash_memory.read(LPN, offset);
                    ++read_miss_count;
                }
                //�p�G�{�b�nŪ��LSN���w�g�QŪ�L
                else
                {
                    ++read_hit_count;
                }

                //�Ыؤ@��page(LPN)
                Logical_page trace_page;
                Initialize_Page(trace_page, trace_sector);

                //�⦹page��Jbuffer�̭�
                buffer[LPN] = trace_page;

                //��s��e��buffer�j�p
                current_buffer_size += sector_size;

                //�p�⦹PPN��block_id�A�H��block�̭���page_id
                long long block_id = PPN_number / pages_in_a_block;
                int page_id = PPN_number % pages_in_a_block;

                //�o�ӬObebug��
                //�p�G�S��즹block���N�N��i�঳���D
                unordered_map<long long, Block>::iterator used_block = flash_memory.used_block_list.find(block_id);
                if (used_block == flash_memory.used_block_list.end())
                {
                    printf("Buffer::read : used_block == flash_memory.used_block_list.end() \n");
                    system("pause");
                    exit(0);
                }

                //���flasm memory�̭��A�nŪ��block�̭����u����PPN
                const Physical_page& PPN_read = used_block->second.PPN[page_id];

                //�T�{�nŪ��PPN�̭����C��sector
                for (int i = 0; i < sectors_in_a_page; ++i)
                {
                    //�p�G�nŪ��PPN����i��sector�O�Q�g�J�L���A�ӥB��sector���O�ڲ{�b���nŪ��sector
                    //���N����o��PPN�̭�����Lsector�Ȧs��LSN_map�W��
                    if (PPN_read.valid_sector[i] == true && PPN_read.sector_number[i] != LSN_now)
                    {
                        //�p��nŪ��PPN����i��sector�O����LSN
                        long long LSN_temp = PPN_read.sector_number[i];

                        //�d��LSN_map�ݬO�_���e�����e�N�����Ȧs�_��
                        //�p�G�S���N���LSN_map
                        unordered_map<long long, int>::iterator LSN_map_find = LSN_map.find(LSN_temp);
                        if (LSN_map_find == LSN_map.end())
                        {
                            LSN_map[LSN_temp] = 1;
                        }
                    }
                }
            }
            //�p�G��LPN�����s�bbuffer�Mflash�̭�
            //�d��mapping table�o�쪺PPN�p�G�O-1�A�N���s�bflash memory�̭�
            //�B�z�o�ؤ��s�b��LSNŪ���A�ĥη�@��Ū����buffer�̭��A���O����o��LSN�|�Q�g�J��flash memory�̭�
            else
            {
                //�p�G�{�b�nŪ��LSN�S���]�����Ū���O��LSN�ɡA�Q�@�_�Ȧs��LSN_map�̭�
                //���N�̵M�n�hflash memory�̭�Ū��
                if (LSN_map.find(LSN_now) == LSN_map.end())
                {
                    //�hflash memoryŪ��
                    flash_memory.read(LPN, offset);
                    ++read_miss_count;                    
                }
                //�p�G�{�b�nŪ��LSN���w�g�QŪ�L
                else
                {
                    ++read_hit_count;
                }

                //�Ыؤ@��page(LPN)
                Logical_page trace_page;

                //page(LPN)��sector���A�]�w���u���s�b��Ū��sector�v
                //�N���o�ӭnŪ��sector�L���s�bbuffer�Mflash memory�̭�
                //���O�o��sector����n�Q�g��flash memory�̭�
                trace_sector.status = NOT_EXIST_READ;

                //��l��page(LPN)�A��sector��JLPN�̭�
                Initialize_Page(trace_page, trace_sector);

                //��LPN��Jbuffer�̭�
                buffer[LPN] = trace_page;

                //��s��e��buffer�j�p
                current_buffer_size += sector_size;

                //++skip_read_count;                
            }
        }
        //�p�G��LPN�s�bbuffer�̭��A���٤���T�w�۹�����sector�O�_�s�bbuffer�̭�
        else
        {
            //�p�G��LPN��sector���s�bbuffer�̭�
            if (buffer_LPN->second.sector_flag[offset] == false)
            {
                //�]���nŪLPN���s�bbuffer�̭�
                //�ҥH�hmapping table��ݬݬO�_�s�bflash memory�̭�	
                long long PPN_number = mapping_table.read(LPN, offset);

                //���M��LPN���s�bbuffer�̭�
                //���O��LPN�s�bflash�̭��A���N�hflash memory��Ū��
                if (PPN_number != -1)
                {
                    //�p�G�{�b�nŪ��LSN�S���]�����Ū���O��LSN�ɡA�Q�@�_�Ȧs��LSN_map�̭�
                    //���N�̵M�n�hflash memory�̭�Ū��
                    if (LSN_map.find(LSN_now) == LSN_map.end())
                    {
                        //�hflash memoryŪ��
                        flash_memory.read(LPN, offset);
                        ++read_miss_count;                        
                    }
                    //�p�G�{�b�nŪ��LSN���w�g�QŪ�L
                    else
                    {
                        ++read_hit_count;
                    }

                    //��s��LPN����T
                    buffer_LPN->second.sector[offset] = trace_sector;
                    buffer_LPN->second.sector[offset].status = READ;
                    buffer_LPN->second.sector_flag[offset] = true;
                    if (flag_duplicate == false) ++(buffer_LPN->second.read_count);
                    ++(buffer_LPN->second.sector_count);
                    buffer_LPN->second.time_access = time_current;

                    //��s��e��buffer�j�p
                    current_buffer_size += sector_size;

                    //�p�⦹PPN��block_id�A�H��block�̭���page_id
                    long long block_id = PPN_number / pages_in_a_block;
                    int page_id = PPN_number % pages_in_a_block;

                    //�o�ӬObebug��
                    //�p�G�S��즹block���N�N��i�঳���D
                    unordered_map<long long, Block>::iterator used_block = flash_memory.used_block_list.find(block_id);
                    if (used_block == flash_memory.used_block_list.end())
                    {
                        printf("Buffer::read : used_block == flash_memory.used_block_list.end() \n");
                        system("pause");
                        exit(0);
                    }

                    //���flasm memory�̭��A�nŪ��block�̭����u����PPN
                    const Physical_page& PPN_read = used_block->second.PPN[page_id];

                    //�T�{�nŪ��PPN�̭����C��sector
                    for (int i = 0; i < sectors_in_a_page; ++i)
                    {
                        //�p�G�nŪ��PPN����i��sector�O�Q�g�J�L���A�ӥB��sector���O�ڲ{�b���nŪ��sector
                        //���N����o��PPN�̭�����Lsector�Ȧs��LSN_map�W��
                        if (PPN_read.valid_sector[i] == true && PPN_read.sector_number[i] != LSN_now)
                        {
                            //�p��nŪ��PPN����i��sector�O����LSN
                            long long LSN_temp = PPN_read.sector_number[i];

                            //�d��LSN_map�ݬO�_���e�����e�N�����Ȧs�_��
                            //�p�G�S���N���LSN_map
                            unordered_map<long long, int>::iterator LSN_map_find = LSN_map.find(LSN_temp);
                            if (LSN_map_find == LSN_map.end())
                            {
                                LSN_map[LSN_temp] = 1;
                            }
                        }
                    }
                }
                //�p�G��LPN�����s�bbuffer�Mflash�̭�
                //�d��mapping table�o�쪺PPN�p�G�O-1�A�N���s�bflash memory�̭�
                //�B�z�o�ؤ��s�b��LSNŪ���A�ĥη�@��Ū����buffer�̭��A���O����o��LSN�|�Q�g�J��flash memory�̭�
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

                    //��s��LPN����T
                    buffer_LPN->second.sector[offset] = trace_sector;

                    //LPN��sector���A�]�w���u���s�b��Ū��sector�v
                    //�N���o�ӭnŪ��sector�L���s�bbuffer�Mflash memory�̭�
                    //���O�o��sector����n�Q�g��flash memory�̭�
                    buffer_LPN->second.sector[offset].status = NOT_EXIST_READ;

                    buffer_LPN->second.sector_flag[offset] = true;
                    if (flag_duplicate == false) ++(buffer_LPN->second.read_count);
                    ++(buffer_LPN->second.sector_count);
                    buffer_LPN->second.time_access = time_current;

                    //��s��e��buffer�j�p
                    current_buffer_size += sector_size;

                    //++skip_read_count;                    
                }
            }
            //�p�G��LPN��sector�s�bbuffer�̭�
            else
            {
                //��s��LPN����T
                ++(buffer_LPN->second.sector[offset].read_count);
                if (flag_duplicate == false) ++(buffer_LPN->second.read_count);
                buffer_LPN->second.time_access = time_current;

                ++read_hit_count;                         
            }
        }
    }   
}

//�P�_buffer�O�_�Ŷ��ٰ�
bool Buffer::full(int request_size)
{
    //�p�G��e��buffer size�[�Wrequest size��̦h�i�e�Ǫ�buffer size�٤j
    //���N�N��o����request�|�ɭPbuffer�Q�g��
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
    //�p��ݭn�bbuffer�M�X�h��size
    int required_size = request_size - (max_buffer_size - current_buffer_size);

    //�D��Xvictim LPNs
    victim_selection(victim_LPN_list, required_size);
}

void Buffer::victim_selection(vector<Logical_page>& victim_LPN_list, int required_size)
{
    //Logical_page victim_LPN;

    //�p��ثe��F�X��victim LPNs
    int evict_count = 0;

    //�λݭn�M�X�h��size�ӭp��̦h�n��X��LPN
    int required_size_max_count = ceil((double)required_size / sector_size);

    //����̦h�n��X��LPN�Mbuffer��l�]�w�̤֭n��X��LPN
    //����j�����ӡA��@�̦h�n�᪺LPN�Ӽ�
    int max_evict_count = (required_size_max_count > least_buffer_evict_count) ? required_size_max_count : least_buffer_evict_count;

    //�Q��max_heap�Ӧs���ƫe�X�p��LPN
    priority_queue <Logical_page, vector<Logical_page>, Page_compare > max_heap;

    //�����e�ɶ��[1�A�קK���@�U�p��time_current-time_access�ܦ�0
    ++time_current;

    //��buffer�̭���LPN�@�Ӥ@�ӭp�����
    //�M��P�_�O�_�n��imax_heap�̭�
    int heap_count = 0;
    for (auto& LPN : buffer)
    {        
        //�p��C��LPN��weight(score)
        double score = (a * LPN.second.write_count + (1 - a) * LPN.second.read_count) / ((LPN.second.sector_count) * (time_current - LPN.second.time_access));
        LPN.second.score = score;

        //�p�Gmax_heap�̭���LPN�ӼơA�٤���̦h�n�᪺LPN�Ӽ�
        //���N�⦹LPN��imax_heap�̭�
        //�o�ӱ��p�u�|�o�ͦb�@�}�l���Jmax_heap���ɭ�
        if (heap_count < max_evict_count)
        {
            Logical_page temp = LPN.second;
            max_heap.push(temp);
        }
        //�p�Gmax_heap�̭���LPN�ӼƤw�g�����F
        //�ҥH�n�}�l�P�_�O�_�n�⦹LPN��Jmax_heap�̭�
        else
        {
            //�p�G��LPN��weight(score)�A��max_heap�̭��̤j��LPN��weight�٭n�p
            //���N�N��LPN���ӳQ��Jmax_heap�A�]��weight(score)�V�C�V���ӳQ�D�אּvictim LPN
            if (max_heap.top().score > score)
            {
                Logical_page temp = LPN.second;
                max_heap.push(temp);
                max_heap.pop();
            }
            //�p�G��LPN��weight(score)�A��max_heap�̭��̤j��LPN��weight�@�ˤj
            //�ӥB��LPN�A��max_heap�̭��̤j��weight��LPN�A����Q�s���L
            //���N�N��LPN���ӳQ��Jmax_heap�A������max_heap��top��LPN
            else if (max_heap.top().score == score && max_heap.top().time_access < LPN.second.time_access)
            {
                Logical_page temp = LPN.second;
                max_heap.push(temp);
                max_heap.pop();
            }
        }

        ++heap_count;
    }

    //�p�G�X�{front() called on empty vector�����~�A�N��heap�S�F��i�H���F�A���S�Q�h���ɭP���C

    //��max heap�̭���LPN�qtop�ݨ̧ǥ�X��minimum_LPN�̭�
    //�]��top�ݳ��O�̤j���A�ҥH�qminimum_LPN�̫�@�Ӧ�m�}�l��J
    vector<Logical_page> minimum_LPN(max_heap.size());
    for (int i = minimum_LPN.size() - 1; max_heap.size() > 0; --i)
    {
        minimum_LPN.at(i) = max_heap.top();
        max_heap.pop();
    }

    //�W����������N�|�o��@��array�HLPN��weight�Ѥp��j�Ƨ�
    //�M��N�̧Ǳqarray���q�}�l��LPN
    //����ݭn�M�X��size�p��0 �Ϊ� �w�g�ᱼ���ӼƹF��F�ܤ֭n�᪺buffer�Ӽ�
    int evict_index = 0;
    while (required_size > 0 || evict_count < least_buffer_evict_count)
    {
        //fin_1����4663814��request��size��16.32MB
        //�ҥH�p�G����O�]16MB buffer size���]�w
        //�|��buffer���Ҧ��F�賣�ᱼ
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

//��victim_LPN_list�̭����C��LPN��clean sector���ᱼ
void Buffer::clean_sector_discard(vector<Logical_page>& victim_LPN_list)
{
    for (int i = 0; i < victim_LPN_list.size(); ++i)
    {
        Logical_page& victim_LPN_now = victim_LPN_list.at(i);

        //�p�G��LPN���QŪ���L�A�~�N��L��clean sector
        if (victim_LPN_now.read_count > 0)
        {
            for (int offset = 0; offset < sectors_in_a_page; ++offset)
            {
                //�p�G�Oclean sector�A�N�⦹sector��T����l��
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

//��LPN������(hotRead_hotWrite, hotRead_coldWrite, coldRead_hotWrite, coldRead_coldWrite)
int Buffer::LPN_classification(Logical_page& victim_LPN)
{
    //return false;   

    //�p�G��LPN�����C��sector�Qread�ܤ�2���H�W�A�N��@��LPN��hot read LPN
    if (victim_LPN.read_count / victim_LPN.sector_count >= 2)
    {
        //�p�G��LPN�����C��sector�Qwrite�ܤ�2���H�W�A�N��@��LPN��hot write LPN
        if (victim_LPN.write_count / victim_LPN.sector_count >= 2)
        {
            return hotRead_hotWrite;
        }
        else
        {
            return hotRead_coldWrite;
        }
    }
    //�p�G��LPN�����C��sector�Qread1���H�U�A�N��@��LPN��cold read LPN
    else
    {
        //�p�G��LPN�����C��sector�Qwrite�ܤ�2���H�W�A�N��@��LPN��hot write LPN
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

//��victim_LPN_list�̭�������LPN�������A�̧ǩ�J���������Olist�̭�
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

//��hot_list�̭���LPN��mix�A�M�ᵲ�G�s��bmixed_PPN�̭�
void Buffer::mix(vector<Logical_page>& hot_list, vector<Logical_page>& mixed_PPN)
{
    //��hot_list����victim_LPN_list�o�ӦW�r
    vector<Logical_page>& victim_LPN_list = hot_list;
    if (victim_LPN_list.size() <= 0)
    {
        return;
    }

    //mix�N����bin-packing���D
    //�ҥH���]�{�b�O��0��bin(�q0�}�l)
    int bin = 0;

    //�����ƧǥѤj��p�A�̷ӨC��LPN��sector�Ӽ�
    quick_sort(victim_LPN_list,0, victim_LPN_list.size()-1);

    //�]���観�ƧǹL�A�ҥH�N�q�YLPN�}�l��bin-packing
    //�o��O��first-fit�Ӱ�
    for (int i = 0; i < victim_LPN_list.size(); ++i)
    {
        Logical_page& victim_LPN_now = victim_LPN_list.at(i);

        //�q��0��bin�}�l�d�ݨC�Ӥw�ϥιL��bin
        //�p�G��bin�i�H�e�ǡA���@�U�N�i�H�⦹LPN��J����bin
        for (bin = 0; bin < mixed_PPN.size(); ++bin)
        {
            //�P�_bin��sector�ƶq�[�WLPN��sector�ƶq
            //�p�G�b�`�@�i�e�Ǫ�sector�ƶq�H�U�A�N�N��bin�i�H�ϥ�
            if (mixed_PPN.at(bin).sector_count + victim_LPN_now.sector_count <= sectors_in_a_page)
            {
                break;
            }
        }

        //�p�G���e�w�ϥιL��bin���񤣤U
        //���N�����A�}�@�ӷs��bin�F
        if (bin >= mixed_PPN.size())
        {
            //�Ыؤ@�ӷs��bin
            Logical_page empty_PPN;
            empty_PPN.sector_count = 0;

            //��l��bin���@�Ǹ�T
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

            //��bin��Jmixed_PPN list�̭��A����@�_�g��flash memory�̭�
            mixed_PPN.push_back(empty_PPN);
        }
        //�p�G���e�w�ϥιL���Y��bin��o�U 
        else
        {
            //��mixed_PPN.at(bin)�Υt�@�ӦW�l�A�����K�R�W
            Logical_page& mixed_PPN_now = mixed_PPN.at(bin);

            //��s��bin����T
            //�⦹LPN����i�o��bin�̭�
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

//rmw�N�Ocompact LPN
void Buffer::rmw(vector<Logical_page>& cold_list, vector<Logical_page>& non_mixed_PPN)
{
    //��cold_list����victim_LPN_list�o�ӦW�r
    vector<Logical_page>& victim_LPN_list = cold_list;
    if (victim_LPN_list.size() <= 0)
    {
        return;
    }

    //�d�ݦ�LPN�bflash��sector�O�_�|�\�Lbuffer��sector
    bool flag_cover = false;   

    //�C��LPN�̧ǧP�_�O�_�Ӱ�compact LPN
    for (int i = 0; i < victim_LPN_list.size(); ++i)
    {
        Logical_page& victim_LPN_now = victim_LPN_list.at(i);
        long long LPN = victim_LPN_now.LPN;

        //�d�ݦ�LPN��mapping table
        unordered_map<long long, L2P_entry>::iterator L2P_LPN = mapping_table.L2P.find(LPN);

        //�Ы�hash map�A�Ψӭp�⦹LPN�`�@�Qwrite��X�Ӥ��P�������m(PPN)
        unordered_map<long long, int> flash_PPN_map;

        //�Ы�hash map�A�Ψӭp�⦹LPN�p�G��compact LPN
        //�`�@�nread�X�Ӥ��P�������m(PPN)
        unordered_map<long long, int> rmw_PPN_map;

        if (victim_LPN_now.status == READ)
        {
            continue;
        }

        //�p�Gmapping table�W�䪺�즹LPN�A�N��LPN���e���Q�g��flash�̭�
        if (L2P_LPN != mapping_table.L2P.end())
        {
            //�q��LPN����0��sector�@����ݨ�̫�@��sector
            for (int offset = 0; offset < sectors_in_a_page; ++offset)
            {
                //�d�ݦ�LPN����offset��sector�������m(PPN)
                long long PPN_number = L2P_LPN->second.PPN[offset];

                //�p�G��sector���Q�g��flash memory�L
                //�N���L���|�O-1
                if (PPN_number != -1)
                {
                    //�⦹LPN��sector�s�b��PPN������hash map�̭�
                    //�T�O���|���Ƭ���
                    flash_PPN_map[PPN_number] = 1;
                }                

                //�p�G��LPN�bflash memory�̭���buffer�S����sector
                //�N��@�w�ݭn���B�~Ū��
                if (victim_LPN_now.sector_flag[offset] == false && L2P_LPN->second.PPN_flag[offset] == true)
                {
                    //�����nread��PPN
                    //�Q��hash map���קK���Ƭ���
                    rmw_PPN_map[PPN_number] = 1;

                    //��flag�]��true�A�N��LPN�n���B�~��read�ӹF��compact LPN
                    flag_cover = true;                    
                }                
            }
        }

        //�Ыؤ@��compact LPN
        Logical_page temp_PPN;
        temp_PPN.sector_count = 0;

        //��l��compcat LPN����T
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

        //�p�G��LPN�Qwrite��5�ܤ֨��PPN�H�W�A�Ϊ�flash memory�̭���buffer�S����sector
        //�N�N��LPN��compact LPN���ܡA�ݭn�B�~��read
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

        //�ⰵ��compact LPN��LPN��Jlist�̭�
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
        //�o�价���g��.score = =�j���S���A���ǵ��G�ܤ��n
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